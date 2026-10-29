/* wuss/furniture/scroll-action.c -- wuss - minimal window manager */

#include "../core/types.h"
#include "../core/geometry.h"

/* The pixels the sausage travels along one bar ("track_px"), the document's
 * size along it and the furthest it scrolls. Returns zero when the sausage
 * cannot move: no track, or the document fits. */
static int sausage_track(const wuss_window_t *window,
                         int                  horizontal,
                         int                 *track_px,
                         int                 *doc_size,
                         int                 *max_scroll)
{
  box_t content;
  int   well_px, end_gap, content_size;

  wuss__content_box(window, &content);

  if (horizontal)
  {
    well_px      = wuss__hscroll_well_px(window);
    content_size = content.x1 - content.x0;
    *doc_size    = window->doc.w;
  }
  else
  {
    well_px      = wuss__vscroll_well_px(window);
    content_size = content.y1 - content.y0;
    *doc_size    = window->doc.h;
  }

  /* Sausage travels over the well minus the cosmetic end gaps; must match
   * wuss__*scroll_sausage_box so drag tracks the pointer 1:1. */
  end_gap   = (well_px > 2 * WUSS_SCROLL_END_GAP + WUSS_MIN_SAUSAGE) ? WUSS_SCROLL_END_GAP : 0;
  *track_px = well_px - 2 * end_gap;

  *max_scroll = MAX(*doc_size - content_size, 0);

  return *track_px > 0 && *doc_size > content_size;
}

void wuss__furniture_drag_sausage(wuss_window_t *window,
                                  int            delta_px,
                                  int            scroll_start,
                                  int            horizontal)
{
  int track_px, doc_size, max_scroll, new_scroll;

  if (sausage_track(window, horizontal, &track_px, &doc_size, &max_scroll))
    new_scroll = scroll_start + delta_px * doc_size / track_px;
  else
    new_scroll = 0;

  new_scroll = CLAMP(new_scroll, 0, max_scroll);

  if (horizontal)
    wuss_window_set_scroll(window, POINT(new_scroll, window->scroll.y));
  else
    wuss_window_set_scroll(window, POINT(window->scroll.x, new_scroll));
}

/* The pointer offsets along one bar, from a drag started at "scroll_start",
 * that scroll to 0 (*lo) and to the far end (*hi): the inverse of
 * wuss__furniture_drag_sausage's truncating delta_px * doc_size / track_px,
 * rounded outward so both ends stay reachable. */
static void sausage_reach(const wuss_window_t *window,
                          int                  horizontal,
                          int                  scroll_start,
                          int                 *lo,
                          int                 *hi)
{
  int track_px, doc_size, max_scroll;

  if (!sausage_track(window, horizontal, &track_px, &doc_size, &max_scroll))
  {
    *lo = *hi = 0;
    return;
  }

  *lo = -DIVIDE_ROUNDING_UP(scroll_start * track_px, doc_size);
  *hi = DIVIDE_ROUNDING_UP((max_scroll - scroll_start) * track_px, doc_size);
}

void wuss__furniture_sausage_bounds(const wuss_window_t       *window,
                                    wuss_furniture_drag_kind_t kind,
                                    int                        both,
                                    point_t                    p,
                                    box_t                     *out)
{
  int lo, hi;

  /* an axis the drag doesn't scroll stays within the grabbed bar's breadth */
  if (kind == wuss_FURNITURE_DRAG_VSCROLL_SAUSAGE)
    wuss__vscroll_well_box(window, out);
  else
    wuss__hscroll_well_box(window, out);

  /* the same axes wuss_mouse_move scrolls */
  if (kind == wuss_FURNITURE_DRAG_HSCROLL_SAUSAGE ||
      (both && (window->flags & wuss_WINDOW_HSCROLL)))
  {
    sausage_reach(window, 1, window->scroll.x, &lo, &hi);
    out->x0 = p.x + lo;
    out->x1 = p.x + hi + 1;
  }
  if (kind == wuss_FURNITURE_DRAG_VSCROLL_SAUSAGE ||
      (both && (window->flags & wuss_WINDOW_VSCROLL)))
  {
    sausage_reach(window, 0, window->scroll.y, &lo, &hi);
    out->y0 = p.y + lo;
    out->y1 = p.y + hi + 1;
  }
}

void wuss__scroll_repeat(wuss_t *wuss)
{
  struct wuss__furniture          *f;
  wuss_window_t                   *win;
  const wuss__furniture_element_t *element;

  f   = &wuss->furniture;
  win = f->dragging;
  if (win == NULL || f->drag_kind != wuss_FURNITURE_DRAG_NONE || f->repeat_step == 0)
    return;

  element = wuss__furniture_element(f->pressed_region);
  if (element == NULL || (element->step.x == 0 && element->step.y == 0))
    return; /* held, but not a scroll arrow */

  /* like RISC OS, sliding off the arrow pauses the repeat until it returns */
  if (wuss__furniture_hit_test(win, wuss->pointer) != f->pressed_region)
    return;

  if (--f->repeat_frames > 0)
    return;

  f->repeat_frames = WUSS_SCROLL_REPEAT_INTERVAL;
  wuss__scroll_step(win, POINT(element->step.x * f->repeat_step,
                               element->step.y * f->repeat_step));
}
