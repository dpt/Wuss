/* wuss/gadget/swatches.c -- a grid of colour swatches on a window */

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "framebuf/pattern.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/size.h"

#include "wuss/gadget/swatches.h"

/* ----------------------------------------------------------------------- */

/* Columns and cell size for the grid. False if there is nothing to show. */
static int swatches_layout(const wuss_swatches_t *sw,
                           int                   *cols,
                           size2d_t              *cell)
{
  int rows;

  if (sw->ncolours <= 0)
    return 0;

  *cols = sw->columns;
  if (*cols <= 0)
  {
    *cols = 1;
    while (*cols * *cols < sw->ncolours)
      (*cols)++;
  }
  rows = (sw->ncolours + *cols - 1) / *cols;

  cell->w = (sw->bbox.x1 - sw->bbox.x0 - (*cols - 1) * sw->gap) / *cols;
  cell->h = (sw->bbox.y1 - sw->bbox.y0 - (rows - 1) * sw->gap) / rows;

  return cell->w > 0 && cell->h > 0;
}

/* ----------------------------------------------------------------------- */

void wuss_swatches_draw(const wuss_swatches_t *swatches,
                        const wuss_event_t    *event)
{
  screen_t *scr;
  int       cols;
  size2d_t  cell;
  int       ox, oy;
  colour_t  black;
  int       nphys;
  int       i;
  int       x, y;
  pattern_t pat;
  box_t     box;

  scr = event->data.redraw.scr;
  if (!swatches_layout(swatches, &cols, &cell))
    return;

  ox    = event->data.redraw.bounds->x0 - event->data.redraw.scroll.x +
          swatches->bbox.x0;
  oy    = event->data.redraw.bounds->y0 - event->data.redraw.scroll.y +
          swatches->bbox.y0;
  black = colour_rgb(0x00, 0x00, 0x00);
  nphys = (scr->palette != NULL) ?
          pixelfmt_paletted_nentries(scr->format) : 0;

  for (i = 0; i < swatches->ncolours; i++)
  {
    x = ox + (i % cols) * (cell.w + swatches->gap);
    y = oy + (i / cols) * (cell.h + swatches->gap);

    if (nphys == 0)
    {
      screen_fill_rect(scr, x, y, cell, swatches->colours[i]);
    }
    else
    {
      /* on a paletted screen show a colour the palette lacks as a Bayer
       * mix of two entries, phased to the cell so it holds still */
      pat        = pattern_from_colour(scr->palette, nphys,
                                       swatches->colours[i]);
      pat.origin = POINT(x, y);
      box        = (box_t) BOX_POS_SIZE(x, y, cell.w, cell.h);
      screen_fill_pattern(scr, &box, &pat);
    }
    if (swatches->outline)
      screen_draw_rect(scr, x, y, cell, black);
  }
}

int wuss_swatches_hit_test(const wuss_swatches_t *swatches, point_t point)
{
  int      cols;
  size2d_t cell;
  int      x, y;
  int      col, row;
  int      i;

  if (!swatches_layout(swatches, &cols, &cell))
    return -1;

  x = point.x - swatches->bbox.x0;
  y = point.y - swatches->bbox.y0;
  if (x < 0 || y < 0)
    return -1;

  col = x / (cell.w + swatches->gap);
  row = y / (cell.h + swatches->gap);
  if (col >= cols ||
      x % (cell.w + swatches->gap) >= cell.w ||
      y % (cell.h + swatches->gap) >= cell.h)
    return -1; /* off the right, or in a gap */

  i = row * cols + col;

  return i < swatches->ncolours ? i : -1;
}
