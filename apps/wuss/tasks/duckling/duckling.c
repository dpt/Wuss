/* wuss/tasks/duckling/duckling.c -- a desk pet duck that roams the backdrop */

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
#define DUCK_MAX        7   /* ducks: the pond, Mama, ducks and bubbles fit
                             * Pinboard's 16 actor slots */
#define MAMA_SPEED      16  /* px/s, slower, so the ducks keep up */
#define MAMA_BOB        4   /* px walked per 1px bob */

/* Actor ids. Pinboard draws actors in slot order, and the pond is sent
 * first, so it takes a lower slot and lies under the ducks.
 * ponytail: holds only while no other task frees a slot below, and a bubble
 * may take a slot under a later duck; give actors a depth if that bites. */
#define POND_ACTOR      0
#define DUCK_ACTOR(i)   (1 + 2 * (i))
#define BUBBLE_ACTOR(i) (2 + 2 * (i))
#define ACTOR_DUCK(id)  (((id) - 1) / 2) /* the duck a duck or bubble is */
#define MAMA_ACTOR      (2 * DUCK_MAX + 1) /* after the last bubble */

/* mama_33x34.png is hand-drawn: a column per heading, in duckling_compass
 * order. She has no walk or swim frames, so she bobs as she goes. */

enum /* the columns of duckling_16x16.png */
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
 * first row of duckling_transitions whose states hold the current one and
 * whose event happened picks the next state, and a click is one more event.
 * Entering a state starts its timer, and duckling_states says what the duck
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
  FOLLOW,  /* walks behind Mama, or the duck before it */
  STATE__LIMIT
};

#define IN(state) (1u << (state))
#define ANY       (IN(STATE__LIMIT) - 1)
#define AWAKE     (IN(PAUSE) | IN(WANDER) | IN(CHASE) | IN(CAUGHT) | \
                   IN(TO_POND) | IN(FOLLOW))
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
  ON_DRY     = 1 << 10, /* the duck or the pond was dragged */
  ON_MAMA    = 1 << 11, /* Mama is out */
  ON_ALONE   = 1 << 12  /* ... or not */
};

enum /* what a state does */
{
  WALKS   = 1 << 0, /* steps along the heading, on the backdrop */
  SWIMS   = 1 << 1, /* steps along the heading, on the pond */
  STEERS  = 1 << 2, /* heads at the pointer */
  HOMES   = 1 << 3, /* heads for the pond */
  ROAMS   = 1 << 4, /* takes a random heading on entry */
  BUBBLES = 1 << 5, /* shows the bubble */
  FOLLOWS = 1 << 6  /* heads for its leader, resting when close */
};

typedef struct duckling_state
{
  unsigned char frame, other; /* alternated */
  unsigned char period;       /* ticks per frame, 0 to alternate per step */
  unsigned char does;
  short         ticks;        /* the timer on entry, 0 for none */
  short         spread;       /* plus up to this, at random */
}
duckling_state_t;

static const duckling_state_t duckling_states[STATE__LIMIT] =
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
  [SQUACK]  = { FRAME_SQUACK, FRAME_SQUACK,  0, BUBBLES,         90,   0 },
  [FOLLOW]  = { FRAME_WALK1,  FRAME_WALK2,   0, WALKS | FOLLOWS,  0,   0 }
};

typedef struct duckling_transition
{
  unsigned      from; /* IN() set of states */
  unsigned      on;   /* event */
  unsigned char to;
}
duckling_transition_t;

/* in priority order */
static const duckling_transition_t duckling_transitions[] =
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
  { IN(FOLLOW),                          ON_ALONE,   PAUSE   },
  { IN(PAUSE) | IN(WANDER),              ON_MAMA,    FOLLOW  },
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
  DUCK_MENU_MAMA,
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

typedef struct duckling
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
  const bitmap_t      *mama_frames[8]; /* by duckling_compass heading */
  size2d_t             mama_size;
  duck_t               mama;
  int                  mama_out;
  int                  mama_face;      /* duckling_compass point shown */
  int                  still;   /* ticks the pointer has been still */
  point_t              pointer; /* where the pointer was last tick */
  int                  paused;
  int                  quit;    /* destroy the task on the next idle */
}
duckling_t;

/* ----------------------------------------------------------------------- */

static void duckling_send(duckling_t     *desk,
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
static int duckling_index(const duckling_t *desk, const duck_t *duck)
{
  return (int) (duck - desk->ducks);
}

/* show duck's bubble, if it has one */
static void duckling_bubble_show(duckling_t *desk, const duck_t *duck)
{
  if (desk->bubble.base == NULL)
    return;

  duckling_send(desk, wuss_PINBOARD_ACTOR_SET,
                BUBBLE_ACTOR(duckling_index(desk, duck)), &desk->bubble,
                POINT(duck->pos.x + DUCK_SIZE / 2 - desk->bubble.size.w / 2,
                      duck->pos.y - desk->bubble.size.h));
}

/* the bubble's bitmap: "Quack!" in the system font on a rounded rectangle */
static result_t duckling_bubble_create(duckling_t *desk)
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
static void duckling_wander(duckling_t *desk, duck_t *duck)
{
  do
  {
    duck->heading.x = rng_range(&desk->rng, 3) - 1;
    duck->heading.y = rng_range(&desk->rng, 3) - 1;
  }
  while (duck->heading.x == 0 && duck->heading.y == 0);
}

/* the eight headings, clockwise from east */
static const point_t duckling_compass[8] =
{
  { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
  { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }
};

/* heading's index in duckling_compass, or -1 if it is standing still */
static int duckling_bearing(point_t heading)
{
  int i;

  for (i = 0; i < 8; i++)
    if (duckling_compass[i].x == heading.x &&
        duckling_compass[i].y == heading.y)
      return i;

  return -1;
}

/* Turn a compass point towards (dx,dy) every DUCK_TURN ticks, the shorter
 * way round, so it arcs about and shows the front or back view on the way. */
static void duckling_steer(duck_t *duck, int dx, int dy)
{
  int to, from;
  int turn;

  to = duckling_bearing(POINT(SIGN(dx), SIGN(dy)));
  if (to < 0 || ++duck->turning < DUCK_TURN)
    return;
  duck->turning = 0;

  from = duckling_bearing(duck->heading);
  if (from < 0)
  {
    duck->heading = duckling_compass[to];
    return;
  }

  turn = (to - from + 8) % 8;
  if (turn != 0)
    duck->heading = duckling_compass[(from + (turn <= 4 ? 1 : 7)) % 8];
}

static void duckling_enter(duckling_t *desk, duck_t *duck, int to)
{
  const duckling_state_t *from;
  const duckling_state_t *state;

  from  = &duckling_states[duck->state];
  state = &duckling_states[to];

  if ((from->does & BUBBLES) && !(state->does & BUBBLES))
    duckling_send(desk, wuss_PINBOARD_ACTOR_REMOVE,
                  BUBBLE_ACTOR(duckling_index(desk, duck)), NULL, duck->pos);
  if (state->does & BUBBLES)
    duckling_bubble_show(desk, duck);

  duck->state = to;
  duck->timer = state->ticks;
  if (state->spread > 0)
    duck->timer += rng_range(&desk->rng, state->spread);
  if (state->does & ROAMS)
    duckling_wander(desk, duck);
}

/* take the first transition the events allow, if any */
static void duckling_fire(duckling_t *desk, duck_t *duck, unsigned events)
{
  size_t i;

  for (i = 0; i < NELEMS(duckling_transitions); i++)
  {
    if ((duckling_transitions[i].from & IN(duck->state)) &&
        (duckling_transitions[i].on & events))
    {
      duckling_enter(desk, duck, duckling_transitions[i].to);
      return;
    }
  }
}

/* the positions a duck of the given size may take wholly inside the w by h
 * area at x,y */
static box_t duckling_room(int x, int y, int w, int h, size2d_t duck)
{
  box_t room;

  room.x0 = x;
  room.y0 = y;
  room.x1 = x + w - duck.w + 1;
  room.y1 = y + h - duck.h + 1;
  return room;
}

/* where a duck of the given size may stand */
static box_t duckling_land(const duckling_t *desk, size2d_t duck)
{
  size2d_t screen;

  screen = wuss_get_screen_size(desk->wuss);
  return duckling_room(0, 0, screen.w, screen.h - DUCK_ICONBAR, duck);
}

/* where a duck of the given size may swim: its feet in the same band of the
 * pond whatever its height, so Mama, taller than the pond, fits too
 * ponytail: a box inside the oval, so a corner can stray onto the bank */
static box_t duckling_water(const duckling_t *desk, size2d_t duck)
{
  size2d_t size;

  size = desk->pond_bitmap->size;
  return duckling_room(desk->pond.x + 8, desk->pond.y + 12 - duck.h,
                       size.w - 16, size.h - 16 + duck.h, duck);
}

/* one pixel step along the heading, when speed (px/s) allows, bouncing off
 * the edges of bounds */
static void duckling_walk(duck_t *duck, box_t bounds, int speed)
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

/* the events from duck's timer and the pond, of which water is the part it
 * may swim */
static unsigned duckling_sense(duckling_t *desk, duck_t *duck, box_t water)
{
  unsigned events;
  int      wet;

  events = 0;
  if (duck->timer > 0 && --duck->timer == 0)
    events |= ON_TIMEOUT | (rng_range(&desk->rng, 3) == 0 ? ON_WHIM : 0);

  wet = box_contains_point(&water, duck->pos.x, duck->pos.y);
  if (wet)
    events |= ON_WET | (duck->wet ? 0 : ON_SPLASH);
  duck->wet = wet;
  return events;
}

/* Set (*dx,*dy) to the way from duck to whoever it follows, Mama for the
 * first duck, and return non-zero if it is close enough to rest. */
static int duckling_ahead(const duckling_t *desk,
                          const duck_t     *duck,
                          int              *dx,
                          int              *dy)
{
  const duck_t *leader;
  size2d_t      size;

  if (duck == desk->ducks)
  {
    leader = &desk->mama;
    size   = desk->mama_size;
  }
  else
  {
    leader = duck - 1;
    size   = SIZE2D(DUCK_SIZE, DUCK_SIZE);
  }

  *dx = leader->pos.x + size.w / 2 - (duck->pos.x + DUCK_SIZE / 2);
  *dy = leader->pos.y + size.h / 2 - (duck->pos.y + DUCK_SIZE / 2);
  return MAX(abs(*dx), abs(*dy)) <= (size.w + DUCK_SIZE) / 2 + 2;
}

/* Sense, step the state machine, act, and return the frame to show. */
static int duckling_tick(duckling_t *desk,
                         duck_t     *duck,
                         point_t     pointer,
                         int         moved)
{
  box_t                   water;
  unsigned                events;
  int                     dx, dy;
  const duckling_state_t *state;
  int                     rest;
  int                     alt;
  int                     frame;

  water  = duckling_water(desk, SIZE2D(DUCK_SIZE, DUCK_SIZE));
  events = duckling_sense(desk, duck, water);
  if (moved)
    events |= ON_MOVED;
  if (desk->still >= DUCK_SLEEP)
    events |= ON_STILL;
  events |= desk->mama_out ? ON_MAMA : ON_ALONE;

  dx = pointer.x - (duck->pos.x + DUCK_SIZE / 2);
  dy = pointer.y - (duck->pos.y + DUCK_SIZE / 2);
  if (dx * dx + dy * dy >= DUCK_CHASE * DUCK_CHASE)
    events |= ON_FAR;
  else if (abs(dx) + abs(dy) < DUCK_SIZE / 2)
    events |= ON_CAUGHT;
  else
    events |= ON_NEAR;

  duckling_fire(desk, duck, events);

  state = &duckling_states[duck->state];
  rest  = 0;
  /* ponytail: eight-way steps, so a chase zigzags towards the pointer */
  if (state->does & STEERS)
    duckling_steer(duck, dx, dy);
  if (state->does & HOMES)
    duckling_steer(duck, (water.x0 + water.x1) / 2 - duck->pos.x,
                   (water.y0 + water.y1) / 2 - duck->pos.y);
  if (state->does & FOLLOWS)
  {
    rest = duckling_ahead(desk, duck, &dx, &dy);
    if (!rest)
      duckling_steer(duck, dx, dy);
  }
  if ((state->does & WALKS) && !rest)
    duckling_walk(duck, duckling_land(desk, SIZE2D(DUCK_SIZE, DUCK_SIZE)),
                  DUCK_SPEED);
  if (state->does & SWIMS)
    duckling_walk(duck, water, DUCK_SWIM);

  if (rest)
    return FRAME_IDLE;

  alt   = state->period ? duck->ticks / state->period : duck->steps;
  frame = (alt & 1) ? state->other : state->frame;
  /* walking straight up or down the screen: the back or front view, as the
   * WALKS states all show FRAME_WALK1 and 2 */
  if ((state->does & WALKS) && duck->heading.x == 0)
    frame += (duck->heading.y > 0 ? FRAME_FRONT1 : FRAME_BACK1) -
             FRAME_WALK1;
  return frame;
}

/* Mama runs the same machine, but with only the pointerless events, so she
 * never chases, quacks or sleeps. She turns on the spot before she sets off
 * a new way, an eighth every DUCK_TURN ticks, and bobs as she goes. */
static void duckling_mama_tick(duckling_t *desk)
{
  duck_t                 *mama;
  box_t                   water;
  const duckling_state_t *state;
  int                     to;
  int                     turn;
  int                     bob;
  const bitmap_t         *bitmap;
  point_t                 pos;

  mama  = &desk->mama;
  water = duckling_water(desk, desk->mama_size);
  duckling_fire(desk, mama, duckling_sense(desk, mama, water));

  state = &duckling_states[mama->state];
  if (state->does & HOMES)
    mama->heading = POINT(SIGN((water.x0 + water.x1) / 2 - mama->pos.x),
                          SIGN((water.y0 + water.y1) / 2 - mama->pos.y));

  to   = duckling_bearing(mama->heading);
  turn = to < 0 ? 0 : (to - desk->mama_face + 8) % 8;
  if (turn != 0 && ++mama->turning >= DUCK_TURN)
  {
    mama->turning   = 0;
    desk->mama_face = (desk->mama_face + (turn <= 4 ? 1 : 7)) % 8;
  }

  bob = 0;
  if (turn <= 1 || turn == 7) /* a slight turn she makes on the move */
  {
    if (state->does & WALKS)
      duckling_walk(mama, duckling_land(desk, desk->mama_size), MAMA_SPEED);
    if (state->does & SWIMS)
      duckling_walk(mama, water, DUCK_SWIM);
    if (state->does & (WALKS | SWIMS))
      bob = (mama->steps / MAMA_BOB) & 1;
  }

  bitmap = desk->mama_frames[desk->mama_face];
  pos    = POINT(mama->pos.x, mama->pos.y - bob);
  if (bitmap == mama->shown &&
      pos.x == mama->shown_pos.x && pos.y == mama->shown_pos.y)
    return;

  mama->shown     = bitmap;
  mama->shown_pos = pos;
  duckling_send(desk, wuss_PINBOARD_ACTOR_SET, MAMA_ACTOR, bitmap, pos);
}

static void duckling_idle(duckling_t *desk)
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

  if (desk->mama_out)
    duckling_mama_tick(desk); /* first, so the ducks follow where she is */

  for (i = 0; i < desk->nducks; i++)
  {
    duck = &desk->ducks[i];
    duck->ticks++;

    bitmap = desk->frames[duckling_tick(desk, duck, pointer, moved)]
                         [duck->left];
    if (bitmap == duck->shown &&
        duck->pos.x == duck->shown_pos.x && duck->pos.y == duck->shown_pos.y)
      continue;

    duck->shown     = bitmap;
    duck->shown_pos = duck->pos;
    duckling_send(desk, wuss_PINBOARD_ACTOR_SET, DUCK_ACTOR(i), bitmap,
                  duck->pos);
  }
}

/* ----------------------------------------------------------------------- */

/* shade Add duck at the most ducks and Remove duck at the last one */
static void duckling_shade_rows(duckling_t *desk)
{
  wuss_menu_disable_item_live(desk->menu_handle, &desk->menu, DUCK_MENU_ADD,
                              desk->nducks == DUCK_MAX);
  wuss_menu_disable_item_live(desk->menu_handle, &desk->menu,
                              DUCK_MENU_REMOVE, desk->nducks <= 1);
}

/* a new duck somewhere on the backdrop, wandering off */
static void duckling_add(duckling_t *desk)
{
  box_t   land;
  duck_t *duck;

  if (desk->nducks == DUCK_MAX)
    return;

  land = duckling_land(desk, SIZE2D(DUCK_SIZE, DUCK_SIZE));
  duck = &desk->ducks[desk->nducks++];
  memset(duck, 0, sizeof(*duck));
  duck->pos = POINT(rng_range(&desk->rng, MAX(land.x1 - 1, 1)),
                    rng_range(&desk->rng, MAX(land.y1 - 1, 1)));
  duckling_enter(desk, duck, WANDER);
}

/* the last duck, and its bubble, gone */
static void duckling_remove(duckling_t *desk)
{
  if (desk->nducks <= 1)
    return;

  desk->nducks--;
  duckling_send(desk, wuss_PINBOARD_ACTOR_REMOVE, DUCK_ACTOR(desk->nducks),
                NULL, POINT(0, 0));
  duckling_send(desk, wuss_PINBOARD_ACTOR_REMOVE, BUBBLE_ACTOR(desk->nducks),
                NULL, POINT(0, 0));
}

/* Mama out somewhere on the backdrop, facing the viewer, or back in */
static void duckling_mama(duckling_t *desk, int out)
{
  box_t   land;
  duck_t *mama;

  desk->mama_out = out;
  if (!out)
  {
    duckling_send(desk, wuss_PINBOARD_ACTOR_REMOVE, MAMA_ACTOR, NULL,
                  POINT(0, 0));
    return;
  }

  land = duckling_land(desk, desk->mama_size);
  mama = &desk->mama;
  memset(mama, 0, sizeof(*mama));
  mama->pos = POINT(rng_range(&desk->rng, MAX(land.x1 - 1, 1)),
                    rng_range(&desk->rng, MAX(land.y1 - 1, 1)));
  desk->mama_face = 2; /* south */
  duckling_enter(desk, mama, WANDER);
}

/* the duck or the pond was dragged */
static void duckling_dry(duckling_t *desk, duck_t *duck)
{
  duck->wet = 0; /* so a drop on the pond splashes next tick */
  duckling_fire(desk, duck, ON_DRY);
  if (duckling_states[duck->state].does & BUBBLES)
    duckling_bubble_show(desk, duck);
}

static void duckling_message(duckling_t *desk, const wuss_message_t *msg)
{
  const wuss_pinboard_actor_click_t *click;
  const wuss_pinboard_actor_drop_t  *drop;
  int                                i;
  duck_t                            *duck;

  if (msg->action == wuss_MESSAGE_PINBOARD_ACTOR_CLICK &&
      msg->size == sizeof(*click))
  {
    click = (const wuss_pinboard_actor_click_t *) msg->data;
    if (click->actor.owner != desk->delegate ||
        click->actor.id == POND_ACTOR || click->actor.id == MAMA_ACTOR ||
        ACTOR_DUCK(click->actor.id) >= desk->nducks || desk->paused)
      return;
    desk->still = 0; /* so it does not drop straight back to sleep */
    duckling_fire(desk, &desk->ducks[ACTOR_DUCK(click->actor.id)],
                  ON_CLICK);
  }
  else if (msg->action == wuss_MESSAGE_PINBOARD_ACTOR_DROP &&
           msg->size == sizeof(*drop))
  {
    drop = (const wuss_pinboard_actor_drop_t *) msg->data;
    if (drop->actor.owner != desk->delegate || drop->cancelled)
      return;
    /* Pinboard has already moved it there */
    if (drop->actor.id == POND_ACTOR)
    {
      desk->pond = drop->pos;
      for (i = 0; i < desk->nducks; i++) /* those afloat come ashore */
        duckling_dry(desk, &desk->ducks[i]);
      if (desk->mama_out)
        duckling_dry(desk, &desk->mama);
    }
    else if (drop->actor.id == MAMA_ACTOR && desk->mama_out)
    {
      desk->mama.pos       = drop->pos;
      desk->mama.shown_pos = drop->pos;
      duckling_dry(desk, &desk->mama);
    }
    else if (drop->actor.id % 2 == 1 &&
             ACTOR_DUCK(drop->actor.id) < desk->nducks)
    {
      duck            = &desk->ducks[ACTOR_DUCK(drop->actor.id)];
      duck->pos       = drop->pos;
      duck->shown_pos = drop->pos;
      duckling_dry(desk, duck);
    }
  }
}

static result_t duckling_menu_open(duckling_t *desk)
{
  static const wuss_proginfo_desc_t desc =
    TASK_PROGINFO_DESC("Duckling", "Ducks that roam the backdrop");

  wuss_proginfo_set_desc(&desc);
  desk->menu_items[DUCK_MENU_INFO].window =
    wuss_proginfo_window(desk->delegate);
  wuss_menu_tick_item(&desk->menu, DUCK_MENU_MAMA, desk->mama_out);
  wuss_menu_tick_item(&desk->menu, DUCK_MENU_PAUSE, desk->paused);
  duckling_shade_rows(desk);

  return wuss_iconbar_menu_open(desk->delegate, &desk->menu,
                                &desk->menu_handle);
}

static result_t duckling_handle(wuss_window_t      *window,
                                const wuss_event_t *event,
                                void               *task_data)
{
  result_t    rc;
  duckling_t *desk;

  desk = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_IDLE:
    if (desk->quit)
    {
      wuss_task_destroy(desk->delegate); /* its QUIT frees desk */
      return result_OK;
    }
    duckling_idle(desk);
    return result_OK;

  case wuss_EVENT_ICON:
    if (window == NULL && event->data.iconbar_icon.action == wuss_MOUSE_UP &&
        (event->data.iconbar_icon.button & wuss_BUTTON_MENU))
      return duckling_menu_open(desk);
    return result_OK;

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu != &desk->menu)
      return result_OK;
    switch (event->data.menu_select.index)
    {
    case DUCK_MENU_ADD:
      duckling_add(desk);
      duckling_shade_rows(desk);
      break;

    case DUCK_MENU_REMOVE:
      duckling_remove(desk);
      duckling_shade_rows(desk);
      break;

    case DUCK_MENU_MAMA:
      duckling_mama(desk, !desk->mama_out);
      wuss_menu_tick_item_live(desk->menu_handle, &desk->menu,
                               DUCK_MENU_MAMA, desk->mama_out);
      break;

    case DUCK_MENU_PAUSE:
      desk->paused = !desk->paused;
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
    duckling_message(desk, event->data.message);
    return result_OK;

  case wuss_EVENT_MESSAGE_BOUNCED:
    if (event->data.message->action == wuss_MESSAGE_PINBOARD_ACTOR &&
        !desk->quit)
    {
      (void) wuss_error_report(desk->wuss, NULL, "Duckling",
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

result_t duckling_app_create(wuss_t *wuss)
{
  result_t                 rc;
  duckling_t              *desk;
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
  WUSS_MENU_ITEM(desk->menu_items, DUCK_MENU_MAMA, "Mama duck",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(desk->menu_items, DUCK_MENU_PAUSE, "Pause",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM(desk->menu_items, DUCK_MENU_QUIT, "Quit",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(desk->menu, "Duckling", desk->menu_items,
                  NELEMS(desk->menu_items));

  desc.handle    = duckling_handle;
  desc.task_data = desk;
  desc.name      = "Duckling";
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
      wuss_task_iconpool_frame(desk->delegate, "duckling", i);
    if (desk->frames[i % FRAME__LIMIT][i / FRAME__LIMIT] == NULL)
    {
      rc = result_NOT_FOUND;
      goto failure;
    }
  }

  for (i = 0; i < 8; i++)
  {
    desk->mama_frames[i] = wuss_task_iconpool_frame(desk->delegate, "mama",
                                                    i);
    if (desk->mama_frames[i] == NULL)
    {
      rc = result_NOT_FOUND;
      goto failure;
    }
  }
  desk->mama_size = desk->mama_frames[0]->size;

  desk->pond_bitmap = wuss_task_iconpool_find(desk->delegate, "pond");
  if (desk->pond_bitmap == NULL)
  {
    rc = result_NOT_FOUND;
    goto failure;
  }

  rc = duckling_bubble_create(desk);
  if (rc != result_OK)
    goto failure;

  rng_seed(&desk->rng, (uint32_t) time(NULL) ^ (uint32_t) (size_t) desk);
  screen     = wuss_get_screen_size(wuss);
  desk->pond = POINT(rng_range(&desk->rng,
                               MAX(screen.w - desk->pond_bitmap->size.w, 1)),
                     rng_range(&desk->rng,
                               MAX(screen.h - DUCK_ICONBAR -
                                   desk->pond_bitmap->size.h, 1)));
  duckling_send(desk, wuss_PINBOARD_ACTOR_SET, POND_ACTOR, desk->pond_bitmap,
                desk->pond); /* first, so it lies under the ducks */
  desk->pointer = wuss_get_pointer(wuss);
  duckling_add(desk);

  memset(&spec, 0, sizeof(spec));
  spec.text  = NULL;
  spec.side  = wuss_ICONBAR_RIGHT;
  spec.image = wuss_task_iconpool_find(desk->delegate, "app_duckling");
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
