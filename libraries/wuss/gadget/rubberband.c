/* wuss/gadget/rubberband.c -- a drag-out selection box on a window */

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/size.h"

#include "wuss/gadget/rubberband.h"

#include "../core/types.h"

/* ----------------------------------------------------------------------- */

/* the most an autoscroll moves per frame, on each axis */
#define RUBBERBAND_AUTOSCROLL_MAX 32

/* ----------------------------------------------------------------------- */

/* record the pointer relative to the visible content's top left */
static void rubberband_track(wuss_rubberband_t   *band,
                             const wuss_window_t *window,
                             point_t              point)
{
  point_t scroll;

  wuss_window_get_scroll(window, &scroll);
  band->pointer = POINT(point.x - scroll.x, point.y - scroll.y);
}

void wuss_rubberband_start(wuss_rubberband_t *band,
                           wuss_window_t     *window,
                           point_t            point)
{
  band->active = 1;
  band->anchor = point;
  band->box    = (box_t) BOX_POS_SIZE(point.x, point.y, 1, 1);
  rubberband_track(band, window, point);
  wuss_window_invalidate(window, &band->box);
}

void wuss_rubberband_to(wuss_rubberband_t *band,
                        wuss_window_t     *window,
                        point_t            point)
{
  box_t before, dirty;

  if (!band->active)
    return;

  rubberband_track(band, window, point);

  before       = band->box;
  band->box.x0 = MIN(band->anchor.x, point.x);
  band->box.y0 = MIN(band->anchor.y, point.y);
  band->box.x1 = MAX(band->anchor.x, point.x) + 1;
  band->box.y1 = MAX(band->anchor.y, point.y) + 1;

  box_union(&before, &band->box, &dirty);
  wuss_window_invalidate(window, &dirty);
}

/* One axis: the scroll offset that moves towards pointer position v when it
 * lies outside [0, extent), by its distance outside, capped, without passing
 * 0 or the document's far edge, or scroll itself if v lies inside. */
static int rubberband_scroll_axis(int scroll, int v, int extent, int doc)
{
  if (v < 0)
    return MIN(scroll, MAX(scroll + MAX(v, -RUBBERBAND_AUTOSCROLL_MAX), 0));
  if (v >= extent)
    return MAX(scroll,
               MIN(scroll + MIN(v - extent + 1, RUBBERBAND_AUTOSCROLL_MAX),
                   doc - extent));
  return scroll;
}

int wuss_rubberband_autoscroll(wuss_rubberband_t *band,
                               wuss_window_t     *window)
{
  box_t   content;
  point_t scroll, to;

  if (!band->active)
    return 0;

  wuss_window_get_content_bounds(window, &content);
  wuss_window_get_scroll(window, &scroll);

  to.x = rubberband_scroll_axis(scroll.x, band->pointer.x,
                                content.x1 - content.x0, window->doc.w);
  to.y = rubberband_scroll_axis(scroll.y, band->pointer.y,
                                content.y1 - content.y0, window->doc.h);
  if (to.x == scroll.x && to.y == scroll.y)
    return 0;

  wuss_window_set_scroll(window, to);
  wuss_rubberband_to(band, window,
                     POINT(band->pointer.x + to.x, band->pointer.y + to.y));
  return 1;
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
