/* wuss/window/attach.c -- attach toolbar and pane windows to a parent window */

#include <assert.h>
#include <stddef.h>

#include "base/utils.h"
#include "geom/box.h"

#include "wuss/window.h"

#include "../impl.h"

/* Furniture a toolbar may have: nothing the user could move, resize or
 * close it with. */
#define TOOLBAR_FLAGS (wuss_WINDOW_NO_TITLEBAR    | wuss_WINDOW_NO_OUTLINE  | \
                       wuss_WINDOW_VSCROLL        | wuss_WINDOW_HSCROLL     | \
                       wuss_WINDOW_NO_RESIZE_BLIT | wuss_WINDOW_HIDDEN      | \
                       wuss_WINDOW_NO_REDRAW      | wuss_WINDOW_FOCUSABLE)

/* Furniture a pane must have: none at all. */
#define PANE_NEEDS (wuss_WINDOW_NO_TITLEBAR | wuss_WINDOW_NO_OUTLINE)

/* Flags a pane may have besides. */
#define PANE_FLAGS (PANE_NEEDS                 | wuss_WINDOW_HIDDEN    | \
                    wuss_WINDOW_NO_RESIZE_BLIT | wuss_WINDOW_NO_REDRAW | \
                    wuss_WINDOW_FOCUSABLE)

/* ----------------------------------------------------------------------- */

/* How far "toolbar" overlaps "inner", the window it stacks against: by the
 * thinner of their outlines, so that the two outlines draw as one line. */
static int overlap(const wuss_window_t *toolbar, const wuss_window_t *inner)
{
  return MIN(wuss__outline_px(toolbar), wuss__outline_px(inner));
}

/* Non-zero if "toolbar" takes room in its edge's stack. A pane hidden only
 * along with its parent keeps its room, so the parent's content stays the
 * same size while hidden. */
static int takes_room(const wuss_window_t *toolbar,
                      const wuss_window_t *reveal)
{
  return !(toolbar->flags & wuss_WINDOW_HIDDEN) || toolbar == reveal ||
         (wuss__window_is_pane(toolbar) &&
          (toolbar->state & wuss_WINDOW_STATE_PARENT_HIDDEN));
}

/* Lay out the toolbars of "parent" on one edge. Along the edge each one's
 * content area is aligned against the parent's content area, so their
 * outlines line up; across it they stack outwards
 * from the parent's footprint, each overlapping the outline of the window
 * inside it. The whole stack is clamped as one block so
 * that it stays on the screen, overlapping the parent if needs be, but once
 * the parent's edge itself is off the screen the block goes with it. */
static void layout_edge(wuss_window_t *parent,
                        wuss_window_t *reveal,
                        wuss_edge_t    edge)
{
  wuss_window_t *tb;
  int            horizontal;
  wuss_window_t *inner;
  box_t          content;
  int            total, start, used, thick, length, along;
  int            screen_far, near, far;
  box_t          tb_content;
  int            lead, ov;
  point_t        p;

  horizontal = (edge == wuss_EDGE_LEFT || edge == wuss_EDGE_RIGHT);
  /* aligned against the box the panes carve, so panes don't move them */
  wuss__inner_box(parent, &content);

  /* total is the room the block takes outside the parent's footprint */
  total = 0;
  inner = parent;
  for (tb = wuss__next_member(parent->toolbar.first, 0); tb != NULL;
       tb = wuss__next_member(tb->toolbar.next, 0))
  {
    if (tb->toolbar.edge != edge || !takes_room(tb, reveal))
      continue;

    total += (horizontal ? tb->visible.x1 - tb->visible.x0
                         : tb->visible.y1 - tb->visible.y0) - overlap(tb, inner);
    inner  = tb;
  }

  if (horizontal)
  {
    near       = parent->visible.x0;
    far        = parent->visible.x1;
    screen_far = parent->wuss->scr->size.w;
  }
  else
  {
    near       = parent->visible.y0;
    far        = parent->visible.y1;
    screen_far = parent->wuss->scr->size.h;
  }

  /* outer start of the block on a near edge, inner start on a far one */
  if (edge == wuss_EDGE_LEFT || edge == wuss_EDGE_TOP)
    start = MAX(near - total, MIN(0, near));
  else
    start = MAX(MIN(far, screen_far - total), far - total);

  used  = 0;
  inner = parent;
  for (tb = wuss__next_member(parent->toolbar.first, 0); tb != NULL;
       tb = wuss__next_member(tb->toolbar.next, 0))
  {
    if (tb->toolbar.edge != edge || !takes_room(tb, reveal))
      continue;

    wuss__content_box(tb, &tb_content);

    /* length is the toolbar's content length along the edge, lead the
     * furniture before its content */
    if (horizontal)
    {
      thick  = tb->visible.x1 - tb->visible.x0;
      length = tb_content.y1 - tb_content.y0;
      lead   = tb_content.y0 - tb->visible.y0;
      if (tb->toolbar.align & wuss_ALIGN_TOP)
        along = content.y0;
      else if (tb->toolbar.align & wuss_ALIGN_BOTTOM)
        along = content.y1 - length;
      else
        along = (content.y0 + content.y1 - length) / 2;
    }
    else
    {
      thick  = tb->visible.y1 - tb->visible.y0;
      length = tb_content.x1 - tb_content.x0;
      lead   = tb_content.x0 - tb->visible.x0;
      if (tb->toolbar.align & wuss_ALIGN_LEFT)
        along = content.x0;
      else if (tb->toolbar.align & wuss_ALIGN_RIGHT)
        along = content.x1 - length;
      else
        along = (content.x0 + content.x1 - length) / 2;
    }
    along += tb->toolbar.offset - lead;

    /* p is the toolbar's footprint top-left; first attached is innermost */
    ov = overlap(tb, inner);
    if (edge == wuss_EDGE_LEFT || edge == wuss_EDGE_TOP)
      p.x = start + total - used - thick + ov;
    else
      p.x = start + used - ov;
    p.y = along;
    if (!horizontal)
      SWAP(p.x, p.y);
    used  += thick - ov;
    inner  = tb;

    wuss__window_move(tb, POINT(p.x + tb_content.x0 - tb->visible.x0,
                                p.y + tb_content.y0 - tb->visible.y0));
  }
}

/* Put "pane" at "box". A pure shift is a move, which carries the parent's
 * own cached layout along with it. Anything else sets the footprint
 * directly, without wuss_window_resize's screen cap, as the pane's size is
 * the parent's to choose; its divider is drawn by the parent, so that is
 * repainted where it was and where it goes. */
static void place_pane(wuss_window_t *parent,
                       wuss_window_t *pane,
                       const box_t   *box)
{
  int   hidden;
  box_t before;
  box_t divider;

  if (box_equal(&pane->visible, box))
    return;

  if (box->x1 - box->x0 == pane->visible.x1 - pane->visible.x0 &&
      box->y1 - box->y0 == pane->visible.y1 - pane->visible.y0)
  {
    wuss__window_move(pane, POINT(box->x0, box->y0));
    return;
  }

  hidden = (parent->flags & wuss_WINDOW_HIDDEN) != 0;
  before = pane->visible;

  if (!hidden)
  {
    wuss__pane_divider_box(&before, pane->toolbar.edge, &divider);
    wuss__invalidate_clipped(parent, &divider);
    if (!(pane->flags & wuss_WINDOW_HIDDEN))
      wuss__invalidate_clipped(pane, &before);
  }

  pane->visible = *box;
  pane->scroll  = wuss__scroll_clamp(pane, pane->scroll);
  wuss__chrome_invalidate_layout(pane);
  wuss__notify_open(pane);

  if (!hidden)
  {
    wuss__pane_divider_box(box, pane->toolbar.edge, &divider);
    wuss__invalidate_clipped(parent, &divider);
    if (!(pane->flags & wuss_WINDOW_HIDDEN))
      wuss__invalidate_clipped(pane, box);
  }
  wuss__chrome_invalidate_layout(parent);
}

/* Lay out the panes of "parent". Each takes a strip of its own thickness,
 * plus a divider, off its edge of whatever the panes before it left,
 * running the full length of that; the content is what remains. */
static void layout_panes(wuss_window_t *parent, wuss_window_t *reveal)
{
  box_t          inner;
  size2d_t       min;
  box_t          rest;
  wuss_window_t *pane;
  int            horizontal;
  int            thick;
  box_t          box;
  box_t          panes;

  wuss__inner_box(parent, &inner);
  wuss__min_content(parent, &min);

  rest = inner;
  for (pane = wuss__next_member(parent->toolbar.first, 1); pane != NULL;
       pane = wuss__next_member(pane->toolbar.next, 1))
  {
    if (!takes_room(pane, reveal))
      continue;

    horizontal = (pane->toolbar.edge == wuss_EDGE_LEFT ||
                  pane->toolbar.edge == wuss_EDGE_RIGHT);

    /* ponytail: a pane grown or shown in a parent too small for it is
     * thinned so that the content keeps its minimum, and stays thinned;
     * remember the asked-for thickness if that bites */
    if (horizontal)
      thick = MIN(pane->visible.x1 - pane->visible.x0,
                  rest.x1 - rest.x0 - WUSS__PANE_DIVIDER_PX - min.w);
    else
      thick = MIN(pane->visible.y1 - pane->visible.y0,
                  rest.y1 - rest.y0 - WUSS__PANE_DIVIDER_PX - min.h);
    thick = MAX(thick, 1);

    box = rest;
    switch (pane->toolbar.edge)
    {
    case wuss_EDGE_LEFT:
      box.x1  = rest.x0 + thick;
      rest.x0 = box.x1 + WUSS__PANE_DIVIDER_PX;
      break;
    case wuss_EDGE_RIGHT:
      box.x0  = rest.x1 - thick;
      rest.x1 = box.x0 - WUSS__PANE_DIVIDER_PX;
      break;
    case wuss_EDGE_TOP:
      box.y1  = rest.y0 + thick;
      rest.y0 = box.y1 + WUSS__PANE_DIVIDER_PX;
      break;
    default: /* wuss_EDGE_BOTTOM */
      box.y0  = rest.y1 - thick;
      rest.y1 = box.y0 - WUSS__PANE_DIVIDER_PX;
      break;
    }

    place_pane(parent, pane, &box);
  }

  /* the one-pixel floor above can still overrun a parent with no room */
  rest.x1 = MAX(rest.x1, rest.x0);
  rest.y1 = MAX(rest.y1, rest.y0);

  panes.x0 = rest.x0 - inner.x0;
  panes.y0 = rest.y0 - inner.y0;
  panes.x1 = inner.x1 - rest.x1;
  panes.y1 = inner.y1 - rest.y1;
  if (box_equal(&panes, &parent->panes))
    return;

  /* the content moved under the panes: keep the scroll offset, so the
   * document slides with the content's edge, and repaint the lot along with
   * the scrollbars, whose sausages track the content size */
  parent->panes  = panes;
  parent->scroll = wuss__scroll_clamp(parent, parent->scroll);
  if (!(parent->flags & wuss_WINDOW_HIDDEN))
  {
    wuss__invalidate_clipped(parent, &inner);
    wuss__chrome_repaint(parent);
  }
  else
  {
    wuss__chrome_invalidate_layout(parent);
  }

  wuss__notify_open(parent);
}

void wuss__toolbars_layout(wuss_window_t *parent, wuss_window_t *reveal)
{
  int edge;

  /* run even with no toolbars left, so the last pane's room is given back */
  layout_panes(parent, reveal);

  for (edge = wuss_EDGE_LEFT; edge <= wuss_EDGE_BOTTOM; edge++)
    layout_edge(parent, reveal, (wuss_edge_t) edge);
}

/* ----------------------------------------------------------------------- */

/* Relink "toolbar" into the parent's stack, directly above it. */
static void link_above(wuss_window_t *toolbar, wuss_window_t *parent)
{
  wuss_t *wuss;
  list_t *prev;

  wuss = parent->wuss;

  list_remove(&wuss->z_order[toolbar->stack], &toolbar->link);
  toolbar->stack = parent->stack;

  for (prev = &wuss->z_order[parent->stack];
       prev->next != &parent->link;
       prev = prev->next)
    ;

  toolbar->link.next = &parent->link;
  prev->next         = &toolbar->link;
}

/* Non-zero if "toolbar" may not be attached to "parent" at all, as either
 * kind. */
static int cannot_attach(const wuss_window_t *toolbar,
                         const wuss_window_t *parent)
{
  return toolbar == parent               ||
         toolbar->task != parent->task   ||
         toolbar->toolbar.parent != NULL ||
         toolbar->toolbar.first  != NULL ||
         parent->toolbar.parent  != NULL;
}

/* Attach checked "toolbar" to the end of the parent's list and lay it out. */
static void attach(wuss_window_t *toolbar,
                   wuss_window_t *parent,
                   wuss_edge_t    edge,
                   wuss_align_t   align,
                   int            offset)
{
  wuss_window_t **tail;
  int             hidden;
  wuss_window_t  *tb;

  hidden = (toolbar->flags & wuss_WINDOW_HIDDEN) != 0;

  /* repaint wherever it was; it is painted afresh in place below */
  if (!hidden)
    wuss__invalidate_clipped(toolbar, &toolbar->visible);

  toolbar->toolbar.parent = parent;
  toolbar->toolbar.next   = NULL;
  toolbar->toolbar.edge   = edge;
  toolbar->toolbar.align  = align;
  toolbar->toolbar.offset = offset;

  for (tail = &parent->toolbar.first; *tail != NULL;
       tail = &(*tail)->toolbar.next)
    ;
  *tail = toolbar;

  /* panes are part of the parent, so stay below its toolbars */
  link_above(toolbar, parent);
  if (!wuss__window_is_pane(toolbar))
    for (tb = wuss__next_member(parent->toolbar.first, 1); tb != NULL;
         tb = wuss__next_member(tb->toolbar.next, 1))
      link_above(tb, parent);
  wuss__toolbars_layout(parent, NULL);

  if (!hidden)
    wuss__invalidate_clipped(toolbar, &toolbar->visible);
}

result_t wuss_window_attach(wuss_window_t *toolbar,
                            wuss_window_t *parent,
                            wuss_edge_t    edge,
                            wuss_align_t   align,
                            int            offset)
{
  assert(toolbar != NULL);
  assert(parent  != NULL);
  assert((int) edge >= wuss_EDGE_LEFT && (int) edge <= wuss_EDGE_BOTTOM);

  if (cannot_attach(toolbar, parent)                ||
      !(toolbar->flags & wuss_WINDOW_NO_TITLEBAR) ||
      (toolbar->flags & ~TOOLBAR_FLAGS) != 0)
    return result_BAD_ARG;

  attach(toolbar, parent, edge, align, offset);

  return result_OK;
}

result_t wuss_window_attach_pane(wuss_window_t *pane,
                                 wuss_window_t *parent,
                                 wuss_edge_t    edge)
{
  box_t    content;
  size2d_t min;
  int      room;

  assert(pane   != NULL);
  assert(parent != NULL);
  assert((int) edge >= wuss_EDGE_LEFT && (int) edge <= wuss_EDGE_BOTTOM);

  if (cannot_attach(pane, parent)                ||
      (pane->flags & PANE_NEEDS) != PANE_NEEDS ||
      (pane->flags & ~PANE_FLAGS) != 0)
    return result_BAD_ARG;

  /* room is what the content would keep across the edge, less its floor */
  wuss__content_box(parent, &content);
  wuss__min_content(parent, &min);
  if (edge == wuss_EDGE_LEFT || edge == wuss_EDGE_RIGHT)
    room = (content.x1 - content.x0) - (pane->visible.x1 - pane->visible.x0) - min.w;
  else
    room = (content.y1 - content.y0) - (pane->visible.y1 - pane->visible.y0) - min.h;
  if (room < WUSS__PANE_DIVIDER_PX)
    return result_WUSS_TOO_SMALL;

  pane->state |= wuss_WINDOW_STATE_PANE;
  attach(pane, parent, edge, wuss_ALIGN_CENTRE, 0);

  return result_OK;
}

void wuss_window_detach(wuss_window_t *toolbar)
{
  wuss_window_t  *parent;
  wuss_window_t **link;

  parent = toolbar->toolbar.parent;
  if (parent == NULL)
    return;

  for (link = &parent->toolbar.first; *link != toolbar;
       link = &(*link)->toolbar.next)
    ;
  *link = toolbar->toolbar.next;

  toolbar->toolbar.parent = NULL;
  toolbar->toolbar.next   = NULL;
  toolbar->state &= (wuss_window_state_t) ~(wuss_WINDOW_STATE_PARENT_HIDDEN |
                                            wuss_WINDOW_STATE_PANE);

  wuss__toolbars_layout(parent, NULL);
}
