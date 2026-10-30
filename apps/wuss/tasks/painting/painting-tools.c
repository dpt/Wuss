/* wuss/tasks/painting/painting-tools.c -- Painting's tools */

#ifdef WUSS_APP

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "base/utils.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "utils/array.h"
#include "utils/rng.h"

#include "painting.h"
#include "painting-tools.h"

/* the spray's radius in pixels per unit of the size slider */
#define PAINTING_SPRAY_SCALE 3

/* the ants' redraw: tiles a side of the finest grid over the selection, and
 * the most boxes to invalidate per step */
#define PAINTING_ANTS_GRID  16
#define PAINTING_ANTS_BOXES 8

/* rects and ellipses, outlined or filled */
static int painting_is_shape(const painting_task_t *pt)
{
  return pt->tool == painting_TOOL_RECT ||
         pt->tool == painting_TOOL_RECT_FILL ||
         pt->tool == painting_TOOL_ELLIPSE ||
         pt->tool == painting_TOOL_ELLIPSE_FILL;
}

/* line, the shapes, gradient and select are rubber-banded: shown on the
 * window, not drawn into the canvas, until the button is released */
int painting_is_rubber_band(const painting_task_t *pt)
{
  return pt->tool == painting_TOOL_LINE || painting_is_shape(pt) ||
         pt->tool == painting_TOOL_GRADIENT || pt->tool == painting_TOOL_SELECT;
}

/* select and lasso make a selection rather than paint */
int painting_is_selector(const painting_task_t *pt)
{
  return pt->tool == painting_TOOL_SELECT || pt->tool == painting_TOOL_LASSO;
}

/* the paths, outlined or filled */
int painting_is_path(const painting_task_t *pt)
{
  return pt->tool == painting_TOOL_PATH || pt->tool == painting_TOOL_PATH_FILL;
}

/* the lasso and paths collect the points dragged through */
int painting_collects_points(const painting_task_t *pt)
{
  return pt->tool == painting_TOOL_LASSO || painting_is_path(pt);
}

/* the rubber band's ends, a and b, given the keys held: for a shape or
 * gradient Ctrl makes the anchor the centre rather than an end, and Shift
 * squares a shape or turns a gradient to the nearest 22.5 degrees */
void painting_band_ends(const painting_task_t *pt,
                               point_t               *a,
                               point_t               *b)
{
  int    dx, dy, s;
  double step, len, angle;

  *a = pt->anchor;
  *b = pt->last;

  if (!painting_is_shape(pt) && pt->tool != painting_TOOL_GRADIENT)
    return;

  dx = pt->last.x - pt->anchor.x;
  dy = pt->last.y - pt->anchor.y;

  if (pt->mods & wuss_KEY_MOD_SHIFT)
  {
    if (pt->tool == painting_TOOL_GRADIENT)
    {
      step  = atan(1.0) / 2; /* 22.5 degrees */
      len   = sqrt((double) dx * dx + (double) dy * dy);
      angle = floor(atan2(dy, dx) / step + 0.5) * step;
      dx    = (int) floor(len * cos(angle) + 0.5);
      dy    = (int) floor(len * sin(angle) + 0.5);
    }
    else
    {
      s  = MAX(abs(dx), abs(dy));
      dx = dx < 0 ? -s : s;
      dy = dy < 0 ? -s : s;
    }
  }

  if (pt->mods & wuss_KEY_MOD_CTRL)
    *a = POINT(pt->anchor.x - dx, pt->anchor.y - dy);
  *b = POINT(pt->anchor.x + dx, pt->anchor.y + dy);
}

/* the lasso's path grows by one point; false if out of memory, when the
 * point is dropped */
int painting_lasso_add(painting_task_t *pt, point_t p)
{
  if (pt->nlasso > 0 && pt->lasso[pt->nlasso - 1].x == p.x &&
      pt->lasso[pt->nlasso - 1].y == p.y)
    return 1;

  if (array_grow((void **) &pt->lasso, sizeof(*pt->lasso), pt->nlasso,
                 &pt->caplasso, 1, 256))
    return 0;

  pt->lasso[pt->nlasso++] = p;

  return 1;
}

/* the box covering a and b, with room for a line a few pixels wide */
box_t painting_span_box(point_t a, point_t b)
{
  box_t box;

  box.x0 = MIN(a.x, b.x) - 2;
  box.y0 = MIN(a.y, b.y) - 2;
  box.x1 = MAX(a.x, b.x) + 3;
  box.y1 = MAX(a.y, b.y) + 3;

  return box;
}

/* redraw a box of the canvas, which sits at pt->page on the stage */
void painting_invalidate_box(painting_task_t *pt, const box_t *box)
{
  box_t b;

  if (box_is_empty(box))
    return;

  b = (box_t) { box->x0 + pt->page.x, box->y0 + pt->page.y,
                box->x1 + pt->page.x, box->y1 + pt->page.y };
  wuss_window_invalidate(pt->window, &b);
}

/* the colour a stroke in progress paints: the eraser always wipes to
 * transparent, the others use the colour of the button held */
colour_t painting_stroke_colour(const painting_task_t *pt)
{
  if (pt->tool == painting_TOOL_ERASER)
    return colour_rgba(0, 0, 0, 0);
  if (pt->held == wuss_BUTTON_ADJUST)
    return pt->bg;

  return pt->fg;
}

/* paint the segment a to b of a freehand stroke, returning the dirty box */
box_t painting_stroke_segment(painting_task_t *pt,
                                     point_t          a,
                                     point_t          b)
{
  if (pt->tool == painting_TOOL_PAINTBRUSH)
    return paintingdoc_brush(pt->doc, a, b, pt->nozzle, pt->size,
                             painting_stroke_colour(pt));
  if (pt->tool == painting_TOOL_WOBBLE)
    return paintingdoc_wobble(pt->doc, a, b, pt->size, pt->edge, pt->wander,
                              painting_stroke_colour(pt), &pt->wobble);

  return paintingdoc_stroke(pt->doc, a, b, pt->size,
                            painting_stroke_colour(pt));
}

/* spray at p on rate percent of the calls, made per move and idle while the
 * button is held, returning the dirty box */
box_t painting_spray(painting_task_t *pt, point_t p)
{
  pt->spray_tick += pt->rate;
  if (pt->spray_tick < 100)
    return (box_t) BOX_INIT;

  pt->spray_tick -= 100;

  return paintingdoc_spray(pt->doc, p, pt->size * PAINTING_SPRAY_SCALE,
                           pt->density, painting_stroke_colour(pt), &pt->rng);
}

/* the canvas box the rubber band at its current size may cover */
box_t painting_band_box(const painting_task_t *pt)
{
  int     r;
  point_t a, b;
  box_t   box;

  r = pt->size / 2 + 1;
  painting_band_ends(pt, &a, &b);

  box.x0 = MIN(a.x, b.x) - r;
  box.y0 = MIN(a.y, b.y) - r;
  box.x1 = MAX(a.x, b.x) + r + 1;
  box.y1 = MAX(a.y, b.y) + r + 1;

  return box;
}

/* the canvas box the lasso or path's points, drawn as thin lines, cover */
box_t painting_points_box(const painting_task_t *pt)
{
  box_t drag = BOX_INIT;
  box_t box;
  int   i;

  for (i = 0; i < pt->nlasso; i++)
  {
    box     = painting_span_box(pt->lasso[i], pt->lasso[i]);
    drag.x0 = MIN(drag.x0, box.x0);
    drag.y0 = MIN(drag.y0, box.y0);
    drag.x1 = MAX(drag.x1, box.x1);
    drag.y1 = MAX(drag.y1, box.y1);
  }

  return drag;
}

/* Shift adds the new area to the selection, Alt takes it away, otherwise it
 * replaces it */
paintingdoc_selop_t painting_selop(const painting_task_t *pt)
{
  if (pt->mods & wuss_KEY_MOD_SHIFT)
    return paintingdoc_SELOP_ADD;
  if (pt->mods & wuss_KEY_MOD_ALT)
    return paintingdoc_SELOP_SUBTRACT;
  return paintingdoc_SELOP_REPLACE;
}

/* finish a selection drag: the new area replaces, joins or leaves the
 * selection, and all that the ants and the drag's own outline covered is
 * repainted */
void painting_select_commit(painting_task_t *pt, point_t p)
{
  box_t               old, now, drag;
  paintingdoc_selop_t op;
  box_t               box;

  old      = paintingdoc_selection_bounds(pt->doc);
  pt->last = p;
  op       = painting_selop(pt);

  if (pt->tool == painting_TOOL_SELECT)
  {
    drag = painting_band_box(pt);

    if (pt->anchor.x == p.x && pt->anchor.y == p.y)
    {
      /* a plain click drops the selection; one with a modifier is ignored */
      if (op == paintingdoc_SELOP_REPLACE)
        paintingdoc_select_none(pt->doc);
    }
    else
    {
      box.x0 = MIN(pt->anchor.x, p.x);
      box.y0 = MIN(pt->anchor.y, p.y);
      box.x1 = MAX(pt->anchor.x, p.x) + 1;
      box.y1 = MAX(pt->anchor.y, p.y) + 1;
      paintingdoc_select_rect(pt->doc, box, op);
    }
  }
  else
  {
    drag = painting_points_box(pt);
    paintingdoc_select_poly(pt->doc, pt->lasso, pt->nlasso, op);
    pt->nlasso = 0;
  }

  now = paintingdoc_selection_bounds(pt->doc);

  painting_invalidate_box(pt, &old);
  painting_invalidate_box(pt, &now);
  painting_invalidate_box(pt, &drag);
}

/* the marching ants: a one pixel outline of black and white dashes round the
 * selection, drawn over the canvas and shifted along by ants_phase */
void painting_draw_ants(const painting_task_t *pt,
                        screen_t              *scr,
                        int                    ox,
                        int                    oy)
{
  const box_t *spans;
  colour_t     black, white;
  int          n, lo, hi, mid, i, x, y;

  spans = paintingdoc_selection_outline(pt->doc, &n);
  black = colour_rgb(0x00, 0x00, 0x00);
  white = colour_rgb(0xFF, 0xFF, 0xFF);

  /* the spans run down the rows, so skip straight to the first in the clip */
  lo = 0;
  hi = n;
  while (lo < hi)
  {
    mid = (lo + hi) / 2;
    if (oy + spans[mid].y0 < scr->clip.y0)
      lo = mid + 1;
    else
      hi = mid;
  }

  for (i = lo; i < n; i++)
  {
    y = oy + spans[i].y0;
    if (y >= scr->clip.y1)
      break;

    for (x = MAX(ox + spans[i].x0, scr->clip.x0);
         x < MIN(ox + spans[i].x1, scr->clip.x1);
         x++)
      screen_set_pixel(scr, x, y,
                       ((x - ox + y - oy - pt->ants_phase) & 7) < 4 ? black : white);
  }
}

/* redraw where the ants march. the canvas under them needs no repaint, so
 * rather than the selection's whole bounds this redraws the tiles of a grid
 * laid over it that the outline crosses: the tiles in each grid row are
 * joined into runs and a run stretched down over the rows below with the
 * same run. the grid coarsens until that fits in PAINTING_ANTS_BOXES, as
 * wuss tracks only a few dirty boxes */
void painting_invalidate_ants(painting_task_t *pt)
{
  box_t         bounds;
  const box_t  *spans;
  int           n, nboxes, g, tw, th;
  unsigned char hit[PAINTING_ANTS_GRID][PAINTING_ANTS_GRID];
  int           i, c, r, c1, j;
  box_t         run;
  box_t         boxes[PAINTING_ANTS_BOXES + 1];

  bounds = paintingdoc_selection_bounds(pt->doc);
  spans  = paintingdoc_selection_outline(pt->doc, &n);
  nboxes = 0;

  for (g = PAINTING_ANTS_GRID; g > 1; g /= 2)
  {
    tw = DIVIDE_ROUNDING_UP(bounds.x1 - bounds.x0, g);
    th = DIVIDE_ROUNDING_UP(bounds.y1 - bounds.y0, g);

    memset(hit, 0, sizeof(hit));
    for (i = 0; i < n; i++)
      for (c = (spans[i].x0 - bounds.x0) / tw;
           c <= (spans[i].x1 - 1 - bounds.x0) / tw;
           c++)
        hit[(spans[i].y0 - bounds.y0) / th][c] = 1;

    nboxes = 0;
    for (r = 0; r < g && nboxes <= PAINTING_ANTS_BOXES; r++)
      for (c = 0; c < g && nboxes <= PAINTING_ANTS_BOXES; c = c1)
      {
        for (c1 = c; c1 < g && hit[r][c1] == hit[r][c]; c1++)
          ;
        if (!hit[r][c])
          continue;

        run.x0 = bounds.x0 + c * tw;
        run.y0 = bounds.y0 + r * th;
        run.x1 = MIN(bounds.x0 + c1 * tw, bounds.x1);
        run.y1 = MIN(bounds.y0 + (r + 1) * th, bounds.y1);

        for (j = 0; j < nboxes; j++)
          if (boxes[j].x0 == run.x0 && boxes[j].x1 == run.x1 &&
              boxes[j].y1 == run.y0)
            break;

        if (j < nboxes)
          boxes[j].y1 = run.y1;
        else
          boxes[nboxes++] = run; /* one past the limit means too many */
      }

    if (nboxes <= PAINTING_ANTS_BOXES)
      break;
  }

  if (g == 1)
  {
    painting_invalidate_box(pt, &bounds);
    return;
  }

  for (j = 0; j < nboxes; j++)
    painting_invalidate_box(pt, &boxes[j]);
}

/* draw the finished rubber band into the canvas */
box_t painting_band_commit(painting_task_t *pt)
{
  colour_t colour;
  point_t  a, b;

  colour = painting_stroke_colour(pt);
  painting_band_ends(pt, &a, &b);

  /* Select runs from fg to bg, Adjust the other way */
  if (pt->tool == painting_TOOL_GRADIENT)
    return pt->held == wuss_BUTTON_ADJUST ?
           paintingdoc_gradient(pt->doc, a, b, pt->bg, pt->fg) :
           paintingdoc_gradient(pt->doc, a, b, pt->fg, pt->bg);

  if (pt->tool == painting_TOOL_RECT || pt->tool == painting_TOOL_RECT_FILL)
    return paintingdoc_rect(pt->doc, a, b, pt->size,
                         pt->tool == painting_TOOL_RECT_FILL, colour);

  if (pt->tool == painting_TOOL_ELLIPSE ||
      pt->tool == painting_TOOL_ELLIPSE_FILL)
    return paintingdoc_ellipse(pt->doc, a, b, pt->size,
                               pt->tool == painting_TOOL_ELLIPSE_FILL, colour);

  if (paintingdoc_checkpoint(pt->doc) != result_OK)
    return (box_t) BOX_INIT; /* no undo step, so no line */

  return paintingdoc_stroke(pt->doc, a, b, pt->size, colour);
}

/* place the lifted selection under p: a Select drag moves it, leaving bg
 * behind, and an Adjust drag copies it */
box_t painting_place(painting_task_t *pt, point_t p)
{
  return paintingdoc_place(pt->doc, p.x - pt->anchor.x, p.y - pt->anchor.y,
                           pt->held == wuss_BUTTON_ADJUST ? NULL : &pt->bg);
}

#endif /* WUSS_APP */
