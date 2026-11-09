/* wuss/drag-past-threshold.c -- wuss - minimal window manager */

#include <assert.h>

#include "impl.h"

int wuss_drag_past_threshold(const wuss_t *wuss, point_t from, point_t to)
{
  int dx;
  int dy;
  int t;

  assert(wuss != NULL);

  dx = to.x - from.x;
  dy = to.y - from.y;
  t  = wuss->input.drag_threshold_px;

  return dx * dx + dy * dy >= t * t;
}
