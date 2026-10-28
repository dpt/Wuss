/* wuss/tasks/painting/painting-doc.h -- paint canvas, drawing and undo */

#ifndef TASKS_PAINTING_DOC_H
#define TASKS_PAINTING_DOC_H

#include "base/result.h"
#include "framebuf/bitmap.h"
#include "framebuf/colour.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/point.h"
#include "geom/size.h"
#include "utils/rng.h"

/* the canvas is a bgra8888 bitmap with straight (not premultiplied) alpha.
 * drawing is hard-edged: it replaces pixels, alpha and all, rather than
 * blending over them, so painting with a transparent colour cuts a hole.
 * every drawing operation takes canvas pixel coordinates (clipped to the
 * canvas) and returns the box of canvas pixels it may have changed, empty
 * (see box_is_empty) if none. no wuss dependency, so this is testable
 * headless. */

/* number of undo steps kept; the oldest is dropped beyond this */
#define PAINTINGDOC_UNDO_MAX 20

typedef struct paintingdoc paintingdoc_t;

/* create a white canvas of the given size. fails with result_BAD_ARG if
 * either dimension is not positive */
result_t paintingdoc_create(size2d_t size, paintingdoc_t **doc);

/* as paintingdoc_create, but filled with colour rather than white; a
 * transparent colour makes a transparent canvas */
result_t paintingdoc_create_filled(size2d_t        size,
                                   colour_t        colour,
                                   paintingdoc_t **doc);

/* free a canvas and its undo history. doc may be NULL */
void paintingdoc_destroy(paintingdoc_t *doc);

/* replace the canvas with the PNG at path, resized to match it, keeping any
 * transparency it has. discards the undo history, as it could not be applied
 * to a canvas of another size. on any failure the canvas and its history are
 * left as they were */
result_t paintingdoc_load_png(paintingdoc_t *doc, const char *path);

/* non-zero if the canvas has changed (drawn on or undone) since it was
 * created, loaded or last marked saved */
int paintingdoc_is_edited(const paintingdoc_t *doc);

/* mark the canvas as saved: unedited until it next changes */
void paintingdoc_set_saved(paintingdoc_t *doc);

/* the canvas pixels, for drawing to a window. owned by doc: valid until the
 * next paintingdoc_undo or paintingdoc_destroy */
const bitmap_t *paintingdoc_bitmap(const paintingdoc_t *doc);

/* save the canvas as an undo step. call before the first segment of a stroke:
 * paintingdoc_stroke does not do it itself, as a stroke is many calls. fails
 * with result_OOM, leaving the history as it was */
result_t paintingdoc_checkpoint(paintingdoc_t *doc);

/* draw a stroke, rect or ellipse as paintingdoc_stroke, paintingdoc_rect and
 * paintingdoc_ellipse do, but onto any screen, for a rubber-band preview over
 * the canvas: the canvas, its undo history and the dirty box are not
 * touched. */
void paintingdoc_draw_stroke(screen_t *scr,
                             point_t   a,
                             point_t   b,
                             int       width,
                             colour_t  colour);

void paintingdoc_draw_rect(screen_t *scr,
                           point_t   a,
                           point_t   b,
                           int       width,
                           int       fill,
                           colour_t  colour);

void paintingdoc_draw_ellipse(screen_t *scr,
                              point_t   a,
                              point_t   b,
                              int       width,
                              int       fill,
                              colour_t  colour);

/* draw a round-ended line of the given width in pixels (1 = a single pixel
 * wide) from a to b; a == b draws a dab. also the brush and eraser stroke
 * segment */
box_t paintingdoc_stroke(paintingdoc_t *doc,
                         point_t        a,
                         point_t        b,
                         int            width,
                         colour_t       colour);

/* the paintbrush's nozzles: the shape stamped along a stroke */
typedef enum paintingdoc_nozzle
{
  paintingdoc_NOZZLE_ROUND,
  paintingdoc_NOZZLE_SQUARE,
  paintingdoc_NOZZLE_HBAR,      /* flat chisel, wide */
  paintingdoc_NOZZLE_VBAR,      /* flat chisel, tall */
  paintingdoc_NOZZLE_SLASH,     /* calligraphy pen, rising to the right */
  paintingdoc_NOZZLE_BACKSLASH, /* calligraphy pen, falling to the right */
  paintingdoc_NOZZLE_BRISTLES,  /* every other pixel of a disc */
  paintingdoc_NOZZLE__COUNT
}
paintingdoc_nozzle_t;

/* stamp the nozzle, size pixels across, at every pixel step from a to b
 * (a == b stamps once). the caller takes the checkpoint, as for
 * paintingdoc_stroke */
box_t paintingdoc_brush(paintingdoc_t       *doc,
                        point_t              a,
                        point_t              b,
                        paintingdoc_nozzle_t nozzle,
                        int                  size,
                        colour_t             colour);

/* a wobbly stroke's progress, carried from one segment to the next so the
 * wobble runs on unbroken. set seed to any value (a fresh one per stroke for
 * a fresh wobble) and step to 0 before the first segment */
typedef struct paintingdoc_wobble
{
  uint32_t seed;
  int      step; /* pixel steps drawn so far */
}
paintingdoc_wobble_t;

/* draw a stroke segment a to b about width pixels wide whose two edges
 * wander in and out independently, like an inked line drawn with a shaky
 * hand. edge scales how far the width swells and jitters and wander how far
 * the stroke drifts across its path, each a percentage of the usual: 100 for
 * the usual, 0 for none. the caller takes the checkpoint, as for
 * paintingdoc_stroke */
box_t paintingdoc_wobble(paintingdoc_t        *doc,
                         point_t               a,
                         point_t               b,
                         int                   width,
                         int                   edge,
                         int                   wander,
                         colour_t              colour,
                         paintingdoc_wobble_t *wobble);

/* scatter dots of colour over the disc of the given radius in pixels centred
 * on p. each call aims a dot at density percent of the disc's pixels, so
 * repeat it to build up. the caller takes the checkpoint, as for
 * paintingdoc_stroke */
box_t paintingdoc_spray(paintingdoc_t *doc,
                        point_t        p,
                        int            radius,
                        int            density,
                        colour_t       colour,
                        rng_t         *rng);

/* draw the rectangle with opposite corners a and b (both inclusive, any order),
 * filled or as an outline width pixels thick drawn inside it. takes its own
 * checkpoint */
box_t paintingdoc_rect(paintingdoc_t *doc,
                       point_t        a,
                       point_t        b,
                       int            width,
                       int            fill,
                       colour_t       colour);

/* draw the ellipse filling the rectangle with opposite corners a and b (both
 * inclusive, any order), as paintingdoc_rect does */
box_t paintingdoc_ellipse(paintingdoc_t *doc,
                          point_t        a,
                          point_t        b,
                          int            width,
                          int            fill,
                          colour_t       colour);

/* draw the closed path through n points (the last joins back to the first):
 * filled (even-odd rule, as paintingdoc_select_poly) or as round-ended lines
 * width pixels wide. does nothing, and takes no checkpoint, if there are no
 * points or fewer than 3 to fill; otherwise takes its own checkpoint */
box_t paintingdoc_path(paintingdoc_t *doc,
                       const point_t *pts,
                       int            n,
                       int            width,
                       int            fill,
                       colour_t       colour);

/* flood fill from p, 4-connected, over pixels exactly equal (alpha included)
 * to the one at p.
 * does nothing, and takes no checkpoint, if p is off the canvas or already
 * the fill colour; otherwise takes its own checkpoint */
box_t paintingdoc_fill(paintingdoc_t *doc, point_t p, colour_t colour);

/* fill the selection, or the whole canvas if none, with a linear gradient,
 * alpha included, from c0 at a to c1 at b. pixels beyond either end take
 * that end's colour. does nothing, and takes no checkpoint, if a == b;
 * otherwise takes its own checkpoint */
box_t paintingdoc_gradient(paintingdoc_t *doc,
                           point_t        a,
                           point_t        b,
                           colour_t       c0,
                           colour_t       c1);

/* the colour and alpha of the canvas pixel at p, into *colour. false,
 * leaving *colour alone, if p is off the canvas */
int paintingdoc_pick(const paintingdoc_t *doc, point_t p, colour_t *colour);

/* the selection limits every drawing operation to its pixels, so a stroke,
 * rect or fill leaves the rest of the canvas as it was. there is at most one;
 * making a new one replaces it, adds to it or takes from it, as the
 * paintingdoc_selop_t passed says. it is not part of the undo history.
 * loading a PNG drops it */

/* how a newly made area meets the selection already there */
typedef enum paintingdoc_selop
{
  paintingdoc_SELOP_REPLACE,  /* the area becomes the selection */
  paintingdoc_SELOP_ADD,      /* the area joins the selection */
  paintingdoc_SELOP_SUBTRACT  /* the area leaves the selection */
}
paintingdoc_selop_t;

/* select the canvas pixels in box (clipped to the canvas, x1/y1 exclusive).
 * an empty box is an area of nothing, so replacing with it selects nothing,
 * as paintingdoc_select_none */
result_t paintingdoc_select_rect(paintingdoc_t      *doc,
                                 box_t               box,
                                 paintingdoc_selop_t op);

/* select the area enclosed by the polygon of n points (even-odd rule; the
 * last point joins back to the first). fewer than 3 points, or an area with
 * no pixels in it, is an area of nothing */
result_t paintingdoc_select_poly(paintingdoc_t      *doc,
                                 const point_t      *pts,
                                 int                 n,
                                 paintingdoc_selop_t op);

/* select the pixels 4-connected to p whose every channel, alpha included, is
 * within tolerance (0..255) of p's: a magic wand. 0 takes only exact
 * matches, as paintingdoc_fill does. p off the canvas is an area of nothing */
result_t paintingdoc_select_similar(paintingdoc_t      *doc,
                                    point_t             p,
                                    int                 tolerance,
                                    paintingdoc_selop_t op);

/* select the whole canvas */
result_t paintingdoc_select_all(paintingdoc_t *doc);

/* select exactly the canvas pixels not selected now: with no selection, the
 * whole canvas; with the whole canvas selected, nothing */
result_t paintingdoc_select_invert(paintingdoc_t *doc);

/* drop the selection, so drawing reaches the whole canvas again */
void paintingdoc_select_none(paintingdoc_t *doc);

int paintingdoc_has_selection(const paintingdoc_t *doc);

/* the box (x1/y1 exclusive) enclosing the selected pixels; empty if none */
box_t paintingdoc_selection_bounds(const paintingdoc_t *doc);

/* the selection's boundary pixels as single-row boxes (x1/y1 exclusive), for
 * drawing its outline. *n is their number; a pixel may appear twice. valid
 * until the selection changes */
const box_t *paintingdoc_selection_outline(const paintingdoc_t *doc, int *n);

/* non-zero if there is a step to undo */
int paintingdoc_can_undo(const paintingdoc_t *doc);

/* restore the canvas to the last checkpoint, discarding it. returns the box
 * that changed (the whole canvas), or an empty box if there was nothing to
 * undo or the step could not be decompressed */
box_t paintingdoc_undo(paintingdoc_t *doc);

#endif /* TASKS_PAINTING_DOC_H */
