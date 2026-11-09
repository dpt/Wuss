/* wuss/tasks/deskduck/deskduck.c -- a desk pet duck that roams the backdrop */

#if defined(WUSS_APP) && defined(WUSS_ICONBAR)

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "framebuf/screen.h"
#include "utils/rng.h"
#include "wuss/component/error.h"
#include "wuss/component/proginfo.h"
#include "wuss/iconbar.h"
#include "wuss/menu.h"
#include "wuss/message.h"
#include "wuss/task.h"

#include "../apps.h"
#include "../common/common.h"

/* Timings are in idle ticks, which come at 60 Hz. */
#define DUCK_SIZE       16  /* px, of every frame */
#define DUCK_ICONBAR    68  /* px kept clear at the bottom, as Pinboard does */
#define DUCK_CHASE      120 /* px: the pointer is chased within this */
#define DUCK_SLEEP      (30 * 60) /* the pointer is still this long */
#define DUCK_SPEED      24  /* px/s, top waddle speed */
#define DUCK_SWIM       12  /* px/s, top swim speed */
#define DUCK_TURN       10  /* ticks per eighth of a turn, steering */
#define DUCK_MAX        7   /* ducks: the pond, ducks and bubbles fit
                             * Pinboard's 16 actor slots */

/* Actor ids. Pinboard draws actors in slot order, and the pond is sent
 * first, so it takes a lower slot and lies under the ducks.
 * ponytail: holds only while no other task frees a slot below, and a bubble
 * may take a slot under a later duck; give actors a depth if that bites. */
#define POND_ACTOR      0
#define DUCK_ACTOR(i)   (1 + 2 * (i))
#define BUBBLE_ACTOR(i) (2 + 2 * (i))
#define ACTOR_DUCK(id)  (((id) - 1) / 2) /* the duck a duck or bubble is */

enum /* the columns of deskduck_16x16.png */
{
  FRAME_WALK1,
  FRAME_WALK2,
  FRAME_IDLE,
  FRAME_QUACK,
  FRAME_SLEEP1,
  FRAME_SLEEP2,
  FRAME_SWIM1,
  FRAME_SWIM2,
  FRAME_FRONT,
  FRAME_FRONT1, /* walking towards the viewer */
  FRAME_FRONT2,
  FRAME_BACK1,  /* walking away */
  FRAME_BACK2,
  FRAME_SQUACK, /* quacking afloat */
  FRAME__LIMIT
};

/* ----------------------------------------------------------------------- */

/* The duck is a state machine. Each idle tick senses a set of events. The
 * first row of deskduck_transitions whose states hold the current one and
 * whose event happened picks the next state, and a click is one more event.
 * Entering a state starts its timer, and deskduck_states says what the duck
 * shows and does while in it. */

enum /* states */
{
  PAUSE,   /* stands about */
  WANDER,  /* walks a random heading */
  CHASE,   /* walks at the pointer */
  CAUGHT,  /* stands on the pointer */
  QUACK,   /* says so in a bubble */
  SLEEP,   /* dozes once the pointer has been still a while */
  TO_POND, /* walks to the pond */
  SWIM,    /* paddles about on it */
  SQUACK,  /* quacks afloat */
  STATE__LIMIT
};

#define IN(state) (1u << (state))
#define ANY       (IN(STATE__LIMIT) - 1)
#define AWAKE     (IN(PAUSE) | IN(WANDER) | IN(CHASE) | IN(CAUGHT) | \
                   IN(TO_POND))
#define AFLOAT    (IN(SWIM) | IN(SQUACK))

enum /* events, as bits */
{
  ON_CLICK   = 1 << 0,  /* the duck was clicked */
  ON_TIMEOUT = 1 << 1,  /* the state's timer ran out */
  ON_WHIM    = 1 << 2,  /* with a third of timeouts: off for a swim */
  ON_MOVED   = 1 << 3,  /* the pointer moved */
  ON_STILL   = 1 << 4,  /* the pointer has been still DUCK_SLEEP ticks */
  ON_CAUGHT  = 1 << 5,  /* the pointer is on the duck */
  ON_NEAR    = 1 << 6,  /* the pointer is within DUCK_CHASE */
  ON_FAR     = 1 << 7,  /* the pointer is further off */
  ON_WET     = 1 << 8,  /* the duck is on the pond */
  ON_SPLASH  = 1 << 9,  /* ... and was not last tick */
  ON_DRY     = 1 << 10  /* the duck or the pond was dragged */
};

enum /* what a state does */
{
  WALKS   = 1 << 0, /* steps along the heading, on the backdrop */
  SWIMS   = 1 << 1, /* steps along the heading, on the pond */
  STEERS  = 1 << 2, /* heads at the pointer */
  HOMES   = 1 << 3, /* heads for the pond */
  ROAMS   = 1 << 4, /* takes a random heading on entry */
  BUBBLES = 1 << 5  /* shows the bubble */
};

typedef struct deskduck_state
{
  unsigned char frame, other; /* alternated */
  unsigned char period;       /* ticks per frame, 0 to alternate per step */
  unsigned char does;
  short         ticks;        /* the timer on entry, 0 for none */
  short         spread;       /* plus up to this, at random */
}
deskduck_state_t;

static const deskduck_state_t deskduck_states[STATE__LIMIT] =
{
  /*            frame         other     period does           ticks spread */
  [PAUSE]   = { FRAME_IDLE,   FRAME_IDLE,    0, 0,               60, 120 },
  [WANDER]  = { FRAME_WALK1,  FRAME_WALK2,   0, WALKS | ROAMS,  180, 420 },
  [CHASE]   = { FRAME_WALK1,  FRAME_WALK2,   0, WALKS | STEERS,   0,   0 },
  [CAUGHT]  = { FRAME_FRONT,  FRAME_FRONT,   0, 0,                0,   0 },
  [QUACK]   = { FRAME_QUACK,  FRAME_QUACK,   0, BUBBLES,         90,   0 },
  [SLEEP]   = { FRAME_SLEEP1, FRAME_SLEEP2, 32, 0,                0,   0 },
  [TO_POND] = { FRAME_WALK1,  FRAME_WALK2,   0, WALKS | HOMES,    0,   0 },
  [SWIM]    = { FRAME_SWIM1,  FRAME_SWIM2,  30, SWIMS | ROAMS,  600, 900 },
  [SQUACK]  = { FRAME_SQUACK, FRAME_SQUACK,  0, BUBBLES,         90,   0 }
};

typedef struct deskduck_transition
{
  unsigned      from; /* IN() set of states */
  unsigned      on;   /* event */
  unsigned char to;
}
deskduck_transition_t;

/* in priority order */
static const deskduck_transition_t deskduck_transitions[] =
{
  /* from                                 on          to */
  { ANY & ~AFLOAT,                       ON_CLICK,   QUACK   },
  { IN(SWIM),                            ON_CLICK,   SQUACK  },
  { IN(QUACK),                           ON_TIMEOUT, PAUSE   },
  { IN(SQUACK),                          ON_TIMEOUT, SWIM    },
  { AFLOAT,                              ON_DRY,     WANDER  },
  { ANY & ~AFLOAT,                       ON_SPLASH,  SWIM    },
  { IN(SLEEP),                           ON_MOVED,   PAUSE   },
  { AWAKE,                               ON_STILL,   SLEEP   },
  { AWAKE & ~IN(CAUGHT),                 ON_CAUGHT,  CAUGHT  },
  { AWAKE & ~IN(CHASE),                  ON_NEAR,    CHASE   },
  { IN(CHASE) | IN(CAUGHT),              ON_FAR,     PAUSE   },
  { IN(TO_POND),                         ON_WET,     SWIM    },
  { IN(PAUSE),                           ON_WHIM,    TO_POND },
  { IN(PAUSE) | IN(SWIM),                ON_TIMEOUT, WANDER  },
  { IN(WANDER),                          ON_TIMEOUT, PAUSE   }
};

enum
{
  DUCK_MENU_INFO,
  DUCK_MENU_ADD,
  DUCK_MENU_REMOVE,
  DUCK_MENU_PAUSE,
  DUCK_MENU_QUIT,
  DUCK_MENU__LIMIT
};

typedef struct duck
{
  point_t         pos;
  point_t         heading; /* each -1, 0 or 1 */
  int             left;    /* faces left */
  int             state;
  int             timer;   /* ticks left in the state, 0 for none */
  int             wet;     /* on the pond last tick */
  int             ticks;
  int             stride;  /* px/s summed per tick; a step at 60 */
  int             turning; /* ticks since the last turn */
  int             steps;   /* px walked, for the waddle frame */
  const bitmap_t *shown;   /* the frame last sent, NULL for none */
  point_t         shown_pos;
}
duck_t;

typedef struct deskduck
{
  wuss_t              *wuss;
  wuss_task_t         *delegate;
  wuss_iconbar_icon_t *iconbar_icon;
  wuss_menu_item_t     menu_items[DUCK_MENU__LIMIT];
  wuss_menu_t          menu;
  wuss_menu_handle_t   menu_handle;
  const bitmap_t      *frames[FRAME__LIMIT][2]; /* facing right, left */
  bitmap_t             bubble; /* "Quack!"; no base if there is no font */
  rng_t                rng;
  const bitmap_t      *pond_bitmap;
  point_t              pond;
  duck_t               ducks[DUCK_MAX];
  int                  nducks;
  int                  still;   /* ticks the pointer has been still */
  point_t              pointer; /* where the pointer was last tick */
  int                  paused;
  int                  quit;    /* destroy the task on the next idle */
}
deskduck_t;

/* ----------------------------------------------------------------------- */

static void deskduck_send(deskduck_t     *desk,
                          int             op,
                          int             id,
                          const bitmap_t *bitmap,
                          point_t         pos)
{
  wuss_pinboard_actor_t actor;

  memset(&actor, 0, sizeof(actor));
  actor.op     = op;
  actor.id     = id;
  actor.bitmap = bitmap;
  actor.pos    = pos;
  (void) wuss_send_recorded(desk->wuss, desk->delegate, NULL,
                            wuss_MESSAGE_PINBOARD_ACTOR, &actor,
                            sizeof(actor), 0, NULL);
}

/* duck's index, for its actor ids */
static int deskduck_index(const deskduck_t *desk, const duck_t *duck)
{
  return (int) (duck - desk->ducks);
}

/* show duck's bubble, if it has one */
static void deskduck_bubble_show(deskduck_t *desk, const duck_t *duck)
{
  if (desk->bubble.base == NULL)
    return;

  deskduck_send(desk, wuss_PINBOARD_ACTOR_SET,
                BUBBLE_ACTOR(deskduck_index(desk, duck)), &desk->bubble,
                POINT(duck->pos.x + DUCK_SIZE / 2 - desk->bubble.size.w / 2,
                      duck->pos.y - desk->bubble.size.h));
}

/* the bubble's bitmap: "Quack!" in the system font on a rounded rectangle */
static result_t deskduck_bubble_create(deskduck_t *desk)
{
  result_t       rc;
  bmfont_t      *font;
  int            height, ascent;
  bmfont_width_t width;
  size2d_t       size;
  void          *base;
  screen_t       scr;
  point_t        pos;

  font = wuss_get_font(desk->wuss);
  if (font == NULL)
    return result_OK; /* no bubble */

  bmfont_get_info(font, NULL, &height, &ascent, NULL);
  width = 0;
  rc = wuss_text_measure(desk->wuss, 0, "Quack!", 6, INT_MAX, NULL, &width);
  if (rc != result_OK)
    return rc;

  size = SIZE2D((int) width + 10, height + 6);
  base = malloc((size_t) (size.w * 4 * size.h));
  if (base == NULL)
    return result_OOM;

  rc = bitmap_init(&desk->bubble, size, pixelfmt_bgra8888, size.w * 4, NULL,
                   base);
  if (rc != result_OK)
  {
    free(base);
    return rc;
  }

  bitmap_clear(&desk->bubble, colour_rgba(0, 0, 0, 0));
  screen_for_bitmap(&scr, &desk->bubble);
  screen_fill_rounded_rect(&scr, 0, 0, size, 4, colour_rgb(255, 255, 255));
  screen_draw_rounded_rect(&scr, 0, 0, size, 4, colour_rgb(0, 0, 0));
  pos = POINT(5, 3 + ascent);

  return wuss_text_draw(desk->wuss, 0, &scr, "Quack!", 6,
                        colour_rgb(0, 0, 0), colour_rgba(0, 0, 0, 0), &pos,
                        NULL);
}

/* ----------------------------------------------------------------------- */

/* a new random heading, never standing still */
static void deskduck_wander(deskduck_t *desk, duck_t *duck)
{
  do
  {
    duck->heading.x = rng_range(&desk->rng, 3) - 1;
    duck->heading.y = rng_range(&desk->rng, 3) - 1;
  }
  while (duck->heading.x == 0 && duck->heading.y == 0);
}

static int deskduck_sign(int v)
{
  return (v > 0) - (v < 0);
}

/* the eight headings, clockwise from east */
static const point_t deskduck_compass[8] =
{
  { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
  { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }
};

/* heading's index in deskduck_compass, or -1 if it is standing still */
static int deskduck_bearing(point_t heading)
{
  int i;

  for (i = 0; i < 8; i++)
    if (deskduck_compass[i].x == heading.x &&
        deskduck_compass[i].y == heading.y)
      return i;

  return -1;
}

/* Turn a compass point towards (dx,dy) every DUCK_TURN ticks, the shorter
 * way round, so it arcs about and shows the front or back view on the way. */
static void deskduck_steer(duck_t *duck, int dx, int dy)
{
  int to, from;
  int turn;

  to = deskduck_bearing(POINT(deskduck_sign(dx), deskduck_sign(dy)));
  if (to < 0 || ++duck->turning < DUCK_TURN)
    return;
  duck->turning = 0;

  from = deskduck_bearing(duck->heading);
  if (from < 0)
  {
    duck->heading = deskduck_compass[to];
    return;
  }

  turn = (to - from + 8) % 8;
  if (turn != 0)
    duck->heading = deskduck_compass[(from + (turn <= 4 ? 1 : 7)) % 8];
}

static void deskduck_enter(deskduck_t *desk, duck_t *duck, int to)
{
  const deskduck_state_t *from;
  const deskduck_state_t *state;

  from  = &deskduck_states[duck->state];
  state = &deskduck_states[to];

  if ((from->does & BUBBLES) && !(state->does & BUBBLES))
    deskduck_send(desk, wuss_PINBOARD_ACTOR_REMOVE,
                  BUBBLE_ACTOR(deskduck_index(desk, duck)), NULL, duck->pos);
  if (state->does & BUBBLES)
    deskduck_bubble_show(desk, duck);

  duck->state = to;
  duck->timer = state->ticks;
  if (state->spread > 0)
    duck->timer += rng_range(&desk->rng, state->spread);
  if (state->does & ROAMS)
    deskduck_wander(desk, duck);
}

/* take the first transition the events allow, if any */
static void deskduck_fire(deskduck_t *desk, duck_t *duck, unsigned events)
{
  size_t i;

  for (i = 0; i < NELEMS(deskduck_transitions); i++)
  {
    if ((deskduck_transitions[i].from & IN(duck->state)) &&
        (deskduck_transitions[i].on & events))
    {
      deskduck_enter(desk, duck, deskduck_transitions[i].to);
      return;
    }
  }
}

/* where a duck may stand: the positions it may take */
static box_t deskduck_land(const deskduck_t *desk)
{
  size2d_t screen;
  box_t    land;

  screen = wuss_get_screen_size(desk->wuss);
  land.x0 = 0;
  land.y0 = 0;
  land.x1 = screen.w - DUCK_SIZE + 1;
  land.y1 = screen.h - DUCK_ICONBAR - DUCK_SIZE + 1;
  return land;
}

/* where a duck may swim: the positions it may take on the pond
 * ponytail: a box inside the oval, so a corner can stray onto the bank */
static box_t deskduck_water(const deskduck_t *desk)
{
  size2d_t size;
  box_t    water;

  size     = desk->pond_bitmap->size;
  water.x0 = desk->pond.x + 8;
  water.y0 = desk->pond.y - 4;
  water.x1 = desk->pond.x + size.w - DUCK_SIZE - 8 + 1;
  water.y1 = desk->pond.y + size.h - DUCK_SIZE - 4 + 1;
  return water;
}

/* one pixel step along the heading, when speed (px/s) allows, bouncing off
 * the edges of bounds */
static void deskduck_walk(duck_t *duck, box_t bounds, int speed)
{
  int stride;

  if (duck->heading.x != 0)
    duck->left = duck->heading.x < 0;

  /* A pixel step every 60 / speed ticks, a diagonal one root two times
   * that, so no heading beats the top speed. */
  stride        = (duck->heading.x != 0 && duck->heading.y != 0) ? 85 : 60;
  duck->stride += speed;
  if (duck->stride < stride)
    return;
  duck->stride -= stride;
  duck->steps++;

  duck->pos.x += duck->heading.x;
  duck->pos.y += duck->heading.y;
  if (duck->pos.x < bounds.x0 || duck->pos.x >= bounds.x1)
    duck->heading.x = -duck->heading.x;
  if (duck->pos.y < bounds.y0 || duck->pos.y >= bounds.y1)
    duck->heading.y = -duck->heading.y;
  duck->pos.x = CLAMP(duck->pos.x, bounds.x0, bounds.x1 - 1);
  duck->pos.y = CLAMP(duck->pos.y, bounds.y0, bounds.y1 - 1);
}

/* Sense, step the state machine, act, and return the frame to show. */
static int deskduck_tick(deskduck_t *desk,
                         duck_t     *duck,
                         point_t     pointer,
                         int         moved)
{
  unsigned                events;
  box_t                   water;
  int                     wet;
  int                     dx, dy;
  const deskduck_state_t *state;
  int                     alt;
  int                     frame;

  events = moved ? ON_MOVED : 0;
  if (desk->still >= DUCK_SLEEP)
    events |= ON_STILL;
  if (duck->timer > 0 && --duck->timer == 0)
    events |= ON_TIMEOUT | (rng_range(&desk->rng, 3) == 0 ? ON_WHIM : 0);

  water = deskduck_water(desk);
  wet   = box_contains_point(&water, duck->pos.x, duck->pos.y);
  if (wet)
    events |= ON_WET | (duck->wet ? 0 : ON_SPLASH);
  duck->wet = wet;

  dx = pointer.x - (duck->pos.x + DUCK_SIZE / 2);
  dy = pointer.y - (duck->pos.y + DUCK_SIZE / 2);
  if (dx * dx + dy * dy >= DUCK_CHASE * DUCK_CHASE)
    events |= ON_FAR;
  else if (abs(dx) + abs(dy) < DUCK_SIZE / 2)
    events |= ON_CAUGHT;
  else
    events |= ON_NEAR;

  deskduck_fire(desk, duck, events);

  state = &deskduck_states[duck->state];
  /* ponytail: eight-way steps, so a chase zigzags towards the pointer */
  if (state->does & STEERS)
    deskduck_steer(duck, dx, dy);
  if (state->does & HOMES)
    deskduck_steer(duck, (water.x0 + water.x1) / 2 - duck->pos.x,
                   (water.y0 + water.y1) / 2 - duck->pos.y);
  if (state->does & WALKS)
    deskduck_walk(duck, deskduck_land(desk), DUCK_SPEED);
  if (state->does & SWIMS)
    deskduck_walk(duck, water, DUCK_SWIM);

  alt   = state->period ? duck->ticks / state->period : duck->steps;
  frame = (alt & 1) ? state->other : state->frame;
  /* walking straight up or down the screen: the back or front view, as the
   * WALKS states all show FRAME_WALK1 and 2 */
  if ((state->does & WALKS) && duck->heading.x == 0)
    frame += (duck->heading.y > 0 ? FRAME_FRONT1 : FRAME_BACK1) -
             FRAME_WALK1;
  return frame;
}

static void deskduck_idle(deskduck_t *desk)
{
  point_t         pointer;
  int             moved;
  int             i;
  duck_t         *duck;
  const bitmap_t *bitmap;

  if (desk->paused || wuss_is_dragging(desk->wuss))
    return; /* ponytail: any drag holds the ducks, not just one of them */

  pointer = wuss_get_pointer(desk->wuss);
  moved   = pointer.x != desk->pointer.x || pointer.y != desk->pointer.y;
  if (moved)
    desk->still = 0;
  else if (desk->still < DUCK_SLEEP)
    desk->still++;
  desk->pointer = pointer;

  for (i = 0; i < desk->nducks; i++)
  {
    duck = &desk->ducks[i];
    duck->ticks++;

    bitmap = desk->frames[deskduck_tick(desk, duck, pointer, moved)]
                         [duck->left];
    if (bitmap == duck->shown &&
        duck->pos.x == duck->shown_pos.x && duck->pos.y == duck->shown_pos.y)
      continue;

    duck->shown     = bitmap;
    duck->shown_pos = duck->pos;
    deskduck_send(desk, wuss_PINBOARD_ACTOR_SET, DUCK_ACTOR(i), bitmap,
                  duck->pos);
  }
}

/* ----------------------------------------------------------------------- */

/* shade a menu row, or not, in the menu and in it while open */
static void deskduck_shade(deskduck_t *desk, int row, int disabled)
{
  if (disabled)
    desk->menu_items[row].flags |= wuss_MENU_ITEM_DISABLED;
  else
    desk->menu_items[row].flags &=
      (wuss_menu_item_flags_t) ~wuss_MENU_ITEM_DISABLED;

  wuss_menu_disable_item_live(desk->menu_handle, &desk->menu, row, disabled);
}

/* shade Add duck at the most ducks and Remove duck at the last one */
static void deskduck_shade_rows(deskduck_t *desk)
{
  deskduck_shade(desk, DUCK_MENU_ADD, desk->nducks == DUCK_MAX);
  deskduck_shade(desk, DUCK_MENU_REMOVE, desk->nducks <= 1);
}

/* a new duck somewhere on the backdrop, wandering off */
static void deskduck_add(deskduck_t *desk)
{
  box_t   land;
  duck_t *duck;

  if (desk->nducks == DUCK_MAX)
    return;

  land = deskduck_land(desk);
  duck = &desk->ducks[desk->nducks++];
  memset(duck, 0, sizeof(*duck));
  duck->pos = POINT(rng_range(&desk->rng, MAX(land.x1 - 1, 1)),
                    rng_range(&desk->rng, MAX(land.y1 - 1, 1)));
  deskduck_enter(desk, duck, WANDER);
}

/* the last duck, and its bubble, gone */
static void deskduck_remove(deskduck_t *desk)
{
  if (desk->nducks <= 1)
    return;

  desk->nducks--;
  deskduck_send(desk, wuss_PINBOARD_ACTOR_REMOVE, DUCK_ACTOR(desk->nducks),
                NULL, POINT(0, 0));
  deskduck_send(desk, wuss_PINBOARD_ACTOR_REMOVE, BUBBLE_ACTOR(desk->nducks),
                NULL, POINT(0, 0));
}

/* the duck or the pond was dragged */
static void deskduck_dry(deskduck_t *desk, duck_t *duck)
{
  duck->wet = 0; /* so a drop on the pond splashes next tick */
  deskduck_fire(desk, duck, ON_DRY);
  if (deskduck_states[duck->state].does & BUBBLES)
    deskduck_bubble_show(desk, duck);
}

static void deskduck_message(deskduck_t *desk, const wuss_message_t *msg)
{
  const wuss_pinboard_actor_click_t *click;
  const wuss_pinboard_actor_drop_t  *drop;
  int                                i;
  duck_t                            *duck;

  if (msg->action == wuss_MESSAGE_PINBOARD_ACTOR_CLICK &&
      msg->size == sizeof(*click))
  {
    click = (const wuss_pinboard_actor_click_t *) msg->data;
    if (click->owner != desk->delegate || click->id == POND_ACTOR ||
        ACTOR_DUCK(click->id) >= desk->nducks || desk->paused)
      return;
    desk->still = 0; /* so it does not drop straight back to sleep */
    deskduck_fire(desk, &desk->ducks[ACTOR_DUCK(click->id)], ON_CLICK);
  }
  else if (msg->action == wuss_MESSAGE_PINBOARD_ACTOR_DROP &&
           msg->size == sizeof(*drop))
  {
    drop = (const wuss_pinboard_actor_drop_t *) msg->data;
    if (drop->owner != desk->delegate || drop->cancelled)
      return;
    /* Pinboard has already moved it there */
    if (drop->id == POND_ACTOR)
    {
      desk->pond = drop->pos;
      for (i = 0; i < desk->nducks; i++) /* those afloat come ashore */
        deskduck_dry(desk, &desk->ducks[i]);
    }
    else if (drop->id % 2 == 1 && ACTOR_DUCK(drop->id) < desk->nducks)
    {
      duck            = &desk->ducks[ACTOR_DUCK(drop->id)];
      duck->pos       = drop->pos;
      duck->shown_pos = drop->pos;
      deskduck_dry(desk, duck);
    }
  }
}

static result_t deskduck_menu_open(deskduck_t *desk)
{
  static const wuss_proginfo_desc_t desc =
    TASK_PROGINFO_DESC("DeskDuck", "Ducks that roam the backdrop");

  wuss_proginfo_set_desc(&desc);
  desk->menu_items[DUCK_MENU_INFO].window =
    wuss_proginfo_window(desk->delegate);
  wuss_menu_tick_item(&desk->menu, DUCK_MENU_PAUSE, desk->paused);
  deskduck_shade_rows(desk);

  return wuss_iconbar_menu_open(desk->delegate, &desk->menu,
                                &desk->menu_handle);
}

static result_t deskduck_handle(wuss_window_t      *window,
                                const wuss_event_t *event,
                                void               *task_data)
{
  result_t    rc;
  deskduck_t *desk;

  desk = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_IDLE:
    if (desk->quit)
    {
      wuss_task_destroy(desk->delegate); /* its QUIT frees desk */
      return result_OK;
    }
    deskduck_idle(desk);
    return result_OK;

  case wuss_EVENT_ICON:
    if (window == NULL && event->data.iconbar_icon.action == wuss_MOUSE_UP &&
        (event->data.iconbar_icon.button & wuss_BUTTON_MENU))
      return deskduck_menu_open(desk);
    return result_OK;

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu != &desk->menu)
      return result_OK;
    switch (event->data.menu_select.index)
    {
    case DUCK_MENU_ADD:
      deskduck_add(desk);
      deskduck_shade_rows(desk);
      break;

    case DUCK_MENU_REMOVE:
      deskduck_remove(desk);
      deskduck_shade_rows(desk);
      break;

    case DUCK_MENU_PAUSE:
      desk->paused = !desk->paused;
      wuss_menu_tick_item(&desk->menu, DUCK_MENU_PAUSE, desk->paused);
      wuss_menu_tick_item_live(desk->menu_handle, &desk->menu,
                               DUCK_MENU_PAUSE, desk->paused);
      break;

    case DUCK_MENU_QUIT:
      desk->quit = 1; /* not from inside the menu's own dispatch */
      break;
    }
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    if (window == desk->menu_items[DUCK_MENU_INFO].window)
      rc = wuss_proginfo_handle_pre_show();
    else
      rc = result_OK;
    if (rc != result_OK || event->data.pre_show.handle == NULL)
      return rc;
    return wuss_menu_open_window_now(event->data.pre_show.handle,
                                     event->data.pre_show.index);

  case wuss_EVENT_MENU_CLOSED:
    desk->menu_handle = NULL;
    return result_OK;

  case wuss_EVENT_MESSAGE:
    deskduck_message(desk, event->data.message);
    return result_OK;

  case wuss_EVENT_MESSAGE_BOUNCED:
    if (event->data.message->action == wuss_MESSAGE_PINBOARD_ACTOR &&
        !desk->quit)
    {
      (void) wuss_error_report(desk->wuss, NULL, "DeskDuck",
                               "No pinboard is running",
                               wuss_ERROR_BUTTONS_OK, NULL, NULL);
      desk->quit = 1;
    }
    return result_OK;

  case wuss_EVENT_QUIT:
    wuss_proginfo_release(desk->delegate);
    free(desk->bubble.base);
    free(desk);
    return result_OK;

  default:
    return result_OK;
  }
}

/* ----------------------------------------------------------------------- */

result_t deskduck_app_create(wuss_t *wuss)
{
  result_t                 rc;
  deskduck_t              *desk;
  wuss_task_desc_t         desc;
  int                      i;
  size2d_t                 screen;
  wuss_iconbar_icon_spec_t spec;

  desk = calloc(1, sizeof(*desk));
  if (desk == NULL)
    return result_OOM;

  desk->wuss = wuss;

  WUSS_MENU_ITEM_WINDOW(desk->menu_items, DUCK_MENU_INFO, "Info",
                        wuss_MENU_ITEM_BORROWED_SUBMENU |
                        wuss_MENU_ITEM_PRE_OPEN,
                        NULL); /* retargeted at the shared proginfo
                                * singleton when the menu opens */
  WUSS_MENU_ITEM(desk->menu_items, DUCK_MENU_ADD, "Add duck",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM(desk->menu_items, DUCK_MENU_REMOVE, "Remove duck",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(desk->menu_items, DUCK_MENU_PAUSE, "Pause",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM(desk->menu_items, DUCK_MENU_QUIT, "Quit",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(desk->menu, "DeskDuck", desk->menu_items,
                  NELEMS(desk->menu_items));

  desc.handle    = deskduck_handle;
  desc.task_data = desk;
  desc.name      = "DeskDuck";
  rc = wuss_task_create(wuss, &desc, &desk->delegate);
  if (rc != result_OK)
  {
    free(desk); /* nothing registered yet */
    return rc;
  }

  rc = wuss_task_iconpool_load_resource(desk->delegate,
                                        wuss_get_resources(wuss));
  if (rc != result_OK)
    goto failure;

  /* one sheet: a row facing right, then the same frames facing left */
  for (i = 0; i < FRAME__LIMIT * 2; i++)
  {
    desk->frames[i % FRAME__LIMIT][i / FRAME__LIMIT] =
      wuss_task_iconpool_frame(desk->delegate, "deskduck", i);
    if (desk->frames[i % FRAME__LIMIT][i / FRAME__LIMIT] == NULL)
    {
      rc = result_NOT_FOUND;
      goto failure;
    }
  }

  desk->pond_bitmap = wuss_task_iconpool_find(desk->delegate, "pond");
  if (desk->pond_bitmap == NULL)
  {
    rc = result_NOT_FOUND;
    goto failure;
  }

  rc = deskduck_bubble_create(desk);
  if (rc != result_OK)
    goto failure;

  rng_seed(&desk->rng, (uint32_t) time(NULL) ^ (uint32_t) (size_t) desk);
  screen     = wuss_get_screen_size(wuss);
  desk->pond = POINT(rng_range(&desk->rng,
                               MAX(screen.w - desk->pond_bitmap->size.w, 1)),
                     rng_range(&desk->rng,
                               MAX(screen.h - DUCK_ICONBAR -
                                   desk->pond_bitmap->size.h, 1)));
  deskduck_send(desk, wuss_PINBOARD_ACTOR_SET, POND_ACTOR, desk->pond_bitmap,
                desk->pond); /* first, so it lies under the ducks */
  desk->pointer = wuss_get_pointer(wuss);
  deskduck_add(desk);

  memset(&spec, 0, sizeof(spec));
  spec.text  = NULL;
  spec.side  = wuss_ICONBAR_RIGHT;
  spec.image = wuss_task_iconpool_find(desk->delegate, "app_deskduck");
  rc = wuss_iconbar_icon_create(wuss, desk->delegate, &spec,
                                &desk->iconbar_icon);
  if (rc != result_OK)
    goto failure;

  return result_OK;

failure:
  wuss_task_destroy(desk->delegate); /* its QUIT frees desk */
  return rc;
}

#endif /* WUSS_APP && WUSS_ICONBAR */
