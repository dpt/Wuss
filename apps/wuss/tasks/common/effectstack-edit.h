/* wuss/tasks/common/effectstack-edit.h -- effect stack editing dialogues */

#ifndef TASKS_EFFECTSTACK_EDIT_H
#define TASKS_EFFECTSTACK_EDIT_H

#ifdef WUSS_APP

#include "effectstack.h"

/* the dialogues, in the order effectstack_edit_find reports them */
typedef enum effectstack_edit_which
{
  EFFECTSTACK_EDIT_BLUR,
  EFFECTSTACK_EDIT_TONE,
  EFFECTSTACK_EDIT_LEVEL,
  EFFECTSTACK_EDIT__LIMIT
}
effectstack_edit_which_t;

typedef struct effectstack_edit effectstack_edit_t;

/* effectstack.c, for the dialogues: the selected element (NULL if none;
 * index returned through index if non-NULL), and tell the owner of a change
 * (non-zero rechain runs the effects too, else only the final blend) */
effectstack_element_t *effectstack_selected(effectstack_t *stack,
                                            int           *index);
void effectstack_changed(effectstack_t *stack, int rechain);

/* make the dialogues, hidden */
result_t effectstack_edit_create(effectstack_t       *stack,
                                 effectstack_edit_t **out);

/* free the dialogues; safe to pass NULL */
void effectstack_edit_destroy(effectstack_edit_t *edit);

/* the dialogue's window, for a menu row to open */
wuss_window_t *effectstack_edit_window(const effectstack_edit_t *edit,
                                       effectstack_edit_which_t  which);

/* which dialogue owns window, or -1 */
int effectstack_edit_find(const effectstack_edit_t *edit,
                          const wuss_window_t      *window);

/* a wuss_EVENT_CLOSE on window: forget it if it is one of the dialogues', so
 * effectstack_edit_destroy does not close it again */
void effectstack_edit_closed(effectstack_edit_t *edit,
                             wuss_window_t      *window);

/* a wuss_EVENT_ICON on the dialogue's window */
result_t effectstack_edit_icon(effectstack_edit_t      *edit,
                               effectstack_edit_which_t which,
                               const wuss_event_t      *event);

/* a wuss_EVENT_PRE_SHOW for the dialogue's window: refills it from the
 * selected element */
result_t effectstack_edit_pre_show(effectstack_edit_t      *edit,
                                   effectstack_edit_which_t which);

#endif /* WUSS_APP */

#endif /* TASKS_EFFECTSTACK_EDIT_H */
