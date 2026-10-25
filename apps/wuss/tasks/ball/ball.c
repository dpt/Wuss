/* wuss/tasks/ball/ball.c -- bouncing ball task */

#ifdef WUSS_APP

#include <math.h>
#include <stdlib.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/palettes.h"
#include "framebuf/screen.h"
#include "geom/box.h"

#include "ball.h"
#include "../common/snapshot.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define BALL_MIN_LARGE_RADIUS 14 /* every radius from here up is allowed */
#define BALL_MAX_RADIUS       20
#define BALL_SAVE_NAME "ball.png" /* Save As's initial leafname */
#define BALL_MAX_DY     16 /* cap on vertical speed under gravity */

/* with damping on, a wall bounce keeps 3/4 of the speed, truncated towards
 * zero so every bounce loses something and a slow ball comes to rest. With
 * damping off, a bounce only reverses direction. */
#define BALL_BOUNCE(v, damp) ((v) = -((damp) ? (v) * 3 / 4 : (v)))

/* MENU click pops this menu; the item table and wuss_menu_t live per-instance
 * in ball_task_t, not as a file-scope static */
enum
{
  BALL_MENU_PAUSE = 0,
  BALL_MENU_SAVE,
  BALL_MENU_ADD,
  BALL_MENU_CLEAR,
  BALL_MENU_GRAVITY,
  BALL_MENU_DAMPING,
  BALL_MENU_COLOURS
};

static const char *const ball_colour_names[] = { "Background" };

/* below BALL_MIN_LARGE_RADIUS only these radii are allowed */
static const int ball_small_radii[] = { 5, 7, 8, 12 };

/* a fresh radius, picked evenly from ball_small_radii and
 * [BALL_MIN_LARGE_RADIUS, BALL_MAX_RADIUS] */
static int ball_random_radius(void)
{
  int i;

  i = rand() % (int) (NELEMS(ball_small_radii) +
                      BALL_MAX_RADIUS - BALL_MIN_LARGE_RADIUS + 1);
  if (i < (int) NELEMS(ball_small_radii))
    return ball_small_radii[i];

  return BALL_MIN_LARGE_RADIUS + i - (int) NELEMS(ball_small_radii);
}

/* a fresh system-palette colour, never the background's so the ball stays
 * visible */
static colour_t ball_random_colour(const ball_task_t *bc)
{
  const colour_t *palette;
  int             npalette;
  colour_t        c;

  palette = wuss_get_palette(bc->wuss, &npalette);
  do
    c = palette[rand() % npalette];
  while (c.primary == bc->bg.primary);

  return c;
}

/* Invalidation box (virtual content space, as ball positions are held)
 * covering a ball -- or its swept range -- whose centre spans
 * [vx0,vx1] x [vy0,vy1]. wuss_window_invalidate maps this to the screen and
 * applies scroll, so this must not. The circle occupies [c-radius,c+radius]
 * inclusive, i.e. 2*radius+1 pixels; box_t is half-open so x1/y1 get the
 * extra 1. */
static box_t ball_local_box(int vx0, int vy0, int vx1, int vy1, int radius)
{
  box_t local;

  local.x0 = vx0 - radius;
  local.y0 = vy0 - radius;
  local.x1 = vx1 + radius + 1;
  local.y1 = vy1 + radius + 1;

  return local;
}

/* wuss_saveas_save_fn_t: opaque is the ball_task_t */
static result_t ball_saveas_save(const char *path, void *opaque)
{
  ball_task_t *bc;

  bc = opaque;

  return snapshot_save_png(bc->window, ball_handle, bc, path);
}

result_t ball_create(wuss_t *wuss, ball_task_t **out)
{
  result_t         rc;
  ball_task_t     *task;
  wuss_task_t     *delegate;
  wuss_task_desc_t delegate_desc;
  colour_t        *fields[1];

  task = calloc(1, sizeof(*task));
  if (task == NULL)
    return result_OOM;

  task->wuss    = wuss;
  /* a palette entry, so ball_random_colour can avoid it exactly */
  task->bg      = wuss_get_palette(wuss, NULL)[wuss_nearest_colour(wuss, 0xFF,
                                                                   0x00,
                                                                   0x00)];
  task->nballs  = 1;
  task->grabbed = -1;
  task->damping = 1;

  task->balls[0].x     = 50;
  task->balls[0].y      = 50;
  task->balls[0].dx     = 3;
  task->balls[0].dy     = 2;
  task->balls[0].radius = ball_random_radius();
  task->balls[0].colour = ball_random_colour(task);

  /* ball_redraw paints its own background every frame */
  delegate_desc.handle    = ball_handle;
  delegate_desc.task_data = task;
  delegate_desc.name      = "ball";
  rc = wuss_task_create(wuss, &delegate_desc, &delegate);
  if (rc != result_OK)
  {
    free(task); /* nothing registered yet; nobody else owns it */
    return rc;
  }
  wuss_task_set_autoclose(delegate, 1);
  task->delegate = delegate;

  rc = task_window_create(delegate,
                          SIZE2D(200, 160),
                          "Bouncing Ball",
                          &task->window);
  if (rc != result_OK)
  {
    wuss_task_destroy(delegate); /* unregister; its QUIT frees the task block */
    return rc;
  }

  rc = snapshot_saveas_create(&task->saveas, wuss, BALL_SAVE_NAME,
                              ball_saveas_save, task);
  if (rc != result_OK)
  {
    wuss_task_destroy(delegate);
    return rc;
  }

  fields[0] = &task->bg;
  rc = wuss_colourlist_init(&task->colours, wuss, NELEMS(fields), fields,
                            ball_colour_names);
  if (rc != result_OK)
  {
    wuss_task_destroy(delegate);
    return rc;
  }

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, BALL_MENU_PAUSE, "Pause",
                          wuss_MENU_ITEM_NONE, "SPACE");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, BALL_MENU_SAVE, "Save",
                          wuss_MENU_ITEM_NONE, "F3");
  /* hover opens the Save As dialogue as a submenu; F3 shows it standalone */
  task->menu_items[BALL_MENU_SAVE].window = wuss_saveas_window(task->saveas);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, BALL_MENU_ADD, "Add ball",
                          wuss_MENU_ITEM_DASHED, "A");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, BALL_MENU_CLEAR, "Clear",
                          wuss_MENU_ITEM_DISABLED, "C"); /* one ball */

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, BALL_MENU_GRAVITY, "Gravity",
                          wuss_MENU_ITEM_DASHED, "G");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, BALL_MENU_DAMPING, "Damping",
                          wuss_MENU_ITEM_NONE, "D");

  WUSS_MENU_ITEM_MENU(task->menu_items, BALL_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->colours.menu);

  WUSS_MENU_TITLE(task->menu, "Bouncing Ball", task->menu_items,
                 NELEMS(task->menu_items));

  if (out)
    *out = task;

  return result_OK;
}

result_t ball_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    "Ball",
    "Balls bouncing off edges",
    (taskapp_create_fn_t) ball_create
  };

  return taskapp_install(wuss, &desc);
}

void ball_destroy(ball_task_t *task)
{
  wuss_saveas_destroy(task->saveas);
  free(task);
}

static result_t ball_redraw(const wuss_event_t *event, void *task_data)
{
  ball_task_t *bc;
  screen_t    *scr;
  const box_t *content, *bounds;
  int          sx, sy;
  int          i;

  bc = task_data;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;
  sx      = event->data.redraw.scroll.x;
  sy      = event->data.redraw.scroll.y;

  screen_fill_rect(scr, content->x0, content->y0, box_size(content),
                   bc->bg);

  for (i = 0; i < bc->nballs; i++)
  {
    const ball_t *b;

    b = &bc->balls[i];

    screen_fill_circle(scr, bounds->x0 - sx + b->x,
                            bounds->y0 - sy + b->y,
                            b->radius,
                            b->colour);
  }

  return result_OK;
}

/* match the window and menu titles, and whether Clear has anything to do,
 * to the ball count; an ADJUST pick of Add ball leaves the menu open, so its
 * title and Clear row are updated live as well */
static void ball_set_title(ball_task_t *bc)
{
  const char *title;

  title = (bc->nballs > 1) ? "Bouncing Balls" : "Bouncing Ball";
  wuss_window_set_title(bc->window, title);
  bc->menu.title = title; /* read on the next wuss_menu_open */
  wuss_menu_set_title_live(bc->menu_handle, &bc->menu, title);

  /* the held ball may have just been removed */
  if (bc->grabbed >= bc->nballs)
    bc->grabbed = -1;

  /* Clear has nothing to do while only the first ball remains */
  if (bc->nballs > 1)
    bc->menu_items[BALL_MENU_CLEAR].flags &= ~wuss_MENU_ITEM_DISABLED;
  else
    bc->menu_items[BALL_MENU_CLEAR].flags |= wuss_MENU_ITEM_DISABLED;
  wuss_menu_disable_item_live(bc->menu_handle, &bc->menu, BALL_MENU_CLEAR,
                              bc->nballs <= 1);
}

static result_t ball_mouse(wuss_window_t      *window,
                           wuss_mouse_action_t action,
                           int                 x,
                           int                 y,
                           wuss_button_t       button,
                           void               *task_data)
{
  ball_task_t *bc;
  box_t        local;

  bc = task_data;

  if (window != bc->window)
    return result_OK; /* the proginfo dialogue has no click behaviour of
                       * its own */

  if (action == wuss_MOUSE_MOVE)
  {
    ball_t *b;
    int     old_x, old_y;

    if (bc->grabbed < 0)
      return result_OK;

    b = &bc->balls[bc->grabbed];
    if (x == b->x && y == b->y)
      return result_OK; /* keep the last real step as the throw velocity */

    old_x = b->x;
    old_y = b->y;

    /* x,y already arrive in virtual content space, as ball positions are
     * held. The pointer's step becomes the velocity the ball is thrown with
     * on release. */
    b->dx = CLAMP(x - old_x, -BALL_MAX_DY, BALL_MAX_DY);
    b->dy = CLAMP(y - old_y, -BALL_MAX_DY, BALL_MAX_DY);
    b->x  = x;
    b->y  = y;

    local = ball_local_box(MIN(old_x, b->x), MIN(old_y, b->y),
                           MAX(old_x, b->x), MAX(old_y, b->y),
                           b->radius);
    wuss_window_invalidate(bc->window, &local);

    return result_OK;
  }

  if (action == wuss_MOUSE_UP)
  {
    bc->grabbed = -1; /* idle takes over, from the last pointer step */
    return result_OK;
  }

  if (button & wuss_BUTTON_MENU)
  {
    wuss_menu_tick_item(&bc->menu, BALL_MENU_PAUSE, bc->paused);
    wuss_menu_tick_item(&bc->menu, BALL_MENU_GRAVITY, bc->gravity);
    wuss_menu_tick_item(&bc->menu, BALL_MENU_DAMPING, bc->damping);

    return wuss_menu_open_at_pointer(bc->delegate, &bc->menu,
                                     &bc->menu_handle);
  }

  if (button & wuss_BUTTON_SELECT)
  {
    int i;

    /* topmost first: later balls are drawn over earlier ones */
    for (i = bc->nballs - 1; i >= 0; i--)
    {
      const ball_t *b;
      int           ox, oy;

      b  = &bc->balls[i];
      ox = x - b->x;
      oy = y - b->y;
      if (ox * ox + oy * oy <= b->radius * b->radius)
      {
        bc->grabbed = i;
        break;
      }
    }
  }
  else if (button & wuss_BUTTON_ADJUST)
  {
    ball_t *b;

    if (bc->nballs <= 1)
      return result_OK; /* keep at least one ball on screen */

    b = &bc->balls[--bc->nballs];

    local = ball_local_box(b->x, b->y, b->x, b->y, b->radius);
    wuss_window_invalidate(bc->window, &local);

    ball_set_title(bc);
  }

  return result_OK;
}

static result_t ball_idle(void *task_data)
{
  ball_task_t *bc;
  box_t        content;
  point_t      scroll;
  int          width, height;
  int          i;

  bc = task_data;

  /* the proginfo dialogue is a second window on this same (autoclose)
   * delegate, so closing the ball window alone never empties task->windows
   * and the task lingers until the dialogue closes too -- guard against the
   * dangling window in the meantime */
  if (bc->window == NULL)
    return result_OK;

  if (bc->paused)
    return result_OK;

  wuss_window_get_content_bounds(bc->window, &content);
  wuss_window_get_scroll(bc->window, &scroll);
  width  = content.x1 - content.x0;
  height = content.y1 - content.y0;

  for (i = 0; i < bc->nballs; i++)
  {
    ball_t *b;
    box_t   local;
    int     old_x, old_y;

    if (i == bc->grabbed)
      continue; /* the pointer moves it */

    b = &bc->balls[i];

    old_x = b->x;
    old_y = b->y;

    if (bc->gravity)
      b->dy = MIN(b->dy + 1, BALL_MAX_DY);

    b->x += b->dx;
    b->y += b->dy;

    if (b->x - b->radius < scroll.x)               { b->x = scroll.x + b->radius;              BALL_BOUNCE(b->dx, bc->damping); }
    else if (b->x + b->radius >= scroll.x + width) { b->x = scroll.x + width - 1 - b->radius;  BALL_BOUNCE(b->dx, bc->damping); }
    if (b->y - b->radius < scroll.y)               { b->y = scroll.y + b->radius;              BALL_BOUNCE(b->dy, bc->damping); }
    else if (b->y + b->radius >= scroll.y + height){ b->y = scroll.y + height - 1 - b->radius; BALL_BOUNCE(b->dy, bc->damping); }

    local = ball_local_box(MIN(old_x, b->x), MIN(old_y, b->y),
                           MAX(old_x, b->x), MAX(old_y, b->y),
                           b->radius);

    wuss_window_invalidate(bc->window, &local);
  }

  /* collisions: mass goes with area, so a big ball shoves a small one. The
   * pair is pushed apart along the line of centres, then, if still closing,
   * the velocity along that line is exchanged -- with restitution 3/4 under
   * damping, else elastic. The held ball is immovable, as the pointer owns
   * it. Integer maths throughout, bar the one square root for push-out. */
  for (i = 0; i < bc->nballs; i++)
  {
    int j;

    for (j = i + 1; j < bc->nballs; j++)
    {
      ball_t *a, *b;
      box_t   local;
      int     cx, cy, d2, d, r, k, f;
      int     ma, mb, na, nb, den, pa, pb, pen;

      a  = &bc->balls[i];
      b  = &bc->balls[j];
      cx = b->x - a->x;
      cy = b->y - a->y;
      r  = a->radius + b->radius;
      d2 = cx * cx + cy * cy;
      if (d2 == 0 || d2 > r * r)
        continue;

      /* f/4 is 1 + restitution */
      f   = bc->damping ? 7 : 8;
      ma  = a->radius * a->radius;
      mb  = b->radius * b->radius;
      den = ma + mb;
      na  = mb;
      nb  = ma;
      if (i == bc->grabbed)
      {
        na = 0;
        nb = den;
      }
      else if (j == bc->grabbed)
      {
        na = den;
        nb = 0;
      }

      /* na/den and nb/den are the shares of the change each ball takes */
      d   = (int) sqrt((double) d2);
      pen = r - d;
      if (pen > 0)
      {
        pa = pen * na / den;
        pb = pen * nb / den;
        a->x -= cx * pa / d;
        a->y -= cy * pa / d;
        b->x += cx * pb / d;
        b->y += cy * pb / d;

        local = ball_local_box(a->x, a->y, a->x, a->y, a->radius + pen + 1);
        wuss_window_invalidate(bc->window, &local);
        local = ball_local_box(b->x, b->y, b->x, b->y, b->radius + pen + 1);
        wuss_window_invalidate(bc->window, &local);
      }

      k = (a->dx - b->dx) * cx + (a->dy - b->dy) * cy;
      if (k <= 0)
        continue; /* already moving apart */

      a->dx -= f * na * cx * k / (4 * den * d2);
      a->dy -= f * na * cy * k / (4 * den * d2);
      b->dx += f * nb * cx * k / (4 * den * d2);
      b->dy += f * nb * cy * k / (4 * den * d2);
    }
  }

  return result_OK;
}

/* a new background swaps its colour with any ball wearing it, so no ball
 * vanishes into it */
static result_t ball_menu_select(ball_task_t *bc, const wuss_event_t *event)
{
  colour_t old_bg;
  int      i;

  old_bg = bc->bg;
  if (!wuss_colourlist_selected(&bc->colours, event))
    return result_OK;

  for (i = 0; i < bc->nballs; i++)
    if (bc->balls[i].colour.primary == bc->bg.primary)
      bc->balls[i].colour = old_bg;

  wuss_window_invalidate_visible(bc->window);

  return result_OK;
}

/* The "Pause" row stops or restarts the idle animation; the "Gravity" row
 * pulls the balls downward each tick; the "Damping" row makes wall bounces
 * lose speed. An ADJUST pick keeps the menu open, so retick the live row; a
 * SELECT pick has already closed it. */
static result_t ball_toggle(ball_task_t *bc, const wuss_event_t *event)
{
  int  index;
  int *flag;

  index = event->data.menu_select.index;
  if (index == BALL_MENU_PAUSE)
    flag = &bc->paused;
  else if (index == BALL_MENU_GRAVITY)
    flag = &bc->gravity;
  else
    flag = &bc->damping;
  *flag = !*flag;

  wuss_menu_tick_item_live(bc->menu_handle, &bc->menu, index, *flag);

  return result_OK;
}

/* The "Add ball" row: add a ball at the centre of the visible content. */
static result_t ball_add(ball_task_t *bc)
{
  box_t   content;
  point_t scroll;
  ball_t *b;
  box_t   local;

  if (bc->window == NULL || bc->nballs >= BALL_MAX)
    return result_OK;

  wuss_window_get_content_bounds(bc->window, &content);
  wuss_window_get_scroll(bc->window, &scroll);

  b         = &bc->balls[bc->nballs++];
  b->x      = scroll.x + (content.x1 - content.x0) / 2;
  b->y      = scroll.y + (content.y1 - content.y0) / 2;
  b->dx     = (bc->nballs & 1) ? 3 : -3;
  b->dy     = (bc->nballs & 2) ? 2 : -2;
  b->radius = ball_random_radius();
  b->colour = ball_random_colour(bc);

  local = ball_local_box(b->x, b->y, b->x, b->y, b->radius);
  wuss_window_invalidate(bc->window, &local);

  ball_set_title(bc);

  return result_OK;
}

/* The "Clear" row: remove every ball but the first. */
static result_t ball_clear(ball_task_t *bc)
{
  if (bc->window == NULL || bc->nballs <= 1)
    return result_OK;

  bc->nballs = 1;
  ball_set_title(bc);
  wuss_window_invalidate_visible(bc->window);

  return result_OK;
}

result_t ball_handle(wuss_window_t      *window,
                     const wuss_event_t *event,
                     void               *task_data)
{
  ball_task_t *bc;

  bc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return ball_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    return ball_mouse(window, event->data.mouse.action, event->data.mouse.point.x,
                      event->data.mouse.point.y, event->data.mouse.button, task_data);

  case wuss_EVENT_IDLE:
    return ball_idle(task_data);

  case wuss_EVENT_KEY:
    return wuss_menu_dispatch_shortcut(bc->delegate, &bc->menu, event);

  case wuss_EVENT_PRE_SUBMENU_OPEN:
  {
    result_t rc;

    /* Colours > Background: the shared colourmenu, set up per open */
    if (wuss_colourlist_pre_open(&bc->colours, bc->wuss, event, &rc))
      return rc;

    return result_OK;
  }

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu == &bc->menu &&
        (event->data.menu_select.index == BALL_MENU_PAUSE ||
         event->data.menu_select.index == BALL_MENU_GRAVITY ||
         event->data.menu_select.index == BALL_MENU_DAMPING))
      return ball_toggle(bc, event);
    if (wuss_menu_select_is(event, &bc->menu, BALL_MENU_ADD))
      return ball_add(bc);
    if (wuss_menu_select_is(event, &bc->menu, BALL_MENU_CLEAR))
      return ball_clear(bc);
    if (wuss_menu_select_is(event, &bc->menu, BALL_MENU_SAVE))
      return wuss_saveas_open(bc->saveas);
    return ball_menu_select(bc, event);

  case wuss_EVENT_MENU_CLOSED:
    bc->menu_handle = NULL;
    return result_OK;

  case wuss_EVENT_CLOSE:
    if (window == bc->window)
      bc->window = NULL;
    return result_OK;

  case wuss_EVENT_QUIT:
    ball_destroy(bc);
    return result_OK;

  default:
    return result_OK;
  }
}

#endif /* WUSS_APP */
