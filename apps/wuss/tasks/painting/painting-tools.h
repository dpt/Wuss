/* wuss/tasks/painting/painting-tools.h -- Painting's tools */

#ifndef TASKS_PAINTING_TOOLS_H
#define TASKS_PAINTING_TOOLS_H

#ifdef WUSS_APP

#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/point.h"

#include "painting.h"

/* what the current tool does: rubber-band, select, draw a path or collect
 * the points dragged through */
int painting_is_rubber_band(const painting_task_t *pt);
int painting_is_selector(const painting_task_t *pt);
int painting_is_path(const painting_task_t *pt);
int painting_collects_points(const painting_task_t *pt);

/* the rubber band's ends, given the keys held */
void painting_band_ends(const painting_task_t *pt, point_t *a, point_t *b);

/* grow the lasso's path by p; false if out of memory */
int painting_lasso_add(painting_task_t *pt, point_t p);

/* the box covering a and b, with room for a line a few pixels wide */
box_t painting_span_box(point_t a, point_t b);

/* redraw a box of the canvas */
void painting_invalidate_box(painting_task_t *pt, const box_t *box);

/* the colour a stroke in progress paints */
colour_t painting_stroke_colour(const painting_task_t *pt);

/* paint a freehand segment, or spray, returning the dirty box */
box_t painting_stroke_segment(painting_task_t *pt, point_t a, point_t b);
box_t painting_spray(painting_task_t *pt, point_t p);

/* the canvas boxes the rubber band, and the lasso or path's points, cover */
box_t painting_band_box(const painting_task_t *pt);
box_t painting_points_box(const painting_task_t *pt);

/* the selection operation the keys held ask for */
paintingdoc_selop_t painting_selop(const painting_task_t *pt);

/* finish a selection drag at p */
void painting_select_commit(painting_task_t *pt, point_t p);

/* draw the marching ants round the selection, the canvas at (ox, oy) */
void painting_draw_ants(const painting_task_t *pt,
                        screen_t              *scr,
                        int                    ox,
                        int                    oy);

/* draw the finished rubber band into the canvas, returning the dirty box */
box_t painting_band_commit(painting_task_t *pt);

/* place the lifted selection under p, returning the dirty box */
box_t painting_place(painting_task_t *pt, point_t p);

#endif /* WUSS_APP */

#endif /* TASKS_PAINTING_TOOLS_H */
