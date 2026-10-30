/* wuss/gadget/tagcloud.h -- a cloud or list of tags on a caller's window */

/**
 * \file tagcloud.h
 *
 * A wuss gadget: a set of named tags, each with a use count, drawn on a
 * caller's window either packed into centred lines (a cloud) or in columns
 * (a list). Ported from PrivateEye's AppEngine tag-cloud gadget.
 *
 * Highlighted tags are drawn bold in the accent colour. With scaling on,
 * each tag's count puts it in one of five bands, drawn from faint to full
 * strength; bitmap fonts cannot scale, so colour stands in for size. The tag
 * under the pointer is underlined. A Select or Adjust click on a tag toggles
 * its highlight and calls the toggle callback.
 *
 * The gadget owns no icons and draws its tags itself, so the task must
 * forward wuss_EVENT_REDRAW, wuss_EVENT_MOUSE, wuss_EVENT_OPEN,
 * wuss_EVENT_POINTER_EXIT, wuss_EVENT_PALETTE and wuss_EVENT_RELAYOUT to
 * \ref wuss_tagcloud_handle_event. The layout reflows to the window's
 * visible width on wuss_EVENT_OPEN and the gadget sets the window's document
 * extent to match.
 *
 * Built only when WUSS_GADGETS is defined.
 */

#ifndef WUSS_GADGET_TAGCLOUD_H
#define WUSS_GADGET_TAGCLOUD_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "base/result.h"
#include "geom/point.h"

#include "wuss/window.h"
#include "wuss/wuss.h"

/* ----------------------------------------------------------------------- */

/** Opaque handle: the gadget's tags, layout and options. */
typedef struct wuss_tagcloud wuss_tagcloud_t;

/** One tag, as passed to \ref wuss_tagcloud_set_tags. */
typedef struct wuss_tagcloud_tag
{
  const char *name;        /**< Tag name; copied by the gadget */
  int         count;       /**< Use count; at least 0 */
  int         highlighted; /**< Non-zero to draw the tag highlighted */
}
wuss_tagcloud_tag_t;

/** How tags are laid out. */
typedef enum wuss_tagcloud_display
{
  wuss_TAGCLOUD_DISPLAY_CLOUD, /**< Packed into centred lines */
  wuss_TAGCLOUD_DISPLAY_LIST   /**< In columns of the widest tag */
}
wuss_tagcloud_display_t;

/** The order tags are laid out in. */
typedef enum wuss_tagcloud_sort
{
  wuss_TAGCLOUD_SORT_NAME, /**< By name, ignoring case */
  wuss_TAGCLOUD_SORT_COUNT /**< By count, highest first */
}
wuss_tagcloud_sort_t;

/** Display options, set with \ref wuss_tagcloud_set_options. */
typedef struct wuss_tagcloud_options
{
  wuss_tagcloud_display_t display;
  wuss_tagcloud_sort_t    sort;
  int                     selected_first; /**< Highlighted tags lead */
  int                     scaling;        /**< Shade tags by count */
}
wuss_tagcloud_options_t;

/**
 * Toggle callback: a click changed tag \p index's highlight.
 *
 * \param[in] tagcloud    The gadget.
 * \param[in] index       The tag's index, as passed to \ref
 *                        wuss_tagcloud_set_tags.
 * \param[in] highlighted Its new highlight state.
 * \param[in] opaque      As passed to \ref wuss_tagcloud_create.
 */
typedef void (wuss_tagcloud_toggle_fn_t)(wuss_tagcloud_t *tagcloud,
                                         int              index,
                                         int              highlighted,
                                         void            *opaque);

/* ----------------------------------------------------------------------- */

/**
 * Create a tag cloud on \p window, with no tags, displayed as a cloud sorted
 * by name with scaling on.
 *
 * \param[out] out    Filled with the new handle on success.
 * \param[in]  window Window to draw the tags on.
 * \param[in]  toggle Called when a click toggles a highlight, or NULL.
 * \param[in]  opaque Passed back to \p toggle.
 * \return \ref result_OK, \ref result_NULL_ARG if \p out or \p window is
 *         NULL, or \ref result_OOM.
 */
result_t wuss_tagcloud_create(wuss_tagcloud_t          **out,
                              wuss_window_t             *window,
                              wuss_tagcloud_toggle_fn_t *toggle,
                              void                      *opaque);

/**
 * Free a tag cloud. Safe to pass NULL.
 *
 * \param[in] doomed Handle to free, or NULL.
 */
void wuss_tagcloud_destroy(wuss_tagcloud_t *doomed);

/**
 * Replace the tags, copying them. Reflows and redraws.
 *
 * \param[in] tagcloud Handle.
 * \param[in] tags     Array of \p ntags tags.
 * \param[in] ntags    Number of tags; at least 0.
 * \return \ref result_OK, \ref result_BAD_ARG if \p ntags is negative, or
 *         \ref result_OOM, which leaves the previous tags in place.
 */
result_t wuss_tagcloud_set_tags(wuss_tagcloud_t           *tagcloud,
                                const wuss_tagcloud_tag_t *tags,
                                int                        ntags);

/**
 * Set or clear tag \p index's highlight. Reflows and redraws. An
 * out-of-range index is ignored.
 *
 * \param[in] tagcloud    Handle.
 * \param[in] index       Tag index.
 * \param[in] highlighted Non-zero to highlight the tag.
 */
void wuss_tagcloud_set_highlight(wuss_tagcloud_t *tagcloud,
                                 int              index,
                                 int              highlighted);

/**
 * Set the display options. Reflows and redraws.
 *
 * \param[in] tagcloud Handle.
 * \param[in] options  The new options.
 */
void wuss_tagcloud_set_options(wuss_tagcloud_t               *tagcloud,
                               const wuss_tagcloud_options_t *options);

/**
 * Find the tag under a point.
 *
 * \param[in] tagcloud Handle.
 * \param[in] point    Point in document coordinates.
 * \return Tag index, or -1 if there is no tag there.
 */
int wuss_tagcloud_hit_test(const wuss_tagcloud_t *tagcloud, point_t point);

/**
 * Offer an event from the task's own handler.
 *
 * \param[in] tagcloud Handle.
 * \param[in] event    The event, of any kind.
 * \return 1 if the event was consumed, else 0. A REDRAW is consumed; a
 *         Select or Adjust MOUSE DOWN is consumed when it hits a tag.
 */
int wuss_tagcloud_handle_event(wuss_tagcloud_t    *tagcloud,
                               const wuss_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* WUSS_GADGET_TAGCLOUD_H */
