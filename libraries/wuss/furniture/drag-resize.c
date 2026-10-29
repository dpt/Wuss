/* wuss/furniture/drag-resize.c -- wuss - minimal window manager */

#include "base/utils.h"

#include "../core/types.h"
#include "../core/geometry.h"

void wuss__furniture_drag_resize(wuss_window_t *window, point_t p)
{
  box_t    content;
  size2d_t min, max;
  int      width, height;

  wuss__content_box(window, &content);
  wuss__min_content(window, &min);
  wuss__max_content_anywhere_on_screen(window, &max);

  /* Subtract the offset recorded at drag start so the point originally
   * grabbed on the resize icon stays under the pointer, rather than the
   * window's edge snapping to meet the pointer on the first move. */
  width  = p.x - window->wuss->furniture.drag_offset.x - content.x0;
  height = p.y - window->wuss->furniture.drag_offset.y - content.y0;
  width  = CLAMP(width,  min.w, MAX(window->doc.w, min.w));
  height = CLAMP(height, min.h, MAX(window->doc.h, min.h));
  width  = MIN(width,  max.w);
  height = MIN(height, max.h);

  wuss_window_resize(window, SIZE2D(width, height));
}

void wuss__furniture_resize_bounds(const wuss_window_t *window, box_t *out)
{
  box_t    content, screen;
  size2d_t min, max;
  point_t  origin;

  wuss__content_box(window, &content);
  wuss__min_content(window, &min);
  wuss__max_content_anywhere_on_screen(window, &max);

  /* the pointer positions wuss__furniture_drag_resize maps onto the sizes it
   * allows, kept on the screen */
  origin.x = content.x0 + window->wuss->furniture.drag_offset.x;
  origin.y = content.y0 + window->wuss->furniture.drag_offset.y;
  out->x0  = origin.x + min.w;
  out->y0  = origin.y + min.h;
  out->x1  = origin.x + MIN(MAX(window->doc.w, min.w), max.w) + 1;
  out->y1  = origin.y + MIN(MAX(window->doc.h, min.h), max.h) + 1;

  screen = (box_t) BOX_POS_SIZE(0, 0, window->wuss->scr->size.w,
                                window->wuss->scr->size.h);
  (void) box_intersection(out, &screen, out);
}
