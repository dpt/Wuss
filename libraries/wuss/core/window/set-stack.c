/* wuss/window/set-stack.c -- wuss - minimal window manager */

#include <assert.h>

#include "../impl.h"

static void set_stack_one(wuss_window_t *window, wuss_stack_t stack)
{
  wuss_t *wuss;
  int     hidden, forward;

  if (window->stack == stack)
    return;

  wuss    = window->wuss;
  hidden  = (window->flags & wuss_WINDOW_HIDDEN) != 0;
  forward = stack < window->stack; /* moving in front of more windows */

  /* same before/after split as wuss_window_restack: a window coming forward
   * invalidates what is about to be uncovered, one going back invalidates
   * what the reorder has just covered. A hidden window paints nothing. */
  if (forward && !hidden)
    wuss__invalidate_uncovered(window);

  list_remove(&wuss->z_order[window->stack], &window->link);
  window->stack = stack;
  list_add_to_head(&wuss->z_order[stack], &window->link);

  if (!forward && !hidden)
    wuss__invalidate_uncovered(window);
}

/* A toolbar group moves stack as one, panes landing above the parent and
 * toolbars above the panes. */
void wuss_window_set_stack(wuss_window_t *window, wuss_stack_t stack)
{
  int            panes;
  wuss_window_t *tb;

  assert((int) stack >= 0 && (int) stack < WUSS_STACK_COUNT);

  window = wuss__toolbar_root(window);

  set_stack_one(window, stack);
  for (panes = 1; panes >= 0; panes--)
    for (tb = wuss__next_member(window->toolbar.first, panes); tb != NULL;
         tb = wuss__next_member(tb->toolbar.next, panes))
      set_stack_one(tb, stack);
}
