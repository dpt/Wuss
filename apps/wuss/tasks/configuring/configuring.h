/* wuss/tasks/configuring/configuring.h -- startup settings task (mouse
 * button swap, reverse scroll, system font, backdrop swatches) */

#ifndef TASKS_CONFIGURING_H
#define TASKS_CONFIGURING_H

#ifdef WUSS_APP

/* Longest text a writable timing field ever needs to hold: "65535" (5 chars)
 * plus the terminator. */
#define CONFIGURING_FIELD_BUF  6

#include "framebuf/bitmap.h"
#include "framebuf/pattern.h"
#include "geom/box.h"
#include "wuss/gadget/gridview.h"
#include "wuss/icon.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

/* The sections, each an icon in the root window and a window of its own. */
enum
{
  CONFIGURING_SECTION_MOUSING,
  CONFIGURING_SECTION_FONTS,
  CONFIGURING_SECTION_BACKDROP,
  CONFIGURING_SECTION__LIMIT
};

/* Startup settings: a root window, a grid of one icon per section, and a
 * window per section opened by clicking its icon.
 *
 * "Mousing" has a "Buttons" frame with two option icons (right/middle mouse
 * button swap -- g_tasks.swap_mouse_buttons, read by each frontend's
 * raw-button translator -- and reversed scroll direction --
 * g_tasks.reverse_scroll, read where each frontend fills
 * wuss_input_event_t.wheel) and a "Timing" frame with three writable fields
 * (double-click interval/distance, drag threshold) applied via an "Apply"
 * action calling wuss_set_config. "Fonts" shows the system font with a
 * "Choose" action opening the font menu (a pick is applied via
 * wuss_set_font). "Backdrop" has a foreground and a background colour swatch
 * row, a fill-pattern grid, a result swatch mixing the three, and a "Set
 * backdrop" action applying the result via wuss_set_backdrop. Opened once
 * from run_wuss, and from the icon bar. */
typedef struct configuring_task
{
  wuss_t             *wuss;     /* for wuss_get_pointer when opening the
                                 * menu */
  wuss_task_t        *delegate; /* the task that owns the menu */
  wuss_window_t      *window;   /* the root: one icon per section */
  wuss_gridview_t    *gridview; /* its grid */
  const bitmap_t     *glyphs[CONFIGURING_SECTION__LIMIT]; /* borrowed from
                                                           * the icon pool */
  wuss_window_t      *windows[CONFIGURING_SECTION__LIMIT]; /* NULL when
                                                            * closed */

  /* each section's icons are NULL while its window is closed */
  wuss_icon_t         *swap_icon;
  wuss_icon_t         *reverse_scroll_icon;
  wuss_icon_t         *solid_drags_icon;
  wuss_icon_t         *font_name_icon;
  wuss_icon_t         *choose_font_icon;
  wuss_icon_t         *double_click_ms_icon;
  wuss_icon_t         *double_click_px_icon;
  wuss_icon_t         *drag_threshold_px_icon;
  wuss_icon_t         *apply_timing_icon;
  char                 double_click_ms_text[CONFIGURING_FIELD_BUF];
                                             /* "65535\0" */
  char                 double_click_px_text[CONFIGURING_FIELD_BUF];
  char                 drag_threshold_px_text[CONFIGURING_FIELD_BUF];
  int                  swatch_x;    /* left edge of every swatch column and
                                     * the grid, document space; set once by
                                     * configuring_create, read by redraw and
                                     * click hit-testing so the two always
                                     * agree */
  int                  fg_y;
  int                  bg_y;
  int                  grid_y;
  int                  result_y;
  wuss_colour_t        fg;      /* selected foreground swatch (palette
                                 * index); also the result's ink */
  wuss_colour_t        bg;      /* selected background swatch (palette
                                 * index); also the result's paper */
  screen_pattern_t     pattern; /* selected pattern swatch; also the
                                 * result's tile */
  wuss_icon_t         *set_backdrop_icon;
}
configuring_task_t;

wuss_window_fn_t configuring_handle;

/* create the startup settings window against the given wuss instance; if out
 * is non-NULL, the task block is also returned through it */
result_t configuring_create(wuss_t *wuss, configuring_task_t **out);

/* install the icon bar app that opens config windows on demand */
result_t configuring_app_create(wuss_t *wuss);

/* open a configuring window now; the app must be installed first */
result_t configuring_open(wuss_t *wuss);

/* free a task block allocated by configuring_create; normally called by the
 * window's wuss_EVENT_QUIT handler, not by callers directly */
void configuring_destroy(configuring_task_t *task);

#endif /* WUSS_APP */

#endif /* TASKS_CONFIGURING_H */
