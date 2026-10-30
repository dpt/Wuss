/* wuss/icon/impl.h -- private interface shared by the icon draw files */

#ifndef WUSS_ICON_PRIVATE_H
#define WUSS_ICON_PRIVATE_H

#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/point.h"

#include "../core/types.h"

/* ----------------------------------------------------------------------- */

/* Shared per-draw state, resolved once by wuss__icon_draw and handed to each
 * type's draw helper. "b" is the icon's screen box; "fg" is its resolved
 * foreground; "font" is the icon's selected weight (falling back to slot 0);
 * "font_height", "font_ascent" and "have_font" are 0 when there is no font or
 * no text to draw. Draw helpers vertically centre text via
 * wuss__icon_text_baseline_y(). */
typedef struct icon_draw_ctx
{
  wuss_t              *wuss;
  const wuss_window_t *window;
  screen_t            *scr;
  const wuss_icon_t   *icon;
  bmfont_t            *font;
  box_t                b;
  colour_t             fg;
  int                  font_height;
  int                  font_ascent;
  int                  have_font;
  point_t              origin; /* content box's document origin, screen space */
}
icon_draw_ctx_t;

/* Baseline y for text centred by ascender alone (not the descender too, so a
 * run of ascender-only text -- e.g. all caps and digits -- sits visually
 * centred rather than pulled high by unused descender space) in box "b". */
int wuss__icon_text_baseline_y(const icon_draw_ctx_t *c, const box_t *b);

/* Resolve the ground an icon's text/glyph blends against: an explicit icon bg,
 * else the window's dominant backdrop colour (a pattern fill blends against its
 * clear-bit colour, not its foreground), else "fallback" so bmfont still has a
 * blend colour. */
colour_t wuss__icon_blend_ground(const icon_draw_ctx_t *c,
                                 colour_t               fallback);

/* Draw a wuss_ICON_TYPE_MENU_ENTRY icon. Defined in draw-menu-entry.c. */
void wuss__icon_draw_menu_entry(const icon_draw_ctx_t *c);

#endif /* WUSS_ICON_PRIVATE_H */
