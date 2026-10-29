/* wuss/window/move.c -- wuss - minimal window manager */

#include "../impl.h"

/* p is the window's content top-left; the furniture offset (outline plus
 * any titlebar, plus any left and top panes) is constant for a given window, so the footprint just
 * follows it. The cached furniture layout is translated in place below
 * rather than invalidated -- a pure move changes no piece's size, so there
 * is nothing to rebuild. */
void wuss__window_move(wuss_window_t *window, point_t p)
{
  box_t clean[WUSS_MAX_INVALIDATE_PIECES];
  box_t full_dest[WUSS_MAX_INVALIDATE_PIECES];
  box_t copied[WUSS_MAX_INVALIDATE_PIECES];
  int   width, height, outline_px, titlebar_height;
  int   dx, dy, nclean, ncopied, i;
  box_t before, dirty;

  if (window->flags & wuss_WINDOW_PINNED)
    return;

  width           = window->visible.x1 - window->visible.x0;
  height          = window->visible.y1 - window->visible.y0;
  outline_px      = wuss__outline_px(window);
  titlebar_height = wuss__titlebar_height(window);
  before          = window->visible;

  /* work from the inner box's top-left, outside any panes */
  p.x -= window->panes.x0;
  p.y -= window->panes.y0;

  /* a drag delivers one call per pointer-move event, not one per actual
   * change of position -- a window pinned against the screen edge, or a
   * backend that reports redundant motion, can call this with the same
   * target it's already at. width/height never change here, so an
   * unchanged origin means an unchanged box; skip the layout translate and
   * blit/invalidate machinery entirely. */
  if (p.x - outline_px == before.x0 &&
      p.y - outline_px - titlebar_height == before.y0)
    return;

  /* a hidden window has nothing on screen to slide and must paint nothing;
   * just translate its footprint so it is in place when shown again */
  if (window->flags & wuss_WINDOW_HIDDEN)
  {
    /* nothing is drawn from the cache while hidden, but the next unhide must
     * not paint it back at the old position, so just drop it -- there's no
     * screen-visible dirty region to queue either way */
    wuss__chrome_invalidate_layout(window);

    window->visible.x0 = p.x - outline_px;
    window->visible.y0 = p.y - outline_px - titlebar_height;
    window->visible.x1 = window->visible.x0 + width;
    window->visible.y1 = window->visible.y0 + height;
    wuss__notify_open(window);
    return;
  }

  /* The clean (non-occluded) pieces of "before" are genuinely this
   * window's own rendering; whatever isn't clean is hidden behind some
   * other window and has no valid pixels of this window's content to
   * slide. Computed against the current z-order, before the move. */
  nclean = wuss__clip_to_visible(window, &before, clean, 0);

  /* A piece can also be "clean" by occlusion yet still not show valid
   * pixels on screen: an earlier move (or any other invalidation) this same
   * frame may have queued part of it in wuss->dirty.box[] without wuss_redraw_
   * dirty having run yet to actually repaint it -- e.g. several batched
   * pointer-move events arriving before the next redraw, which pushes a
   * window on and off the screen edge repeatedly. Sliding that stale ground
   * would just paste it, untouched, onto the window's new position. Strip
   * every pending-dirty region out of "clean" first so only pixels already
   * settled on screen are treated as a valid blit source. */
  nclean = wuss__filter_settled(clean, nclean, window->wuss->dirty.box,
                                window->wuss->dirty.n);

  window->visible.x0 = p.x - outline_px;
  window->visible.y0 = p.y - outline_px - titlebar_height;
  window->visible.x1 = window->visible.x0 + width;
  window->visible.y1 = window->visible.y0 + height;

  wuss__notify_open(window);

  dx = window->visible.x0 - before.x0;
  dy = window->visible.y0 - before.y0;

  /* a pure translation leaves every cached rect correct relative to the
   * window, just offset in screen space -- shift it instead of dropping the
   * cache and rebuilding all pieces on the next paint of even a thin
   * sliver */
#ifdef WUSS_FURNITURE
  if (window->furniture_layout.flags & wuss_FURNITURE_LAYOUT__VALID)
    wuss__furniture_layout_translate(window, dx, dy);
#endif

  for (i = 0; i < nclean; i++)
    box_translated(&clean[i], dx, dy, &full_dest[i]);

  /* Slide the clean pieces by (dx, dy); wuss__blit_pieces clips each
   * destination clear of the windows above this one -- an occluder there
   * hasn't moved, so its pixels are already correct and pasting stale ones
   * over them would just have to be repainted straight back. It also
   * repairs any piece that slid partly off-screen. */
  ncopied = 0;
  if (nclean > 0 &&
      wuss__blit_pieces(window, clean, nclean, dx, dy, NULL, copied, &ncopied))
  {
    box_t hidden[WUSS_MAX_INVALIDATE_PIECES];
    int   nhidden;

    /* Each clean piece is, by construction, clear of any occluder at its
     * old position, so the vacated sliver left behind by sliding it to its
     * full (untrimmed) new position is safe to invalidate raw, without
     * re-checking occlusion -- regardless of whether every pixel of that
     * new position actually got a blit above: the part that landed under an
     * occluder was skipped there, but the old position is vacated either
     * way.
     *
     * The sliver is clean[i] minus *every* clean piece's destination, not
     * just its own: with an occluder biting a corner out of "before", one
     * clean piece can slide onto ground another clean piece just vacated
     * (e.g. a full-width bottom band vacated straight into the destination
     * of the right-side band on a downward drag). That overlap already got
     * valid pixels from the other piece's blit, so invalidating it would
     * just repaint good pixels. */
    for (i = 0; i < nclean; i++)
    {
      box_t sliver[WUSS_MAX_INVALIDATE_PIECES];
      int   nsliver, s;

      /* result only feeds wuss_invalidate, so on overflow overpaint_safe
       * lets wuss__subtract_boxes fall back to clean[i] whole rather than
       * dropping pieces -- over-invalidating, not under. */
      nsliver = wuss__subtract_boxes(&clean[i], full_dest, nclean, sliver, 1);
      for (s = 0; s < nsliver; s++)
        wuss_invalidate(window->wuss, &sliver[s]);
    }

    /* Whatever of "before" wasn't clean has no valid source pixels: its
     * translated destination needs a genuine repaint, clipped against
     * whatever's above this window there now. */
    nhidden = wuss__subtract_boxes(&before, clean, nclean, hidden, 1);
    for (i = 0; i < nhidden; i++)
    {
      box_t hidden_dest;

      box_translated(&hidden[i], dx, dy, &hidden_dest);
      wuss__invalidate_clipped(window, &hidden_dest);
    }
  }
  else
  {
    /* Nothing of "before" was clean, splitting overflowed the piece budget,
     * the pieces would have clobbered each other, or the blit was declined:
     * fall back to a normal clipped redraw of the whole moved footprint. */
    logf_warning("wuss_window_move: blit fast path failed, repainting the "
                 "whole footprint (nclean=%d)", nclean);
    box_union(&before, &window->visible, &dirty);
    wuss__invalidate_clipped(window, &dirty);
  }

  (void) ncopied; /* move.c repairs via the sliver logic above, not "copied" */
}

/* ----------------------------------------------------------------------- */

/* The member of the toolbar group of "parent" after "member": the parent
 * first, then its toolbars and panes in attach order. */
static wuss_window_t *next_member(wuss_window_t *parent,
                                  wuss_window_t *member)
{
  return member == parent ? parent->toolbar.first : member->toolbar.next;
}

/* Shift the footprint of "window" by (dx, dy) without touching the screen. */
static void shift(wuss_window_t *window, int dx, int dy)
{
  box_translated(&window->visible, dx, dy, &window->visible);
  wuss__notify_open(window);

  if (window->flags & wuss_WINDOW_HIDDEN)
    wuss__chrome_invalidate_layout(window);
#ifdef WUSS_FURNITURE
  else if (window->furniture_layout.flags & wuss_FURNITURE_LAYOUT__VALID)
    wuss__furniture_layout_translate(window, dx, dy);
#endif
}

/* Slide "parent" and its whole toolbar group by (dx, dy) as one. Moving the
 * members one at a time would have the parent's move invalidate the ground
 * under its toolbars, which then have no settled pixels left to slide and
 * repaint whole. Mirrors wuss__window_move, with the clean pieces of every
 * member pooled into one blit. The group is contiguous in the z-order, so
 * its frontmost member's occluders are the group's. */
static void move_group(wuss_window_t *parent, int dx, int dy)
{
  box_t          src[WUSS_MAX_INVALIDATE_PIECES];
  box_t          dest[WUSS_MAX_INVALIDATE_PIECES];
  box_t          copied[WUSS_MAX_INVALIDATE_PIECES];
  box_t          pieces[WUSS_MAX_INVALIDATE_PIECES];
  wuss_t        *wuss;
  wuss_window_t *top;
  wuss_window_t *m;
  int            nsrc, npieces, ncopied, blitted, i, j;
  box_t          before, moved;

  wuss = parent->wuss;

  for (top = wuss__z_first(wuss); wuss__toolbar_root(top) != parent;
       top = wuss__z_below(top))
    ;

  /* a piece that doesn't fit is left out of the blit and repainted below */
  nsrc = 0;
  for (m = parent; m != NULL; m = next_member(parent, m))
  {
    if (m->flags & wuss_WINDOW_HIDDEN)
      continue;

    npieces = wuss__clip_to_visible(m, &m->visible, pieces, 0);
    npieces = wuss__filter_settled(pieces, npieces, wuss->dirty.box,
                                   wuss->dirty.n);
    for (i = 0; i < npieces && nsrc < WUSS_MAX_INVALIDATE_PIECES; i++)
      src[nsrc++] = pieces[i];
  }

  for (m = parent; m != NULL; m = next_member(parent, m))
    shift(m, dx, dy);

  for (i = 0; i < nsrc; i++)
    box_translated(&src[i], dx, dy, &dest[i]);

  blitted = nsrc > 0 &&
            wuss__blit_pieces(top, src, nsrc, dx, dy, NULL, copied, &ncopied);
  if (!blitted && nsrc > 0)
    logf_warning("wuss_window_move: group blit failed, repainting the whole "
                 "footprint (nsrc=%d)", nsrc);

  for (m = parent; m != NULL; m = next_member(parent, m))
  {
    if (m->flags & wuss_WINDOW_HIDDEN)
      continue;

    box_translated(&m->visible, -dx, -dy, &before);
    if (!blitted)
    {
      box_union(&before, &m->visible, &moved);
      wuss__invalidate_clipped(m, &moved);
      continue;
    }

    /* what no member had settled pixels for needs a genuine repaint */
    npieces = wuss__subtract_boxes(&before, src, nsrc, pieces, 1);
    for (i = 0; i < npieces; i++)
    {
      box_translated(&pieces[i], dx, dy, &moved);
      wuss__invalidate_clipped(m, &moved);
    }
  }

  if (!blitted)
    return;

  /* the vacated slivers, as in wuss__window_move */
  for (i = 0; i < nsrc; i++)
  {
    npieces = wuss__subtract_boxes(&src[i], dest, nsrc, pieces, 1);
    for (j = 0; j < npieces; j++)
      wuss_invalidate(wuss, &pieces[j]);
  }
}

/* A toolbar only moves with its parent; a parent takes its toolbars along. */
void wuss_window_move(wuss_window_t *window, point_t p)
{
  int dx, dy;

  if (window->toolbar.parent != NULL)
    return;

  if (window->toolbar.first == NULL ||
      (window->flags & wuss_WINDOW_PINNED))
  {
    wuss__window_move(window, p);
    wuss__toolbars_layout(window, NULL);
    return;
  }

  /* p is the content top-left, as for wuss__window_move */
  dx = p.x - window->panes.x0 - wuss__outline_px(window) - window->visible.x0;
  dy = p.y - window->panes.y0 - wuss__outline_px(window) -
       wuss__titlebar_height(window) - window->visible.y0;
  if (dx == 0 && dy == 0)
    return;

  move_group(window, dx, dy);

  /* re-clamp toolbars against the screen edges; a no-op for any already in
   * place */
  wuss__toolbars_layout(window, NULL);
}
