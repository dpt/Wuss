/* wuss/icon/hit-test.c -- work-area icon hit testing */

#include "geom/box.h"

#include "../core/types.h"
#include "../icon.h"

wuss_icon_t *wuss__icon_hit_test(wuss_window_t *window, point_t doc_point)
{
  wuss_icon_t *it;
  box_t        box;
  int          i;

  /* last created wins, matching the draw order in redraw_window */
  for (i = window->nicons - 1; i >= 0; i--)
  {
    it = window->icons[i];

    /* a bitmap icon is interactive only when it asks to be; RULE and the
     * static types are always inert; the other types always click */
    if (it->spec.type == wuss_ICON_TYPE_BITMAP)
    {
      if (!(it->spec.flags & wuss_ICON_FLAGS_INTERACTIVE))
        continue;
    }
    else if (it->spec.type != wuss_ICON_TYPE_ACTION &&
             it->spec.type != wuss_ICON_TYPE_RADIO &&
             it->spec.type != wuss_ICON_TYPE_OPTION &&
             it->spec.type != wuss_ICON_TYPE_MENU_ENTRY &&
             it->spec.type != wuss_ICON_TYPE_SLIDER &&
             it->spec.type != wuss_ICON_TYPE_WRITABLE &&
             it->spec.type != wuss_ICON_TYPE_DRAGGABLE)
    {
      continue;
    }

    if (it->spec.flags & (wuss_ICON_FLAGS_HIDDEN | wuss_ICON_FLAGS_DISABLED))
      continue;

    /* only a slider's groove reacts to clicks; its surround is decorative */
    if (it->spec.type == wuss_ICON_TYPE_SLIDER)
    {
      wuss__slider_groove_box(&it->spec.bbox, &box);
    }
    else
    {
      box = it->spec.bbox;
    }

    if (box_contains_point(&box, doc_point.x, doc_point.y))
      return it;
  }

  return NULL;
}
