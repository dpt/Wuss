/* wuss/gadget/nodeview.h -- a node graph editor on a caller's window */

/**
 * \file nodeview.h
 *
 * A wuss gadget: draws a DPTLib node graph (see datastruct/nodegraph.h) on a
 * caller's window and edits its layout and links. The task must forward
 * wuss_EVENT_REDRAW and wuss_EVENT_MOUSE to \ref wuss_nodeview_handle_event.
 *
 * Each node is a rounded box titled with its class name and numbered, at the
 * title's right, by its handle plus one, which the graph never reuses, its
 * input ports as discs on its left edge and its output port on its right; a
 * sink (output type nodegraph_NO_OUTPUT) has none. Links are straight lines
 * from output to input. Ports and links can be coloured by type; see \ref
 * wuss_nodeview_set_type_colours.
 *
 * SELECT on a node selects it and drags it about. SELECT on an output port
 * drags out a link, made when released over an input port of the same type;
 * SELECT on a linked input port lifts its link off, to drop elsewhere or
 * nowhere. A double-click on a node calls the activate callback. SELECT on
 * empty space clears the selection.
 *
 * The gadget never adds or removes nodes; the task does that, then calls
 * \ref wuss_nodeview_refresh. It does not evaluate the graph either: the
 * changed callback tells the task when links changed.
 *
 * Built only when WUSS_GADGETS is defined.
 */

#ifndef WUSS_GADGET_NODEVIEW_H
#define WUSS_GADGET_NODEVIEW_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "base/result.h"
#include "datastruct/nodegraph.h"
#include "framebuf/colour.h"

#include "wuss/task.h"
#include "wuss/window.h"

/* ----------------------------------------------------------------------- */

/** Opaque handle: the gadget's state and selection. */
typedef struct wuss_nodeview wuss_nodeview_t;

/**
 * Activate callback: the user double-clicked a node.
 *
 * \param[in] nodeview The gadget.
 * \param[in] node     The node.
 * \param[in] opaque   As passed to \ref wuss_nodeview_create.
 */
typedef void (wuss_nodeview_activate_fn_t)(wuss_nodeview_t *nodeview,
                                           nodegraph_node_t node,
                                           void            *opaque);

/**
 * Changed callback: the user made or broke a link, so the graph wants
 * evaluating.
 *
 * \param[in] nodeview The gadget.
 * \param[in] opaque   As passed to \ref wuss_nodeview_create.
 */
typedef void (wuss_nodeview_changed_fn_t)(wuss_nodeview_t *nodeview,
                                          void            *opaque);

/* ----------------------------------------------------------------------- */

/**
 * Create a node view of \p graph on \p window, with nothing selected. Sets
 * the window's document extent.
 *
 * \param[out] out      Filled with the new handle on success.
 * \param[in]  window   Window to draw on.
 * \param[in]  graph    Graph to show and edit. Must outlive the gadget.
 * \param[in]  activate Called on a double-click on a node, or NULL.
 * \param[in]  changed  Called when the user changes a link, or NULL.
 * \param[in]  opaque   Passed back to the callbacks.
 * \return \ref result_OK, \ref result_NULL_ARG or \ref result_OOM.
 */
result_t wuss_nodeview_create(wuss_nodeview_t            **out,
                              wuss_window_t               *window,
                              nodegraph_t                 *graph,
                              wuss_nodeview_activate_fn_t *activate,
                              wuss_nodeview_changed_fn_t  *changed,
                              void                        *opaque);

/**
 * Free a node view. Safe to pass NULL. The graph is left alone.
 *
 * \param[in] doomed Handle to free, or NULL.
 */
void wuss_nodeview_destroy(wuss_nodeview_t *doomed);

/**
 * Offer an event from the task's own handler.
 *
 * \param[in] nodeview Handle.
 * \param[in] event    The event, of any kind.
 * \return 1 if the event was consumed, else 0. MENU clicks are never
 *         consumed, so the task can open its menu.
 */
int wuss_nodeview_handle_event(wuss_nodeview_t    *nodeview,
                               const wuss_event_t *event);

/**
 * Redraw after the task changed the graph, e.g. added or removed a node.
 * Drops the selection if its node has gone.
 *
 * \param[in] nodeview Handle.
 */
void wuss_nodeview_refresh(wuss_nodeview_t *nodeview);

/**
 * \param[in] nodeview Handle.
 * \return The selected node, or nodegraph_NONE.
 */
nodegraph_node_t wuss_nodeview_get_selected(const wuss_nodeview_t *nodeview);

/**
 * Find the node under a point, as SELECT would, e.g. to open a menu for the
 * node under a MENU click.
 *
 * \param[in] nodeview Handle.
 * \param[in] p        Point in window-local content (document) coordinates,
 *                     as a wuss_EVENT_MOUSE's point.
 * \return The topmost node whose box or ports contain p, or nodegraph_NONE.
 */
nodegraph_node_t wuss_nodeview_node_at(const wuss_nodeview_t *nodeview,
                                       point_t                p);

/**
 * Select a node, or nodegraph_NONE for none.
 *
 * \param[in] nodeview Handle.
 * \param[in] node     Node to select.
 */
void wuss_nodeview_select(wuss_nodeview_t *nodeview, nodegraph_node_t node);

/**
 * Colour ports and links by port type: a port or link of type t is drawn in
 * colours[t]. Types outside 0..ncolours-1 use the default colour.
 *
 * \param[in] nodeview Handle.
 * \param[in] colours  Colours indexed by port type, or NULL for none. Must
 *                     outlive the gadget, or be replaced first.
 * \param[in] ncolours Number of colours.
 */
void wuss_nodeview_set_type_colours(wuss_nodeview_t *nodeview,
                                    const colour_t  *colours,
                                    int              ncolours);

#ifdef __cplusplus
}
#endif

#endif /* WUSS_GADGET_NODEVIEW_H */
