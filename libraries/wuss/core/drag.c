/* wuss/drag.c -- wuss - minimal window manager */

#include <assert.h>

#include "impl.h"

/* Invalidate just the four 1px edges of "box", not its filled interior --
 * cheap enough to call twice a frame (old position, new position) without
 * repainting whatever the ants box covers each time. */
static void invalidate_edges(wuss_t *wuss, const box_t *box)
{
  if (box_is_empty(box))
    return;

  wuss_invalidate(wuss, &(box_t) BOX_POS_SIZE(box->x0, box->y0,
                                              box->x1 - box->x0, 1));
  wuss_invalidate(wuss, &(box_t) BOX_POS_SIZE(box->x0, box->y1 - 1,
                                              box->x1 - box->x0, 1));
  wuss_invalidate(wuss, &(box_t) BOX_POS_SIZE(box->x0, box->y0,
                                              1, box->y1 - box->y0));
  wuss_invalidate(wuss, &(box_t) BOX_POS_SIZE(box->x1 - 1, box->y0,
                                              1, box->y1 - box->y0));
}

/* Whether the drag shows its solid image rather than the ants. Read live, so
 * wuss_set_config can flip it mid-drag (it invalidates the whole screen). */
static int drag_is_solid(const wuss_t *wuss)
{
  return wuss->drag.draw != NULL && !wuss->input.outline_drags;
}

/* Invalidate what the drag paints at "box": all of it for a solid image, only
 * the edges for the ants. */
static void invalidate_drawn(wuss_t *wuss, const box_t *box)
{
  if (drag_is_solid(wuss))
    wuss_invalidate(wuss, box);
  else
    invalidate_edges(wuss, box);
}

static void box_for_pointer(const wuss_t *wuss, point_t p, box_t *out)
{
  *out = (box_t) BOX_POS_SIZE(p.x - wuss->drag.hotspot.x,
                              p.y - wuss->drag.hotspot.y,
                              wuss->drag.size.w, wuss->drag.size.h);
}

static void draw_bitmap(screen_t *scr, const box_t *box, void *opaque)
{
  (void) screen_copy_bitmap(scr, box->x0, box->y0, opaque);
}

static result_t drag_start(wuss_t              *wuss,
                           wuss_window_t       *window,
                           size2d_t             size,
                           point_t              hotspot,
                           wuss_drag_draw_fn_t *draw,
                           void                *opaque)
{
  assert(wuss != NULL);

  if (window == NULL || window->task->handle == NULL ||
      size.w <= 0 || size.h <= 0)
    return result_BAD_ARG;

  /* the drag owns the pointer from here: a press that was captured by the
   * window starting the drag must not keep intercepting moves and the release */
  wuss->capture = NULL;

  wuss->drag.window  = window;
  wuss->drag.button  = wuss_BUTTON_NONE; /* set by the icon drag caller */
  wuss->drag.size    = size;
  wuss->drag.hotspot = hotspot;
  wuss->drag.frame   = 0;
  wuss->drag.draw    = draw;
  wuss->drag.opaque  = opaque;
  box_for_pointer(wuss, wuss->pointer, &wuss->drag.box);
  wuss->drag.drawn = wuss->drag.box;
  invalidate_drawn(wuss, &wuss->drag.drawn);

  return result_OK;
}

result_t wuss_drag_start(wuss_t        *wuss,
                         wuss_window_t *window,
                         size2d_t       size,
                         point_t        hotspot)
{
  return drag_start(wuss, window, size, hotspot, NULL, NULL);
}

result_t wuss_drag_start_bitmap(wuss_t         *wuss,
                                wuss_window_t  *window,
                                const bitmap_t *bitmap,
                                point_t         hotspot)
{
  if (bitmap == NULL)
    return result_BAD_ARG;

  /* the drawer only reads the bitmap: the cast just fits the opaque slot */
  return drag_start(wuss, window, bitmap->size, hotspot,
                    draw_bitmap, (void *) bitmap);
}

result_t wuss_drag_start_object(wuss_t              *wuss,
                                wuss_window_t       *window,
                                size2d_t             size,
                                point_t              hotspot,
                                wuss_drag_draw_fn_t *draw,
                                void                *opaque)
{
  if (draw == NULL)
    return result_BAD_ARG;

  return drag_start(wuss, window, size, hotspot, draw, opaque);
}

/* Called from mouse-move.c on every move while a drag is active: recompute
 * the ants box at the new pointer position. Nothing is invalidated here --
 * several moves can land in one frame, and invalidating every intermediate
 * box would overflow the dirty list into one big merged region; the next
 * wuss__drag_tick catches the painted box up instead. */
void wuss__drag_move(wuss_t *wuss, point_t p)
{
  box_for_pointer(wuss, p, &wuss->drag.box);
}

/* Called from idle.c once a frame while a drag is active: step the ants one
 * phase on and move them to the latest box, repainting only the edges they
 * leave and the edges they now sit on (the same four when the box has not
 * moved -- wuss_invalidate drops the repeats). A solid image does not march,
 * so it repaints only when it has moved, and then its whole old and new
 * boxes. */
void wuss__drag_tick(wuss_t *wuss)
{
  wuss->drag.frame++;
  if (drag_is_solid(wuss) &&
      box_equal(&wuss->drag.drawn, &wuss->drag.box))
    return;

  invalidate_drawn(wuss, &wuss->drag.drawn);
  wuss->drag.drawn = wuss->drag.box;
  invalidate_drawn(wuss, &wuss->drag.drawn);
}

/* Called from mouse-click.c (a MOUSE_UP) and key.c (Escape) to end an active
 * drag: settles deferred POINTER_ENTER/EXIT, invalidates the ants box's last
 * position and delivers wuss_EVENT_DRAG_END. "drop" is the window under the
 * pointer, ignored (and reported NULL) when cancelled. */
void wuss__drag_end(wuss_t        *wuss,
                    point_t        p,
                    wuss_window_t *drop,
                    int            cancelled)
{
  wuss_window_t *window;
  wuss_event_t   event;

  window = wuss->drag.window;

  event.data.drag_end.drop_icon = NULL;
#ifdef WUSS_ICONBAR
  if (!cancelled && drop != NULL && drop == wuss->iconbar.window)
    event.data.drag_end.drop_icon = wuss__iconbar_hit_test(wuss, p);
#endif

  invalidate_drawn(wuss, &wuss->drag.drawn);
  wuss->drag.window = NULL;

  wuss__pointer_set_window(wuss, cancelled ? NULL : drop);

  event.kind                  = wuss_EVENT_DRAG_END;
  event.data.drag_end.drop    = cancelled ? NULL : drop;
  event.data.drag_end.point   = p;
  event.data.drag_end.cancelled = cancelled;
  event.data.drag_end.button  = wuss->drag.button;
  (void) wuss__deliver(window->task, window, &event);
}

/* Called from wuss_window_close: abandon an active drag whose source window
 * is closing. No wuss_EVENT_DRAG_END -- the task hears about the window
 * going through PRE_CLOSE/CLOSE, and may already have been sent QUIT. */
void wuss__drag_forget_window(wuss_t *wuss, wuss_window_t *window)
{
  if (wuss->drag.window != window)
    return;

  invalidate_drawn(wuss, &wuss->drag.drawn);
  wuss->drag.window = NULL;
}

/* Called from redraw.c, painted last so the drag sits over every window. A
 * solid image is handed to its drawer, clipped to its box. Otherwise each edge starts its dash pattern "phase" steps in, walking the pattern
 * one step further round the box every frame so it appears to march;
 * corners keep the phase continuous rather than each edge restarting at 0. */
void wuss__drag_draw(wuss_t *wuss)
{
  colour_t black;
  box_t    box, saved_clip, clipped;
  int      period, phase;

  if (wuss->drag.window == NULL)
    return;

  black  = wuss->palette[wuss__resolve_colour(wuss, wuss_COLOUR_BLACK)];
  box    = wuss->drag.drawn;
  period = WUSS_DRAG_ANTS_ON + WUSS_DRAG_ANTS_OFF;
  phase  = wuss->drag.frame % period;

  /* Each dash pattern is anchored outside "box" (box.x0 - phase, etc.) so it
   * marches continuously; screen_draw_dashed_line only clips to the screen
   * and the caller's existing clip, neither of which stops it painting past
   * the box's own edge into pixels invalidate_edges() never marks dirty.
   * Pin the clip to box, intersected with whatever the caller (wuss_redraw /
   * wuss_redraw_dirty) already set, and restore it afterwards. */
  saved_clip = wuss->scr->clip;
  if (box_intersection(&box, &saved_clip, &clipped))
    return; /* box is wholly outside the redraw region */
  wuss->scr->clip = clipped;

  if (drag_is_solid(wuss))
  {
    wuss->drag.draw(wuss->scr, &box, wuss->drag.opaque);
    wuss->scr->clip = saved_clip;
    return;
  }

  screen_draw_dashed_line(wuss->scr, box.x0 - phase, box.y0, box.x1 - 1, box.y0,
                          WUSS_DRAG_ANTS_ON, WUSS_DRAG_ANTS_OFF, black);
  screen_draw_dashed_line(wuss->scr, box.x1 - 1 + phase, box.y1 - 1, box.x0, box.y1 - 1,
                          WUSS_DRAG_ANTS_ON, WUSS_DRAG_ANTS_OFF, black);
  screen_draw_dashed_line(wuss->scr, box.x0, box.y1 - 1 + phase, box.x0, box.y0,
                          WUSS_DRAG_ANTS_ON, WUSS_DRAG_ANTS_OFF, black);
  screen_draw_dashed_line(wuss->scr, box.x1 - 1, box.y0 - phase, box.x1 - 1, box.y1 - 1,
                          WUSS_DRAG_ANTS_ON, WUSS_DRAG_ANTS_OFF, black);

  wuss->scr->clip = saved_clip;
}
