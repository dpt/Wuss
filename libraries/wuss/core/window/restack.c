/* wuss/window/restack.c -- wuss - minimal window manager */

#include "../impl.h"

static void restack_one(wuss_window_t *window, wuss_zorder_t reason)
{
  list_t *stack;

  stack = &window->wuss->z_order[window->stack];

  if (reason == wuss_ZORDER_FRONT)
  {
    if (stack->next == &window->link)
      return; /* already at front of its stack */

    wuss__invalidate_uncovered(window);

    list_remove(stack, &window->link);
    list_add_to_head(stack, &window->link);
  }
  else
  {
    if (window->link.next == NULL)
      return; /* already at back of its stack */

    list_remove(stack, &window->link);
    list_add_to_tail(stack, &window->link);

    /* now that the reorder has put other windows above it, this invalidates
     * exactly the parts of its footprint they cover -- the same occlusion
     * calculation as the front case, just run after the reorder instead of
     * before, so "hidden" now means "newly hidden" */
    wuss__invalidate_uncovered(window);
  }
}

/* Restack the members of window's group that are panes (panes != 0) or
 * toolbars (panes == 0), in attach order. */
static void restack_members(wuss_window_t *window,
                            int            panes,
                            wuss_zorder_t  reason)
{
  wuss_window_t *tb;

  for (tb = wuss__next_member(window->toolbar.first, panes); tb != NULL;
       tb = wuss__next_member(tb->toolbar.next, panes))
    restack_one(tb, reason);
}

/* A toolbar group restacks as one: the panes stay directly above their
 * parent, as part of it, and the toolbars above the panes. */
void wuss_window_restack(wuss_window_t *window, wuss_zorder_t reason)
{
  window = wuss__toolbar_root(window);

  if (reason == wuss_ZORDER_FRONT)
  {
    restack_one(window, reason);
    restack_members(window, 1, reason);
    restack_members(window, 0, reason);
  }
  else
  {
    restack_members(window, 0, reason);
    restack_members(window, 1, reason);
    restack_one(window, reason);
  }
}
