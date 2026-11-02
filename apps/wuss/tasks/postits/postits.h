/* wuss/tasks/postits/postits.h -- Post-it notes */

#ifndef TASKS_POSTITS_H
#define TASKS_POSTITS_H

#ifdef WUSS_APP

#include "base/result.h"
#include "wuss/wuss.h"

/* Install the Post-Its app and open its welcome note. Notes are Markdown,
 * kept in the per-user "Notes" directory once pinned to the backdrop. Start
 * it before other apps that open ".md" files so it claims the notes in its
 * own directory first. See docs/windowing/backdrop.md. */
result_t postits_app_create(wuss_t *wuss);

#endif /* WUSS_APP */

#endif /* TASKS_POSTITS_H */
