/* wuss/tasks/taskmanager/taskmanager.c -- permanent icon bar "Tasks" icon owning the Wuss menu */

#ifdef WUSS_ICONBAR

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "utils/alloc.h"
#include "wuss/iconbar.h"
#include "wuss/task.h"

#include "../common/common.h"

#include "taskmanager.h"
#include "../tasks.h"

#define TASKMANAGER_MARGIN 4

/* only one instance: the icon lasts as long as wuss does */
static struct
{
  wuss_t        *wuss;
  wuss_task_t   *task;
  wuss_window_t *window; /* the memory window, or NULL when closed */
  size_t         shown;  /* the count last drawn */
}
tm;

static void taskmanager_format(char *buf, size_t bufsz, size_t bytes)
{
  snprintf(buf, bufsz, "Allocated: %zu bytes", bytes);
}

static result_t taskmanager_open(void)
{
  static const char widest[] = "Allocated: 9999999999 bytes";

  result_t  rc;
  bmfont_t *font;
  int       height;
  int       width;
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
  wuss_text_measure(tm.wuss, 0, widest, (int) strlen(widest),
                    INT_MAX, NULL, &width);
  size = SIZE2D(width + 2 * TASKMANAGER_MARGIN,
                height + 2 * TASKMANAGER_MARGIN);

  rc = wuss_window_create_at(tm.task, &task_place, size, "Task Manager",
                             wuss_WINDOW_CLOSE | wuss_WINDOW_BACK,
                             wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                             size, SIZE2D(0, 0), &tm.window);
  if (rc != result_OK)
    return rc;

  tm.shown = alloc_counting_bytes();

  return result_OK;
}

static result_t taskmanager_redraw(const wuss_event_t *event)
{
  const box_t *bounds;
  bmfont_t    *font;
  int          ascent;
  char         text[48];
  point_t      pos;

  bounds = event->data.redraw.bounds;

  font = wuss_get_font(tm.wuss);
  if (font == NULL)
    return result_OK;

  bmfont_get_info(font, NULL, NULL, &ascent, NULL);

  tm.shown = alloc_counting_bytes();
  taskmanager_format(text, sizeof(text), tm.shown);

  pos = POINT(bounds->x0 + TASKMANAGER_MARGIN,
              bounds->y0 + TASKMANAGER_MARGIN + ascent);
  return wuss_text_draw(tm.wuss, 0, event->data.redraw.scr, text,
                        (int) strlen(text), colour_rgb(0x00, 0x00, 0x00),
                        colour_rgba(0, 0, 0, 0), &pos, NULL);
}

static result_t taskmanager_handle(wuss_window_t      *window,
                                   const wuss_event_t *event,
                                   void               *task_data)
{
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
    if (tm.window != NULL && alloc_counting_bytes() != tm.shown)
    {
      tm.shown = alloc_counting_bytes();
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
