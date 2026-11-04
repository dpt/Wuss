/* wuss/tasks/taskmanager/taskmanager.h -- permanent icon bar "Tasks" icon owning the Wuss menu */

#ifndef WUSS_APP_TASKMANAGER_H
#define WUSS_APP_TASKMANAGER_H

#ifdef WUSS_ICONBAR

#include "base/result.h"
#include "wuss/wuss.h"

/* Create the TaskManager task: a permanent "Tasks" icon on the right of the
 * icon bar. A MENU click on it opens the Wuss menu (see tasks_open_menu); a
 * SELECT click opens a window showing the memory wuss has allocated, which
 * needs wuss_create given alloc_counting; ADJUST does nothing yet. There is
 * no way to quit it short of destroying the wuss instance. Call once at
 * startup, after g_tasks.wuss and g_tasks.menu_task are set. */
result_t taskmanager_create(wuss_t *wuss);

#endif /* WUSS_ICONBAR */

#endif /* WUSS_APP_TASKMANAGER_H */
