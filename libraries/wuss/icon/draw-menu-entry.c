/* wuss/icon/draw-menu-entry.c -- draw a menu entry icon */

#include <string.h>

#include "base/utils.h"
#include "geom/box.h"
#include "geom/point.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/pattern.h"
#include "framebuf/screen.h"
#include "text/utf8.h"

#include "../core/types.h"
#include "../core/palette.h"

#include "impl.h"

/* ----------------------------------------------------------------------- */

/* Draw one menu-decoration glyph (a tick or a submenu arrow) from the symbol
 * font slot, centred on "centre", blending "ink" over "ground". Returns 1 when
 * the glyph was drawn, 0 when the slot is empty or the font lacks that cell --
 * the caller then falls back to its vector rendering. */
static int wuss__draw_symbol_glyph(const wuss_t *wuss,
                                   screen_t     *scr,
                                   point_t       centre,
                                   unsigned long codepoint,
                                   colour_t      ink,
                                   colour_t      ground)
{
  bmfont_t *font;
  char      utf8[4];
  int       len;
  point_t   pos;
  int       font_width, font_height, font_ascent;

  font = wuss->fonts.fonts[WUSS_SYMBOL_FONT];
  if (font == NULL)
    return 0;

  if (bmfont_lookup(font, codepoint) < 0)
    return 0;

  len = utf8_encode(codepoint, utf8);
  bmfont_get_info(font, &font_width, &font_height, &font_ascent, NULL);
  pos.x = centre.x - font_width / 2;
  pos.y = centre.y - font_height / 2 + font_ascent;
  bmfont_draw(font, scr, utf8, len, ink, ground, NULL, &pos, NULL);
  return 1;
}

/* ----------------------------------------------------------------------- */

void wuss__icon_draw_menu_entry(const icon_draw_ctx_t *c)
{
  const wuss_icon_spec_t *icon = &c->icon->spec;
  const box_t            *b    = &c->b;
  colour_t                ink, ground, text_ink, text_ground, tmp;
  int                     disabled, highlit, pad, text_x0, text_x1;
  bmfont_width_t          space_w;

  disabled = (icon->flags & wuss_ICON_FLAGS_DISABLED) != 0;
  highlit  = wuss__icon_hovered(c->icon) && !disabled;
  pad      = WUSS_MENU_ENTRY_PAD;

  /* resolve the row's own ink over its own/inherited ground */
  ground = wuss__icon_blend_ground(c, c->fg);
  ink = disabled ? c->wuss->palette[c->wuss->bevel.dark] : c->fg;

  /* the highlight only swaps fg/bg over the text column -- the tick and
   * submenu-arrow gutters keep the row's normal ink/ground throughout */
  text_ink    = ink;
  text_ground = ground;
  if (highlit)
  {
    tmp         = text_ink;
    text_ink    = text_ground;
    text_ground = tmp;
  }

  /* past the tick gutter; short of the arrow gutter, whether or not this
   * row has an arrow */
  text_x0 = b->x0 + wuss__menu_entry_gutter_left(c->font_height);
  text_x1 = b->x1 - wuss__menu_entry_gutter_right(c->font_height);

  if (highlit)
  {
    box_t text_box;

    text_box    = *b;
    text_box.x0 = text_x0;
    text_box.x1 = text_x1;
    wuss__fill_colour(c->wuss, c->scr, &text_box, text_ground, c->origin);
  }
  else if (icon->bg != wuss_NO_BACKGROUND)
  {
    wuss__fill_colour(c->wuss, c->scr, b, ground, c->origin);
  }

  /* the label is drawn as if a space padded it either side (see below) */
  space_w = c->have_font ? wuss__fontset_space_width(&c->wuss->fonts, c->font)
                         : 0;

  /* colour chip: leads the text column, one space in, so the tick gutter
   * stays free and a row can be both ticked and swatched */
  if (icon->flags & wuss_ICON_FLAGS_SWATCH)
  {
    int cx, cy, h;

    h  = MAX(c->font_height, 8);
    cx = text_x0 + (int) space_w;
    cy = b->y0 + (b->y1 - b->y0 - h) / 2;
    if (icon->u.menu_entry.swatch == wuss_NO_BACKGROUND)
    {
      /* "None" row: hatched black/white chip, no real palette colour */
      pattern_t hatch;
      box_t     chip;

      chip.x0 = cx;
      chip.y0 = cy;
      chip.x1 = cx + h;
      chip.y1 = cy + h;
      hatch = pattern_from_preset(screen_PATTERN_DIAGONAL,
                                  colour_rgb(0, 0, 0),
                                  colour_rgb(255, 255, 255));
      /* phase against the chip's own corner, not the screen origin the
       * preset defaults to -- otherwise a window-move fast path that
       * blits already-settled pixels to their new position (leaving
       * their old screen-relative phase baked in) meets a freshly
       * repainted sliver phased against the new position, and the two
       * halves' hatching no longer lines up at the seam */
      hatch.origin = POINT(chip.x0, chip.y0);
      screen_fill_pattern(c->scr, &chip, &hatch);
    }
    else
    {
      screen_fill_rect(c->scr, cx, cy, SIZE2D(h, h),
                       c->wuss->palette[icon->u.menu_entry.swatch]);
    }
    screen_draw_rect(c->scr, cx, cy, SIZE2D(h, h), ink); /* 1px border */
  }

  /* tick in the left gutter when selected */
  if (wuss__icon_selected(c->icon))
  {
    int     cx, cy, h;
    point_t centre;

    h  = MAX(c->font_height, 8);
    cx = b->x0 + pad;
    cy = b->y0 + (b->y1 - b->y0 - h) / 2;
    centre.x = cx + h / 2;
    centre.y = cy + h / 2;
    if (!wuss__draw_symbol_glyph(c->wuss, c->scr, centre,
                                 WUSS_GLYPH_TICK, ink, ground))
    {
      screen_draw_line(c->scr, cx, cy + h / 2, cx + h / 2 - 1, cy + h - 2, ink);
      screen_draw_line(c->scr, cx + h / 2 - 1, cy + h - 2, cx + h - 2, cy, ink);
    }
  }

  /* right-edge arrow for a submenu entry */
  if (icon->flags & wuss_ICON_FLAGS_SUBMENU)
  {
    int     ax, ay, r, dy;
    point_t centre;

    r  = (c->font_height >= 8) ? c->font_height / 3 : 3;
    ax = b->x1 - 1 - pad - r;
    ay = b->y0 + (b->y1 - b->y0) / 2;
    centre.x = ax + r / 2;
    centre.y = ay;
    if (!wuss__draw_symbol_glyph(c->wuss, c->scr, centre,
                                 WUSS_GLYPH_SUBMENU, ink, ground))
    {
      for (dy = -r; dy <= r; dy++)
        screen_draw_line(c->scr, ax, ay + dy,
                         ax + (r - (dy < 0 ? -dy : dy)), ay + dy, ink);
    }
  }

  if (c->have_font && icon->text != NULL && icon->text[0] != '\0')
  {
    point_t pos;

    /* draw as if a space padded the text either side, without actually
     * touching the string -- only the left inset matters for pos.x, but
     * the same width is left spare at text_x1 too since the fill already
     * spans the full column. A swatch row's label follows its chip. */
    pos.x = text_x0 + (int) space_w;
    if (icon->flags & wuss_ICON_FLAGS_SWATCH)
      pos.x += wuss__menu_entry_swatch_width(c->font_height);
    pos.y = wuss__icon_text_baseline_y(c, b);
    wuss__text_draw_str(c->font, c->scr, icon->text, text_ink, text_ground,
                        &pos, NULL);

    /* shortcut: bold, knocked out of a solid rounded keycap right-aligned
     * against the same one-space inset at text_x1, sharing the label's
     * baseline. The keycap is muted to the bevel_dark grey (a palette entry,
     * so it survives low-depth desktops where a blended colour would snap
     * to ink or ground) except under the highlight, where it takes the
     * inverted ink to stay legible; the label is the row's ground. A
     * disabled row's keycap is only outlined, its label in the same grey as
     * the row's text, so it reads as muted at any depth */
    if (icon->u.menu_entry.shortcut != NULL &&
        icon->u.menu_entry.shortcut[0] != '\0')
    {
      const char    *shortcut;
      bmfont_width_t shortcut_w;
      colour_t       keycap;
      int            box_x0;
      size2d_t       cap_size;

      shortcut   = icon->u.menu_entry.shortcut;
      shortcut_w = wuss__shortcut_measure(c->wuss, shortcut);

      keycap = highlit ? text_ink : c->wuss->palette[c->wuss->bevel.dark];

      box_x0   = text_x1 - (int) space_w - (int) shortcut_w;
      cap_size = SIZE2D((int) shortcut_w, b->y1 - b->y0 - 2);
      pos.x    = box_x0 + WUSS_SHORTCUT_BOX_PAD;
      if (disabled)
      {
        screen_draw_rounded_rect(c->scr, box_x0, b->y0 + 1, cap_size, 2,
                                 keycap);
        wuss__shortcut_draw(c->wuss, c->scr, shortcut, keycap, text_ground,
                            &pos);
      }
      else
      {
        screen_fill_rounded_rect(c->scr, box_x0, b->y0 + 1, cap_size, 2,
                                 keycap);
        wuss__shortcut_draw(c->wuss, c->scr, shortcut, text_ground, keycap,
                            &pos);
      }
    }
  }
}
