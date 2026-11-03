/* wuss/gadget/rubberband.c -- a drag-out selection box on a window */

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/screen.h"
#include "geom/box.h"

#include "wuss/gadget/rubberband.h"

#include "../core/types.h"

/* ----------------------------------------------------------------------- */

/* the most an autoscroll moves per frame, on each axis */
#define RUBBERBAND_AUTOSCROLL_MAX 32

/* the outline's dash and gap, in pixels */
#define RUBBERBAND_DASH 2

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

/* invalidate the one-pixel outline of box, not its inside: only the outline
 * is drawn, so only it changes */
static void rubberband_invalidate_outline(wuss_window_t *window,
                                          const box_t   *box)
{
  box_t edge;

  edge    = *box;
  edge.y1 = box->y0 + 1;
  wuss_window_invalidate(window, &edge); /* top */
  edge.y0 = box->y1 - 1;
  edge.y1 = box->y1;
  wuss_window_invalidate(window, &edge); /* bottom */
  edge    = *box;
  edge.x1 = box->x0 + 1;
  wuss_window_invalidate(window, &edge); /* left */
  edge.x0 = box->x1 - 1;
  edge.x1 = box->x1;
  wuss_window_invalidate(window, &edge); /* right */
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
  box_t before;

  if (!band->active)
    return;

  rubberband_track(band, window, point);

  before       = band->box;
  band->box.x0 = MIN(band->anchor.x, point.x);
  band->box.y0 = MIN(band->anchor.y, point.y);
  band->box.x1 = MAX(band->anchor.x, point.x) + 1;
  band->box.y1 = MAX(band->anchor.y, point.y) + 1;
  if (box_equal(&before, &band->box))
    return;

  /* erase the old outline, draw the new; a moved corner shifts the dashes'
   * phase, so the whole new outline is redrawn */
  rubberband_invalidate_outline(window, &before);
  rubberband_invalidate_outline(window, &band->box);
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
                               wuss_window_t     *window,
                               int                horizontal)
{
  box_t   content;
  point_t scroll, to;

  if (!band->active)
    return 0;

  wuss_window_get_content_bounds(window, &content);
  wuss_window_get_scroll(window, &scroll);

  to.x = scroll.x;
  if (horizontal)
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
  rubberband_invalidate_outline(window, &band->box);
}

void wuss_rubberband_draw(const wuss_rubberband_t *band,
                          const wuss_event_t      *event,
                          colour_t                 colour)
{
  screen_t *scr;
  int       x0, y0;
  int       x1, y1;

  if (!band->active)
    return;

  /* work area to screen; the band's far edges are exclusive */
  scr = event->data.redraw.scr;
  x0  = band->box.x0 + event->data.redraw.bounds->x0 -
        event->data.redraw.scroll.x;
  y0  = band->box.y0 + event->data.redraw.bounds->y0 -
        event->data.redraw.scroll.y;
  x1  = x0 + band->box.x1 - band->box.x0 - 1;
  y1  = y0 + band->box.y1 - band->box.y0 - 1;
  if (x1 < x0 || y1 < y0)
    return;

  screen_draw_dashed_line(scr, x0, y0, x1, y0, RUBBERBAND_DASH,
                          RUBBERBAND_DASH, colour);
  screen_draw_dashed_line(scr, x0, y1, x1, y1, RUBBERBAND_DASH,
                          RUBBERBAND_DASH, colour);
  screen_draw_dashed_line(scr, x0, y0, x0, y1, RUBBERBAND_DASH,
                          RUBBERBAND_DASH, colour);
  screen_draw_dashed_line(scr, x1, y0, x1, y1, RUBBERBAND_DASH,
                          RUBBERBAND_DASH, colour);
}
