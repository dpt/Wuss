/* wuss/gadget/rubberband.h -- a drag-out selection box on a window */

/**
 * \file rubberband.h
 *
 * A wuss gadget: the rectangle a Select or Adjust drag on bare window
 * content stretches out, for selecting every item it touches. The task
 * starts the band on the press, moves it with each captured
 * wuss_EVENT_MOUSE, draws it from its wuss_EVENT_REDRAW handler after its
 * items and ends it on the release, then selects the items that meet
 * wuss_rubberband_t::box.
 *
 * The gadget is a plain struct the caller keeps. It allocates nothing and
 * needs no destroy. Points and the box are in window work area coordinates,
 * as wuss_event_t::data::mouse::point.
 *
 * Built only when WUSS_GADGETS is defined.
 */

#ifndef WUSS_GADGET_RUBBERBAND_H
#define WUSS_GADGET_RUBBERBAND_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "framebuf/colour.h"
#include "geom/box.h"
#include "geom/point.h"

#include "wuss/task.h"
#include "wuss/window.h"

/* ----------------------------------------------------------------------- */

/** A rubber band's state. Zero it (or end it) before first use. */
typedef struct wuss_rubberband
{
  int     active; /**< Non-zero between start and end. */
  point_t anchor; /**< The press point: the corner that stays put. */
  box_t   box;    /**< Anchor to pointer, maxima exclusive. Left as it was by
                   *   \ref wuss_rubberband_end, for the caller to select
                   *   with. */
}
wuss_rubberband_t;

/* ----------------------------------------------------------------------- */

/**
 * Start a band at \p point, one pixel square, and invalidate it.
 *
 * \param[in] band   The band.
 * \param[in] window Window the band is drawn on.
 * \param[in] point  Press point, window work area.
 */
void wuss_rubberband_start(wuss_rubberband_t *band,
                           wuss_window_t     *window,
                           point_t            point);

/**
 * Stretch the band from its anchor to \p point, invalidating the area it
 * covered and now covers. Does nothing unless the band is active.
 *
 * \param[in] band   The band.
 * \param[in] window Window the band is drawn on.
 * \param[in] point  Pointer position, window work area; may lie outside the
 *                   content while the mouse is captured.
 */
void wuss_rubberband_to(wuss_rubberband_t *band,
                        wuss_window_t     *window,
                        point_t            point);

/**
 * End the band and invalidate the area it covered. wuss_rubberband_t::box
 * keeps its final extent. Does nothing unless the band is active.
 *
 * \param[in] band   The band.
 * \param[in] window Window the band is drawn on.
 */
void wuss_rubberband_end(wuss_rubberband_t *band, wuss_window_t *window);

/**
 * Draw the band's outline in \p colour, if it is active. Call from the
 * window's wuss_EVENT_REDRAW handler, after drawing its items.
 *
 * \param[in] band   The band.
 * \param[in] event  The wuss_EVENT_REDRAW event.
 * \param[in] colour Outline colour.
 */
void wuss_rubberband_draw(const wuss_rubberband_t *band,
                          const wuss_event_t      *event,
                          colour_t                 colour);

#ifdef __cplusplus
}
#endif

#endif /* WUSS_GADGET_RUBBERBAND_H */
