/* wuss/gadget/gridview.c -- a glyph+label grid on a caller's window */

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "datastruct/bitvec.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/point.h"
#include "geom/size.h"
#include "utils/array.h"

#include "wuss/icon.h"
#include "wuss/window.h"

#include "wuss/gadget/gridview.h"
#include "wuss/gadget/rubberband.h"

#include "../core/types.h"
#include "../core/palette.h"
#include "../font/font.h"
#include "../icon.h"

/* ----------------------------------------------------------------------- */

#define GRIDVIEW_ELLIPSIS "..."

/* the window never shrinks narrower than a cell with a label this long */
#define GRIDVIEW_MIN_LABEL_CHARS 10

/* with no items there is no glyph to measure, so an empty grid assumes one
 * this tall (a RISC OS file icon's height) to keep a cell's room for one */
#define GRIDVIEW_EMPTY_GLYPH_H 34

static void gridview_reflow(wuss_gridview_t *gv);

/* a glyph and its half-size copy, made for the small modes; small is NULL
 * if it could not be made (the glyph is then plotted scaled, if it can be) */
struct gridview_small
{
  const bitmap_t *src;
  bitmap_t       *small;
};

struct wuss_gridview
{
  alloc_t                       alloc;   /* copied hooks; wuss_t not retained */
  wuss_window_t                 *window;
  int                            count;
  wuss_gridview_item_fn_t       *item_fn;
  wuss_gridview_activate_fn_t   *activate;
  wuss_gridview_drag_fn_t       *drag;    /* NULL: items are not draggable */
  int                            press_index; /* item pressed, -1 for none */
  int                            cursor;  /* item arrow keys move from, -1
                                           * for none */
  int                            menu_index; /* item selected only for an
                                              * open menu, -1 for none */
  int                            menu_closed; /* that menu has picked and
                                              * closed; undo at the next
                                              * event */
  point_t                        press_point; /* its document point */
  wuss_button_t                  press_button; /* button that pressed it */
  wuss_rubberband_t              band;    /* a press on empty space */
  void                         *opaque;
  bitvec_t                      *selected;
  int                            label_chars;
  wuss_gridview_mode_t           mode;
  size2d_t                       cell;    /* glyph+label cell size, with padding */
  size2d_t                       glyph;   /* widest and tallest glyph */
  int                            label_w; /* widest label, capped, with its
                                           * capsule's ends */
  int                            info_w[wuss_GRIDVIEW_MAX_INFO]; /* widest
                                           * of each info column; measured
                                           * in wuss_GRIDVIEW_FULL only */
  int                            min_label_w; /* GRIDVIEW_MIN_LABEL_CHARS wide */
  struct gridview_small         *smalls;  /* half-size glyphs made so far */
  int                            nsmalls;
  int                            smalls_cap;
  bmfont_t                      *measured_font; /* font last measured with */
  int                            measured_height; /* its height then */
  int                            pad_x;   /* cell side edge to its contents */
  int                            pad_y;   /* cell top/bottom edge to contents */
  int                            spacing; /* glyph to label */
  int                            columns; /* set by reflow; at least 1 once
                                           * reflowed, 0 before */
  size2d_t                       doc;     /* extent last given the window */
  int                            inset;   /* margin round the whole grid */
  int                            max_columns; /* 0 for no limit */
  int                            pow2;    /* columns restricted to 1, 2, 4... */
  int                            selectable;
  int                            fixed_cell; /* cell set by the caller */
  int                            has_bg;  /* bg replaces window_bg */
  colour_t                       bg;
  wuss_gridview_draw_fn_t       *draw;    /* NULL: draw glyph and label */
};

/* ----------------------------------------------------------------------- */

/* Item "index"'s appearance, cleared first so the callback need only set
 * what it has. */
static void gridview_get_item(const wuss_gridview_t *gv,
                              int                    index,
                              wuss_gridview_item_t  *item)
{
  memset(item, 0, sizeof(*item));
  gv->item_fn(index, gv->opaque, item);
}

/* A glyph's size in the small modes: half, rounded up. */
static size2d_t gridview_small_size(size2d_t size)
{
  return SIZE2D((size.w + 1) / 2, (size.h + 1) / 2);
}

/* Walk every item once and record the widest and tallest glyph
 * (GRIDVIEW_EMPTY_GLYPH_H tall if there are no items), the widest label
 * (capped at "label_chars" characters of the window's font) and, in
 * wuss_GRIDVIEW_FULL, the widest of each info column. Costs a text measure
 * per item, so it runs only when the items, the font or the mode may have
 * changed -- the label cap stops one long name inflating the grid. */
static void gridview_measure_items(wuss_gridview_t *gv)
{
  wuss_t              *wuss;
  bmfont_t            *font;
  wuss_gridview_item_t item;
  bmfont_width_t       char_w, text_w;
  int                  glyph_w, glyph_h, label_w, cap_w, i, j;

  wuss    = gv->window->task->wuss;
  font    = wuss->fonts.fonts[0];
  glyph_w = 0;
  glyph_h = 0;
  label_w = 0;
  cap_w   = 0;
  for (j = 0; j < wuss_GRIDVIEW_MAX_INFO; j++)
    gv->info_w[j] = 0;

  gv->min_label_w = 0;
  if (font != NULL)
  {
    char_w = wuss__text_width(font, "M");
    cap_w           = (int) char_w * gv->label_chars;
    gv->min_label_w = (int) char_w * MIN(gv->label_chars,
                                         GRIDVIEW_MIN_LABEL_CHARS);
  }

  for (i = 0; i < gv->count; i++)
  {
    gridview_get_item(gv, i, &item);
    if (item.glyph != NULL)
    {
      glyph_w = MAX(glyph_w, item.glyph->size.w);
      glyph_h = MAX(glyph_h, item.glyph->size.h);
    }

    if (font == NULL)
      continue;

    if (item.label != NULL)
    {
      text_w = wuss__text_width(font, item.label);
      label_w = MAX(label_w, MIN((int) text_w, cap_w));
    }

    if (gv->mode != wuss_GRIDVIEW_FULL)
      continue;

    for (j = 0; j < wuss_GRIDVIEW_MAX_INFO; j++)
    {
      if (item.info[j] == NULL || item.info[j][0] == '\0')
        continue;

      text_w = wuss__text_width(font, item.info[j]);
      gv->info_w[j] = MAX(gv->info_w[j], (int) text_w);
    }
  }

  /* labels are drawn in a capsule with half-height round ends */
  if (label_w > 0)
  {
    int height;

    bmfont_get_info(font, NULL, &height, NULL, NULL);
    label_w += 2 * (height / 2);
  }

  if (gv->count == 0)
    glyph_h = GRIDVIEW_EMPTY_GLYPH_H;

  gv->glyph           = SIZE2D(glyph_w, glyph_h);
  gv->label_w         = label_w;
  gv->measured_font   = font;
  gv->measured_height = wuss__fontset_height(&wuss->fonts, 0);
}

/* Width the small modes give the glyph and the gap after it, 0 with no
 * glyphs. */
static int gridview_small_glyph_space(const wuss_gridview_t *gv)
{
  int w;

  w = gridview_small_size(gv->glyph).w;
  return w > 0 ? w + gv->spacing : 0;
}

/* Width the info columns take at a cell's right, each with a gap before it;
 * 0 outside wuss_GRIDVIEW_FULL. */
static int gridview_info_space(const wuss_gridview_t *gv)
{
  int w, j;

  w = 0;
  if (gv->mode == wuss_GRIDVIEW_FULL)
    for (j = 0; j < wuss_GRIDVIEW_MAX_INFO; j++)
      if (gv->info_w[j] > 0)
        w += gv->pad_x + gv->info_w[j];

  return w;
}

/* Cell size for a label "label_w" wide, from the cached glyph and info sizes,
 * the padding and the font height: glyph stacked over label in
 * wuss_GRIDVIEW_LARGE, else side by side. Cheap, so safe to call whenever
 * padding changes. */
static size2d_t gridview_cell_for(const wuss_gridview_t *gv, int label_w)
{
  const wuss_t *wuss;
  int           font_h;

  wuss   = gv->window->task->wuss;
  font_h = wuss__fontset_height(&wuss->fonts, 0);

  if (gv->mode == wuss_GRIDVIEW_LARGE)
    return SIZE2D(MAX(gv->glyph.w, label_w) + 2 * gv->pad_x,
                  gv->glyph.h + font_h + 2 * gv->pad_y + gv->spacing);

  return SIZE2D(2 * gv->pad_x + gridview_small_glyph_space(gv) + label_w +
                gridview_info_space(gv),
                MAX(gridview_small_size(gv->glyph).h, font_h) +
                2 * gv->pad_y);
}

static size2d_t gridview_cell_size(const wuss_gridview_t *gv)
{
  return gridview_cell_for(gv, gv->label_w);
}

/* Forget the half-size glyphs, e.g. once the pool's pixels may have
 * changed. */
static void gridview_free_smalls(wuss_gridview_t *gv)
{
  int i;

  for (i = 0; i < gv->nsmalls; i++)
  {
    if (gv->smalls[i].small == NULL)
      continue;

    /* as bitmap_resize allocated them */
    free(gv->smalls[i].small->base);
    free(gv->smalls[i].small);
  }

  gv->nsmalls = 0;
}

/* The half-size copy of "glyph", made on first use: area-averaged so fine
 * detail survives. NULL if it cannot be made (paletted, or out of memory),
 * in which case the caller plots the glyph scaled instead. */
static const bitmap_t *gridview_small_glyph(wuss_gridview_t *gv,
                                            const bitmap_t  *glyph)
{
  bitmap_t  raw;
  bitmap_t *deep;
  bitmap_t *small;
  int       i;

  for (i = 0; i < gv->nsmalls; i++)
    if (gv->smalls[i].src == glyph)
      return gv->smalls[i].small;

  if (array_grow_alloc(&gv->alloc, (void **) &gv->smalls,
                       sizeof(*gv->smalls), gv->nsmalls, &gv->smalls_cap, 1,
                       8))
    return NULL;

  /* resize needs 32bpp: expand an RLE-compressed glyph, then a paletted one,
   * keeping its palette's alpha */
  small = NULL;
  if (bitmap_clone(glyph, &raw) == result_OK)
  {
    deep = NULL;
    if (pixelfmt_log2bpp(raw.format) == 5 ||
        bitmap_convert(&raw, pixelfmt_bgra8888, &deep) == result_OK)
    {
      if (bitmap_resize(deep != NULL ? deep : &raw,
                        gridview_small_size(glyph->size),
                        bitmap_resample_BOX, 1, &small) != result_OK)
        small = NULL;
    }
    if (deep != NULL)
    {
      free(deep->base);
      free(deep);
    }
    free(raw.base);
    free(raw.palette);
  }

  gv->smalls[gv->nsmalls].src   = glyph;
  gv->smalls[gv->nsmalls].small = small;
  gv->nsmalls++;

  return small;
}

result_t wuss_gridview_create(wuss_gridview_t            **out,
                              wuss_window_t               *window,
                              int                          count,
                              wuss_gridview_item_fn_t     *item_fn,
                              int                          label_chars,
                              wuss_gridview_activate_fn_t *activate,
                              void                        *opaque)
{
  wuss_t          *wuss;
  wuss_gridview_t *gv;

  if (out == NULL || window == NULL || item_fn == NULL)
    return result_NULL_ARG;

  if (count < 0 || label_chars < 1)
    return result_BAD_ARG;

  wuss = window->task->wuss;

  gv = wuss->alloc.malloc(sizeof(*gv));
  if (gv == NULL)
    return result_OOM;

  gv->selected = bitvec_create((unsigned int) count);
  if (gv->selected == NULL)
  {
    wuss->alloc.free(gv);
    return result_OOM;
  }

  gv->alloc       = wuss->alloc;
  gv->window      = window;
  gv->count       = count;
  gv->item_fn     = item_fn;
  gv->activate    = activate;
  gv->drag        = NULL;
  gv->press_index = -1;
  gv->band.active = 0;
  gv->cursor      = -1;
  gv->menu_index  = -1;
  gv->menu_closed = 0;
  gv->opaque      = opaque;
  gv->label_chars = label_chars;
  gv->mode        = wuss_GRIDVIEW_LARGE;
  gv->smalls      = NULL;
  gv->nsmalls     = 0;
  gv->smalls_cap  = 0;
  gv->columns     = 0;
  gv->doc         = SIZE2D(0, 0);
  gv->inset       = 0;
  gv->max_columns = 0;
  gv->pow2        = 0;
  gv->selectable  = 1;
  gv->fixed_cell  = 0;
  gv->pad_x       = wuss_STD_GAP * 2;
  gv->pad_y       = wuss_STD_GAP;
  gv->spacing     = wuss_STD_GAP;
  gv->has_bg      = 0;
  gv->draw        = NULL;
  gridview_measure_items(gv);
  gv->cell        = gridview_cell_size(gv);

  gridview_reflow(gv);

  *out = gv;
  return result_OK;
}

void wuss_gridview_destroy(wuss_gridview_t *doomed)
{
  if (doomed == NULL)
    return;

  gridview_free_smalls(doomed);
  doomed->alloc.free(doomed->smalls);
  bitvec_destroy(doomed->selected);
  doomed->alloc.free(doomed);
}

/* ----------------------------------------------------------------------- */

/* Columns that fit "width" under the layout policy: at least 1, at most the
 * item count and max_columns, and a power of two when pow2 is set. */
static int gridview_columns_for_width(const wuss_gridview_t *gv, int width)
{
  int cols, p;

  cols = (width - 2 * gv->inset) / gv->cell.w;
  cols = MAX(cols, 1);
  if (gv->mode == wuss_GRIDVIEW_FULL)
    return 1;
  if (gv->max_columns > 0)
    cols = MIN(cols, gv->max_columns);
  else
    cols = MIN(cols, MAX(gv->count, 1));

  if (gv->pow2)
  {
    p = 1;
    while (p * 2 <= cols)
      p *= 2;
    cols = p;
  }

  return cols;
}

static int gridview_width_for_columns(const wuss_gridview_t *gv, int cols)
{
  return 2 * gv->inset + cols * gv->cell.w;
}

/* Smallest content size: one cell, widened to fit a GRIDVIEW_MIN_LABEL_CHARS
 * label unless the caller fixed the cell size. */
static size2d_t gridview_min_size(const wuss_gridview_t *gv)
{
  int w;

  w = gv->cell.w;
  if (!gv->fixed_cell)
    w = MAX(w, gridview_cell_for(gv, gv->min_label_w).w);
  return SIZE2D(w + 2 * gv->inset, gv->cell.h + 2 * gv->inset);
}

/* Columns wanted, held to the item count, and to one in wuss_GRIDVIEW_FULL. */
static int gridview_column_cap(const wuss_gridview_t *gv, int columns)
{
  if (gv->mode == wuss_GRIDVIEW_FULL)
    return 1;

  return MIN(MAX(columns, 1), MAX(gv->count, 1));
}

/* Top-left of "glyph" in a cell whose top-left is "cell", and its size as
 * drawn. */
static point_t gridview_glyph_at(const wuss_gridview_t *gv,
                                 const bitmap_t        *glyph,
                                 point_t                cell,
                                 size2d_t              *size)
{
  size2d_t room;

  if (gv->mode == wuss_GRIDVIEW_LARGE)
  {
    *size = glyph->size;
    return POINT(cell.x + (gv->cell.w - size->w) / 2, cell.y + gv->pad_y);
  }

  *size = gridview_small_size(glyph->size);
  room  = gridview_small_size(gv->glyph);
  return POINT(cell.x + gv->pad_x + (room.w - size->w) / 2,
               cell.y + (gv->cell.h - size->h) / 2);
}

/* Recompute the column count from the window's current content width, and
 * set the document extent to match. The extent is as wide as the widest
 * layout this screen and item count allow, so the window can always be
 * dragged wider; it is as tall as the rows the current layout needs. Does
 * nothing if neither the columns nor the extent changed. */
static void gridview_reflow(wuss_gridview_t *gv)
{
  box_t    content;
  size2d_t doc, max, min;
  int      cols, max_cols, rows;

  wuss_window_get_content_bounds(gv->window, &content);
  wuss_window_get_max_content(gv->window, &max);

  cols     = gridview_columns_for_width(gv, content.x1 - content.x0);
  max_cols = MAX(gridview_columns_for_width(gv, max.w), cols);
  rows     = DIVIDE_ROUNDING_UP(gv->count, cols);
  min      = gridview_min_size(gv);

  /* always positive: cell size and at least one column and row; at least
   * min wide, as the window's min_doc is capped to its doc */
  doc = SIZE2D(MAX(gridview_width_for_columns(gv, max_cols), min.w),
               MAX(rows, 1) * gv->cell.h + 2 * gv->inset);

  /* cannot fail: both sizes are positive */
  (void) wuss_window_set_min_size(gv->window, min);

  if (cols == gv->columns && doc.w == gv->doc.w && doc.h == gv->doc.h)
    return;

  gv->columns = cols;
  gv->doc     = doc;

  (void) wuss_window_set_doc(gv->window, doc);
}

/* Document-space bbox of cell "index". */
static box_t gridview_cell_box(const wuss_gridview_t *gv, int index)
{
  int col, row;

  col = index % gv->columns;
  row = index / gv->columns;
  return (box_t) BOX_POS_SIZE(gv->inset + col * gv->cell.w,
                              gv->inset + row * gv->cell.h,
                              gv->cell.w, gv->cell.h);
}

/* Document origin in screen space for a redraw event: the phase the
 * window's own background fill uses, so dithered fills line up with it. */
static point_t gridview_origin(const wuss_event_t *event)
{
  return POINT(event->data.redraw.bounds->x0 - event->data.redraw.scroll.x,
               event->data.redraw.bounds->y0 - event->data.redraw.scroll.y);
}

/* Item index under document point "p", or -1 if none (including gaps and
 * past the last item). */
static int gridview_hit_test(const wuss_gridview_t *gv, point_t p)
{
  int col, row, index;

  p.x -= gv->inset;
  p.y -= gv->inset;
  if (p.x < 0 || p.y < 0)
    return -1;

  col = p.x / gv->cell.w;
  row = p.y / gv->cell.h;
  if (col >= gv->columns)
    return -1;

  index = row * gv->columns + col;
  return (index >= 0 && index < gv->count) ? index : -1;
}

/* Mark one cell dirty, leaving the rest of the window alone. */
static void gridview_invalidate_cell(wuss_gridview_t *gv, int index)
{
  box_t box;

  box = gridview_cell_box(gv, index);
  wuss_window_invalidate(gv->window, &box);
}

/* The pointer moved with an item pressed: past the drag threshold, offer the
 * item to the drag callback and, if it takes it, start a core drag of the
 * item's glyph (as redraw places it), or of the cell's ants with no glyph.
 * Hover moves also arrive here, so a press the pointer left the window
 * during (see wuss_EVENT_POINTER_EXIT) must already have been forgotten. */
static void gridview_drag_maybe(wuss_gridview_t *gv, point_t point)
{
  result_t             rc;
  wuss_t              *wuss;
  box_t                cell;
  wuss_gridview_item_t item;
  point_t              at;
  size2d_t             drawn;
  point_t              hotspot;
  int                  threshold;
  int                  index;
  int                  dx;
  int                  dy;

  if (gv->press_index < 0)
    return;

  wuss      = gv->window->task->wuss;
  threshold = wuss->input.drag_threshold_px;
  dx        = point.x - gv->press_point.x;
  dy        = point.y - gv->press_point.y;
  if (dx * dx + dy * dy < threshold * threshold)
    return;

  index           = gv->press_index;
  gv->press_index = -1;

  if (!gv->drag(gv, index, gv->opaque))
    return;

  cell = gridview_cell_box(gv, index);
  gridview_get_item(gv, index, &item);
  if (item.glyph != NULL)
  {
    /* the full-size glyph, held at the point pressed on the drawn one */
    at      = gridview_glyph_at(gv, item.glyph, POINT(cell.x0, cell.y0),
                                &drawn);
    hotspot = POINT((gv->press_point.x - at.x) * item.glyph->size.w / drawn.w,
                    (gv->press_point.y - at.y) * item.glyph->size.h / drawn.h);
    rc = wuss_drag_start_bitmap(wuss, gv->window, item.glyph, hotspot);
  }
  else
  {
    hotspot = POINT(gv->press_point.x - cell.x0,
                    gv->press_point.y - cell.y0);
    rc = wuss_drag_start(wuss, gv->window, gv->cell, hotspot);
  }
  if (rc == result_OK)
    wuss->drag.button = gv->press_button; /* as an icon drag does */
}

/* Deselect everything, dirtying only the cells that were selected. */
static void gridview_clear_selected(wuss_gridview_t *gv)
{
  int i;

  for (i = bitvec_next(gv->selected, -1); i >= 0;
       i = bitvec_next(gv->selected, i))
    gridview_invalidate_cell(gv, i);

  bitvec_clear_all(gv->selected);
  gv->menu_index = -1;
}

/* The band is released: select every cell it touches. */
static void gridview_band_end(wuss_gridview_t *gv)
{
  int   i;
  box_t cell, clipped;

  wuss_rubberband_end(&gv->band, gv->window);

  for (i = 0; i < gv->count; i++)
  {
    cell = gridview_cell_box(gv, i);
    if (!box_intersection(&gv->band.box, &cell, &clipped) &&
        !bitvec_get(gv->selected, (bitvec_index_t) i))
    {
      (void) bitvec_set(gv->selected, (bitvec_index_t) i);
      gridview_invalidate_cell(gv, i);
      gv->cursor = i;
    }
  }
}

/* Scroll the window the least that brings cell "index" fully into view. */
static void gridview_scroll_to(wuss_gridview_t *gv, int index)
{
  box_t   cell;
  box_t   content;
  point_t scroll;
  int     h;

  cell = gridview_cell_box(gv, index);
  wuss_window_get_content_bounds(gv->window, &content);
  wuss_window_get_scroll(gv->window, &scroll);
  h = content.y1 - content.y0;

  if (cell.y0 < scroll.y)
    scroll.y = cell.y0;
  else if (cell.y1 > scroll.y + h)
    scroll.y = cell.y1 - h;
  else
    return;

  wuss_window_set_scroll(gv->window, scroll);
}

/* Return: activate the cursor item if selected, else the first selected one,
 * deselecting it first as a double-click does. Returns 1 if consumed. */
static int gridview_return(wuss_gridview_t *gv)
{
  int index;

  if (gv->activate == NULL)
    return 0;

  index = gv->cursor;
  if (index < 0 || index >= gv->count ||
      !bitvec_get(gv->selected, (bitvec_index_t) index))
    index = bitvec_next(gv->selected, -1);
  if (index < 0 || index >= gv->count)
    return 0;

  /* before the call: it may rescan or reorder the items */
  bitvec_clear(gv->selected, (bitvec_index_t) index);
  gridview_invalidate_cell(gv, index);

  gv->activate(gv, index, wuss_BUTTON_SELECT | wuss_BUTTON_DOUBLE,
               gv->opaque);
  return 1;
}

/* An arrow key: select only the item beside the cursor, or the first selected
 * item (or item 0) if there is no cursor yet. Return activates. Returns 1 if
 * consumed. */
static int gridview_key(wuss_gridview_t *gv, int code)
{
  int index;

  if (!gv->selectable || gv->count == 0 || gv->columns == 0)
    return 0;

  if (code == wuss_KEY_RETURN)
    return gridview_return(gv);

  if (code != wuss_KEY_LEFT && code != wuss_KEY_RIGHT &&
      code != wuss_KEY_UP && code != wuss_KEY_DOWN)
    return 0;

  index = gv->cursor;
  if (index < 0 || index >= gv->count)
  {
    index = bitvec_next(gv->selected, -1);
    if (index < 0 || index >= gv->count)
      index = 0;
  }
  else if (code == wuss_KEY_LEFT && index % gv->columns > 0)
  {
    index--;
  }
  else if (code == wuss_KEY_RIGHT && index % gv->columns < gv->columns - 1 &&
           index + 1 < gv->count)
  {
    index++;
  }
  else if (code == wuss_KEY_UP && index >= gv->columns)
  {
    index -= gv->columns;
  }
  else if (code == wuss_KEY_DOWN &&
           index / gv->columns < (gv->count - 1) / gv->columns)
  {
    /* into a short last row: the last item */
    index = MIN(index + gv->columns, gv->count - 1);
  }

  wuss_gridview_select(gv, index);
  gridview_scroll_to(gv, index);
  return 1;
}

/* ----------------------------------------------------------------------- */

static void gridview_draw_cell(wuss_gridview_t    *gv,
                               const wuss_event_t *event,
                               int                 index)
{
  wuss_t              *wuss;
  screen_t            *scr;
  bmfont_t            *font;
  wuss_gridview_item_t item;
  box_t                doc_box, screen_box, clipped_clip;
  screen_t             clipped;
  colour_t             fg, bg, cell_fg, cell_bg;
  point_t              at, pos;
  size2d_t             drawn;
  bmfont_width_t       w;
  int                  len, split, ascent, height, end_w, avail, info_x, j;
  int                  capsule_w, label_x, label_y;
  char                 buf[64];

  wuss = gv->window->task->wuss;
  scr  = event->data.redraw.scr;
  font = wuss->fonts.fonts[0];

  doc_box = gridview_cell_box(gv, index);
  wuss__icon_box_to_screen(event->data.redraw.bounds,
                          event->data.redraw.scroll, &doc_box, &screen_box);
  if (box_intersection(&scr->clip, &screen_box, &clipped_clip))
    return;
  if (gv->draw != NULL)
  {
    box_t saved;

    saved    = scr->clip;
    scr->clip = clipped_clip;
    gv->draw(gv, event, index, &screen_box, gv->opaque);
    scr->clip = saved;
    return;
  }

  clipped      = *scr;
  clipped.clip = clipped_clip;

  gridview_get_item(gv, index, &item);

  cell_fg = wuss->palette[wuss->button.fg];
  cell_bg = wuss_gridview_is_selected(gv, index)
          ? wuss->palette[wuss->accent]
          : wuss->palette[wuss->body.window];
  wuss__fill_colour(wuss, &clipped, &screen_box, cell_bg,
                    gridview_origin(event));

  /* a selected label is drawn inverted in its capsule */
  fg = cell_fg;
  bg = cell_bg;
  if (wuss_gridview_is_selected(gv, index))
  {
    fg = cell_bg;
    bg = cell_fg;
  }

  if (item.glyph != NULL)
  {
    at = gridview_glyph_at(gv, item.glyph,
                           POINT(screen_box.x0, screen_box.y0), &drawn);
    if (gv->mode == wuss_GRIDVIEW_LARGE)
    {
      screen_copy_bitmap(&clipped, at.x, at.y, item.glyph);
    }
    else
    {
      const bitmap_t *small;

      small = gridview_small_glyph(gv, item.glyph);
      if (small != NULL)
      {
        screen_copy_bitmap(&clipped, at.x, at.y, small);
      }
      else
      {
        static const screen_scale_t half = { 1, 1, 2, 2 };

        box_t dst;

        /* fails for a compressed glyph, which is then left out */
        dst = (box_t) BOX_POS_SIZE(at.x, at.y, drawn.w, drawn.h);
        (void) screen_copy_bitmap_scaled(&clipped, &dst, item.glyph, &half);
      }
    }
  }

  if (font == NULL)
    return;

  bmfont_get_info(font, NULL, &height, &ascent, NULL);

  /* the label sits in a capsule whose round ends are each half the font
   * height wide */
  end_w = height / 2;

  if (gv->mode == wuss_GRIDVIEW_FULL)
  {
    /* info columns, anchored to the cell's right, uninverted */
    info_x = screen_box.x1 - gv->pad_x - gridview_info_space(gv);
    for (j = 0; j < wuss_GRIDVIEW_MAX_INFO; j++)
    {
      if (gv->info_w[j] == 0)
        continue;

      info_x += gv->pad_x;
      if (item.info[j] != NULL && item.info[j][0] != '\0')
      {
        pos.x = info_x;
        pos.y = screen_box.y0 + (gv->cell.h - height) / 2 + ascent;
        (void) wuss__text_draw_str(font, &clipped, item.info[j], cell_fg,
                                   cell_bg, &pos, NULL);
      }
      info_x += gv->info_w[j];
    }
  }

  if (item.label != NULL)
  {
    if (gv->mode == wuss_GRIDVIEW_LARGE)
      avail = gv->cell.w - 2 * gv->pad_x - 2 * end_w;
    else
      avail = gv->cell.w - 2 * gv->pad_x - gridview_small_glyph_space(gv) -
              gridview_info_space(gv) - 2 * end_w;

    len = (int) strlen(item.label);
    if (len > (int) sizeof(buf))
      len = (int) sizeof(buf);
    wuss__text_measure(font, item.label, len, INT_MAX, NULL, &w);
    if ((int) w > avail)
    {
      bmfont_width_t ellipsis_w, target;

      wuss__text_measure(font, GRIDVIEW_ELLIPSIS,
                        (int) strlen(GRIDVIEW_ELLIPSIS), INT_MAX, NULL,
                        &ellipsis_w);
      target = (bmfont_width_t) MAX(avail - (int) ellipsis_w, 0);
      wuss__text_measure(font, item.label, len, target, &split, &w);
      if (split > (int) sizeof(buf) + 1 - (int) sizeof(GRIDVIEW_ELLIPSIS))
        split = (int) sizeof(buf) + 1 - (int) sizeof(GRIDVIEW_ELLIPSIS);
      memcpy(buf, item.label, (size_t) split);
      memcpy(buf + split, GRIDVIEW_ELLIPSIS, sizeof(GRIDVIEW_ELLIPSIS) - 1);
      len = split + (int) sizeof(GRIDVIEW_ELLIPSIS) - 1;
      wuss__text_measure(font, buf, len, INT_MAX, NULL, &w);
    }
    else
    {
      memcpy(buf, item.label, (size_t) len);
    }

    capsule_w = (int) w + 2 * end_w;
    if (gv->mode == wuss_GRIDVIEW_LARGE)
    {
      label_x = screen_box.x0 + (gv->cell.w - capsule_w) / 2;
      label_y = screen_box.y1 - gv->pad_y - height;
    }
    else
    {
      label_x = screen_box.x0 + gv->pad_x + gridview_small_glyph_space(gv);
      label_y = screen_box.y0 + (gv->cell.h - height) / 2;
    }
    if (wuss_gridview_is_selected(gv, index))
      screen_fill_rounded_rect(&clipped, label_x - 2, label_y - 2,
                               SIZE2D(capsule_w + 4, height + 4), end_w -1,
                               bg);

    pos.x = label_x + end_w;
    pos.y = label_y + ascent;
    wuss__text_draw(font, &clipped, buf, len, fg, bg, &pos, NULL);
  }
}

/* Undoes the selection a MENU click made, unless the menu's action changed
 * it. */
static void gridview_menu_undo(wuss_gridview_t *gv)
{
  if (gv->menu_index >= 0 && bitvec_count(gv->selected) == 1 &&
      bitvec_get(gv->selected, (bitvec_index_t) gv->menu_index))
    gridview_clear_selected(gv);

  gv->menu_index  = -1;
  gv->menu_closed = 0;
}

/* ----------------------------------------------------------------------- */

int wuss_gridview_handle_event(wuss_gridview_t    *gv,
                               const wuss_event_t *event)
{
  int index, i, first, last;

  if (gv->menu_closed && event->kind != wuss_EVENT_MENU_CLOSED)
    gridview_menu_undo(gv);

  if (event->kind == wuss_EVENT_IDLE)
  {
    (void) wuss_rubberband_autoscroll(&gv->band, gv->window, 0);
    return 0;
  }

  switch (event->kind)
  {
  case wuss_EVENT_OPEN:
    gridview_reflow(gv);
    return 0; /* other listeners may care about OPEN too */

  case wuss_EVENT_PALETTE:
  case wuss_EVENT_RELAYOUT:
    wuss_gridview_remeasure(gv);
    return 0;

  case wuss_EVENT_REDRAW:
  {
    wuss_t *wuss;
    box_t   content;
    int     clip_y0, clip_y1;

    wuss    = gv->window->task->wuss;
    content = *event->data.redraw.content;
    wuss__fill_colour(wuss, event->data.redraw.scr, &content,
                      gv->has_bg ? gv->bg
                                 : wuss->palette[wuss->body.window],
                      gridview_origin(event));

    /* only the rows the clip reaches */
    clip_y0 = MAX(event->data.redraw.scr->clip.y0, content.y0);
    clip_y1 = MIN(event->data.redraw.scr->clip.y1, content.y1);
    if (clip_y1 <= clip_y0)
      return 1; /* empty clip: nothing to draw */

    first = (clip_y0 - event->data.redraw.bounds->y0 +
             event->data.redraw.scroll.y - gv->inset) / gv->cell.h;
    last  = (clip_y1 - event->data.redraw.bounds->y0 +
             event->data.redraw.scroll.y - gv->inset) / gv->cell.h;
    first = MAX(first, 0) * gv->columns;
    last  = MIN((last + 1) * gv->columns, gv->count);
    for (i = first; i < last; i++)
      gridview_draw_cell(gv, event, i);
    wuss_rubberband_draw(&gv->band, event, wuss->palette[wuss->button.fg]);
    return 1;
  }

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action == wuss_MOUSE_MOVE)
    {
      if (gv->band.active)
      {
        wuss_rubberband_to(&gv->band, gv->window, event->data.mouse.point);
        return 1;
      }
      gridview_drag_maybe(gv, event->data.mouse.point);
      return 0;
    }

    if (event->data.mouse.action == wuss_MOUSE_UP)
    {
      gv->press_index = -1;
      if (!gv->band.active)
        return 0;
      gridview_band_end(gv);
      return 1;
    }

    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return 0;

    gv->press_index = -1;
    wuss_rubberband_end(&gv->band, gv->window);

    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      /* a MENU click over an unselected item, with nothing selected, selects
       * it until the menu closes; otherwise it leaves the selection alone */
      index = gridview_hit_test(gv, event->data.mouse.point);
      if (gv->selectable && index >= 0 && bitvec_count(gv->selected) == 0)
      {
        (void) bitvec_set(gv->selected, (bitvec_index_t) index);
        gv->menu_index = index;
        gridview_invalidate_cell(gv, index);
      }
      return 0;
    }

    if (!(event->data.mouse.button & WUSS_BUTTON_CLICK))
      return 0;

    index = gridview_hit_test(gv, event->data.mouse.point);

    if (index < 0)
    {
      /* a press on the background starts a rubber band; SELECT clears the
       * selection first, ADJUST adds to it */
      if (!gv->selectable)
        return 0;
      if (event->data.mouse.button & wuss_BUTTON_SELECT)
        gridview_clear_selected(gv);
      wuss_rubberband_start(&gv->band, gv->window, event->data.mouse.point);
      return 1;
    }

    if (gv->drag != NULL)
    {
      gv->press_index = index;
      gv->press_point  = event->data.mouse.point;
      gv->press_button = event->data.mouse.button;
    }

    if (!gv->selectable)
    {
      /* no selection state to change */
    }
    else if (event->data.mouse.button & wuss_BUTTON_ADJUST)
    {
      gv->cursor = index;
      (void) bitvec_toggle(gv->selected, (bitvec_index_t) index);
      gridview_invalidate_cell(gv, index);
    }
    else if ((event->data.mouse.button & wuss_BUTTON_SELECT) &&
             !bitvec_get(gv->selected, (bitvec_index_t) index))
    {
      /* a SELECT on an item already selected changes nothing, so the whole
       * selection can still be dragged */
      gridview_clear_selected(gv);
      (void) bitvec_set(gv->selected, (bitvec_index_t) index);
      gridview_invalidate_cell(gv, index);
      gv->cursor = index;
    }

    if ((event->data.mouse.button & wuss_BUTTON_DOUBLE) &&
        gv->activate != NULL)
    {
      /* before the call: it may rescan or reorder the items */
      if (bitvec_get(gv->selected, (bitvec_index_t) index))
      {
        bitvec_clear(gv->selected, (bitvec_index_t) index);
        gridview_invalidate_cell(gv, index);
      }

      gv->activate(gv, index, event->data.mouse.button, gv->opaque);
    }

    return 1;

  case wuss_EVENT_MENU_CLOSED:
    /* a pick is closed before its wuss_EVENT_MENU_SELECT is delivered, and
     * the task's action needs the selection: undo at the next event */
    if (event->data.menu_closed.picked)
      gv->menu_closed = 1;
    else
      gridview_menu_undo(gv);
    return 0;

  case wuss_EVENT_POINTER_EXIT:
    /* no button state reaches a hover move, so a press the pointer leaves the
     * window during must end here or the next hover would start a drag */
    gv->press_index = -1;
    return 0;

  case wuss_EVENT_KEY:
    if (event->data.key.modifiers & (wuss_KEY_MOD_CTRL | wuss_KEY_MOD_ALT))
      return 0; /* leave chords to shortcuts */
    return gridview_key(gv, event->data.key.code);

  default:
    return 0;
  }
}

/* ----------------------------------------------------------------------- */

int wuss_gridview_is_selected(const wuss_gridview_t *gv, int index)
{
  return bitvec_get(gv->selected, (bitvec_index_t) index);
}

void wuss_gridview_clear_selection(wuss_gridview_t *gv)
{
  if (bitvec_count(gv->selected) == 0)
    return;

  gridview_clear_selected(gv);
}

void wuss_gridview_select_all(wuss_gridview_t *gv)
{
  int i;

  /* bitvec_set_all would also set the spare bits past count */
  for (i = 0; i < gv->count; i++)
  {
    if (bitvec_get(gv->selected, (bitvec_index_t) i))
      continue;

    (void) bitvec_set(gv->selected, (bitvec_index_t) i);
    gridview_invalidate_cell(gv, i);
  }
}

void wuss_gridview_select(wuss_gridview_t *gv, int index)
{
  if (index < 0 || index >= gv->count)
    return;

  gridview_clear_selected(gv);
  (void) bitvec_set(gv->selected, (bitvec_index_t) index);
  gridview_invalidate_cell(gv, index);
  gv->cursor = index;
}

int wuss_gridview_selected_count(const wuss_gridview_t *gv)
{
  return (int) bitvec_count(gv->selected);
}

result_t wuss_gridview_set_count(wuss_gridview_t *gv, int count)
{
  bitvec_t *fresh;
  int       i;

  if (count < 0)
    return result_BAD_ARG;

  fresh = bitvec_create((unsigned int) count);
  if (fresh == NULL)
    return result_OOM;

  for (i = 0; i < count && i < gv->count; i++)
    if (bitvec_get(gv->selected, (bitvec_index_t) i))
      (void) bitvec_set(fresh, (bitvec_index_t) i);

  bitvec_destroy(gv->selected);
  gv->selected = fresh;
  gv->count    = count;

  /* the items behind the count may differ too (a directory rescan, say), so
   * their widest label is measured afresh.
   * ponytail: a text measure per item on every call (~78ms for 100k items in
   * the debug ASan build); add an items-unchanged flag if a large list calls
   * this often */
  if (!gv->fixed_cell)
  {
    gridview_measure_items(gv);
    gv->cell = gridview_cell_size(gv);
  }

  gridview_reflow(gv);
  wuss_window_invalidate_visible(gv->window);
  return result_OK;
}

size2d_t wuss_gridview_get_cell_size(const wuss_gridview_t *gv)
{
  return gv->cell;
}

void wuss_gridview_remeasure(wuss_gridview_t *gv)
{
  wuss_t *wuss;

  wuss = gv->window->task->wuss;

  /* the pool's glyphs may have been converted for a new screen format */
  gridview_free_smalls(gv);

  /* label widths only change with the font, so skip the item walk when the
   * font and its height are as they were when last measured */
  if (wuss->fonts.fonts[0] != gv->measured_font ||
      wuss__fontset_height(&wuss->fonts, 0) != gv->measured_height)
    gridview_measure_items(gv);
  if (!gv->fixed_cell)
    gv->cell = gridview_cell_size(gv);
  gridview_reflow(gv);
  wuss_window_invalidate_visible(gv->window);
}

void wuss_gridview_set_cell_size(wuss_gridview_t *gv, size2d_t cell)
{
  gv->cell       = SIZE2D(MAX(cell.w, 1), MAX(cell.h, 1));
  gv->fixed_cell = 1;

  gridview_reflow(gv);
  wuss_window_invalidate_visible(gv->window);
}

void wuss_gridview_set_mode(wuss_gridview_t *gv, wuss_gridview_mode_t mode)
{
  if (mode == gv->mode)
    return;

  gv->mode = mode;

  /* the info columns are measured only in wuss_GRIDVIEW_FULL */
  gridview_measure_items(gv);
  if (!gv->fixed_cell)
    gv->cell = gridview_cell_size(gv);
  gridview_reflow(gv);
  wuss_window_invalidate_visible(gv->window);
}

wuss_gridview_mode_t wuss_gridview_get_mode(const wuss_gridview_t *gv)
{
  return gv->mode;
}

void wuss_gridview_set_background(wuss_gridview_t *gv, colour_t colour)
{
  gv->bg     = colour;
  gv->has_bg = 1;

  wuss_window_invalidate_visible(gv->window);
}

void wuss_gridview_set_layout(wuss_gridview_t *gv,
                              int              max_columns,
                              int              pow2,
                              int              inset)
{
  gv->max_columns = MAX(max_columns, 0);
  gv->pow2        = pow2 != 0;
  gv->inset       = MAX(inset, 0);

  gridview_reflow(gv);
  wuss_window_invalidate_visible(gv->window);
}

void wuss_gridview_set_padding(wuss_gridview_t *gv,
                               int              pad_x,
                               int              pad_y,
                               int              spacing)
{
  gv->pad_x   = MAX(pad_x, 0);
  gv->pad_y   = MAX(pad_y, 0);
  gv->spacing = MAX(spacing, 0);

  /* items and font are unchanged, so the cached measurements still hold */
  if (!gv->fixed_cell)
    gv->cell = gridview_cell_size(gv);
  gridview_reflow(gv);
  wuss_window_invalidate_visible(gv->window);
}

void wuss_gridview_set_drag(wuss_gridview_t         *gv,
                            wuss_gridview_drag_fn_t *drag)
{
  gv->drag        = drag;
  gv->press_index = -1;
}

void wuss_gridview_set_draw(wuss_gridview_t         *gv,
                            wuss_gridview_draw_fn_t *draw,
                            int                      selectable)
{
  gv->draw       = draw;
  gv->selectable = selectable != 0;

  wuss_window_invalidate_visible(gv->window);
}

int wuss_gridview_get_columns(const wuss_gridview_t *gv)
{
  return gv->columns;
}

box_t wuss_gridview_cell_box(const wuss_gridview_t *gv, int index)
{
  return gridview_cell_box(gv, index);
}

int wuss_gridview_width_for_columns(const wuss_gridview_t *gv, int columns)
{
  return gridview_width_for_columns(gv, MAX(columns, 1));
}

size2d_t wuss_gridview_natural_size(const wuss_gridview_t *gv,
                                    int                    columns,
                                    int                    max_rows)
{
  int rows;

  columns = gridview_column_cap(gv, columns);
  rows    = DIVIDE_ROUNDING_UP(gv->count, columns);
  rows    = MIN(MAX(rows, 1), MAX(max_rows, 1));
  return SIZE2D(MAX(gridview_width_for_columns(gv, columns),
                    gridview_min_size(gv).w),
                rows * gv->cell.h + 2 * gv->inset);
}

int wuss_gridview_hit_test(const wuss_gridview_t *gv, point_t point)
{
  return gridview_hit_test(gv, point);
}

result_t wuss_gridview_window_create(wuss_gridview_t            **out,
                                     wuss_task_t                 *task,
                                     const wuss_place_t          *place,
                                     const char                  *title,
                                     int                          count,
                                     wuss_gridview_item_fn_t     *item_fn,
                                     int                          label_chars,
                                     wuss_gridview_activate_fn_t *activate,
                                     void                        *opaque,
                                     wuss_window_flags_t          omit,
                                     wuss_window_t              **window)
{
  static const wuss_place_t spare = { wuss_PLACE_SPARE, wuss_ALIGN_CENTRE,
                                       NULL, { 0, 0 }, { 0, 0, 0, 0 } };

  result_t       rc;
  wuss_window_t *win;

  if (out == NULL || window == NULL)
    return result_NULL_ARG;

  if (place == NULL)
    place = &spare;

  /* No horizontal scrollbar: the grid reflows to the width instead. */
  rc = wuss_window_create_at(task, place, SIZE2D(320, 240), title,
                             (wuss_WINDOW_DEFAULT & ~(wuss_WINDOW_HSCROLL | omit)) |
                             wuss_WINDOW_FOCUSABLE,
                             wuss_NO_BACKDROP, SIZE2D(320, 240),
                             SIZE2D(0, 0), &win);
  if (rc != result_OK)
    return rc;

  rc = wuss_gridview_create(out, win, count, item_fn, label_chars, activate,
                            opaque);
  if (rc != result_OK)
  {
    wuss_window_close(win);
    return rc;
  }

  *window = win;
  return result_OK;
}

void wuss_gridview_fit_window(wuss_gridview_t    *gv,
                              int                 columns,
                              int                 max_rows,
                              const wuss_place_t *place)
{
  box_t    content;
  int      cols;
  size2d_t size;

  (void) wuss_window_resize(gv->window,
                            wuss_gridview_natural_size(gv, columns, max_rows));

  /* The resize is capped to the screen from the window's top-left, so it may
   * have stopped part way across a column: drop to the columns that fit. */
  wuss_window_get_content_bounds(gv->window, &content);
  cols = (content.x1 - content.x0) / gv->cell.w;
  if (cols < gridview_column_cap(gv, columns))
  {
    size = wuss_gridview_natural_size(gv, MAX(cols, 1), max_rows);
    (void) wuss_window_resize(gv->window, size);
  }

  if (place != NULL)
    (void) wuss_window_place(gv->window, place);
}
