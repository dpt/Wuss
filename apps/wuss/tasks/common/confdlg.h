/* wuss/tasks/common/confdlg.h -- a task's slider configuration dialogue */

#ifndef TASKS_CONFDLG_H
#define TASKS_CONFDLG_H

#ifdef WUSS_APP

#include <stddef.h>

#include "base/result.h"
#include "wuss/component/dialogue.h"
#include "wuss/icon-spec.h"
#include "wuss/window.h"

#include "taskapp.h"

#define CONFDLG_MAX_ROWS 4

/* One slider row: its label, range, snap step (0 for none) and the byte
 * offset of the int it edits in the task's config block */
typedef struct confdlg_row
{
  const char *label;
  int         min, max, step;
  size_t      offset;
}
confdlg_row_t;

/* Called once Apply or Default has written the config. old is the value
 * rows[0] held before. The parameter is the task's own block type. */
typedef result_t (*confdlg_applied_fn_t)(void *instance, int old);

typedef struct confdlg_desc
{
  const confdlg_row_t *rows;
  int                  nrows;       /* at most CONFDLG_MAX_ROWS */
  const void          *defaults;    /* what Default restores */
  size_t               config_size; /* bytes in defaults and config block */
  confdlg_applied_fn_t applied;
}
confdlg_desc_t;

typedef struct confdlg
{
  wuss_dialogue_t      *dialogue;
  taskapp_instance_t   *instance;
  const confdlg_desc_t *desc;
  void                 *config;
  wuss_slider_row_t     rows[CONFDLG_MAX_ROWS];
}
confdlg_t;

/* Build the hidden dialogue, titled "Configuration": a row per desc->rows
 * above Default, Cancel and Apply buttons. Use
 * wuss_dialogue_window(conf->dialogue) as a menu row's window. */
result_t confdlg_create(confdlg_t            *conf,
                        taskapp_instance_t   *instance,
                        const confdlg_desc_t *desc,
                        void                 *config);

/* Handle wuss_EVENT_ICON, wuss_EVENT_PRE_SHOW, wuss_EVENT_CLOSE and
 * wuss_EVENT_QUIT for the task that owns conf. */
result_t confdlg_event(confdlg_t          *conf,
                       wuss_window_t      *window,
                       const wuss_event_t *event);

#endif /* WUSS_APP */

#endif /* TASKS_CONFDLG_H */
