/* wuss/gadget/nodeview.c -- a node graph editor on a caller's window */

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include <stdio.h>

#include "base/result.h"
#include "base/utils.h"
#include "datastruct/nodegraph.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/point.h"
#include "geom/size.h"

#include "wuss/window.h"

#include "wuss/gadget/nodeview.h"

#include "../core/types.h"
#include "../core/palette.h"
#include "../font/font.h"

/* ----------------------------------------------------------------------- */

#define NODEVIEW_NODE_W   112 /* node box width */
#define NODEVIEW_RADIUS   7   /* node box corner radius: 6 rasterises as a
                                 45-degree chamfer, so looks squared off */
#define NODEVIEW_PORT     8   /* port disc diameter */
#define NODEVIEW_PITCH    20  /* vertical spacing of input ports */
#define NODEVIEW_PAD      6   /* space round the title and the ports */
#define NODEVIEW_SLOP     2   /* extra pixels round a port that still hit */
#define NODEVIEW_DOC_W    1024
#define NODEVIEW_DOC_H    768

typedef enum nodeview_drag
{
  NODEVIEW_DRAG_NONE,
  NODEVIEW_DRAG_MOVE, /* moving 'node' */
  NODEVIEW_DRAG_LINK  /* dragging a link out of 'node' */
}
nodeview_drag_t;

struct wuss_nodeview
{
  alloc_t                      alloc;
  wuss_window_t               *window;
  nodegraph_t                 *graph;
  wuss_nodeview_activate_fn_t *activate;
  wuss_nodeview_changed_fn_t  *changed;
  void                        *opaque;
  nodegraph_node_t             selected;
  nodeview_drag_t              drag;
  nodegraph_node_t             drag_node;
  point_t                      drag_point; /* MOVE: grab offset; LINK: end */
  const colour_t              *type_colours; /* borrowed, or NULL */
  int                          ntype_colours;
};

/* ----------------------------------------------------------------------- */

static int nodeview_title_h(const wuss_nodeview_t *nv)
{
  return wuss__fontset_height(&nv->window->task->wuss->fonts, 0) +
         NODEVIEW_PAD;
}

/* Node's box in document coordinates. */
static box_t nodeview_node_box(const wuss_nodeview_t *nv, nodegraph_node_t n)
{
  const nodegraph_class_t *cls;
  int                      x, y;

  cls = nodegraph_class(nv->graph, n);
  (void) nodegraph_get_pos(nv->graph, n, &x, &y);
  return (box_t) BOX_POS_SIZE(x, y, NODEVIEW_NODE_W,
                              nodeview_title_h(nv) +
                              MAX(cls->ninputs, 1) * NODEVIEW_PITCH +
                              NODEVIEW_PAD * 2);
}

/* Centre of input port i, or of the output port when i is -1. */
static point_t nodeview_port(const wuss_nodeview_t *nv,
                             nodegraph_node_t       n,
                             int                    i)
{
  int x, y;

  (void) nodegraph_get_pos(nv->graph, n, &x, &y);
  y += nodeview_title_h(nv) + NODEVIEW_PAD + NODEVIEW_PITCH / 2;
  if (i < 0)
    return POINT(x + NODEVIEW_NODE_W, y);
  return POINT(x, y + i * NODEVIEW_PITCH);
}

static int nodeview_near(point_t port, point_t p)
{
  int r;

  r = NODEVIEW_PORT / 2 + NODEVIEW_SLOP;
  return p.x >= port.x - r && p.x < port.x + r &&
         p.y >= port.y - r && p.y < port.y + r;
}

/* Find what lies under p, topmost (latest drawn) first. Sets *port to the
 * input index, -1 for the output port or -2 for the body. */
static nodegraph_node_t nodeview_hit(const wuss_nodeview_t *nv,
                                     point_t                p,
                                     int                   *port)
{
  const nodegraph_class_t *cls;
  nodegraph_node_t         n, hit;
  box_t                    box;
  int                      i;

  hit   = nodegraph_NONE;
  *port = -2;
  for (n = nodegraph_next(nv->graph, nodegraph_NONE); n != nodegraph_NONE;
       n = nodegraph_next(nv->graph, n))
  {
    cls = nodegraph_class(nv->graph, n);
    if (cls->output_type != nodegraph_NO_OUTPUT &&
        nodeview_near(nodeview_port(nv, n, -1), p))
    {
      hit   = n;
      *port = -1;
      continue;
    }
    for (i = 0; i < cls->ninputs; i++)
      if (nodeview_near(nodeview_port(nv, n, i), p))
        break;
    if (i < cls->ninputs)
    {
      hit   = n;
      *port = i;
      continue;
    }
    box = nodeview_node_box(nv, n);
    if (box_contains_point(&box, p.x, p.y))
    {
      hit   = n;
      *port = -2;
    }
  }
  return hit;
}

/* ponytail: every change repaints the whole visible area; invalidate just
 * the node and link bounds if large graphs flicker */
static void nodeview_invalidate(wuss_nodeview_t *nv)
{
  wuss_window_invalidate_visible(nv->window);
}

/* The colour of ports and links of port type, else fallback. */
static colour_t nodeview_type_colour(const wuss_nodeview_t *nv,
                                     int                    type,
                                     colour_t               fallback)
{
  if (type < 0 || type >= nv->ntype_colours)
    return fallback;
  return nv->type_colours[type];
}

/* ----------------------------------------------------------------------- */

static void nodeview_draw_port(screen_t *scr,
                               point_t   origin,
                               point_t   c,
                               colour_t  colour)
{
  screen_fill_circle(scr, origin.x + c.x, origin.y + c.y, NODEVIEW_PORT / 2,
                     colour);
}

/* Fill the part of the rounded node box that lies within part. */
static void nodeview_fill_part(const screen_t *scr,
                               const box_t    *box,
                               const box_t    *part,
                               colour_t        colour)
{
  screen_t clipped;

  clipped = *scr;
  if (box_intersection(&scr->clip, part, &clipped.clip))
    return;
  screen_fill_rounded_rect(&clipped, box->x0, box->y0,
                           SIZE2D(box->x1 - box->x0, box->y1 - box->y0),
                           NODEVIEW_RADIUS, colour);
}

static void nodeview_draw_node(wuss_nodeview_t    *nv,
                               const wuss_event_t *event,
                               point_t             origin,
                               nodegraph_node_t    n)
{
  wuss_t                  *wuss;
  screen_t                *scr;
  const nodegraph_class_t *cls;
  bmfont_t                *font;
  box_t                    box, title, body, clip;
  screen_t                 clipped;
  colour_t                 fg, title_bg;
  point_t                  pos;
  int                      ascent, i;
  char                     number[12];

  wuss = nv->window->task->wuss;
  scr  = event->data.redraw.scr;
  cls  = nodegraph_class(nv->graph, n);
  font = wuss->fonts.fonts[0];
  fg   = wuss->palette[wuss->button.fg];

  box = nodeview_node_box(nv, n);
  box_translated(&box, origin.x, origin.y, &box);
  if (box_intersection(&scr->clip, &box, &clip))
    return;

  title_bg = n == nv->selected ? wuss->palette[wuss->accent]
                               : wuss->palette[wuss->button.pressed];
  title    = box;
  title.y1 = box.y0 + nodeview_title_h(nv);
  body     = box;
  body.y0  = title.y1;
  nodeview_fill_part(scr, &box, &title, title_bg);
  nodeview_fill_part(scr, &box, &body, wuss->palette[wuss->button.bg]);
  screen_draw_rounded_rect(scr, box.x0, box.y0,
                           SIZE2D(box.x1 - box.x0, box.y1 - box.y0),
                           NODEVIEW_RADIUS, wuss->palette[wuss->bevel.dark]);

  if (font != NULL && !box_intersection(&scr->clip, &title, &clip))
  {
    clipped      = *scr;
    clipped.clip = clip;
    bmfont_get_info(font, NULL, NULL, &ascent, NULL);
    pos.x = title.x0 + NODEVIEW_PAD;
    pos.y = title.y0 + NODEVIEW_PAD / 2 + ascent;
    (void) wuss__text_draw_str(wuss, font, &clipped, cls->name, fg,
                               wuss__text_ground(wuss, title_bg), &pos,
                               NULL);

    /* the handle, never reused, numbers the node at the title's right */
    snprintf(number, sizeof(number), "%d", n + 1);
    pos.x = title.x1 - NODEVIEW_PAD - wuss__text_width(font, number);
    (void) wuss__text_draw_str(wuss, font, &clipped, number, fg,
                               wuss__text_ground(wuss, title_bg), &pos,
                               NULL);
  }

  for (i = 0; i < cls->ninputs; i++)
    nodeview_draw_port(scr, origin, nodeview_port(nv, n, i),
                       nodeview_type_colour(nv, cls->input_types[i], fg));
  if (cls->output_type != nodegraph_NO_OUTPUT)
    nodeview_draw_port(scr, origin, nodeview_port(nv, n, -1),
                       nodeview_type_colour(nv, cls->output_type, fg));
}

static void nodeview_draw_line(screen_t *scr,
                               point_t   origin,
                               point_t   a,
                               point_t   b,
                               colour_t  colour)
{
  screen_draw_line(scr, a.x + origin.x, a.y + origin.y, b.x + origin.x,
                   b.y + origin.y, colour);
}

static void nodeview_redraw(wuss_nodeview_t *nv, const wuss_event_t *event)
{
  wuss_t                  *wuss;
  screen_t                *scr;
  const nodegraph_class_t *cls;
  point_t                  origin;
  colour_t                 fg;
  nodegraph_node_t         n, src;
  int                      i;

  wuss   = nv->window->task->wuss;
  scr    = event->data.redraw.scr;
  origin = POINT(event->data.redraw.bounds->x0 - event->data.redraw.scroll.x,
                 event->data.redraw.bounds->y0 - event->data.redraw.scroll.y);
  fg     = wuss->palette[wuss->button.fg];

  wuss__fill_colour(wuss, scr, event->data.redraw.content,
                    wuss->palette[wuss->body.window], origin);

  /* links under the nodes */
  for (n = nodegraph_next(nv->graph, nodegraph_NONE); n != nodegraph_NONE;
       n = nodegraph_next(nv->graph, n))
  {
    cls = nodegraph_class(nv->graph, n);
    for (i = 0; i < cls->ninputs; i++)
    {
      src = nodegraph_source(nv->graph, n, i);
      if (src != nodegraph_NONE)
        nodeview_draw_line(scr, origin, nodeview_port(nv, src, -1),
                           nodeview_port(nv, n, i),
                           nodeview_type_colour(nv, cls->input_types[i],
                                                fg));
    }
  }

  for (n = nodegraph_next(nv->graph, nodegraph_NONE); n != nodegraph_NONE;
       n = nodegraph_next(nv->graph, n))
    nodeview_draw_node(nv, event, origin, n);

  if (nv->drag == NODEVIEW_DRAG_LINK)
    nodeview_draw_line(scr, origin, nodeview_port(nv, nv->drag_node, -1),
                       nv->drag_point, wuss->palette[wuss->accent]);
}

/* ----------------------------------------------------------------------- */

static void nodeview_changed(wuss_nodeview_t *nv)
{
  if (nv->changed)
    nv->changed(nv, nv->opaque);
}

static int nodeview_down(wuss_nodeview_t *nv,
                         point_t          p,
                         wuss_button_t    button)
{
  nodegraph_node_t n, src;
  int              port, x, y;

  if (!(button & wuss_BUTTON_SELECT))
    return 0;

  n = nodeview_hit(nv, p, &port);
  if (n == nodegraph_NONE)
  {
    wuss_nodeview_select(nv, nodegraph_NONE);
    return 1;
  }

  if (port == -1)
  {
    nv->drag       = NODEVIEW_DRAG_LINK;
    nv->drag_node  = n;
    nv->drag_point = p;
  }
  else if (port >= 0)
  {
    /* lift the link off this input, to drop elsewhere */
    src = nodegraph_source(nv->graph, n, port);
    if (src == nodegraph_NONE)
      return 1;
    (void) nodegraph_unlink(nv->graph, n, port);
    nodeview_changed(nv);
    nv->drag       = NODEVIEW_DRAG_LINK;
    nv->drag_node  = src;
    nv->drag_point = p;
  }
  else
  {
    wuss_nodeview_select(nv, n);
    if (button & wuss_BUTTON_DOUBLE)
    {
      nv->drag = NODEVIEW_DRAG_NONE;
      if (nv->activate)
        nv->activate(nv, n, nv->opaque);
      return 1;
    }
    (void) nodegraph_get_pos(nv->graph, n, &x, &y);
    nv->drag       = NODEVIEW_DRAG_MOVE;
    nv->drag_node  = n;
    nv->drag_point = POINT(p.x - x, p.y - y);
  }
  nodeview_invalidate(nv);
  return 1;
}

static int nodeview_move(wuss_nodeview_t *nv, point_t p)
{
  switch (nv->drag)
  {
  case NODEVIEW_DRAG_MOVE:
    (void) nodegraph_set_pos(nv->graph, nv->drag_node,
                             CLAMP(p.x - nv->drag_point.x, 0,
                                   NODEVIEW_DOC_W - NODEVIEW_NODE_W),
                             CLAMP(p.y - nv->drag_point.y, 0,
                                   NODEVIEW_DOC_H - NODEVIEW_PITCH));
    break;

  case NODEVIEW_DRAG_LINK:
    nv->drag_point = p;
    break;

  default:
    return 0;
  }
  nodeview_invalidate(nv);
  return 1;
}

static int nodeview_up(wuss_nodeview_t *nv, point_t p)
{
  nodegraph_node_t n;
  int              port;

  if (nv->drag == NODEVIEW_DRAG_NONE)
    return 0;

  if (nv->drag == NODEVIEW_DRAG_LINK)
  {
    /* a refused link (wrong type, a cycle) is simply not made */
    n = nodeview_hit(nv, p, &port);
    if (n != nodegraph_NONE && port >= 0 &&
        nodegraph_link(nv->graph, nv->drag_node, n, port) == result_OK)
      nodeview_changed(nv);
  }
  nv->drag = NODEVIEW_DRAG_NONE;
  nodeview_invalidate(nv);
  return 1;
}

/* ----------------------------------------------------------------------- */

result_t wuss_nodeview_create(wuss_nodeview_t            **out,
                              wuss_window_t               *window,
                              nodegraph_t                 *graph,
                              wuss_nodeview_activate_fn_t *activate,
                              wuss_nodeview_changed_fn_t  *changed,
                              void                        *opaque)
{
  wuss_t          *wuss;
  wuss_nodeview_t *nv;

  if (out == NULL || window == NULL || graph == NULL)
    return result_NULL_ARG;

  wuss = window->task->wuss;

  nv = wuss->alloc.malloc(sizeof(*nv));
  if (nv == NULL)
    return result_OOM;

  nv->alloc         = wuss->alloc;
  nv->window        = window;
  nv->graph         = graph;
  nv->activate      = activate;
  nv->changed       = changed;
  nv->opaque        = opaque;
  nv->selected      = nodegraph_NONE;
  nv->drag          = NODEVIEW_DRAG_NONE;
  nv->drag_node     = nodegraph_NONE;
  nv->type_colours  = NULL;
  nv->ntype_colours = 0;

  /* cannot fail: both sizes are positive */
  (void) wuss_window_set_doc(window, SIZE2D(NODEVIEW_DOC_W, NODEVIEW_DOC_H));

  *out = nv;
  return result_OK;
}

void wuss_nodeview_destroy(wuss_nodeview_t *doomed)
{
  if (doomed == NULL)
    return;

  doomed->alloc.free(doomed);
}

int wuss_nodeview_handle_event(wuss_nodeview_t    *nv,
                               const wuss_event_t *event)
{
  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    nodeview_redraw(nv, event);
    return 1;

  case wuss_EVENT_MOUSE:
    switch (event->data.mouse.action)
    {
    case wuss_MOUSE_DOWN:
      return nodeview_down(nv, event->data.mouse.point,
                           event->data.mouse.button);
    case wuss_MOUSE_MOVE:
      return nodeview_move(nv, event->data.mouse.point);
    case wuss_MOUSE_UP:
      return nodeview_up(nv, event->data.mouse.point);
    }
    return 0;

  default:
    return 0;
  }
}

void wuss_nodeview_refresh(wuss_nodeview_t *nv)
{
  if (nodegraph_class(nv->graph, nv->selected) == NULL)
    nv->selected = nodegraph_NONE;
  if (nodegraph_class(nv->graph, nv->drag_node) == NULL)
    nv->drag = NODEVIEW_DRAG_NONE;
  nodeview_invalidate(nv);
}

nodegraph_node_t wuss_nodeview_get_selected(const wuss_nodeview_t *nv)
{
  return nv->selected;
}

nodegraph_node_t wuss_nodeview_node_at(const wuss_nodeview_t *nv, point_t p)
{
  int port;

  return nodeview_hit(nv, p, &port);
}

void wuss_nodeview_select(wuss_nodeview_t *nv, nodegraph_node_t node)
{
  if (node == nv->selected)
    return;

  nv->selected = node;
  nodeview_invalidate(nv);
}

void wuss_nodeview_set_type_colours(wuss_nodeview_t *nv,
                                    const colour_t  *colours,
                                    int              ncolours)
{
  nv->type_colours  = colours;
  nv->ntype_colours = colours != NULL ? ncolours : 0;
  nodeview_invalidate(nv);
}
