/* wuss/idle.c -- wuss - minimal window manager */

#include "impl.h"

result_t wuss_idle(wuss_t *wuss)
{
  result_t          rc;
  wuss_event_t      event;
  wuss__task_walk_t walk;
  wuss_task_t      *task;

  event.kind = wuss_EVENT_IDLE;

  wuss__message_enter(wuss);
  wuss__message_leave(wuss); /* drain before IDLE, per the file overview */
  wuss__message_enter(wuss);

#ifdef WUSS_FURNITURE
  wuss__scroll_repeat(wuss);
#endif

#ifdef WUSS_ICONBAR
  wuss__iconbar_scroll_tick(wuss);
  wuss__iconbar_pop_tick(wuss);
#endif
  wuss__toolbar_scroll_tick(wuss);

  if (wuss->drag.window != NULL)
    wuss__drag_tick(wuss);

  rc = result_OK;
  wuss__task_walk_begin(wuss, &walk);
  while ((task = wuss__task_walk_next(&walk)) != NULL)
  {
    result_t crc;

    crc = wuss__deliver(task, NULL, &event);
    if (crc != result_OK && rc == result_OK)
      rc = crc;
  }
  wuss__task_walk_end(wuss, &walk);

  wuss__message_leave(wuss); /* drain after IDLE too */

  return rc;
}
