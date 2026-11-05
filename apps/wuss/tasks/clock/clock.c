/* wuss/tasks/clock/clock.c -- analogue clock task */

#ifdef WUSS_APP

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <math.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/point.h"
#include "utils/fxp.h"
#include "wuss/component/colourmenu.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* a round-faced analogue clock: a tick-marked bezel with 1..12 numerals and
 * hour, minute and second hands read from the system clock and advanced on
 * every idle tick. A Select click, or Menu > Seconds, shows or hides the
 * seconds. An Adjust click, or Menu > Digital, swaps the face for an
 * HH:MM:SS readout (and back); Menu > 12-hour shows that readout as
 * 12-hour time with AM/PM; Menu > Save opens a Save As dialogue (drag
 * its icon onto a Filer window, or Save with a full path already typed)
 * that writes the window's content out. Once a click has given the window
 * the input focus, D, S and H toggle Digital, Seconds and 12-hour. */
typedef struct clock_task
{
  taskapp_instance_t  base;
  wuss_menu_item_t    menu_items[5]; /* per-instance: a shared static would
                                       * leak one instance's .window pointer
                                       * into another's menu */
  bmfont_t          *font; /* borrowed; the numerals are drawn with it */
  colour_t            bg, bezel, hand, second_hand;
  wuss_colourlist_t   colours; /* Menu > Colours, its one row Background */
  bool                show_second;
  bool                digital; /* Menu > Digital: text readout, no face */
  bool                twelve_hour; /* Menu > 12-hour: AM/PM readout */
  time_t              shown; /* time last painted, to the second or minute */
}
clock_task_t;

/* MENU click pops this menu; the item table and wuss_menu_t live per-instance
 * in clock_task_t, not as a file-scope static, so that each window's Info row
 * can hold its own .window pointer to the shared proginfo singleton, retargeted
 * just before wuss_menu_open */
enum
{
  CLOCK_MENU_SAVE = 0,
  CLOCK_MENU_DIGITAL,
  CLOCK_MENU_SECONDS,
  CLOCK_MENU_12_HOUR,
  CLOCK_MENU_COLOURS
};

static const char *const clock_colour_names[] = { "Background" };

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CLOCK_FACE_FRACTION  0.92 /* bezel radius as a fraction of half the smaller side */
#define CLOCK_HOUR_TICK      0.10 /* hour-tick length, fraction of face radius */
#define CLOCK_MINUTE_TICK    0.05 /* minute-tick length, fraction of face radius */
#define CLOCK_NUMERAL_RING   0.75 /* numeral-centre radius, fraction of face radius */
#define CLOCK_HOUR_HAND      0.50 /* hand lengths, fraction of face radius */
#define CLOCK_MINUTE_HAND    0.68
#define CLOCK_SECOND_HAND    0.80

/* a screen-space point in fix8_t, for screen_draw_line_wu_fix8 */
typedef struct fix8_point { fix8_t x, y; } fix8_point_t;

/* point on a circle of radius r about (cx,cy); angle 0 points straight up
 * (12 o'clock) and increases clockwise, matching a clock face */
static fix8_point_t clock_polar(double cx, double cy, double r, double angle)
{
  fix8_point_t p;

  p.x = FLOAT_TO_FIX8(cx + r * sin(angle));
  p.y = FLOAT_TO_FIX8(cy - r * cos(angle));

  return p;
}

static void clock_draw_hand(screen_t *scr,
                            double    cx,
                            double    cy,
                            double    r,
                            double    angle,
                            colour_t  colour)
{
  fix8_point_t tip;

  tip = clock_polar(cx, cy, r, angle);
  screen_draw_line_wu_fix8(scr,
                           FLOAT_TO_FIX8(cx), FLOAT_TO_FIX8(cy),
                           tip.x, tip.y,
                           colour);
}

/* draw text centred on (x,y), background transparent so it sits on the face */
static void clock_draw_centred(bmfont_t   *font,
                               screen_t   *scr,
                               const char *text,
                               double      x,
                               double      y,
                               colour_t    fg)
{
  bmfont_width_t width;
  point_t        pos;
  int            len, fh, ascent;

  len = (int) strlen(text);
  bmfont_measure(font, text, len, NULL, INT_MAX, NULL, &width);
  bmfont_get_info(font, NULL, &fh, &ascent, NULL);

  pos.x = (int) (x - width / 2.0);
  pos.y = (int) (y - fh / 2.0) + ascent;
  bmfont_draw(font, scr, text, len, fg, colour_rgba(0, 0, 0, 0), NULL, &pos,
             NULL);
}

static result_t clock_init(clock_task_t *task)
{
  result_t  rc;
  colour_t *fields[1];

  task->font        = wuss_get_font_n(task->base.wuss, 0);
  task->bg          = colour_rgb(0x1D, 0x2B, 0x53);
  task->bezel       = colour_rgb(0xFF, 0xF1, 0xE8);
  task->hand        = colour_rgb(0xFF, 0xF1, 0xE8);
  task->second_hand = colour_rgb(0xFF, 0x00, 0x4D);
  task->show_second = true;

  fields[0] = &task->bg;
  rc = wuss_colourlist_init(&task->colours, task->base.wuss, NELEMS(fields),
                            fields, clock_colour_names);
  if (rc != result_OK)
    return rc;

  taskapp_save_row(&task->base, task->menu_items, CLOCK_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, CLOCK_MENU_DIGITAL, "Digital",
                          wuss_MENU_ITEM_DASHED, "D");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, CLOCK_MENU_SECONDS, "Seconds",
                          wuss_MENU_ITEM_NONE, "S");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, CLOCK_MENU_12_HOUR, "12-hour",
                          wuss_MENU_ITEM_NONE, "H");

  WUSS_MENU_ITEM_MENU(task->menu_items, CLOCK_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->colours.menu);

  WUSS_MENU_TITLE(task->base.menu, "Clock", task->menu_items,
                 NELEMS(task->menu_items));

  return result_OK;
}

static result_t clock_redraw(const wuss_event_t *event, void *task_data)
{
  clock_task_t *cc;
  screen_t     *scr;
  const box_t  *content, *bounds;
  time_t        now;
  struct tm    *lt;
  double cx, cy, r;
  double hour_angle, minute_angle, second_angle;
  int    i, sx, sy;

  cc = task_data;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;
  sx      = event->data.redraw.scroll.x;
  sy      = event->data.redraw.scroll.y;

  screen_fill_rect(scr,
                   content->x0,
                   content->y0, box_size(content),
                   cc->bg);

  now = time(NULL);
  lt  = localtime(&now);

  cx = bounds->x0 - sx + (bounds->x1 - bounds->x0) / 2.0;
  cy = bounds->y0 - sy + (bounds->y1 - bounds->y0) / 2.0;

  if (cc->digital)
  {
    char        buf[12]; /* "HH:MM:SS PM" */
    int         hour;
    const char *suffix;

    hour   = lt->tm_hour;
    suffix = "";
    if (cc->twelve_hour)
    {
      suffix = (hour < 12) ? " AM" : " PM";
      hour   = (hour + 11) % 12 + 1; /* 0..23 -> 12, 1..11, 12, 1..11 */
    }

    if (cc->show_second)
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d%s",
               hour, lt->tm_min, lt->tm_sec, suffix);
    else
      snprintf(buf, sizeof(buf), "%02d:%02d%s", hour, lt->tm_min, suffix);
    clock_draw_centred(cc->font, scr, buf, cx, cy, cc->hand);
    return result_OK;
  }

  r  = MIN(bounds->x1 - bounds->x0, bounds->y1 - bounds->y0)
     / 2.0 * CLOCK_FACE_FRACTION;

  /* bezel */
  screen_draw_circle(scr, (int) (cx + 0.5), (int) (cy + 0.5), (int) (r + 0.5),
                     cc->bezel);

  /* 60 minute ticks, every fifth one an hour tick drawn longer */
  for (i = 0; i < 60; i++)
  {
    double       angle, inner;
    fix8_point_t a, b;

    angle = i * 2.0 * M_PI / 60.0;
    inner = (i % 5 == 0) ? (1.0 - CLOCK_HOUR_TICK) : (1.0 - CLOCK_MINUTE_TICK);
    a = clock_polar(cx, cy, r * inner, angle);
    b = clock_polar(cx, cy, r, angle);
    screen_draw_line_wu_fix8(scr, a.x, a.y, b.x, b.y, cc->bezel);
  }

  /* the 1..12 numerals, centred on a ring inside the hour ticks */
  for (i = 1; i <= 12; i++)
  {
    double       angle;
    fix8_point_t at;
    char         buf[3];

    angle = i * 2.0 * M_PI / 12.0;
    at    = clock_polar(cx, cy, r * CLOCK_NUMERAL_RING, angle);
    snprintf(buf, sizeof(buf), "%d", i);
    clock_draw_centred(cc->font, scr, buf,
                       FIX8_ROUND_TO_INT(at.x), FIX8_ROUND_TO_INT(at.y),
                       cc->bezel);
  }

  second_angle = lt->tm_sec * 2.0 * M_PI / 60.0;
  minute_angle = (lt->tm_min + lt->tm_sec / 60.0) * 2.0 * M_PI / 60.0;
  hour_angle   = ((lt->tm_hour % 12) + lt->tm_min / 60.0) * 2.0 * M_PI / 12.0;

  clock_draw_hand(scr, cx, cy, r * CLOCK_HOUR_HAND,   hour_angle,   cc->hand);
  clock_draw_hand(scr, cx, cy, r * CLOCK_MINUTE_HAND, minute_angle, cc->hand);
  if (cc->show_second)
    clock_draw_hand(scr, cx, cy, r * CLOCK_SECOND_HAND, second_angle,
                    cc->second_hand);

  return result_OK;
}

static result_t clock_mouse(clock_task_t *cc, wuss_button_t button)
{
  if (button & wuss_BUTTON_MENU)
  {
    wuss_menu_tick_item(&cc->base.menu, CLOCK_MENU_DIGITAL, cc->digital);
    wuss_menu_tick_item(&cc->base.menu, CLOCK_MENU_SECONDS, cc->show_second);
    wuss_menu_tick_item(&cc->base.menu, CLOCK_MENU_12_HOUR, cc->twelve_hour);

    return wuss_menu_open_at_pointer(cc->base.delegate, &cc->base.menu,
                                     &cc->base.menu_handle);
  }

  if (button & wuss_BUTTON_SELECT)
  {
    cc->show_second = !cc->show_second;
    wuss_window_invalidate_visible(cc->base.window);
  }
  else if (button & wuss_BUTTON_ADJUST)
  {
    cc->digital = !cc->digital;
    wuss_window_invalidate_visible(cc->base.window);
  }

  return result_OK;
}

static result_t clock_menu_select(clock_task_t       *cc,
                                  const wuss_event_t *event)
{
  if (wuss_colourlist_selected(&cc->colours, event))
    wuss_window_invalidate_visible(cc->base.window);

  return result_OK;
}

/* The "Digital" row swaps between the analogue face and a text readout;
 * the "Seconds" row shows or hides the seconds, as a Select click does; the
 * "12-hour" row switches the readout between 24-hour and 12-hour AM/PM. An
 * ADJUST pick keeps the menu open, so retick the live row. */
static result_t clock_toggle(clock_task_t *cc, const wuss_event_t *event)
{
  int   index;
  bool *flag;

  index = event->data.menu_select.index;
  if (index == CLOCK_MENU_DIGITAL)
    flag = &cc->digital;
  else if (index == CLOCK_MENU_SECONDS)
    flag = &cc->show_second;
  else
    flag = &cc->twelve_hour;
  *flag = !*flag;

  wuss_menu_tick_item_live(cc->base.menu_handle, &cc->base.menu, index, *flag);

  wuss_window_invalidate_visible(cc->base.window);

  return result_OK;
}

static result_t clock_handle(wuss_window_t      *window,
                             const wuss_event_t *event,
                             void               *task_data)
{
  clock_task_t *cc;

  NOT_USED(window);

  cc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return clock_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;
    return clock_mouse(cc, event->data.mouse.button);

  case wuss_EVENT_KEY:
    return wuss_menu_dispatch_shortcut(cc->base.delegate, &cc->base.menu, event);

  case wuss_EVENT_IDLE:
  {
    time_t now;

    /* the proginfo dialogue is a second window on this same (autoclose)
     * delegate, so closing the clock window alone never empties
     * task->windows and the task lingers until the dialogue closes too --
     * guard against the dangling window in the meantime */
    if (cc->base.window == NULL)
      return result_OK;

    /* repaint only when the time shown changes: each second with seconds
     * on, each minute without */
    now = time(NULL);
    if (!cc->show_second)
      now -= now % 60;
    if (now == cc->shown)
      return result_OK;

    cc->shown = now;
    wuss_window_invalidate_visible(cc->base.window);
    return result_OK;
  }

  case wuss_EVENT_PRE_SUBMENU_OPEN:
  {
    result_t rc;

    /* Colours > Background: the shared colourmenu, set up per open */
    if (wuss_colourlist_pre_open(&cc->colours, cc->base.wuss, event, &rc))
      return rc;

    return result_OK;
  }

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu == &cc->base.menu &&
        (event->data.menu_select.index == CLOCK_MENU_DIGITAL ||
         event->data.menu_select.index == CLOCK_MENU_SECONDS ||
         event->data.menu_select.index == CLOCK_MENU_12_HOUR))
      return clock_toggle(cc, event);
    return clock_menu_select(cc, event);

  default:
    return result_OK;
  }
}

result_t clock_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Clock",
    .purpose       = "Analogue or digital clock",
    .instance_size = sizeof(clock_task_t),
    .window_size   = { 160, 160 },
    .init          = (taskapp_init_fn_t) clock_init,
    .handle        = clock_handle,
    .save_name     = "clock.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
