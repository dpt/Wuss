/* wuss/tasks/taskmanager/taskmanager.c -- permanent icon bar "Tasks" icon owning the Wuss menu */

#ifdef WUSS_ICONBAR

#include <limits.h>
#include <string.h>

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "text/numfmt.h"
#include "utils/alloc.h"
#include "wuss/iconbar.h"
#include "wuss/task.h"

#include "../common/common.h"

#include "taskmanager.h"
#include "../tasks.h"

#define TASKMANAGER_MARGIN  4
#define TASKMANAGER_GAP     8
#define TASKMANAGER_COLUMNS 3 /* of figures, after the labels */
#define TASKMANAGER_ROWS    5 /* the headings, then the statistics */

/* only one instance: the icon lasts as long as wuss does */
static struct
{
  wuss_t        *wuss;
  wuss_task_t   *task;
  wuss_window_t *window;      /* the statistics window, or NULL when closed */
  int            label_width;
  int            value_width;
  alloc_stats_t  shown;       /* the statistics last drawn */
}
tm;

static int taskmanager_text_width(const char *text)
{
  int width;

  width = 0;
  wuss_text_measure(tm.wuss, 0, text, (int) strlen(text), INT_MAX, NULL,
                    &width);

  return width;
}

static result_t taskmanager_open(void)
{
  result_t  rc;
  bmfont_t *font;
  int       height;
  size2d_t  size;

  if (tm.window != NULL)
  {
    wuss_window_restack(tm.window, wuss_ZORDER_FRONT);
    return result_OK;
  }

  font = wuss_get_font(tm.wuss);
  if (font == NULL)
    return result_OK;

  bmfont_get_info(font, NULL, &height, NULL, NULL);

  tm.label_width = taskmanager_text_width("Allocations");
  tm.value_width = MAX(taskmanager_text_width("9,999,999,999"),
                       taskmanager_text_width("Highest"));

  size = SIZE2D(2 * TASKMANAGER_MARGIN + tm.label_width +
                TASKMANAGER_COLUMNS * (TASKMANAGER_GAP + tm.value_width),
                2 * TASKMANAGER_MARGIN + TASKMANAGER_ROWS * height);

  rc = wuss_window_create_at(tm.task, &task_place, size, "Task Manager",
                             wuss_WINDOW_CLOSE | wuss_WINDOW_BACK,
                             wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                             size, SIZE2D(0, 0), &tm.window);
  if (rc != result_OK)
    return rc;

  alloc_counting_get_stats(&tm.shown);

  return result_OK;
}

/* Draw 'text' in a row: left aligned in the labels at column 0, otherwise
 * right aligned in that column of figures. */
static result_t taskmanager_cell(const wuss_event_t *event,
                                 int                 row,
                                 int                 column,
                                 const char         *text)
{
  const box_t *bounds;
  bmfont_t    *font;
  int          height, ascent;
  int          x;
  point_t      pos;

  bounds = event->data.redraw.bounds;
  font   = wuss_get_font(tm.wuss);

  bmfont_get_info(font, NULL, &height, &ascent, NULL);

  x = bounds->x0 + TASKMANAGER_MARGIN;
  if (column > 0)
    x += tm.label_width + column * (TASKMANAGER_GAP + tm.value_width) -
         taskmanager_text_width(text);

  pos = POINT(x, bounds->y0 + TASKMANAGER_MARGIN + row * height + ascent);

  return wuss_text_draw(tm.wuss, 0, event->data.redraw.scr, text,
                        (int) strlen(text), colour_rgb(0x00, 0x00, 0x00),
                        colour_rgba(0, 0, 0, 0), &pos, NULL);
}

static result_t taskmanager_redraw(const wuss_event_t *event)
{
  static const char *const headings[TASKMANAGER_COLUMNS] =
  {
    "Current", "Lowest", "Highest"
  };
  static const char *const labels[TASKMANAGER_ROWS - 1] =
  {
    "Bytes", "Blocks", "Allocations", "Frees"
  };
  static const int ncolumns[TASKMANAGER_ROWS - 1] = { 3, 3, 1, 1 };

  result_t rc;
  size_t   values[TASKMANAGER_ROWS - 1][TASKMANAGER_COLUMNS];
  int      row;
  int      column;
  char     text[numfmt_GROUPED_MAX];

  if (wuss_get_font(tm.wuss) == NULL)
    return result_OK;

  alloc_counting_get_stats(&tm.shown);

  values[0][0] = tm.shown.bytes;
  values[0][1] = tm.shown.bytes_low;
  values[0][2] = tm.shown.bytes_high;
  values[1][0] = tm.shown.blocks;
  values[1][1] = tm.shown.blocks_low;
  values[1][2] = tm.shown.blocks_high;
  values[2][0] = tm.shown.allocs;
  values[3][0] = tm.shown.frees;

  for (column = 0; column < TASKMANAGER_COLUMNS; column++)
  {
    rc = taskmanager_cell(event, 0, column + 1, headings[column]);
    if (rc != result_OK)
      return rc;
  }

  for (row = 0; row < TASKMANAGER_ROWS - 1; row++)
  {
    rc = taskmanager_cell(event, row + 1, 0, labels[row]);
    if (rc != result_OK)
      return rc;

    for (column = 0; column < ncolumns[row]; column++)
    {
      numfmt_grouped(values[row][column], text);
      rc = taskmanager_cell(event, row + 1, column + 1, text);
      if (rc != result_OK)
        return rc;
    }
  }

  return result_OK;
}

static result_t taskmanager_handle(wuss_window_t      *window,
                                   const wuss_event_t *event,
                                   void               *task_data)
{
  alloc_stats_t stats;

  NOT_USED(task_data);

  switch (event->kind)
  {
  case wuss_EVENT_ICON:
    if (window != NULL || event->data.iconbar_icon.action != wuss_MOUSE_UP)
      return result_OK;
    if (event->data.iconbar_icon.button & wuss_BUTTON_MENU)
      return tasks_open_menu();
    if (event->data.iconbar_icon.button & wuss_BUTTON_SELECT)
      return taskmanager_open();
    return result_OK; /* ADJUST: nothing yet */

  case wuss_EVENT_REDRAW:
    return taskmanager_redraw(event);

  case wuss_EVENT_IDLE:
    if (tm.window != NULL)
    {
      alloc_counting_get_stats(&stats);
      if (memcmp(&stats, &tm.shown, sizeof(stats)) != 0)
        wuss_window_invalidate_visible(tm.window);
    }
    return result_OK;

  case wuss_EVENT_CLOSE:
    if (window == tm.window)
      tm.window = NULL;
    return result_OK;

  default:
    return result_OK;
  }
}

result_t taskmanager_create(wuss_t *wuss)
{
  result_t                 rc;
  wuss_task_t             *wtask;
  wuss_task_desc_t         desc;
  wuss_iconbar_icon_spec_t spec;
  wuss_iconbar_icon_t     *icon;

  desc.handle    = taskmanager_handle;
  desc.task_data = NULL;
  desc.name      = "taskmanager";
  rc = wuss_task_create(wuss, &desc, &wtask);
  if (rc != result_OK)
    return rc;

  tm.wuss = wuss;
  tm.task = wtask;

  alloc_counting_reset_lows();

  memset(&spec, 0, sizeof(spec));
  spec.text     = NULL;
  spec.side     = wuss_ICONBAR_RIGHT;
  spec.priority = 0x60000000; /* outermost */
  spec.image = wuss_task_iconpool_find(wtask, "app_taskmanager");
  if (spec.image == NULL)
    logf_warning("taskmanager: icon 'file_app' unavailable");
  rc = wuss_iconbar_icon_create(wuss, wtask, &spec, &icon);
  if (rc != result_OK)
    wuss_task_destroy(wtask);

  return rc;
}

#endif /* WUSS_ICONBAR */
