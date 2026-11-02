/* wuss/tasks/jottings/jottings.h -- Jottings sticky notes */

#ifndef TASKS_JOTTINGS_H
#define TASKS_JOTTINGS_H

#ifdef WUSS_APP

#include "base/result.h"
#include "wuss/wuss.h"

/* Install the Jottings app and open its welcome note. Notes are Markdown,
 * kept in the per-user "Notes" directory once pinned to the backdrop. Start
 * it before other apps that open ".md" files so it claims the notes in its
 * own directory first. See docs/windowing/backdrop.md. */
result_t jottings_app_create(wuss_t *wuss);

#endif /* WUSS_APP */

#endif /* TASKS_JOTTINGS_H */
