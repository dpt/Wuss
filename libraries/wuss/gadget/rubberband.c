/* wuss/gadget/rubberband.c -- a drag-out selection box on a window */

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/size.h"

#include "wuss/gadget/rubberband.h"

/* ----------------------------------------------------------------------- */

void wuss_rubberband_start(wuss_rubberband_t *band,
                           wuss_window_t     *window,
                           point_t            point)
{
  band->active = 1;
  band->anchor = point;
  band->box    = (box_t) BOX_POS_SIZE(point.x, point.y, 1, 1);
  wuss_window_invalidate(window, &band->box);
}

void wuss_rubberband_to(wuss_rubberband_t *band,
                        wuss_window_t     *window,
                        point_t            point)
{
  box_t before, dirty;

  if (!band->active)
    return;

  before       = band->box;
  band->box.x0 = MIN(band->anchor.x, point.x);
  band->box.y0 = MIN(band->anchor.y, point.y);
  band->box.x1 = MAX(band->anchor.x, point.x) + 1;
  band->box.y1 = MAX(band->anchor.y, point.y) + 1;

  box_union(&before, &band->box, &dirty);
  wuss_window_invalidate(window, &dirty);
}

void wuss_rubberband_end(wuss_rubberband_t *band, wuss_window_t *window)
{
  if (!band->active)
    return;

  band->active = 0;
  wuss_window_invalidate(window, &band->box);
}

void wuss_rubberband_draw(const wuss_rubberband_t *band,
                          const wuss_event_t      *event,
                          colour_t                 colour)
{
  int x, y;

  if (!band->active)
    return;

  /* work area to screen */
  x = event->data.redraw.bounds->x0 - event->data.redraw.scroll.x;
  y = event->data.redraw.bounds->y0 - event->data.redraw.scroll.y;
  screen_draw_rect(event->data.redraw.scr, band->box.x0 + x,
                   band->box.y0 + y, box_size(&band->box), colour);
}
