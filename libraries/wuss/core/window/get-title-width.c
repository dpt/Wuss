/* wuss/window/get-title-width.c -- wuss - minimal window manager */

#include <limits.h>
#include <string.h>

#include "../impl.h"

int wuss_window_get_title_width(const wuss_window_t *window)
{
#ifdef WUSS_FURNITURE
  bmfont_t      *font;
  int            len;
  int            split_point;
  bmfont_width_t text_w;
  box_t          slot;
  box_t          content;

  if (window->title[0] == '\0' || (window->flags & wuss_WINDOW_NO_TITLEBAR))
    return 0;

  font = wuss__bold_font(window->task->wuss);
  if (font == NULL)
    return 0;

  len = (int) strlen(window->title);
  if (wuss__text_measure(font, window->title, len, INT_MAX / 2, &split_point,
                         &text_w))
    return 0;

  /* the slot and the content area grow and shrink together, so the content
   * needs to grow by however much the slot is short */
  wuss__title_slot_box(window, &slot);
  wuss__content_box(window, &content);
  return MAX(0, (content.x1 - content.x0) + (int) text_w
                - (slot.x1 - slot.x0));
#else
  NOT_USED(window);
  return 0;
#endif
}
