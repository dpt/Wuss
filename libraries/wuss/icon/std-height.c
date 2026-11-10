/* wuss/icon/std-height.c -- standard layout extents from the system font */

#include <assert.h>

#include "wuss/icon.h"

#include "../core/types.h"
#include "../font/font.h"
#include "../icon.h"

/* space either side of the text inside a field, slider or button bevel */
#define STD_FIELD_PAD  6
#define STD_SLIDER_PAD 4
#define STD_BUTTON_PAD 6

/* space below a frame caption before its contents' inset */
#define STD_FRAME_CAPTION_GAP 2

int wuss_std_height(const wuss_t *wuss, wuss_std_height_t which)
{
  int h;

  assert(wuss != NULL);

  h = wuss__system_font_height(wuss);

  switch (which)
  {
  case wuss_STD_FIELD:
    return h + 2 * STD_FIELD_PAD;
  case wuss_STD_SLIDER:
    return h + 2 * STD_SLIDER_PAD;
  case wuss_STD_SECONDARY_BUTTON:
    return h + 2 * (wuss_STD_SECONDARY_BUTTON_BORDER + STD_BUTTON_PAD);
  case wuss_STD_PRIMARY_BUTTON:
    return h + 2 * (wuss_STD_PRIMARY_BUTTON_BORDER + STD_BUTTON_PAD);
  case wuss_STD_FRAME_TOP:
    return h + STD_FRAME_CAPTION_GAP;
  case wuss_STD_LABEL:
  default:
    return h;
  }
}

inset_t wuss_std_frame_insets(const wuss_t *wuss)
{
  inset_t insets;

  insets.t = wuss_std_height(wuss, wuss_STD_FRAME_TOP);
  insets.r = wuss_STD_FRAME_INSET + wuss_STD_INSET;
  insets.b = wuss_STD_FRAME_INSET + wuss_STD_INSET;
  insets.l = wuss_STD_FRAME_INSET + wuss_STD_INSET;

  return insets;
}

int wuss__button_width(const wuss_t *wuss, const char *text, int border)
{
  bmfont_t *font;

  font = wuss_get_font(wuss);

  return (int) (wuss__text_width(font, text) +
                4 * wuss__text_width(font, " ")) + 2 * border;
}
