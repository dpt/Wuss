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
#include "wuss/window.h"

#include "../common/common.h"

#include "taskmanager.h"
#include "../tasks.h"

#define TASKMANAGER_MARGIN  4
#define TASKMANAGER_GAP     8
#define TASKMANAGER_COLUMNS 3 /* of figures, after the labels */
#define TASKMANAGER_ROWS    10 /* the headings, then the statistics */
#define TASKMANAGER_HEADING (TASKMANAGER_ROWS + 1) /* row of the task names'
                                                    * heading, after a gap */
#define TASKMANAGER_NAMES   (TASKMANAGER_HEADING + 1) /* first task name */

typedef struct taskmanager_stats
{
  int           apps;
  int           tasks;
  int           windows;
  int           visible;
  alloc_stats_t alloc;
}
taskmanager_stats_t;

/* only one instance: the icon lasts as long as wuss does */
static struct
{
  wuss_t             *wuss;
  wuss_task_t        *task;
  wuss_window_t      *window; /* the statistics window, or NULL when closed */
  int                 label_width;
  int                 value_width;
  int                 row_height;
  taskmanager_stats_t shown;  /* the statistics last drawn */
}
tm;

static void taskmanager_get_stats(taskmanager_stats_t *stats)
{
  memset(stats, 0, sizeof(*stats)); /* it gets memcmp'd, padding included */

  stats->apps  = wuss_iconbar_icon_count(tm.wuss); /* each app has one icon */
  stats->tasks = wuss_task_count(tm.wuss);
  wuss_window_count(tm.wuss, &stats->windows, &stats->visible);
  alloc_counting_get_stats(&stats->alloc);
}

static int taskmanager_text_width(const char *text)
{
  int width;

  width = 0;
  wuss_text_measure(tm.wuss, 0, text, (int) strlen(text), INT_MAX, NULL,
                    &width);

  return width;
}

/* The document: the statistics, then one row for each task's name. */
static size2d_t taskmanager_doc_size(int ntasks)
{
  return SIZE2D(2 * TASKMANAGER_MARGIN + tm.label_width +
                TASKMANAGER_COLUMNS * (TASKMANAGER_GAP + tm.value_width),
                2 * TASKMANAGER_MARGIN +
                (TASKMANAGER_NAMES + ntasks) * tm.row_height);
}

static result_t taskmanager_open(void)
{
  result_t  rc;
  bmfont_t *font;
  size2d_t  size;

  if (tm.window != NULL)
  {
    wuss_window_restack(tm.window, wuss_ZORDER_FRONT);
    return result_OK;
  }

  font = wuss_get_font(tm.wuss);
  if (font == NULL)
    return result_OK;

  bmfont_get_info(font, NULL, &tm.row_height, NULL, NULL);

  tm.label_width = taskmanager_text_width("Current allocations");
  tm.value_width = MAX(taskmanager_text_width("9,999,999,999"),
                       taskmanager_text_width("Highest"));

  size = taskmanager_doc_size(wuss_task_count(tm.wuss));

  rc = wuss_window_create_at(tm.task, &task_place, size, "Task Manager",
                             wuss_WINDOW_CLOSE | wuss_WINDOW_BACK |
                             wuss_WINDOW_VSCROLL | wuss_WINDOW_RESIZE,
                             wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                             size, SIZE2D(0, 0), &tm.window);
  if (rc != result_OK)
    return rc;

  taskmanager_get_stats(&tm.shown);

  return result_OK;
}

/* Draw 'text' in a row, in font 'slot': left aligned in the labels at column
 * 0, otherwise right aligned in that column of figures, which are measured in
 * the regular font. */
static result_t taskmanager_cell(const wuss_event_t *event,
                                 int                 slot,
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

  pos = POINT(x, bounds->y0 - event->data.redraw.scroll.y +
                 TASKMANAGER_MARGIN + row * height + ascent);

  return wuss_text_draw(tm.wuss, slot, event->data.redraw.scr, text,
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
    "Running apps", "Tasks", "Windows", "Visible windows",
    "Bytes", "Blocks", "Current allocations", "Total allocations", "Total frees"
  };
  static const int ncolumns[TASKMANAGER_ROWS - 1] =
  {
    1, 1, 1, 1, 3, 3, 1, 1, 1
  };

  result_t           rc;
  size_t             values[TASKMANAGER_ROWS - 1][TASKMANAGER_COLUMNS];
  int                row;
  int                column;
  char               text[numfmt_GROUPED_MAX];
  const wuss_task_t *task;
  const char        *name;

  if (wuss_get_font(tm.wuss) == NULL)
    return result_OK;

  taskmanager_get_stats(&tm.shown);

  values[0][0] = (size_t) tm.shown.apps;
  values[1][0] = (size_t) tm.shown.tasks;
  values[2][0] = (size_t) tm.shown.windows;
  values[3][0] = (size_t) tm.shown.visible;
  values[4][0] = tm.shown.alloc.bytes;
  values[4][1] = tm.shown.alloc.bytes_low;
  values[4][2] = tm.shown.alloc.bytes_high;
  values[5][0] = tm.shown.alloc.blocks;
  values[5][1] = tm.shown.alloc.blocks_low;
  values[5][2] = tm.shown.alloc.blocks_high;
  values[6][0] = tm.shown.alloc.allocs - tm.shown.alloc.frees;
  values[7][0] = tm.shown.alloc.allocs;
  values[8][0] = tm.shown.alloc.frees;

  for (column = 0; column < TASKMANAGER_COLUMNS; column++)
  {
    rc = taskmanager_cell(event, 0, 0, column + 1, headings[column]);
    if (rc != result_OK)
      return rc;
  }

  for (row = 0; row < TASKMANAGER_ROWS - 1; row++)
  {
    rc = taskmanager_cell(event, 0, row + 1, 0, labels[row]);
    if (rc != result_OK)
      return rc;

    for (column = 0; column < ncolumns[row]; column++)
    {
      numfmt_grouped(values[row][column], text);
      rc = taskmanager_cell(event, 0, row + 1, column + 1, text);
      if (rc != result_OK)
        return rc;
    }
  }

  /* slot 1 is the bold weight, when one was supplied */
  rc = taskmanager_cell(event, (wuss_get_font_n(tm.wuss, 1) != NULL) ? 1 : 0,
                        TASKMANAGER_HEADING, 0, "Task names");
  if (rc != result_OK)
    return rc;

  row = TASKMANAGER_NAMES;
  for (task = wuss_task_next(tm.wuss, NULL);
       task != NULL;
       task = wuss_task_next(tm.wuss, task))
  {
    name = wuss_task_get_name(task);
    rc   = taskmanager_cell(event, 0, row++, 0, name ? name : "(unnamed)");
    if (rc != result_OK)
      return rc;
  }

  return result_OK;
}

static result_t taskmanager_handle(wuss_window_t      *window,
                                   const wuss_event_t *event,
                                   void               *task_data)
{
  result_t            rc;
  taskmanager_stats_t stats;

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
      taskmanager_get_stats(&stats);
      if (stats.tasks != tm.shown.tasks)
      {
        rc = wuss_window_set_doc(tm.window, taskmanager_doc_size(stats.tasks));
        if (rc != result_OK)
          return rc;
      }
      /* creating a task allocates, so this catches a change of names even
       * when the number of tasks is unchanged */
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
  desc.name      = "TaskManager";
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
