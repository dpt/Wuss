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
#define DUCK_SIZE       24  /* px, of every frame */
#define DUCK_ICONBAR    68  /* px kept clear at the bottom, as Pinboard does */
#define DUCK_CHASE      120 /* px: the pointer is chased within this */
#define DUCK_SLEEP      (30 * 60) /* the pointer is still this long */
#define DUCK_QUACK      90  /* the bubble shows this long */
#define DUCK_DOZE       32  /* ticks per sleep frame */
#define DUCK_SPEED      24  /* px/s, top waddle speed */

enum { DUCK_ACTOR, BUBBLE_ACTOR }; /* actor ids */

enum /* the columns of deskduck_24x24.png */
{
  FRAME_WALK1,
  FRAME_WALK2,
  FRAME_IDLE,
  FRAME_QUACK,
  FRAME_SLEEP1,
  FRAME_SLEEP2,
  FRAME__LIMIT
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
  int                  walk;    /* ticks left walking, else pausing */
  int                  pause;   /* ticks left pausing */
  int                  quack;   /* ticks left showing the bubble */
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

  duck->walk = 180 + rng_range(&duck->rng, 420);
}

static int deskduck_sign(int v)
{
  return (v > 0) - (v < 0);
}

/* Move the duck one tick and return the frame to show. */
static int deskduck_step(deskduck_t *duck, point_t pointer)
{
  size2d_t screen;
  point_t  centre;
  int      dx, dy;
  int      stride;

  if (duck->quack > 0)
  {
    if (--duck->quack == 0)
      deskduck_send(duck, wuss_PINBOARD_ACTOR_REMOVE, BUBBLE_ACTOR, NULL,
                    duck->pos);
    return FRAME_QUACK;
  }

  if (duck->still >= DUCK_SLEEP)
    return ((duck->ticks / DUCK_DOZE) & 1) ? FRAME_SLEEP2 : FRAME_SLEEP1;

  centre = POINT(duck->pos.x + DUCK_SIZE / 2, duck->pos.y + DUCK_SIZE / 2);
  dx     = pointer.x - centre.x;
  dy     = pointer.y - centre.y;

  /* ponytail: eight-way steps, so a chase zigzags towards the pointer */
  if (dx * dx + dy * dy < DUCK_CHASE * DUCK_CHASE)
  {
    if (abs(dx) + abs(dy) < DUCK_SIZE / 2)
      return FRAME_IDLE; /* caught it */
    duck->heading = POINT(deskduck_sign(dx), deskduck_sign(dy));
    duck->walk    = 1; /* wander on once the pointer gets away */
  }
  else if (duck->walk > 0)
  {
    if (--duck->walk == 0)
      duck->pause = 60 + rng_range(&duck->rng, 120);
  }
  else
  {
    if (--duck->pause <= 0)
      deskduck_wander(duck);
    return FRAME_IDLE;
  }

  if (duck->heading.x != 0)
    duck->left = duck->heading.x < 0;

  /* A pixel step every 60 / DUCK_SPEED ticks, a diagonal one root two times
   * that, so no heading beats the top speed. */
  stride        = (duck->heading.x != 0 && duck->heading.y != 0) ? 85 : 60;
  duck->stride += DUCK_SPEED;
  if (duck->stride < stride)
    return (duck->steps & 1) ? FRAME_WALK2 : FRAME_WALK1;
  duck->stride -= stride;
  duck->steps++;

  /* bounce off the screen edges */
  screen = wuss_get_screen_size(duck->wuss);
  duck->pos.x += duck->heading.x;
  duck->pos.y += duck->heading.y;
  if (duck->pos.x < 0 || duck->pos.x > screen.w - DUCK_SIZE)
    duck->heading.x = -duck->heading.x;
  if (duck->pos.y < 0 || duck->pos.y > screen.h - DUCK_ICONBAR - DUCK_SIZE)
    duck->heading.y = -duck->heading.y;
  duck->pos.x = CLAMP(duck->pos.x, 0, screen.w - DUCK_SIZE);
  duck->pos.y = CLAMP(duck->pos.y, 0, screen.h - DUCK_ICONBAR - DUCK_SIZE);

  return (duck->steps & 1) ? FRAME_WALK2 : FRAME_WALK1;
}

static void deskduck_idle(deskduck_t *duck)
{
  point_t         pointer;
  const bitmap_t *bitmap;

  if (duck->paused || wuss_is_dragging(duck->wuss))
    return; /* ponytail: any drag holds the duck, not just one of it */

  pointer = wuss_get_pointer(duck->wuss);
  if (pointer.x != duck->pointer.x || pointer.y != duck->pointer.y)
    duck->still = 0;
  else if (duck->still < DUCK_SLEEP)
    duck->still++;
  duck->pointer = pointer;
  duck->ticks++;

  bitmap = duck->frames[deskduck_step(duck, pointer)][duck->left];
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
    if (click->owner != duck->delegate || duck->paused)
      return;
    duck->still = 0; /* wakes it */
    duck->quack = DUCK_QUACK;
    if (duck->bubble.base != NULL)
      deskduck_send(duck, wuss_PINBOARD_ACTOR_SET, BUBBLE_ACTOR,
                    &duck->bubble, deskduck_bubble_pos(duck));
  }
  else if (msg->action == wuss_MESSAGE_PINBOARD_ACTOR_DROP &&
           msg->size == sizeof(*drop))
  {
    drop = (const wuss_pinboard_actor_drop_t *) msg->data;
    if (drop->owner != duck->delegate || drop->id != DUCK_ACTOR ||
        drop->cancelled)
      return;
    duck->pos       = drop->pos; /* Pinboard has already moved it there */
    duck->shown_pos = drop->pos;
    if (duck->quack > 0 && duck->bubble.base != NULL)
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

  rc = deskduck_bubble_create(duck);
  if (rc != result_OK)
    goto failure;

  rng_seed(&duck->rng, (uint32_t) time(NULL) ^ (uint32_t) (size_t) duck);
  screen    = wuss_get_screen_size(wuss);
  duck->pos = POINT(rng_range(&duck->rng, MAX(screen.w - DUCK_SIZE, 1)),
                    rng_range(&duck->rng,
                              MAX(screen.h - DUCK_ICONBAR - DUCK_SIZE, 1)));
  duck->pointer = wuss_get_pointer(wuss);
  deskduck_wander(duck);

  memset(&spec, 0, sizeof(spec));
  spec.text  = NULL;
  spec.side  = wuss_ICONBAR_RIGHT;
  spec.image = wuss_task_iconpool_find(duck->delegate, "head");
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
