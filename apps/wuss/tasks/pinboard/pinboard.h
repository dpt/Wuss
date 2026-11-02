/* wuss/tasks/pinboard/pinboard.h -- backdrop window of pinned files */

#ifndef TASKS_PINBOARD_H
#define TASKS_PINBOARD_H

#ifdef WUSS_APP

#include "base/result.h"
#include "wuss/wuss.h"

/* Start the Pinboard: create the backdrop window (wuss_WINDOW_BACKDROP) and
 * take over the desktop. Does nothing, returning result_OK, if it is already
 * running. See docs/windowing/backdrop.md. */
result_t pinboard_app_create(wuss_t *wuss);

#endif /* WUSS_APP */

#endif /* TASKS_PINBOARD_H */
