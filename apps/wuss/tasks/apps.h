/* wuss/tasks/apps.h -- entry points of the self-contained demo tasks */

#ifndef WUSS_APP_TASKS_APPS_H
#define WUSS_APP_TASKS_APPS_H

#ifdef WUSS_APP

#include "base/result.h"
#include "wuss/wuss.h"

/* Each installs the icon bar app that opens that task's windows on demand.
 * Tasks with more of a public interface than this keep their own header. */
result_t ball_app_create(wuss_t *wuss);
result_t chars_app_create(wuss_t *wuss);
result_t chasehq_app_create(wuss_t *wuss);
result_t checker_app_create(wuss_t *wuss);
result_t clock_app_create(wuss_t *wuss);
result_t compositing_app_create(wuss_t *wuss);
result_t curving_app_create(wuss_t *wuss);
result_t dithering_app_create(wuss_t *wuss);
result_t doughnut_app_create(wuss_t *wuss);
result_t duckling_app_create(wuss_t *wuss);
result_t fern_app_create(wuss_t *wuss);
result_t fireworks_app_create(wuss_t *wuss);
result_t gradient_app_create(wuss_t *wuss);
result_t greatescape_app_create(wuss_t *wuss);
result_t greebling_app_create(wuss_t *wuss);
result_t icons_app_create(wuss_t *wuss);
result_t jottings_app_create(wuss_t *wuss);
result_t keys_app_create(wuss_t *wuss);
result_t minesweeping_app_create(wuss_t *wuss);
result_t orbing_app_create(wuss_t *wuss);
result_t oscillating_app_create(wuss_t *wuss);
result_t pinboard_app_create(wuss_t *wuss);
result_t pooling_app_create(wuss_t *wuss);
result_t resizing_app_create(wuss_t *wuss);
result_t saturn_app_create(wuss_t *wuss);
result_t shortcuts_app_create(wuss_t *wuss);
result_t sofa_app_create(wuss_t *wuss);
result_t texturing_app_create(wuss_t *wuss);
result_t sudoku_app_create(wuss_t *wuss);
result_t tagging_app_create(wuss_t *wuss);
result_t viewing_app_create(wuss_t *wuss);

#endif /* WUSS_APP */

#endif /* WUSS_APP_TASKS_APPS_H */
