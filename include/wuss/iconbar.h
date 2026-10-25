/* wuss/iconbar.h -- wuss RISC OS-style icon bar */

/**
 * \file iconbar.h
 *
 * A RISC OS-style icon bar: a strip pinned to the bottom edge of the screen,
 * full width, always drawn on top of every window and never hit-tested as
 * part of the ordinary window stack. Any task can add an icon to it; a click
 * or drag is delivered to that icon's owning task as a task-view
 * wuss_EVENT_ICON (window == NULL, data.iconbar_icon), the same event kind a
 * work-area wuss_ICON_TYPE_ACTION icon raises in the window view.
 *
 * The bar itself always exists once the library is built with the
 * WUSS_ICONBAR option on -- there is no create/destroy call for the bar,
 * only for the icons on it. Icons are as wide as their label and sit 8
 * pixels apart, packed from the left or right edge as requested, ordered by
 * priority (higher further outward) or placed beside an existing icon; an
 * icon bar wider than the screen scrolls rather than wrapping.
 */

#ifndef WUSS_ICONBAR_H
#define WUSS_ICONBAR_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "base/result.h"
#include "framebuf/screen.h"

#include "wuss/menu.h"
#include "wuss/wuss.h"

/* The icon bar is a compile-time option (CMake WUSS_ICONBAR). With it off
 * the library has no wuss_iconbar_* symbols, so this header body is
 * skipped. */
#ifdef WUSS_ICONBAR

/* ----------------------------------------------------------------------- */

/* wuss_iconbar_icon_t (opaque) is forward-declared in wuss.h so it can be
 * named regardless of this option. */

/** Which end of the icon bar an icon is packed against. */
typedef enum wuss_iconbar_side
{
  /** Packed from the left edge, each new icon to the right of the last
   *  (the default). */
  wuss_ICONBAR_LEFT = 0,

  /** Packed from the right edge, each new icon to the left of the last. */
  wuss_ICONBAR_RIGHT
}
wuss_iconbar_side_t;

/** Where a priority scan starts along its side of the icon bar. */
typedef enum wuss_iconbar_scan
{
  /** From the bar's outer edge inward: the new icon goes inside every icon
   *  of equal or higher priority, so equals stack inward in creation order
   *  (the default). */
  wuss_ICONBAR_SCAN_FROM_EDGE = 0,

  /** From the middle of the bar outward: the new icon goes outside every
   *  icon of equal or lower priority. */
  wuss_ICONBAR_SCAN_FROM_MIDDLE
}
wuss_iconbar_scan_t;

/** Which side of its anchor an associated icon is placed, on screen. */
typedef enum wuss_iconbar_beside
{
  wuss_ICONBAR_LEFT_OF = 0,
  wuss_ICONBAR_RIGHT_OF
}
wuss_iconbar_beside_t;

/**
 * Description of an icon bar icon at creation. Copied by value into the
 * icon; the caller keeps ownership of \c text, which is copied.
 */
typedef struct wuss_iconbar_icon_spec
{
  /** NUL-terminated label; copied. NULL means "". */
  const char     *text;

  /** The image to draw, centred in the icon with no border. Borrowed, not
   *  copied; must outlive the icon. NULL (the default) draws just the label
   *  text. */
  const bitmap_t *image;

  /** Which end of the bar to place the icon at. */
  wuss_iconbar_side_t side;

  /** Placement priority: higher priorities sit further outward on their
   *  side. Ordinary applications use 0. Kept by the icon, so later scans
   *  compare against it. */
  int                 priority;

  /** Where the priority scan starts; decides where equal priorities go. */
  wuss_iconbar_scan_t scan;

  /** An icon already on the bar to place this one directly beside, or NULL
   *  (the default) to place by side, priority and scan. The new icon joins
   *  the anchor's side and takes its priority; the placement is fixed at
   *  creation, so destroying the anchor leaves it where it is. */
  const wuss_iconbar_icon_t *anchor;

  /** Which side of the anchor, on screen. Ignored without an anchor. */
  wuss_iconbar_beside_t beside;
}
wuss_iconbar_icon_spec_t;

/**
 * Add an icon to the icon bar. Without an anchor it goes on spec->side,
 * placed by spec->priority and spec->scan; with one, directly beside the
 * anchor. A zeroed spec (bar the label) packs from the left edge, each new
 * icon inside the last. The icon is as wide as its label (or image if wider;
 * 34 pixels with neither) and is 8 pixels from its neighbours and the bar's
 * edge. The bar is invalidated so the next redraw paints it.
 *
 * \param[in]  wuss  Window manager.
 * \param[in]  owner Task to receive this icon's wuss_EVENT_ICON events (as a
 *                   task-view event, window == NULL). Must not be NULL.
 * \param[in]  spec  Icon description; copied.
 * \param[out] icon  Filled in with the new icon handle, or NULL if the
 *                   caller does not need it.
 * \return \ref result_OK on success, \ref result_OOM on allocation failure,
 *         \ref result_BAD_ARG if spec->anchor is not on the bar.
 */
result_t wuss_iconbar_icon_create(wuss_t                         *wuss,
                                  wuss_task_t                    *owner,
                                  const wuss_iconbar_icon_spec_t *spec,
                                  wuss_iconbar_icon_t           **icon);

/**
 * Remove an icon from the icon bar, closing the gap and invalidating its
 * bounding box so the next redraw clears it. Safe to pass NULL for \p icon.
 *
 * \param[in] wuss Window manager.
 * \param[in] icon Icon to remove, or NULL.
 */
void wuss_iconbar_icon_destroy(wuss_t *wuss, wuss_iconbar_icon_t *icon);

#ifdef WUSS_MENUS
/**
 * Open \p menu for an icon bar icon, as wuss_menu_open_above placed at the
 * pointer's x, centred on it, with the bar's standard gap (47px) between the
 * menu's bottom edge and the bottom of the screen. Call it from the icon's
 * wuss_EVENT_ICON handler.
 *
 * \param[in]  task Task that receives the menu's events.
 * \param[in]  menu Menu to show; borrowed, must outlive the open chain.
 * \param[out] out  Filled with the chain handle, or NULL if not wanted.
 * \return \ref result_OK, \ref result_OOM, or a wuss_window_create code.
 */
result_t wuss_iconbar_menu_open(wuss_task_t        *task,
                                const wuss_menu_t  *menu,
                                wuss_menu_handle_t *out);

/**
 * Open the caller's own \p window where wuss_iconbar_menu_open would open a
 * menu: centred on the pointer's x with its bottom edge the bar's standard
 * gap above the bottom of the screen. It is shown and dismissed as a menu,
 * as wuss_menu_open_window. Suits a dialogue opened from an icon bar icon.
 *
 * \param[in]  task   Task that receives wuss_EVENT_MENU_CLOSED.
 * \param[in]  window Window to show; borrowed, must outlive the open chain.
 * \param[out] out    Filled with the chain handle, or NULL if not wanted.
 * \return \ref result_OK, \ref result_OOM, or a wuss_window_set_hidden code.
 */
result_t wuss_iconbar_window_open(wuss_task_t        *task,
                                  wuss_window_t      *window,
                                  wuss_menu_handle_t *out);
#endif /* WUSS_MENUS */

#endif /* WUSS_ICONBAR */

#ifdef __cplusplus
}
#endif

#endif /* WUSS_ICONBAR_H */
