/* wuss/tasks/painting/painting-doc.c -- paint canvas, drawing and undo */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/region.h"

#include "painting-doc.h"

/* ----------------------------------------------------------------------- */

struct paintingdoc
{
  bitmap_t bm;
  screen_t scr;
  /* RLE-compressed copies of bm, oldest first */
  bitmap_t undo[PAINTINGDOC_UNDO_MAX];
  int      nundo;
  /* the selection, or NULL for none, and its boundary pixels for the ants.
   * pre is a raw copy of the canvas as of the last checkpoint, kept while
   * there is a selection so drawing outside it can be put back */
  region_t *sel;
  box_t    *outline;
  int       noutline;
  uint8_t  *pre;
  /* changed since the last load or paintingdoc_set_saved */
  int       edited;
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

  rc = canvas_make(size, colour, &d->bm);
  if (rc != result_OK)
  {
    free(d);
    return rc;
  }

  screen_for_bitmap(&d->scr, &d->bm);

  *doc = d;
  return result_OK;
}

/* drop every undo step */
static void undo_clear(paintingdoc_t *doc)
{
  int i;

  for (i = 0; i < doc->nundo; i++)
    free(doc->undo[i].base);

  doc->nundo = 0;
}

void paintingdoc_destroy(paintingdoc_t *doc)
{
  if (doc == NULL)
    return;

  undo_clear(doc);
  paintingdoc_select_none(doc);
  free(doc->bm.base);
  free(doc);
}

/* free the pixels and palette of a bitmap that bitmap_load_png made */
static void bitmap_discard(bitmap_t *bm)
{
  free(bm->base);
  free(bm->palette);
}

result_t paintingdoc_load_png(paintingdoc_t *doc, const char *path)
{
  result_t rc;
  bitmap_t src;
  bitmap_t next;
  screen_t scr;

  rc = bitmap_load_png(&src, path);
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
  free(doc->bm.base);
  doc->bm = next;
  screen_for_bitmap(&doc->scr, &doc->bm);
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

const bitmap_t *paintingdoc_bitmap(const paintingdoc_t *doc)
{
  return &doc->bm;
}

/* ----------------------------------------------------------------------- */

/* the canvas box */
static box_t canvas_box(const paintingdoc_t *doc)
{
  box_t box = BOX_POS_SIZE(0, 0, doc->bm.size.w, doc->bm.size.h);

  return box;
}

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

void paintingdoc_select_none(paintingdoc_t *doc)
{
  region_destroy(doc->sel);
  free(doc->outline);
  free(doc->pre);
  doc->sel      = NULL;
  doc->outline  = NULL;
  doc->noutline = 0;
  doc->pre      = NULL;
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

  doc->edited = 1;

  if (doc->sel == NULL || doc->pre == NULL)
    return;

  pre      = doc->bm;
  pre.base = doc->pre;
  (void) bitmap_restore_outside_region(&doc->bm, &pre, doc->sel, &dirty);
}

/* ----------------------------------------------------------------------- */

result_t paintingdoc_checkpoint(paintingdoc_t *doc)
{
  result_t rc;
  size_t   n;
  void    *pixels;
  bitmap_t snap;

  n = (size_t) doc->bm.rowbytes * (size_t) doc->bm.size.h;

  if (doc->sel != NULL)
  {
    if (doc->pre == NULL)
      doc->pre = malloc(n);
    if (doc->pre != NULL)
      memcpy(doc->pre, doc->bm.base, n);
    else
      logf_warning("painting: no memory to clip drawing to the selection");
  }

  pixels = malloc(n);
  if (pixels == NULL)
    return result_OOM;

  memcpy(pixels, doc->bm.base, n);

  rc = bitmap_init(&snap, doc->bm.size, pixelfmt_bgra8888, doc->bm.rowbytes,
                   NULL, pixels);
  if (rc == result_OK)
    rc = bitmap_compress(&snap);
  if (rc != result_OK)
  {
    free(pixels);
    return rc;
  }

  if (doc->nundo == PAINTINGDOC_UNDO_MAX)
  {
    free(doc->undo[0].base);
    memmove(&doc->undo[0], &doc->undo[1],
            (PAINTINGDOC_UNDO_MAX - 1) * sizeof(doc->undo[0]));
    doc->nundo--;
  }

  doc->undo[doc->nundo++] = snap;
  return result_OK;
}

/* a drawing operation that takes its own checkpoint, but still draws if it
 * cannot: no undo for that step beats losing the user's edit */
static void checkpoint_or_warn(paintingdoc_t *doc)
{
  if (paintingdoc_checkpoint(doc) != result_OK)
    logf_warning("painting: no memory for an undo step");
}

int paintingdoc_can_undo(const paintingdoc_t *doc)
{
  return doc->nundo > 0;
}

box_t paintingdoc_undo(paintingdoc_t *doc)
{
  bitmap_t *snap;
  size_t    n;

  if (doc->nundo == 0)
    return (box_t) BOX_INIT;

  snap = &doc->undo[doc->nundo - 1];
  if (bitmap_decompress(snap) != result_OK)
    return (box_t) BOX_INIT;

  n = (size_t) doc->bm.rowbytes * (size_t) doc->bm.size.h;
  memcpy(doc->bm.base, snap->base, n);

  free(snap->base);
  doc->nundo--;
  doc->edited = 1;

  return canvas_box(doc);
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
                        colour_t             colour)
{
  box_t dirty;
  int   r, n, i, dx, dy, cx, cy, x, y;

  r = MAX(size, 1) / 2;
  n = MAX(abs(b.x - a.x), abs(b.y - a.y));

  for (i = 0; i <= n; i++)
  {
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

        if (nozzle_hits(nozzle, r, dx, dy))
          screen_set_pixel(&doc->scr, x, y, colour);
      }
    }
  }

  dirty = clipped_box(doc, a, b, r);
  sel_clip(doc, dirty);

  return dirty;
}

/* ----------------------------------------------------------------------- */

/* a well-mixed 32-bit value for knot k of the noise seeded by seed */
static uint32_t wobble_hash(uint32_t seed, int k)
{
  uint32_t h;

  h  = seed ^ ((uint32_t) k * 0x9E3779B9u);
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;

  return h;
}

/* smooth value noise at step s (>= 0), -256..256, with a random knot every
 * period steps eased into the next */
static int wobble_noise(uint32_t seed, int s, int period)
{
  int k, f, v0, v1, t;

  k  = s / period;
  f  = s % period;
  v0 = (int) (wobble_hash(seed, k) & 511) - 256;
  v1 = (int) (wobble_hash(seed, k + 1) & 511) - 256;
  t  = 256 * f * f * (3 * period - 2 * f) / (period * period * period);

  return v0 + (v1 - v0) * t / 256;
}

/* stamp a disc at every pixel step: its radius wanders, moving both edges
 * apart and together, and its centre wanders across the stroke, moving them
 * together, so each edge wobbles on its own. the radius swells slowly and
 * jitters quickly: the slow swell alone looks like a string of beads */
box_t paintingdoc_wobble(paintingdoc_t        *doc,
                         point_t               a,
                         point_t               b,
                         int                   width,
                         colour_t              colour,
                         paintingdoc_wobble_t *wobble)
{
  box_t dirty;
  int   r, as, aj, ao, ps, pj, dx, dy, n, i, s, rr, o, cx, cy;

  r  = MAX(width, 1) / 2;
  as = r / 4;           /* how far the slow swell moves the radius */
  aj = (r + 4) / 5;     /* how far the fast jitter moves it */
  ao = r / 4;           /* how far the centre wanders */
  ps = MAX(3 * r, 8);   /* steps between the swell's knots */
  pj = MAX(r / 2, 2);   /* steps between the jitter's knots */
  dx = b.x - a.x;
  dy = b.y - a.y;
  n  = MAX(abs(dx), abs(dy));

  for (i = 0; i <= n; i++)
  {
    s  = wobble->step + i;
    rr = wobble_noise(wobble->seed, s, ps) * as +
         wobble_noise(wobble->seed ^ 0x27D4EB2Fu, s, pj) * aj;
    rr = r + (rr >= 0 ? rr + 128 : rr - 128) / 256; /* rounded: a thin
                                                      * line's one-pixel
                                                      * jitter survives */
    o  = wobble_noise(wobble->seed ^ 0x5BD1E995u, s, ps) * ao;
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
                        colour_t       colour,
                        rng_t         *rng)
{
  box_t dirty;
  int   n, x, y, w;

  radius = MAX(radius, 1);
  w      = 2 * radius + 1;

  /* about one dot per four pixels of the disc's area, at least a few */
  n = MAX(radius * radius * 3 / 4, 3);

  while (n-- > 0)
  {
    x = (int) (rng_xorshift32(rng) % (uint32_t) w) - radius;
    y = (int) (rng_xorshift32(rng) % (uint32_t) w) - radius;
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
  return (uint32_t *) ((uint8_t *) doc->bm.base +
                       (size_t) y * doc->bm.rowbytes);
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
