/* wuss/tasks/pooling/pooling.c -- system sprite pool viewer task */

#ifdef WUSS_APP

#include <stdlib.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "wuss/gadget/gridview.h"
#include "wuss/icon.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define POOLING_LABEL_CHARS 16 /* label column width */
#define POOLING_COLUMNS      6 /* window opens this many cells wide */
#define POOLING_ROWS         6 /* ... and at most this many tall */

/* shows every entry of the shared icon pool (the system sprites) as a grid
 * of bitmaps labelled with their names */
typedef struct pooling_task
{
  taskapp_instance_t base;
  wuss_gridview_t   *gv;
}
pooling_task_t;

/* wuss_gridview_item_fn_t: opaque is the pooling_task_t */
static void pooling_item(int index, void *opaque, wuss_gridview_item_t *out)
{
  pooling_task_t *task;

  task = opaque;

  out->glyph = wuss_iconpool_bitmap(task->base.wuss, index);
  out->label = wuss_iconpool_name(task->base.wuss, index);
}

static result_t pooling_init(pooling_task_t *task)
{
  result_t rc;

  rc = wuss_gridview_window_create(&task->gv, task->base.delegate, &task_place,
                                   "Sprite pool",
                                   wuss_iconpool_count(task->base.wuss),
                                   pooling_item,
                                   POOLING_LABEL_CHARS, NULL, task, 0,
                                   &task->base.window);
  if (rc != result_OK)
    return rc;

  wuss_gridview_fit_window(task->gv, POOLING_COLUMNS, POOLING_ROWS,
                           &task_place);

  return result_OK;
}

static result_t pooling_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data)
{
  pooling_task_t *pt;

  NOT_USED(window);

  pt = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_QUIT:
    wuss_gridview_destroy(pt->gv);
    return result_OK;

  default:
    if (pt->gv != NULL)
      (void) wuss_gridview_handle_event(pt->gv, event);
    return result_OK;
  }
}

result_t pooling_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Pooling",
    .purpose       = "System sprite pool viewer",
    .instance_size = sizeof(pooling_task_t),
    .init          = (taskapp_init_fn_t) pooling_init,
    .handle        = pooling_handle
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
