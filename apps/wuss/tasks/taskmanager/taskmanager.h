/* wuss/tasks/taskmanager/taskmanager.h -- permanent icon bar "Tasks" icon owning the Wuss menu */

#ifndef WUSS_APP_TASKMANAGER_H
#define WUSS_APP_TASKMANAGER_H

#ifdef WUSS_ICONBAR

#include "base/result.h"
#include "wuss/wuss.h"

/* Create the TaskManager task: a permanent "Tasks" icon on the right of the
 * icon bar. A MENU click on it opens the Wuss menu (see tasks_open_menu); a
 * SELECT click opens the Apps window; ADJUST does nothing yet. There is no
 * way to quit it short of destroying the wuss instance. Call once at startup,
 * after g_tasks.wuss and g_tasks.menu_task are set: the lowest figures start
 * from then. */
result_t taskmanager_create(wuss_t *wuss);

/* Open, or bring to the front, the Apps window: the name of every task, a
 * child indented under its parent. */
result_t taskmanager_open_apps(void);

/* Open, or bring to the front, the Stats window: counts of apps, tasks and
 * windows, then allocation statistics, which need the DPTLIB_ALLOC_COUNT
 * build option, each with its lowest and highest. */
result_t taskmanager_open_stats(void);

#endif /* WUSS_ICONBAR */

#endif /* WUSS_APP_TASKMANAGER_H */
