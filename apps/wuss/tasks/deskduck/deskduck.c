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

/* Actor ids. Pinboard draws actors in slot order, and the pond is sent
 * first, so it takes a lower slot and lies under the duck.
 * ponytail: holds only while no other task frees a slot below; give actors
 * a depth if that bites. */
enum { POND_ACTOR, DUCK_ACTOR, BUBBLE_ACTOR };

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
  STATE__LIMIT
};

#define IN(state) (1u << (state))
#define ANY       (IN(STATE__LIMIT) - 1)
#define AWAKE     (IN(PAUSE) | IN(WANDER) | IN(CHASE) | IN(CAUGHT) | \
                   IN(TO_POND))

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
  [CAUGHT]  = { FRAME_IDLE,   FRAME_IDLE,    0, 0,                0,   0 },
  [QUACK]   = { FRAME_QUACK,  FRAME_QUACK,   0, BUBBLES,         90,   0 },
  [SLEEP]   = { FRAME_SLEEP1, FRAME_SLEEP2, 32, 0,                0,   0 },
  [TO_POND] = { FRAME_WALK1,  FRAME_WALK2,   0, WALKS | HOMES,    0,   0 },
  [SWIM]    = { FRAME_SWIM1,  FRAME_SWIM2,  30, SWIMS | ROAMS,  600, 900 }
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
  { ANY & ~IN(SWIM),                     ON_CLICK,   QUACK   },
  { IN(QUACK),                           ON_TIMEOUT, PAUSE   },
  { IN(SWIM),                            ON_DRY,     WANDER  },
  { ANY & ~IN(SWIM),                     ON_SPLASH,  SWIM    },
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
  DUCK_MENU_PAUSE,
  DUCK_MENU_QUIT,
  DUCK_MENU__LIMIT
};

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
  point_t              pos;
  point_t              heading; /* each -1, 0 or 1 */
  int                  left;    /* faces left */
  int                  state;
  int                  timer;   /* ticks left in the state, 0 for none */
  const bitmap_t      *pond_bitmap;
  point_t              pond;
  int                  wet;     /* on the pond last tick */
  int                  still;   /* ticks the pointer has been still */
  int                  ticks;
  int                  stride;  /* px/s summed per tick; a step at 60 */
  int                  steps;   /* px walked, for the waddle frame */
  point_t              pointer; /* where the pointer was last tick */
  const bitmap_t      *shown;   /* the frame last sent, NULL for none */
  point_t              shown_pos;
  int                  paused;
  int                  quit;    /* destroy the task on the next idle */
}
deskduck_t;

/* ----------------------------------------------------------------------- */

static void deskduck_send(deskduck_t     *duck,
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
  (void) wuss_send_recorded(duck->wuss, duck->delegate, NULL,
                            wuss_MESSAGE_PINBOARD_ACTOR, &actor,
                            sizeof(actor), 0, NULL);
}

static point_t deskduck_bubble_pos(const deskduck_t *duck)
{
  return POINT(duck->pos.x + DUCK_SIZE / 2 - duck->bubble.size.w / 2,
               duck->pos.y - duck->bubble.size.h);
}

/* the bubble's bitmap: "Quack!" in the system font on a rounded rectangle */
static result_t deskduck_bubble_create(deskduck_t *duck)
{
  result_t       rc;
  bmfont_t      *font;
  int            height, ascent;
  bmfont_width_t width;
  size2d_t       size;
  void          *base;
  screen_t       scr;
  point_t        pos;

  font = wuss_get_font(duck->wuss);
  if (font == NULL)
    return result_OK; /* no bubble */

  bmfont_get_info(font, NULL, &height, &ascent, NULL);
  width = 0;
  rc = wuss_text_measure(duck->wuss, 0, "Quack!", 6, INT_MAX, NULL, &width);
  if (rc != result_OK)
    return rc;

  size = SIZE2D((int) width + 10, height + 6);
  base = malloc((size_t) (size.w * 4 * size.h));
  if (base == NULL)
    return result_OOM;

  rc = bitmap_init(&duck->bubble, size, pixelfmt_bgra8888, size.w * 4, NULL,
                   base);
  if (rc != result_OK)
  {
    free(base);
    return rc;
  }

  bitmap_clear(&duck->bubble, colour_rgba(0, 0, 0, 0));
  screen_for_bitmap(&scr, &duck->bubble);
  screen_fill_rounded_rect(&scr, 0, 0, size, 4, colour_rgb(255, 255, 255));
  screen_draw_rounded_rect(&scr, 0, 0, size, 4, colour_rgb(0, 0, 0));
  pos = POINT(5, 3 + ascent);

  return wuss_text_draw(duck->wuss, 0, &scr, "Quack!", 6,
                        colour_rgb(0, 0, 0), colour_rgba(0, 0, 0, 0), &pos,
                        NULL);
}

/* ----------------------------------------------------------------------- */

/* a new random heading, never standing still */
static void deskduck_wander(deskduck_t *duck)
{
  do
  {
    duck->heading.x = rng_range(&duck->rng, 3) - 1;
    duck->heading.y = rng_range(&duck->rng, 3) - 1;
  }
  while (duck->heading.x == 0 && duck->heading.y == 0);
}

static int deskduck_sign(int v)
{
  return (v > 0) - (v < 0);
}

static void deskduck_enter(deskduck_t *duck, int to)
{
  const deskduck_state_t *from;
  const deskduck_state_t *state;

  from  = &deskduck_states[duck->state];
  state = &deskduck_states[to];

  if ((from->does & BUBBLES) && !(state->does & BUBBLES))
    deskduck_send(duck, wuss_PINBOARD_ACTOR_REMOVE, BUBBLE_ACTOR, NULL,
                  duck->pos);
  if ((state->does & BUBBLES) && duck->bubble.base != NULL)
    deskduck_send(duck, wuss_PINBOARD_ACTOR_SET, BUBBLE_ACTOR,
                  &duck->bubble, deskduck_bubble_pos(duck));

  duck->state = to;
  duck->timer = state->ticks;
  if (state->spread > 0)
    duck->timer += rng_range(&duck->rng, state->spread);
  if (state->does & ROAMS)
    deskduck_wander(duck);
}

/* take the first transition the events allow, if any */
static void deskduck_fire(deskduck_t *duck, unsigned events)
{
  size_t i;

  for (i = 0; i < NELEMS(deskduck_transitions); i++)
  {
    if ((deskduck_transitions[i].from & IN(duck->state)) &&
        (deskduck_transitions[i].on & events))
    {
      deskduck_enter(duck, deskduck_transitions[i].to);
      return;
    }
  }
}

/* where the duck may stand: the positions it may take */
static box_t deskduck_land(const deskduck_t *duck)
{
  size2d_t screen;
  box_t    land;

  screen = wuss_get_screen_size(duck->wuss);
  land.x0 = 0;
  land.y0 = 0;
  land.x1 = screen.w - DUCK_SIZE + 1;
  land.y1 = screen.h - DUCK_ICONBAR - DUCK_SIZE + 1;
  return land;
}

/* where the duck may swim: the positions it may take on the pond
 * ponytail: a box inside the oval, so a corner can stray onto the bank */
static box_t deskduck_water(const deskduck_t *duck)
{
  size2d_t size;
  box_t    water;

  size     = duck->pond_bitmap->size;
  water.x0 = duck->pond.x + 8;
  water.y0 = duck->pond.y - 4;
  water.x1 = duck->pond.x + size.w - DUCK_SIZE - 8 + 1;
  water.y1 = duck->pond.y + size.h - DUCK_SIZE - 4 + 1;
  return water;
}

/* one pixel step along the heading, when the speed allows, bouncing off
 * the edges of bounds */
static void deskduck_walk(deskduck_t *duck, box_t bounds)
{
  int stride;

  if (duck->heading.x != 0)
    duck->left = duck->heading.x < 0;

  /* A pixel step every 60 / DUCK_SPEED ticks, a diagonal one root two times
   * that, so no heading beats the top speed. */
  stride        = (duck->heading.x != 0 && duck->heading.y != 0) ? 85 : 60;
  duck->stride += DUCK_SPEED;
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
static int deskduck_tick(deskduck_t *duck, point_t pointer, int moved)
{
  unsigned                events;
  box_t                   water;
  int                     wet;
  int                     dx, dy;
  const deskduck_state_t *state;
  int                     alt;

  events = moved ? ON_MOVED : 0;
  if (duck->still >= DUCK_SLEEP)
    events |= ON_STILL;
  if (duck->timer > 0 && --duck->timer == 0)
    events |= ON_TIMEOUT | (rng_range(&duck->rng, 3) == 0 ? ON_WHIM : 0);

  water = deskduck_water(duck);
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

  deskduck_fire(duck, events);

  state = &deskduck_states[duck->state];
  /* ponytail: eight-way steps, so a chase zigzags towards the pointer */
  if (state->does & STEERS)
    duck->heading = POINT(deskduck_sign(dx), deskduck_sign(dy));
  if (state->does & HOMES)
    duck->heading =
      POINT(deskduck_sign((water.x0 + water.x1) / 2 - duck->pos.x),
            deskduck_sign((water.y0 + water.y1) / 2 - duck->pos.y));
  if (state->does & WALKS)
    deskduck_walk(duck, deskduck_land(duck));
  if (state->does & SWIMS)
    deskduck_walk(duck, water);

  alt = state->period ? duck->ticks / state->period : duck->steps;
  return (alt & 1) ? state->other : state->frame;
}

static void deskduck_idle(deskduck_t *duck)
{
  point_t         pointer;
  int             moved;
  const bitmap_t *bitmap;

  if (duck->paused || wuss_is_dragging(duck->wuss))
    return; /* ponytail: any drag holds the duck, not just one of it */

  pointer = wuss_get_pointer(duck->wuss);
  moved   = pointer.x != duck->pointer.x || pointer.y != duck->pointer.y;
  if (moved)
    duck->still = 0;
  else if (duck->still < DUCK_SLEEP)
    duck->still++;
  duck->pointer = pointer;
  duck->ticks++;

  bitmap = duck->frames[deskduck_tick(duck, pointer, moved)][duck->left];
  if (bitmap == duck->shown &&
      duck->pos.x == duck->shown_pos.x && duck->pos.y == duck->shown_pos.y)
    return;

  duck->shown     = bitmap;
  duck->shown_pos = duck->pos;
  deskduck_send(duck, wuss_PINBOARD_ACTOR_SET, DUCK_ACTOR, bitmap, duck->pos);
}

/* ----------------------------------------------------------------------- */

static void deskduck_message(deskduck_t *duck, const wuss_message_t *msg)
{
  const wuss_pinboard_actor_click_t *click;
  const wuss_pinboard_actor_drop_t  *drop;

  if (msg->action == wuss_MESSAGE_PINBOARD_ACTOR_CLICK &&
      msg->size == sizeof(*click))
  {
    click = (const wuss_pinboard_actor_click_t *) msg->data;
    if (click->owner != duck->delegate || click->id == POND_ACTOR ||
        duck->paused)
      return;
    duck->still = 0; /* so it does not drop straight back to sleep */
    deskduck_fire(duck, ON_CLICK);
  }
  else if (msg->action == wuss_MESSAGE_PINBOARD_ACTOR_DROP &&
           msg->size == sizeof(*drop))
  {
    drop = (const wuss_pinboard_actor_drop_t *) msg->data;
    if (drop->owner != duck->delegate || drop->cancelled)
      return;
    /* Pinboard has already moved it there */
    if (drop->id == POND_ACTOR)
    {
      duck->pond = drop->pos;
    }
    else if (drop->id == DUCK_ACTOR)
    {
      duck->pos       = drop->pos;
      duck->shown_pos = drop->pos;
    }
    else
    {
      return;
    }
    duck->wet = 0; /* so a drop on the pond splashes next tick */
    deskduck_fire(duck, ON_DRY);
    if ((deskduck_states[duck->state].does & BUBBLES) &&
        duck->bubble.base != NULL)
      deskduck_send(duck, wuss_PINBOARD_ACTOR_SET, BUBBLE_ACTOR,
                    &duck->bubble, deskduck_bubble_pos(duck));
  }
}

static result_t deskduck_menu_open(deskduck_t *duck)
{
  static const wuss_proginfo_desc_t desc =
    TASK_PROGINFO_DESC("DeskDuck", "A duck that roams the backdrop");

  wuss_proginfo_set_desc(&desc);
  duck->menu_items[DUCK_MENU_INFO].window =
    wuss_proginfo_window(duck->delegate);
  wuss_menu_tick_item(&duck->menu, DUCK_MENU_PAUSE, duck->paused);

  return wuss_iconbar_menu_open(duck->delegate, &duck->menu,
                                &duck->menu_handle);
}

static result_t deskduck_handle(wuss_window_t      *window,
                                const wuss_event_t *event,
                                void               *task_data)
{
  result_t    rc;
  deskduck_t *duck;

  duck = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_IDLE:
    if (duck->quit)
    {
      wuss_task_destroy(duck->delegate); /* its QUIT frees duck */
      return result_OK;
    }
    deskduck_idle(duck);
    return result_OK;

  case wuss_EVENT_ICON:
    if (window == NULL && event->data.iconbar_icon.action == wuss_MOUSE_UP &&
        (event->data.iconbar_icon.button & wuss_BUTTON_MENU))
      return deskduck_menu_open(duck);
    return result_OK;

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu != &duck->menu)
      return result_OK;
    if (event->data.menu_select.index == DUCK_MENU_PAUSE)
    {
      duck->paused = !duck->paused;
      wuss_menu_tick_item(&duck->menu, DUCK_MENU_PAUSE, duck->paused);
      if (duck->menu_handle != NULL)
        wuss_menu_tick_item_live(duck->menu_handle, &duck->menu,
                                 DUCK_MENU_PAUSE, duck->paused);
    }
    else if (event->data.menu_select.index == DUCK_MENU_QUIT)
    {
      duck->quit = 1; /* not from inside the menu's own dispatch */
    }
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    if (window == duck->menu_items[DUCK_MENU_INFO].window)
      rc = wuss_proginfo_handle_pre_show();
    else
      rc = result_OK;
    if (rc != result_OK || event->data.pre_show.handle == NULL)
      return rc;
    return wuss_menu_open_window_now(event->data.pre_show.handle,
                                     event->data.pre_show.index);

  case wuss_EVENT_MENU_CLOSED:
    duck->menu_handle = NULL;
    return result_OK;

  case wuss_EVENT_MESSAGE:
    deskduck_message(duck, event->data.message);
    return result_OK;

  case wuss_EVENT_MESSAGE_BOUNCED:
    if (event->data.message->action == wuss_MESSAGE_PINBOARD_ACTOR &&
        !duck->quit)
    {
      (void) wuss_error_report(duck->wuss, NULL, "DeskDuck",
                               "No pinboard is running",
                               wuss_ERROR_BUTTONS_OK, NULL, NULL);
      duck->quit = 1;
    }
    return result_OK;

  case wuss_EVENT_QUIT:
    free(duck->bubble.base);
    free(duck);
    return result_OK;

  default:
    return result_OK;
  }
}

/* ----------------------------------------------------------------------- */

result_t deskduck_app_create(wuss_t *wuss)
{
  result_t                 rc;
  deskduck_t              *duck;
  wuss_task_desc_t         desc;
  int                      i;
  size2d_t                 screen;
  wuss_iconbar_icon_spec_t spec;

  duck = calloc(1, sizeof(*duck));
  if (duck == NULL)
    return result_OOM;

  duck->wuss = wuss;

  WUSS_MENU_ITEM_WINDOW(duck->menu_items, DUCK_MENU_INFO, "Info",
                        wuss_MENU_ITEM_BORROWED_SUBMENU |
                        wuss_MENU_ITEM_PRE_OPEN,
                        NULL); /* retargeted at the shared proginfo
                                * singleton when the menu opens */
  WUSS_MENU_ITEM(duck->menu_items, DUCK_MENU_PAUSE, "Pause",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(duck->menu_items, DUCK_MENU_QUIT, "Quit",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(duck->menu, "DeskDuck", duck->menu_items,
                  NELEMS(duck->menu_items));

  desc.handle    = deskduck_handle;
  desc.task_data = duck;
  desc.name      = "DeskDuck";
  rc = wuss_task_create(wuss, &desc, &duck->delegate);
  if (rc != result_OK)
  {
    free(duck); /* nothing registered yet */
    return rc;
  }

  rc = wuss_task_iconpool_load_resource(duck->delegate,
                                        wuss_get_resources(wuss));
  if (rc != result_OK)
    goto failure;

  /* one sheet: a row facing right, then the same frames facing left */
  for (i = 0; i < FRAME__LIMIT * 2; i++)
  {
    duck->frames[i % FRAME__LIMIT][i / FRAME__LIMIT] =
      wuss_task_iconpool_frame(duck->delegate, "deskduck", i);
    if (duck->frames[i % FRAME__LIMIT][i / FRAME__LIMIT] == NULL)
    {
      rc = result_NOT_FOUND;
      goto failure;
    }
  }

  duck->pond_bitmap = wuss_task_iconpool_find(duck->delegate, "pond");
  if (duck->pond_bitmap == NULL)
  {
    rc = result_NOT_FOUND;
    goto failure;
  }

  rc = deskduck_bubble_create(duck);
  if (rc != result_OK)
    goto failure;

  rng_seed(&duck->rng, (uint32_t) time(NULL) ^ (uint32_t) (size_t) duck);
  screen     = wuss_get_screen_size(wuss);
  duck->pond = POINT(rng_range(&duck->rng,
                               MAX(screen.w - duck->pond_bitmap->size.w, 1)),
                     rng_range(&duck->rng,
                               MAX(screen.h - DUCK_ICONBAR -
                                   duck->pond_bitmap->size.h, 1)));
  deskduck_send(duck, wuss_PINBOARD_ACTOR_SET, POND_ACTOR, duck->pond_bitmap,
                duck->pond); /* first, so it lies under the duck */
  duck->pos = POINT(rng_range(&duck->rng, MAX(screen.w - DUCK_SIZE, 1)),
                    rng_range(&duck->rng,
                              MAX(screen.h - DUCK_ICONBAR - DUCK_SIZE, 1)));
  duck->pointer = wuss_get_pointer(wuss);
  deskduck_enter(duck, WANDER);

  memset(&spec, 0, sizeof(spec));
  spec.text  = NULL;
  spec.side  = wuss_ICONBAR_RIGHT;
  spec.image = wuss_task_iconpool_find(duck->delegate, "app_deskduck");
  rc = wuss_iconbar_icon_create(wuss, duck->delegate, &spec,
                                &duck->iconbar_icon);
  if (rc != result_OK)
    goto failure;

  return result_OK;

failure:
  wuss_task_destroy(duck->delegate); /* its QUIT frees duck */
  return rc;
}

#endif /* WUSS_APP && WUSS_ICONBAR */
