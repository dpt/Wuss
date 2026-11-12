/* wuss/icon/set-bbox.c -- move or resize a work-area icon */

#include "../core/types.h"

void wuss_icon_set_bbox(wuss_window_t *window,
                        wuss_icon_t   *icon,
                        const box_t   *bbox)
{
  if (box_equal(&icon->spec.bbox, bbox))
    return;

  wuss__icon_invalidate(window, icon);
  icon->spec.bbox = *bbox;
  wuss__icon_invalidate(window, icon);
}
