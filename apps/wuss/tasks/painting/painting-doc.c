/* wuss/tasks/painting/painting-doc.c -- paint canvas, drawing and undo */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/composite.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/region.h"

#include "painting-doc.h"

/* ----------------------------------------------------------------------- */

/* an undo step: RLE-compressed copies of one layer, or of them all */
typedef struct step
{
  paintingdoc_layer_t *layers;
  int                  n;     /* layers held */
  int                  layer; /* the one layer held, or -1 for every layer */
  int                  cur;   /* the current layer when taken */
}
step_t;

struct paintingdoc
{
  /* the layers, bottom first, and the current one */
  paintingdoc_layer_t layers[PAINTINGDOC_LAYERS_MAX];
  int                 nlayers;
  int                 cur;
  /* the id the next new layer gets */
  int                 nextid;
  /* the current layer's bitmap, sharing its pixels, and a screen on it. every
   * drawing operation works on these */
  bitmap_t bm;
  screen_t scr;
  /* the visible layers composited, and the box of it out of date */
  bitmap_t flat;
  box_t    stale;
  /* the steps to undo, oldest first, and those undone, which a new
   * checkpoint drops */
  step_t   undo[PAINTINGDOC_UNDO_MAX];
  int      nundo;
  step_t   redo[PAINTINGDOC_UNDO_MAX];
  int      nredo;
  /* the selection, or NULL for none, and its boundary pixels for the ants.
   * pre is a raw copy of the canvas as of the last checkpoint, kept while
   * there is a selection so drawing outside it can be put back */
  region_t *sel;
  box_t    *outline;
  int       noutline;
  uint8_t  *pre;
  /* the selection as paintingdoc_lift found it, or NULL if not lifted. its
   * pixels are in pre */
  region_t *lifted;
  /* pasted pixels, or base NULL if none: while set they float, lifted
   * from here rather than pre, with their top left at origin as lifted. d
   * is how far the last place moved them */
  bitmap_t  floating;
  point_t   origin;
  point_t   d;
  /* changed since the last load or paintingdoc_set_saved */
  int       edited;
  /* paintingdoc_hold: 0 not held, 1 held, 2 held with its checkpoint taken */
  int       hold;
};

/* ----------------------------------------------------------------------- */

/* a bgra8888 bitmap of the given size and colour, with its own pixel store */
static result_t canvas_make(size2d_t size, colour_t colour, bitmap_t *bm)
{
  result_t rc;
  void    *pixels;

  pixels = malloc((size_t) size.w * (size_t) size.h * 4);
  if (pixels == NULL)
    return result_OOM;

  rc = bitmap_init(bm, size, pixelfmt_bgra8888, size.w * 4, NULL, pixels);
  if (rc != result_OK)
  {
    free(pixels);
    return rc;
  }

  bitmap_clear(bm, colour);

  return result_OK;
}

/* point bm and scr at the current layer, after it or its pixels change. pre
 * follows, so drawing clipped to the selection puts back this layer's pixels
 * outside it. callers end any lift first: its pixels live in pre */
static void layer_sync(paintingdoc_t *doc)
{
  doc->bm = doc->layers[doc->cur].bm;
  screen_for_bitmap(&doc->scr, &doc->bm);

  if (doc->pre != NULL && doc->lifted == NULL)
    memcpy(doc->pre, doc->bm.base,
           (size_t) doc->bm.rowbytes * (size_t) doc->bm.size.h);
}

/* the canvas box */
static box_t canvas_box(const paintingdoc_t *doc)
{
  box_t box = BOX_POS_SIZE(0, 0, doc->bm.size.w, doc->bm.size.h);

  return box;
}

/* the canvas's bytes, per layer */
static size_t canvas_bytes(const paintingdoc_t *doc)
{
  return (size_t) doc->bm.rowbytes * (size_t) doc->bm.size.h;
}

/* row y of a bgra8888 bitmap */
static uint32_t *row_of(const bitmap_t *bm, int y)
{
  return (uint32_t *) ((uint8_t *) bm->base + (size_t) y * bm->rowbytes);
}

/* note that box of the canvas changed, to recomposite it */
static void touch(paintingdoc_t *doc, box_t box)
{
  doc->edited = 1;
  box_union(&doc->stale, &box, &doc->stale);
}

result_t paintingdoc_create(size2d_t size, paintingdoc_t **doc)
{
  return paintingdoc_create_filled(size, colour_rgb(255, 255, 255), doc);
}

result_t paintingdoc_create_filled(size2d_t        size,
                                   colour_t        colour,
                                   paintingdoc_t **doc)
{
  result_t       rc;
  paintingdoc_t *d;

  if (size.w <= 0 || size.h <= 0)
    return result_BAD_ARG;

  d = calloc(1, sizeof(*d));
  if (d == NULL)
    return result_OOM;

  rc = canvas_make(size, colour, &d->layers[0].bm);
  if (rc != result_OK)
  {
    free(d);
    return rc;
  }

  d->layers[0].id      = d->nextid++;
  d->layers[0].visible = 1;
  d->layers[0].opacity = 255;
  d->layers[0].locked  = 0;
  d->nlayers           = 1;
  d->stale             = (box_t) BOX_INIT;
  layer_sync(d);

  *doc = d;
  return result_OK;
}

/* free n layers' pixels */
static void layers_free(paintingdoc_layer_t *layers, int n)
{
  int i;

  for (i = 0; i < n; i++)
    free(layers[i].bm.base);
}

/* drop every step of a stack */
static void steps_clear(step_t *steps, int *n)
{
  int i;

  for (i = 0; i < *n; i++)
  {
    layers_free(steps[i].layers, steps[i].n);
    free(steps[i].layers);
  }

  *n = 0;
}

/* drop every undo and redo step */
static void undo_clear(paintingdoc_t *doc)
{
  steps_clear(doc->undo, &doc->nundo);
  steps_clear(doc->redo, &doc->nredo);
}

void paintingdoc_destroy(paintingdoc_t *doc)
{
  if (doc == NULL)
    return;

  undo_clear(doc);
  paintingdoc_select_none(doc);
  layers_free(doc->layers, doc->nlayers);
  free(doc->flat.base);
  free(doc);
}

/* free the pixels and palette of a bitmap that bitmap_load made */
static void bitmap_discard(bitmap_t *bm)
{
  free(bm->base);
  free(bm->palette);
}

result_t paintingdoc_load(paintingdoc_t *doc, const char *path)
{
  result_t rc;
  bitmap_t src;
  bitmap_t next;
  screen_t scr;

  rc = bitmap_load(&src, path);
  if (rc != result_OK)
    return rc;

  /* blended over a fully transparent canvas, each pixel lands with its own
   * colour and alpha, a fully transparent one as 0 */
  rc = canvas_make(src.size, colour_rgba(0, 0, 0, 0), &next);
  if (rc == result_OK)
  {
    screen_for_bitmap(&scr, &next);
    rc = screen_copy_bitmap(&scr, 0, 0, &src);
    if (rc != result_OK)
      free(next.base);
  }

  bitmap_discard(&src);

  if (rc != result_OK)
    return rc;

  undo_clear(doc);
  paintingdoc_select_none(doc);
  layers_free(doc->layers, doc->nlayers);
  doc->layers[0].bm      = next;
  doc->layers[0].id      = doc->nextid++;
  doc->layers[0].visible = 1;
  doc->layers[0].opacity = 255;
  doc->layers[0].locked  = 0;
  doc->nlayers           = 1;
  doc->cur               = 0;
  layer_sync(doc);
  touch(doc, canvas_box(doc));
  doc->edited = 0;

  return result_OK;
}

int paintingdoc_is_edited(const paintingdoc_t *doc)
{
  return doc->edited;
}

void paintingdoc_set_saved(paintingdoc_t *doc)
{
  doc->edited = 0;
}

const bitmap_t *paintingdoc_bitmap(paintingdoc_t *doc)
{
  const paintingdoc_layer_t *l;
  box_t                      b;
  box_t                      canvas;
  void                      *pixels;
  int                        i;
  int                        x, y;
  uint32_t                  *out;
  const uint32_t            *in;

  /* one opaque layer is the image as it stands */
  l = &doc->layers[0];
  if (doc->nlayers == 1 && l->visible && l->opacity == 255)
    return &doc->bm;

  canvas = canvas_box(doc);
  if (doc->flat.base == NULL || doc->flat.size.w != doc->bm.size.w ||
      doc->flat.size.h != doc->bm.size.h)
  {
    pixels = malloc(canvas_bytes(doc));
    if (pixels == NULL)
    {
      logf_warning("painting: no memory to composite the layers");
      return &doc->bm;
    }
    free(doc->flat.base);
    doc->flat      = doc->bm;
    doc->flat.base = pixels;
    doc->stale     = canvas;
  }

  if (box_intersection(&doc->stale, &canvas, &b))
    return &doc->flat;
  doc->stale = (box_t) BOX_INIT;

  for (y = b.y0; y < b.y1; y++)
    memset(row_of(&doc->flat, y) + b.x0, 0, (size_t) (b.x1 - b.x0) * 4);

  for (i = 0; i < doc->nlayers; i++)
  {
    l = &doc->layers[i];
    if (!l->visible || l->opacity == 0)
      continue;

    for (y = b.y0; y < b.y1; y++)
    {
      out = row_of(&doc->flat, y);
      in  = row_of(&l->bm, y);
      for (x = b.x0; x < b.x1; x++)
        out[x] = composite_over_pixel(out[x], in[x],
                                      (unsigned int) l->opacity);
    }
  }

  return &doc->flat;
}

/* ----------------------------------------------------------------------- */

/* box covering a and b, grown by margin, clipped to the canvas */
static box_t clipped_box(const paintingdoc_t *doc,
                         point_t              a,
                         point_t              b,
                         int                  margin)
{
  box_t box;
  box_t canvas;
  box_t out;

  box.x0 = MIN(a.x, b.x) - margin;
  box.y0 = MIN(a.y, b.y) - margin;
  box.x1 = MAX(a.x, b.x) + margin + 1;
  box.y1 = MAX(a.y, b.y) + margin + 1;

  canvas = canvas_box(doc);
  if (box_intersection(&box, &canvas, &out))
    out = (box_t) BOX_INIT;

  return out;
}

/* ----------------------------------------------------------------------- */

/* end a lift, floating pixels included, leaving them where last placed */
static void lift_end(paintingdoc_t *doc)
{
  region_destroy(doc->lifted);
  free(doc->floating.base);
  doc->lifted        = NULL;
  doc->floating.base = NULL;
}

void paintingdoc_select_none(paintingdoc_t *doc)
{
  lift_end(doc);
  region_destroy(doc->sel);
  free(doc->outline);
  free(doc->pre);
  doc->sel      = NULL;
  doc->outline  = NULL;
  doc->noutline = 0;
  doc->pre      = NULL;
}

/* make a freshly built region (NULL for none) the selection, keeping pre */
static result_t sel_replace(paintingdoc_t *doc, region_t *sel)
{
  result_t rc;

  region_destroy(doc->sel);
  free(doc->outline);
  doc->sel      = NULL;
  doc->outline  = NULL;
  doc->noutline = 0;

  if (sel == NULL)
    return result_OK;

  rc = region_outline(sel, &doc->outline, &doc->noutline);
  if (rc != result_OK)
  {
    region_destroy(sel);
    return rc;
  }

  doc->sel = sel;
  return result_OK;
}

/* take a freshly built region (NULL for none) as, into or out of the
 * selection, as op says */
static result_t sel_adopt(paintingdoc_t      *doc,
                          region_t           *sel,
                          paintingdoc_selop_t op)
{
  result_t  rc;
  region_t *combined;

  if (op != paintingdoc_SELOP_REPLACE)
  {
    if (op == paintingdoc_SELOP_ADD)
      rc = region_union(doc->sel, sel, &combined);
    else
      rc = region_subtract(doc->sel, sel, &combined);
    region_destroy(sel);
    if (rc != result_OK)
      return rc;

    sel = combined;
  }

  paintingdoc_select_none(doc);
  return sel_replace(doc, sel);
}

result_t paintingdoc_select_rect(paintingdoc_t      *doc,
                                 box_t               box,
                                 paintingdoc_selop_t op)
{
  result_t  rc;
  box_t     canvas, r;
  region_t *sel;

  canvas = canvas_box(doc);
  if (box_intersection(&box, &canvas, &r))
    r = (box_t) BOX_INIT;

  rc = region_from_box(&r, &sel);
  if (rc != result_OK)
    return rc;

  return sel_adopt(doc, sel, op);
}

result_t paintingdoc_select_all(paintingdoc_t *doc)
{
  return paintingdoc_select_rect(doc, canvas_box(doc),
                                 paintingdoc_SELOP_REPLACE);
}

result_t paintingdoc_select_invert(paintingdoc_t *doc)
{
  result_t  rc;
  box_t     canvas;
  region_t *sel;

  canvas = canvas_box(doc);
  rc     = region_invert(doc->sel, &canvas, &sel);
  if (rc != result_OK)
    return rc;

  return sel_adopt(doc, sel, paintingdoc_SELOP_REPLACE);
}

result_t paintingdoc_select_poly(paintingdoc_t      *doc,
                                 const point_t      *pts,
                                 int                 n,
                                 paintingdoc_selop_t op)
{
  result_t  rc;
  box_t     canvas;
  region_t *sel;

  canvas = canvas_box(doc);
  rc     = region_from_polygon(pts, n, &canvas, &sel);
  if (rc != result_OK)
    return rc;

  return sel_adopt(doc, sel, op);
}

int paintingdoc_has_selection(const paintingdoc_t *doc)
{
  return doc->sel != NULL;
}

box_t paintingdoc_selection_bounds(const paintingdoc_t *doc)
{
  return region_bounds(doc->sel);
}

const box_t *paintingdoc_selection_outline(const paintingdoc_t *doc, int *n)
{
  *n = doc->noutline;
  return doc->outline;
}

/* put back, from the last checkpoint, the pixels of dirty that lie outside the
 * selection. every drawing operation ends here, so it also marks doc edited */
static void sel_clip(paintingdoc_t *doc, box_t dirty)
{
  bitmap_t pre;

  touch(doc, dirty);

  if (doc->sel == NULL || doc->pre == NULL)
    return;

  pre      = doc->bm;
  pre.base = doc->pre;
  (void) bitmap_restore_outside_region(&doc->bm, &pre, doc->sel, &dirty);
}

/* ----------------------------------------------------------------------- */

/* an RLE-compressed copy of a layer, with its own pixel store */
static result_t layer_snap(const paintingdoc_layer_t *l,
                           paintingdoc_layer_t       *snap)
{
  result_t rc;
  size_t   bytes;
  void    *pixels;

  bytes  = (size_t) l->bm.rowbytes * (size_t) l->bm.size.h;
  pixels = malloc(bytes);
  if (pixels == NULL)
    return result_OOM;

  memcpy(pixels, l->bm.base, bytes);

  *snap = *l;
  rc    = bitmap_init(&snap->bm, l->bm.size, pixelfmt_bgra8888,
                      l->bm.rowbytes, NULL, pixels);
  if (rc == result_OK)
    rc = bitmap_compress(&snap->bm);
  if (rc != result_OK)
    free(pixels);

  return rc;
}

/* push a copy of layer `layer`, or of every layer if -1, onto a stack of
 * steps, dropping its oldest step if full */
static result_t steps_push(paintingdoc_t *doc,
                           step_t        *steps,
                           int           *n,
                           int            layer)
{
  result_t rc;
  step_t   step;
  int      first;
  int      i;

  first      = MAX(layer, 0);
  step.n     = layer < 0 ? doc->nlayers : 1;
  step.layer = layer;
  step.cur   = doc->cur;
  step.layers = malloc((size_t) step.n * sizeof(*step.layers));
  if (step.layers == NULL)
    return result_OOM;

  for (i = 0; i < step.n; i++)
  {
    rc = layer_snap(&doc->layers[first + i], &step.layers[i]);
    if (rc != result_OK)
    {
      layers_free(step.layers, i);
      free(step.layers);
      return rc;
    }
  }

  if (*n == PAINTINGDOC_UNDO_MAX)
  {
    layers_free(steps[0].layers, steps[0].n);
    free(steps[0].layers);
    memmove(&steps[0], &steps[1],
            (PAINTINGDOC_UNDO_MAX - 1) * sizeof(steps[0]));
    (*n)--;
  }

  steps[(*n)++] = step;
  return result_OK;
}

/* put the layers back to the top step of a stack, popping it, at the step's
 * size. returns the box that changed, or an empty box if it could not be
 * decompressed */
static box_t steps_pop(paintingdoc_t *doc, step_t *steps, int *n)
{
  step_t              *step;
  int                  i;
  int                  j;
  paintingdoc_layer_t *was;
  paintingdoc_layer_t *now;

  step = &steps[*n - 1];
  for (i = 0; i < step->n; i++)
    if (bitmap_decompress(&step->layers[i].bm) != result_OK)
      return (box_t) BOX_INIT;

  /* the selection and pre fit only a canvas of the same size */
  if (step->layers[0].bm.size.w != doc->bm.size.w ||
      step->layers[0].bm.size.h != doc->bm.size.h)
    paintingdoc_select_none(doc);

  /* a layer gets back its pixels alone: its look is not undone. a layer
   * still here keeps its look; one the step brings back has its old look */
  for (i = 0; i < step->n; i++)
  {
    was = &step->layers[i];
    for (j = 0; j < doc->nlayers; j++)
    {
      now = &doc->layers[j];
      if (now->id != was->id)
        continue;
      was->visible = now->visible;
      was->opacity = now->opacity;
      was->locked  = now->locked;
    }
  }

  if (step->layer >= 0)
  {
    free(doc->layers[step->layer].bm.base);
    doc->layers[step->layer].bm = step->layers[0].bm;
    doc->cur                    = step->layer;
  }
  else
  {
    layers_free(doc->layers, doc->nlayers);
    memcpy(doc->layers, step->layers, (size_t) step->n * sizeof(*step->layers));
    doc->nlayers = step->n;
    doc->cur     = step->cur;
  }
  free(step->layers);
  (*n)--;

  layer_sync(doc);
  touch(doc, canvas_box(doc));
  return canvas_box(doc);
}

/* save layer `layer`, or every layer if -1, as an undo step */
static result_t checkpoint(paintingdoc_t *doc, int layer)
{
  result_t rc;

  /* pre is about to move on, so a lift or float must end on it */
  lift_end(doc);

  if (doc->sel != NULL)
  {
    if (doc->pre == NULL)
      doc->pre = malloc(canvas_bytes(doc));
    if (doc->pre != NULL)
      memcpy(doc->pre, doc->bm.base, canvas_bytes(doc));
    else
      logf_warning("painting: no memory to clip drawing to the selection");
  }

  rc = steps_push(doc, doc->undo, &doc->nundo, layer);
  if (rc == result_OK)
    steps_clear(doc->redo, &doc->nredo); /* a new edit forks the history */

  return rc;
}

result_t paintingdoc_checkpoint(paintingdoc_t *doc)
{
  return checkpoint(doc, doc->cur);
}

/* as checkpoint_or_warn, for an operation on every layer */
static void checkpoint_all(paintingdoc_t *doc)
{
  if (checkpoint(doc, -1) != result_OK)
    logf_warning("painting: no memory for an undo step");
}

/* a drawing operation that takes its own checkpoint, but still draws if it
 * cannot: no undo for that step beats losing the user's edit */
static void checkpoint_or_warn(paintingdoc_t *doc)
{
  if (doc->hold == 2)
    return;
  if (doc->hold == 1)
    doc->hold = 2;

  if (paintingdoc_checkpoint(doc) != result_OK)
    logf_warning("painting: no memory for an undo step");
}

void paintingdoc_hold(paintingdoc_t *doc, int hold)
{
  doc->hold = hold ? MAX(doc->hold, 1) : 0;
}

int paintingdoc_can_undo(const paintingdoc_t *doc)
{
  return doc->nundo > 0;
}

box_t paintingdoc_undo(paintingdoc_t *doc)
{
  if (doc->nundo == 0)
    return (box_t) BOX_INIT;

  lift_end(doc);

  /* ponytail: no memory to keep the redo step undoes all the same */
  if (steps_push(doc, doc->redo, &doc->nredo,
                 doc->undo[doc->nundo - 1].layer) != result_OK)
    logf_warning("painting: no memory for a redo step");

  return steps_pop(doc, doc->undo, &doc->nundo);
}

int paintingdoc_can_redo(const paintingdoc_t *doc)
{
  return doc->nredo > 0;
}

box_t paintingdoc_redo(paintingdoc_t *doc)
{
  if (doc->nredo == 0)
    return (box_t) BOX_INIT;

  lift_end(doc);

  if (steps_push(doc, doc->undo, &doc->nundo,
                 doc->redo[doc->nredo - 1].layer) != result_OK)
    logf_warning("painting: no memory for an undo step");

  return steps_pop(doc, doc->redo, &doc->nredo);
}

/* ----------------------------------------------------------------------- */

void paintingdoc_draw_stroke(screen_t *scr,
                             point_t   a,
                             point_t   b,
                             int       width,
                             colour_t  colour)
{
  width = MAX(width, 1);

  if (width == 1)
  {
    screen_draw_line(scr, a.x, a.y, b.x, b.y, colour);
    return;
  }

  /* a zero-length thick line is a square, not a dab */
  if (a.x != b.x || a.y != b.y)
    screen_draw_thick_line(scr, a.x, a.y, b.x, b.y, width, colour);
  screen_fill_circle(scr, a.x, a.y, width / 2, colour);
  screen_fill_circle(scr, b.x, b.y, width / 2, colour);
}

box_t paintingdoc_stroke(paintingdoc_t *doc,
                         point_t        a,
                         point_t        b,
                         int            width,
                         colour_t       colour)
{
  box_t dirty;

  width = MAX(width, 1);

  paintingdoc_draw_stroke(&doc->scr, a, b, width, colour);

  dirty = clipped_box(doc, a, b, width / 2);
  sel_clip(doc, dirty);

  return dirty;
}

/* ----------------------------------------------------------------------- */

/* whether the nozzle of radius r covers the pixel offset (dx, dy) from its
 * centre; the thin shapes are r / 4 thick (one pixel at size 1) */
static int nozzle_hits(paintingdoc_nozzle_t nozzle, int r, int dx, int dy)
{
  int t;

  t = r / 4;

  switch (nozzle)
  {
  case paintingdoc_NOZZLE_SQUARE:
    return 1;

  case paintingdoc_NOZZLE_HBAR:
    return abs(dy) <= t;

  case paintingdoc_NOZZLE_VBAR:
    return abs(dx) <= t;

  case paintingdoc_NOZZLE_SLASH:
    return abs(dx + dy) <= t;

  case paintingdoc_NOZZLE_BACKSLASH:
    return abs(dx - dy) <= t;

  case paintingdoc_NOZZLE_BRISTLES:
    return dx * dx + dy * dy <= r * r && ((dx + dy) & 1) == 0;

  default:
  case paintingdoc_NOZZLE_ROUND:
    return dx * dx + dy * dy <= r * r;
  }
}

box_t paintingdoc_brush(paintingdoc_t       *doc,
                        point_t              a,
                        point_t              b,
                        paintingdoc_nozzle_t nozzle,
                        int                  size,
                        colour_t             colour,
                        paintingdoc_pot_t   *pot)
{
  box_t    dirty;
  int      r, n;
  uint32_t per_left;
  int      i;
  uint32_t cover;
  int      cx, cy, dx, dy, x, y;

  r = MAX(size, 1) / 2;
  n = MAX(abs(b.x - a.x), abs(b.y - a.y));

  /* 2^24 / load, so a stamp's cover, left * 2^17 / load, needs no divide;
   * 0 if the paint never runs out */
  per_left = (pot != NULL && pot->load > 0) ? (1u << 24) / (uint32_t) pot->load
                                            : 0;

  for (i = 0; i <= n; i++)
  {
    /* the share of the nozzle's pixels this stamp paints, out of 65536.
     * the first stamp of a later segment repeats the last one's end, so is
     * free */
    cover = 65536;
    if (per_left != 0)
    {
      if (pot->left <= 0)
        break;
      if (pot->left * 2 < pot->load)
        cover = ((uint32_t) pot->left * per_left) >> 7;
      if (i > 0 || n == 0)
        pot->left--;
    }

    cx = n ? a.x + (b.x - a.x) * i / n : a.x;
    cy = n ? a.y + (b.y - a.y) * i / n : a.y;

    for (dy = -r; dy <= r; dy++)
    {
      y = cy + dy;
      if (y < 0 || y >= doc->bm.size.h)
        continue;

      for (dx = -r; dx <= r; dx++)
      {
        x = cx + dx;
        if (x < 0 || x >= doc->bm.size.w)
          continue;

        if (!nozzle_hits(nozzle, r, dx, dy))
          continue;
        if (cover < 65536 &&
            (rng_hash32(pot->seed, y * doc->bm.size.w + x) & 0xFFFFu) >= cover)
          continue;

        screen_set_pixel(&doc->scr, x, y, colour);
      }
    }
  }

  dirty = clipped_box(doc, a, b, r);
  sel_clip(doc, dirty);

  return dirty;
}

/* ----------------------------------------------------------------------- */

/* stamp a disc at every pixel step: its radius wanders, moving both edges
 * apart and together, and its centre wanders across the stroke, moving them
 * together, so each edge wobbles on its own. the radius swells slowly and
 * jitters quickly: the slow swell alone looks like a string of beads */
box_t paintingdoc_wobble(paintingdoc_t        *doc,
                         point_t               a,
                         point_t               b,
                         int                   width,
                         int                   edge,
                         int                   wander,
                         colour_t              colour,
                         paintingdoc_wobble_t *wobble)
{
  box_t dirty;
  int   r, as, aj, ao, ps, pj, dx, dy, n, i, s, rr, o, cx, cy;

  r  = MAX(width, 1) / 2;
  as = r * edge / 400;       /* how far the slow swell moves the radius */
  aj = (r + 4) * edge / 500; /* how far the fast jitter moves it */
  ao = r * wander / 400;     /* how far the centre wanders */
  ps = MAX(3 * r, 8);   /* steps between the swell's knots */
  pj = MAX(r / 2, 2);   /* steps between the jitter's knots */
  dx = b.x - a.x;
  dy = b.y - a.y;
  n  = MAX(abs(dx), abs(dy));

  for (i = 0; i <= n; i++)
  {
    s  = wobble->step + i;
    rr = rng_noise1d(wobble->seed, s, ps) * as +
         rng_noise1d(wobble->seed ^ 0x27D4EB2Fu, s, pj) * aj;
    rr = r + (rr >= 0 ? rr + 128 : rr - 128) / 256; /* rounded: a thin
                                                      * line's one-pixel
                                                      * jitter survives */
    o  = rng_noise1d(wobble->seed ^ 0x5BD1E995u, s, ps) * ao;
    o  = (o >= 0 ? o + 128 : o - 128) / 256;
    cx = a.x;
    cy = a.y;
    if (n)
    {
      /* (-dy, dx) / n is across the stroke, near enough unit length */
      cx += (dx * i - dy * o) / n;
      cy += (dy * i + dx * o) / n;
    }
    screen_fill_circle(&doc->scr, cx, cy, MAX(rr, 0), colour);
  }
  wobble->step += n;

  dirty = clipped_box(doc, a, b, r + as + aj + ao + 1);
  sel_clip(doc, dirty);

  return dirty;
}

/* ----------------------------------------------------------------------- */

box_t paintingdoc_spray(paintingdoc_t *doc,
                        point_t        p,
                        int            radius,
                        int            density,
                        colour_t       colour,
                        rng_t         *rng)
{
  box_t dirty;
  int   n, x, y, w;

  radius = MAX(radius, 1);
  w      = 2 * radius + 1;

  /* aim at density percent of the square round the disc, at least a few
   * dots: those landing outside the disc are dropped, so about that share of
   * the disc's own pixels are hit */
  n = MAX(w * w * density / 100, 3);

  /* scale each random word into [0, w) by the top of a multiply rather than
   * by a remainder, which would divide twice per dot */
  while (n-- > 0)
  {
    x = (int) (((uint64_t) rng_xorshift32(rng) * (uint32_t) w) >> 32) -
        radius;
    y = (int) (((uint64_t) rng_xorshift32(rng) * (uint32_t) w) >> 32) -
        radius;
    if (x * x + y * y > radius * radius)
      continue;

    x += p.x;
    y += p.y;
    if (x < 0 || y < 0 || x >= doc->bm.size.w || y >= doc->bm.size.h)
      continue;

    screen_set_pixel(&doc->scr, x, y, colour);
  }

  dirty = clipped_box(doc, p, p, radius);
  sel_clip(doc, dirty);

  return dirty;
}

/* ----------------------------------------------------------------------- */

void paintingdoc_draw_rect(screen_t *scr,
                           point_t   a,
                           point_t   b,
                           int       width,
                           int       fill,
                           colour_t  colour)
{
  int x0, y0, w, h, t;

  x0 = MIN(a.x, b.x);
  y0 = MIN(a.y, b.y);
  w  = MAX(a.x, b.x) - x0 + 1;
  h  = MAX(a.y, b.y) - y0 + 1;
  t  = MAX(width, 1);

  if (fill || 2 * t >= w || 2 * t >= h)
  {
    screen_fill_rect(scr, x0, y0, SIZE2D(w, h), colour);
  }
  else
  {
    screen_fill_rect(scr, x0, y0, SIZE2D(w, t), colour);
    screen_fill_rect(scr, x0, y0 + h - t, SIZE2D(w, t), colour);
    screen_fill_rect(scr, x0, y0 + t, SIZE2D(t, h - 2 * t), colour);
    screen_fill_rect(scr, x0 + w - t, y0 + t, SIZE2D(t, h - 2 * t), colour);
  }
}

box_t paintingdoc_rect(paintingdoc_t *doc,
                       point_t        a,
                       point_t        b,
                       int            width,
                       int            fill,
                       colour_t       colour)
{
  box_t dirty;

  checkpoint_or_warn(doc);

  paintingdoc_draw_rect(&doc->scr, a, b, width, fill, colour);

  dirty = clipped_box(doc, a, b, 0);
  sel_clip(doc, dirty);

  return dirty;
}

void paintingdoc_draw_ellipse(screen_t *scr,
                              point_t   a,
                              point_t   b,
                              int       width,
                              int       fill,
                              colour_t  colour)
{
  int      x, y;
  size2d_t size;

  x    = MIN(a.x, b.x);
  y    = MIN(a.y, b.y);
  size = SIZE2D(MAX(a.x, b.x) - x + 1, MAX(a.y, b.y) - y + 1);

  if (fill)
    screen_fill_ellipse(scr, x, y, size, colour);
  else
    screen_draw_ellipse(scr, x, y, size, width, colour);
}

box_t paintingdoc_ellipse(paintingdoc_t *doc,
                          point_t        a,
                          point_t        b,
                          int            width,
                          int            fill,
                          colour_t       colour)
{
  box_t dirty;

  checkpoint_or_warn(doc);

  paintingdoc_draw_ellipse(&doc->scr, a, b, width, fill, colour);

  dirty = clipped_box(doc, a, b, 0);
  sel_clip(doc, dirty);

  return dirty;
}

box_t paintingdoc_path(paintingdoc_t *doc,
                       const point_t *pts,
                       int            n,
                       int            width,
                       int            fill,
                       colour_t       colour)
{
  point_t    lo, hi;
  int        i, y, nruns;
  box_t      canvas;
  region_t  *area;
  const int *runs;
  box_t      dirty;

  if (n < (fill ? 3 : 1))
    return (box_t) BOX_INIT;

  lo = hi = pts[0];
  for (i = 1; i < n; i++)
  {
    lo.x = MIN(lo.x, pts[i].x);
    lo.y = MIN(lo.y, pts[i].y);
    hi.x = MAX(hi.x, pts[i].x);
    hi.y = MAX(hi.y, pts[i].y);
  }

  checkpoint_or_warn(doc);

  if (fill)
  {
    canvas = canvas_box(doc);
    if (region_from_polygon(pts, n, &canvas, &area) != result_OK)
    {
      logf_warning("painting: no memory to fill path");
      area = NULL;
    }

    for (y = lo.y; y <= hi.y; y++)
    {
      nruns = region_row(area, y, &runs);
      for (i = 0; i < nruns; i++)
        screen_fill_hline(&doc->scr, runs[i * 2], y,
                          runs[i * 2 + 1] - runs[i * 2], colour);
    }

    region_destroy(area);
  }
  else
  {
    /* the last point joins back to the first */
    for (i = 0; i < n; i++)
      paintingdoc_draw_stroke(&doc->scr, pts[i], pts[(i + 1) % n], width,
                              colour);
  }

  dirty = clipped_box(doc, lo, hi, fill ? 0 : MAX(width, 1) / 2);
  sel_clip(doc, dirty);

  return dirty;
}

/* ----------------------------------------------------------------------- */

static uint32_t *pixel_row(const paintingdoc_t *doc, int y)
{
  return row_of(&doc->bm, y);
}

/* plot p as the pencil's next pixel, dropping the last one if it is the
 * corner of an L */
static void pencil_plot(paintingdoc_t       *doc,
                        point_t              p,
                        colour_t             colour,
                        paintingdoc_trail_t *trail)
{
  point_t  *last;
  uint32_t *px;

  last = &trail->p[MAX(trail->n - 1, 0)];
  if (trail->n > 0 && last->x == p.x && last->y == p.y)
    return;

  if (p.x < 0 || p.y < 0 || p.x >= doc->bm.size.w || p.y >= doc->bm.size.h)
  {
    trail->n = 0; /* off the canvas: start afresh on coming back */
    return;
  }

  px = &pixel_row(doc, p.y)[p.x];

  /* the pixel before last and p touch diagonally, so the last is a corner */
  if (trail->n == 2 &&
      abs(p.x - trail->p[0].x) == 1 && abs(p.y - trail->p[0].y) == 1)
  {
    pixel_row(doc, last->y)[last->x] = trail->under;
    trail->n = 1;
  }

  if (trail->n == 2)
    trail->p[0] = trail->p[1];
  else
    trail->n++;
  trail->p[trail->n - 1] = p;
  trail->under           = *px;

  screen_set_pixel(&doc->scr, p.x, p.y, colour);
}

box_t paintingdoc_pencil(paintingdoc_t       *doc,
                         point_t              a,
                         point_t              b,
                         colour_t             colour,
                         paintingdoc_trail_t *trail)
{
  box_t   dirty;
  point_t start;
  int     dx, dy, sx, sy, err, e2;

  start = a;

  /* Bresenham, so each step lands on a neighbour of the last */
  dx  = abs(b.x - a.x);
  dy  = -abs(b.y - a.y);
  sx  = a.x < b.x ? 1 : -1;
  sy  = a.y < b.y ? 1 : -1;
  err = dx + dy;

  for (;;)
  {
    pencil_plot(doc, a, colour, trail);
    if (a.x == b.x && a.y == b.y)
      break;

    e2 = 2 * err;
    if (e2 >= dy)
    {
      err += dy;
      a.x += sx;
    }
    if (e2 <= dx)
    {
      err += dx;
      a.y += sy;
    }
  }

  /* a dropped corner is a neighbour of the segment's start */
  dirty = clipped_box(doc, start, b, 1);
  sel_clip(doc, dirty);

  return dirty;
}

box_t paintingdoc_fill(paintingdoc_t *doc, point_t p, colour_t colour)
{
  box_t dirty = BOX_INIT;

  if (p.x < 0 || p.x >= doc->bm.size.w || p.y < 0 || p.y >= doc->bm.size.h)
    return dirty;

  if (pixel_row(doc, p.y)[p.x] == screen_colour_to_pixel(&doc->scr, colour))
    return dirty;

  checkpoint_or_warn(doc);

  if (bitmap_flood_fill(&doc->bm, p, colour, &dirty) != result_OK)
    logf_warning("painting: no memory to finish fill");

  sel_clip(doc, dirty);
  return dirty;
}

/* what select_similar's flood compares each pixel against */
typedef struct
{
  const paintingdoc_t *doc;
  uint32_t             seed;
  int                  tolerance;
}
similar_t;

/* every channel, alpha included, within tolerance of the seed's */
static int similar_inside(void *opaque, int x, int y)
{
  const similar_t *s = opaque;
  uint32_t         px;
  int              shift;
  int              d;

  px = pixel_row(s->doc, y)[x];
  for (shift = 0; shift < 32; shift += 8)
  {
    d = (int) ((px >> shift) & 0xFF) - (int) ((s->seed >> shift) & 0xFF);
    if (abs(d) > s->tolerance)
      return 0;
  }

  return 1;
}

result_t paintingdoc_select_similar(paintingdoc_t      *doc,
                                    point_t             p,
                                    int                 tolerance,
                                    paintingdoc_selop_t op)
{
  result_t  rc;
  box_t     canvas;
  similar_t s;
  region_t *sel;

  canvas = canvas_box(doc);
  if (p.x < 0 || p.x >= doc->bm.size.w || p.y < 0 || p.y >= doc->bm.size.h)
    return sel_adopt(doc, NULL, op);

  s.doc       = doc;
  s.seed      = pixel_row(doc, p.y)[p.x];
  s.tolerance = tolerance;

  rc = region_from_flood(p, &canvas, similar_inside, &s, &sel);
  if (rc != result_OK)
    return rc;

  return sel_adopt(doc, sel, op);
}

box_t paintingdoc_gradient(paintingdoc_t *doc,
                           point_t        a,
                           point_t        b,
                           colour_t       c0,
                           colour_t       c1)
{
  box_t dirty = BOX_INIT;

  if (a.x == b.x && a.y == b.y)
    return dirty;

  dirty = doc->sel ? paintingdoc_selection_bounds(doc) : canvas_box(doc);

  checkpoint_or_warn(doc);

  screen_fill_linear_gradient(&doc->scr, &dirty, a, b, c0, c1);

  sel_clip(doc, dirty);
  return dirty;
}

int paintingdoc_pick(const paintingdoc_t *doc, point_t p, colour_t *colour)
{
  uint32_t px;

  if (p.x < 0 || p.x >= doc->bm.size.w || p.y < 0 || p.y >= doc->bm.size.h)
    return 0;

  px      = pixel_row(doc, p.y)[p.x];
  *colour = colour_rgba((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF,
                        px >> 24);

  return 1;
}

/* ----------------------------------------------------------------------- */

/* far enough to keep floating pixels anywhere they can be dragged, off the
 * canvas included */
static const box_t unbounded = { -0x1000000, -0x1000000,
                                 0x1000000, 0x1000000 };

result_t paintingdoc_lift(paintingdoc_t *doc)
{
  result_t rc;
  box_t    canvas;

  /* floating pixels are lifted already */
  if (doc->floating.base != NULL)
    return result_OK;

  if (doc->sel == NULL)
    return result_BAD_ARG;

  /* the checkpoint copies the canvas, the lifted pixels included, into pre,
   * and ends any lift before */
  rc = paintingdoc_checkpoint(doc);
  if (rc != result_OK)
    return rc;
  if (doc->pre == NULL)
    return result_OOM;

  canvas = canvas_box(doc);
  return region_translate(doc->sel, 0, 0, &canvas, &doc->lifted);
}

box_t paintingdoc_place(paintingdoc_t  *doc,
                        int             dx,
                        int             dy,
                        const colour_t *leave)
{
  box_t           dirty;
  box_t           canvas;
  region_t       *moved;
  box_t           from, to, was;
  uint32_t        fill;
  int             y;
  int             n, i;
  const int      *runs;
  uint32_t       *row;
  int             x;
  const uint8_t  *base;
  size_t          rowbytes;
  point_t         o;
  const uint32_t *src;
  int             x0, x1;

  dirty = (box_t) BOX_INIT;
  if (doc->lifted == NULL)
    return dirty;

  /* floating pixels leave nothing behind and come from their own bitmap */
  if (doc->floating.base != NULL)
  {
    leave    = NULL;
    base     = doc->floating.base;
    rowbytes = (size_t) doc->floating.rowbytes;
    o        = doc->origin;
  }
  else
  {
    base     = doc->pre;
    rowbytes = (size_t) doc->bm.rowbytes;
    o        = POINT(0, 0);
  }
  doc->d = POINT(dx, dy);

  canvas = canvas_box(doc);
  if (region_translate(doc->lifted, dx, dy, &canvas, &moved) != result_OK)
  {
    logf_warning("painting: no memory to move the selection");
    return dirty;
  }

  /* everything the lifted pixels have covered since the lift */
  from = region_bounds(doc->lifted);
  to   = region_bounds(moved);
  was  = region_bounds(doc->sel);
  box_union(&from, &to, &dirty);
  box_union(&dirty, &was, &dirty);
  if (box_intersection(&dirty, &canvas, &dirty))
    dirty = (box_t) BOX_INIT;

  memcpy(doc->bm.base, doc->pre,
         (size_t) doc->bm.rowbytes * (size_t) doc->bm.size.h);

  /* a copy leaves the lifted pixels as they were */
  if (leave == NULL)
    from = (box_t) BOX_INIT;
  else
    fill = screen_colour_to_pixel(&doc->scr, *leave);
  for (y = from.y0; y < from.y1; y++)
  {
    n   = region_row(doc->lifted, y, &runs);
    row = pixel_row(doc, y);
    for (i = 0; i < n; i++)
      for (x = runs[i * 2]; x < runs[i * 2 + 1]; x++)
        row[x] = fill;
  }

  /* the moved selection is the lifted one clipped to the canvas, so copy
   * its runs from where they were lifted */
  for (y = to.y0; y < to.y1; y++)
  {
    n   = region_row(moved, y, &runs);
    row = pixel_row(doc, y);
    src = (const uint32_t *) (base + (size_t) (y - dy - o.y) * rowbytes);
    for (i = 0; i < n; i++)
    {
      x0 = runs[i * 2];
      x1 = runs[i * 2 + 1];
      memcpy(row + x0, src + (x0 - dx - o.x),
             (size_t) (x1 - x0) * sizeof(*row));
    }
  }

  if (sel_replace(doc, moved) != result_OK)
    logf_warning("painting: no memory to outline the selection");

  touch(doc, dirty);
  return dirty;
}

void paintingdoc_drop(paintingdoc_t *doc)
{
  region_t *moved;

  /* floating pixels stay lifted, from where they were placed */
  if (doc->floating.base != NULL &&
      region_translate(doc->lifted, doc->d.x, doc->d.y, &unbounded,
                       &moved) == result_OK)
  {
    region_destroy(doc->lifted);
    doc->lifted    = moved;
    doc->origin.x += doc->d.x;
    doc->origin.y += doc->d.y;
    doc->d         = POINT(0, 0);
    return;
  }

  lift_end(doc);
}

box_t paintingdoc_nudge(paintingdoc_t  *doc,
                        int             dx,
                        int             dy,
                        const colour_t *leave)
{
  box_t dirty;

  if (doc->lifted == NULL)
  {
    if (paintingdoc_lift(doc) != result_OK)
      return (box_t) BOX_INIT;
    doc->d = POINT(0, 0);
  }

  dirty = paintingdoc_place(doc, doc->d.x + dx, doc->d.y + dy, leave);

  /* floating pixels rebase so a later drag starts from here; lifted ones
   * stay lifted from where they began */
  if (doc->floating.base != NULL)
    paintingdoc_drop(doc);

  return dirty;
}

/* ----------------------------------------------------------------------- */

void paintingdoc_clip_free(paintingdoc_clip_t *clip)
{
  free(clip->bm.base);
  region_destroy(clip->mask);
  clip->bm.base = NULL;
  clip->mask    = NULL;
}

result_t paintingdoc_copy(const paintingdoc_t *doc, paintingdoc_clip_t *clip)
{
  result_t  rc;
  box_t     b;
  box_t     to;
  size2d_t  size;
  uint8_t  *pixels;
  region_t *mask;
  bitmap_t  bm;
  int       y;

  if (doc->sel == NULL)
    return result_BAD_ARG;

  b      = region_bounds(doc->sel);
  size   = SIZE2D(b.x1 - b.x0, b.y1 - b.y0);
  to     = (box_t) BOX_POS_SIZE(0, 0, size.w, size.h);
  pixels = malloc((size_t) size.w * (size_t) size.h * 4);
  if (pixels == NULL)
    return result_OOM;

  rc = region_translate(doc->sel, -b.x0, -b.y0, &to, &mask);
  if (rc == result_OK)
    rc = bitmap_init(&bm, size, pixelfmt_bgra8888, size.w * 4, NULL,
                     pixels);
  if (rc != result_OK)
  {
    region_destroy(mask);
    free(pixels);
    return rc;
  }

  for (y = 0; y < size.h; y++)
    memcpy(pixels + (size_t) y * size.w * 4, pixel_row(doc, b.y0 + y) + b.x0,
           (size_t) size.w * 4);

  paintingdoc_clip_free(clip);
  clip->bm   = bm;
  clip->mask = mask;
  clip->at   = POINT(b.x0, b.y0);

  return result_OK;
}

box_t paintingdoc_paste(paintingdoc_t            *doc,
                        const paintingdoc_clip_t *clip,
                        point_t                   p)
{
  box_t     canvas;
  region_t *lifted;
  region_t *sel;
  size_t    bytes;
  void     *pixels;

  if (clip->mask == NULL)
    return (box_t) BOX_INIT;

  /* the pasted pixels become the selection, so the checkpoint keeps the
   * canvas beneath them in pre */
  canvas = canvas_box(doc);
  if (region_translate(clip->mask, p.x, p.y, &canvas, &sel) != result_OK ||
      sel == NULL)
    return (box_t) BOX_INIT;

  paintingdoc_select_none(doc);
  if (sel_replace(doc, sel) != result_OK)
    return (box_t) BOX_INIT;
  /* a step of its own even mid-stroke: checkpoint_or_warn would skip it when
   * held, leaving pre unmade */
  if (checkpoint(doc, doc->cur) != result_OK)
    logf_warning("painting: no memory for an undo step");

  bytes  = (size_t) clip->bm.rowbytes * (size_t) clip->bm.size.h;
  pixels = malloc(bytes);
  if (doc->pre == NULL || pixels == NULL ||
      region_translate(clip->mask, p.x, p.y, &unbounded, &lifted) !=
      result_OK)
  {
    free(pixels);
    logf_warning("painting: no memory to paste");
    return (box_t) BOX_INIT;
  }

  memcpy(pixels, clip->bm.base, bytes);
  doc->floating      = clip->bm;
  doc->floating.base = pixels;
  doc->origin        = p;
  doc->lifted        = lifted;

  return paintingdoc_place(doc, 0, 0, NULL);
}

/* ----------------------------------------------------------------------- */

/* replace every layer's pixels with next's, which have their own stores */
static box_t layers_replace(paintingdoc_t *doc, const bitmap_t *next)
{
  int i;

  for (i = 0; i < doc->nlayers; i++)
  {
    free(doc->layers[i].bm.base);
    doc->layers[i].bm = next[i];
  }

  layer_sync(doc);
  touch(doc, canvas_box(doc));
  return canvas_box(doc);
}

box_t paintingdoc_transform_selection(paintingdoc_t *doc,
                                      orient_t       orient,
                                      colour_t       leave)
{
  result_t           rc;
  int                floating;
  paintingdoc_clip_t clip;
  bitmap_t           bm;
  region_t          *mask;
  size2d_t           size;
  point_t            o;
  region_t          *lifted;
  uint32_t           fill;
  box_t              b;
  int                y;
  int                n, i;
  const int         *runs;
  uint32_t          *row;
  int                x;

  if (doc->sel == NULL)
    return (box_t) BOX_INIT;

  /* floating pixels turn in their own bitmap; others are copied out first */
  floating     = doc->floating.base != NULL;
  clip.bm.base = NULL;
  clip.mask    = NULL;
  if (floating)
  {
    clip.bm = doc->floating;
    clip.at = POINT(doc->origin.x + doc->d.x, doc->origin.y + doc->d.y);
    rc      = region_translate(doc->lifted, -doc->origin.x, -doc->origin.y,
                               &unbounded, &clip.mask);
  }
  else
  {
    rc = paintingdoc_copy(doc, &clip);
  }

  mask    = NULL;
  bm.base = NULL;
  if (rc == result_OK)
    rc = bitmap_orient(&clip.bm, orient, &bm);
  if (rc == result_OK)
    rc = region_orient(clip.mask, orient, clip.bm.size, &mask);

  /* turn about the centre of the selection's bounds. clip is unset if the
   * copy failed */
  lifted = NULL;
  if (rc == result_OK)
  {
    size = orient_size(orient, clip.bm.size);
    o.x  = clip.at.x + (clip.bm.size.w - size.w) / 2;
    o.y  = clip.at.y + (clip.bm.size.h - size.h) / 2;
    rc   = region_translate(mask, o.x, o.y, &unbounded, &lifted);
  }

  region_destroy(mask);
  region_destroy(clip.mask);
  if (!floating)
    free(clip.bm.base);

  /* the transformed pixels float, so the checkpoint keeps the canvas, with
   * the selection's pixels cleared to leave, in pre */
  if (rc == result_OK && !floating)
  {
    checkpoint_or_warn(doc);
    if (doc->pre == NULL)
      rc = result_OOM;
  }
  if (rc != result_OK || lifted == NULL)
  {
    free(bm.base);
    region_destroy(lifted);
    logf_warning("painting: no memory to transform the selection");
    return (box_t) BOX_INIT;
  }

  if (!floating)
  {
    fill = screen_colour_to_pixel(&doc->scr, leave);
    b    = region_bounds(doc->sel);
    for (y = b.y0; y < b.y1; y++)
    {
      n   = region_row(doc->sel, y, &runs);
      row = (uint32_t *) (doc->pre + (size_t) y * (size_t) doc->bm.rowbytes);
      for (i = 0; i < n; i++)
        for (x = runs[i * 2]; x < runs[i * 2 + 1]; x++)
          row[x] = fill;
    }
  }

  lift_end(doc);
  doc->floating = bm;
  doc->lifted   = lifted;
  doc->origin   = o;

  return paintingdoc_place(doc, 0, 0, NULL);
}

/* the area box of src into *next, with its own pixel store, filled with fill
 * where box lies beyond src */
static result_t layer_resize(const bitmap_t *src,
                             box_t           box,
                             colour_t        fill,
                             bitmap_t       *next)
{
  result_t rc;
  box_t    canvas;
  box_t    keep;
  int      y;

  rc = canvas_make(SIZE2D(box.x1 - box.x0, box.y1 - box.y0), fill, next);
  if (rc != result_OK)
    return rc;

  /* copy across the part of src that box covers */
  canvas = (box_t) BOX_POS_SIZE(0, 0, src->size.w, src->size.h);
  if (!box_intersection(&box, &canvas, &keep))
    for (y = keep.y0; y < keep.y1; y++)
      memcpy(row_of(next, y - box.y0) + (keep.x0 - box.x0),
             row_of(src, y) + keep.x0,
             (size_t) (keep.x1 - keep.x0) * 4);

  return result_OK;
}

box_t paintingdoc_transform(paintingdoc_t *doc, orient_t orient)
{
  bitmap_t next[PAINTINGDOC_LAYERS_MAX];
  int      i, j;

  for (i = 0; i < doc->nlayers; i++)
  {
    if (bitmap_orient(&doc->layers[i].bm, orient, &next[i]) != result_OK)
    {
      for (j = 0; j < i; j++)
        free(next[j].base);
      logf_warning("painting: no memory to transform the canvas");
      return (box_t) BOX_INIT;
    }
  }

  paintingdoc_select_none(doc);
  checkpoint_all(doc);
  return layers_replace(doc, next);
}

box_t paintingdoc_resize(paintingdoc_t *doc, box_t box, colour_t fill)
{
  bitmap_t next[PAINTINGDOC_LAYERS_MAX];
  int      i, j;

  if (box.x1 <= box.x0 || box.y1 <= box.y0)
    return (box_t) BOX_INIT;

  /* the bottom layer takes fill; those above stay clear */
  for (i = 0; i < doc->nlayers; i++)
  {
    if (layer_resize(&doc->layers[i].bm, box,
                     i == 0 ? fill : colour_rgba(0, 0, 0, 0),
                     &next[i]) != result_OK)
    {
      for (j = 0; j < i; j++)
        free(next[j].base);
      logf_warning("painting: no memory to resize the canvas");
      return (box_t) BOX_INIT;
    }
  }

  paintingdoc_select_none(doc);
  checkpoint_all(doc);
  return layers_replace(doc, next);
}

box_t paintingdoc_scale(paintingdoc_t *doc, size2d_t size)
{
  bitmap_t  next[PAINTINGDOC_LAYERS_MAX];
  bitmap_t *scaled;
  int       i, j;

  if (size.w <= 0 || size.h <= 0)
    return (box_t) BOX_INIT;

  for (i = 0; i < doc->nlayers; i++)
  {
    if (bitmap_resize(&doc->layers[i].bm, size, bitmap_resample_NEAREST, 0,
                      &scaled) != result_OK)
    {
      for (j = 0; j < i; j++)
        free(next[j].base);
      logf_warning("painting: no memory to scale the canvas");
      return (box_t) BOX_INIT;
    }
    next[i] = *scaled; /* keep its pixels, which canvas_make's match */
    free(scaled);
  }

  paintingdoc_select_none(doc);
  checkpoint_all(doc);
  return layers_replace(doc, next);
}

box_t paintingdoc_trim_bounds(const paintingdoc_t *doc)
{
  box_t           box;
  int             i;
  const bitmap_t *bm;
  box_t           b;

  /* the bottom layer against its corner, those above against clear */
  box = (box_t) BOX_INIT;
  for (i = 0; i < doc->nlayers; i++)
  {
    bm = &doc->layers[i].bm;
    b  = i == 0 ? bitmap_content_bounds(bm, row_of(bm, 0)[0], 0xFFFFFFFFu)
                : bitmap_content_bounds(bm, 0, 0xFF000000u);
    box.x0 = MIN(box.x0, b.x0);
    box.y0 = MIN(box.y0, b.y0);
    box.x1 = MAX(box.x1, b.x1);
    box.y1 = MAX(box.y1, b.y1);
  }

  return box;
}

/* ----------------------------------------------------------------------- */

int paintingdoc_layers(const paintingdoc_t *doc)
{
  return doc->nlayers;
}

int paintingdoc_layer(const paintingdoc_t *doc)
{
  return doc->cur;
}

const paintingdoc_layer_t *paintingdoc_layer_get(const paintingdoc_t *doc,
                                                 int                  i)
{
  return &doc->layers[i];
}

void paintingdoc_layer_select(paintingdoc_t *doc, int i)
{
  if (i < 0 || i >= doc->nlayers)
    return;

  lift_end(doc);
  doc->cur = i;
  layer_sync(doc);
}

result_t paintingdoc_layer_add(paintingdoc_t *doc)
{
  result_t             rc;
  bitmap_t             bm;
  int                  i;
  paintingdoc_layer_t *l;

  if (doc->nlayers == PAINTINGDOC_LAYERS_MAX)
    return result_TOO_BIG;

  rc = canvas_make(doc->bm.size, colour_rgba(0, 0, 0, 0), &bm);
  if (rc != result_OK)
    return rc;

  rc = checkpoint(doc, -1);
  if (rc != result_OK)
  {
    free(bm.base);
    return rc;
  }

  i = doc->cur + 1;
  memmove(&doc->layers[i + 1], &doc->layers[i],
          (size_t) (doc->nlayers - i) * sizeof(doc->layers[0]));
  l          = &doc->layers[i];
  l->bm      = bm;
  l->id      = doc->nextid++;
  l->visible = 1;
  l->opacity = 255;
  l->locked  = 0;
  doc->nlayers++;
  doc->cur = i;
  layer_sync(doc);
  touch(doc, canvas_box(doc));

  return result_OK;
}

result_t paintingdoc_layer_delete(paintingdoc_t *doc)
{
  result_t rc;
  int      i;

  if (doc->nlayers == 1)
    return result_BAD_ARG;

  rc = checkpoint(doc, -1);
  if (rc != result_OK)
    return rc;

  i = doc->cur;
  free(doc->layers[i].bm.base);
  memmove(&doc->layers[i], &doc->layers[i + 1],
          (size_t) (doc->nlayers - i - 1) * sizeof(doc->layers[0]));
  doc->nlayers--;
  doc->cur = MAX(i - 1, 0);
  layer_sync(doc);
  touch(doc, canvas_box(doc));

  return result_OK;
}

result_t paintingdoc_layer_move(paintingdoc_t *doc, int dir)
{
  result_t rc;
  int      j;

  j = doc->cur + dir;
  if (j < 0 || j >= doc->nlayers)
    return result_BAD_ARG;

  rc = checkpoint(doc, -1);
  if (rc != result_OK)
    return rc;

  SWAP(doc->layers[doc->cur], doc->layers[j]);
  doc->cur = j;
  layer_sync(doc);
  touch(doc, canvas_box(doc));

  return result_OK;
}

box_t paintingdoc_layer_show(paintingdoc_t *doc,
                             int            i,
                             int            visible,
                             int            opacity)
{
  doc->layers[i].visible = visible;
  doc->layers[i].opacity = CLAMP(opacity, 0, 255);
  touch(doc, canvas_box(doc));

  return canvas_box(doc);
}

void paintingdoc_layer_lock(paintingdoc_t *doc, int i, int locked)
{
  doc->layers[i].locked = locked;
}
