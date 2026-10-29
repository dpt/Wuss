/* wuss/pointer-bounds.c -- wuss - box a drag locks the pointer to */

#include <assert.h>

#include "impl.h"

int wuss__pointer_held(const wuss_t *wuss)
{
  if (wuss->drag.window != NULL || wuss->capture != NULL)
    return 1;
#ifdef WUSS_FURNITURE
  if (wuss->furniture.dragging != NULL)
    return 1;
#endif
#ifdef WUSS_ICONS
  if (wuss->pressed.icon != NULL)
    return 1;
#endif
  return 0;
}

result_t wuss_set_pointer_bounds(wuss_t *wuss, const box_t *bounds)
{
  assert(wuss != NULL);

  if (!wuss__pointer_held(wuss))
    return result_BAD_ARG;

  if (bounds == NULL)
    wuss->pointer_bounds = (box_t) BOX_INIT;
  else
    wuss->pointer_bounds = *bounds;

  return result_OK;
}

int wuss_get_pointer_bounds(const wuss_t *wuss, box_t *bounds)
{
  assert(wuss != NULL);
  assert(bounds != NULL);

  /* bounds outlive the press that set them until the next idle press resets
   * them, so a release, Escape or window close needs no clean-up of its own */
  if (!wuss__pointer_held(wuss) || box_is_empty(&wuss->pointer_bounds))
    return 0;

  *bounds = wuss->pointer_bounds;
  return 1;
}
