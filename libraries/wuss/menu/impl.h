/* wuss/menu/impl.h -- wuss pop-up menu helper, private to menu/ */

#ifndef WUSS_MENU_PRIVATE_H
#define WUSS_MENU_PRIVATE_H

#include "base/result.h"
#include "geom/point.h"

#include "wuss/icon.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

#include "../menu.h"

/* Row padding above/below the glyph, in pixels. The tick and submenu-arrow
 * gutters come from wuss__menu_entry_gutter_left/right, shared with the row
 * drawing. */
#define WUSS_MENU_ROW_PAD         4

/* All menu state that is per wuss_t. Core never sees these fields: it reaches
 * them only through the wuss__menu_* entry points in menu.h. */
struct wuss__menu_session
{
  /* Head (root) of the open pop-up menu chain, NULL when none open. */
  struct wuss__menu *chain;
  /* Internal task owning every borderless menu window; created lazily on
   * first wuss_menu_open, freed by wuss__menu_teardown. */
  wuss_task_t       *task;
  /* Non-zero only during wuss_menu_open_above: the root menu's bottom edge
   * sits this far above the screen bottom and it is centred on x. */
  int                bottom_gap;
  /* A menu opened on the last MOUSE_DOWN; swallow its matching MOUSE_UP so
   * the release does not immediately pick row 0. */
  int                eat_up;
};

/* ----- menu.c ----- */

/* Find the open chain node whose window is `window`, or NULL. */
struct wuss__menu *wuss__menu_node_for(wuss_t              *wuss,
                                       const wuss_window_t *window);

/* Close `node` and everything it opened, leaf-first. The caller clears any
 * parent's child pointer. */
void wuss__menu_close_from(struct wuss__menu *node);

/* Where a submenu (or a borrowed window) opens off row `icon` of `self`:
 * content top-left in screen space. */
point_t wuss__submenu_anchor(struct wuss__menu *self,
                             const wuss_icon_t *icon);

/* Open item `index`'s borrowed window as level `self->child`. */
result_t wuss__menu_open_window(struct wuss__menu *self, int index);

/* ----- menu-handle.c ----- */

/* The internal task that owns every borderless menu window, created on the
 * first wuss_menu_open of a session. */
wuss_task_t *wuss__menu_task(wuss_t *wuss);

/* ----- menu-spawn.c ----- */

/* Measure the menu, create its borderless window and one MENU_ENTRY icon per
 * item, and hang the node off *out. */
result_t wuss__menu_spawn(wuss_t             *wuss,
                          wuss_task_t        *owner,
                          const wuss_menu_t  *menu,
                          point_t             at,
                          struct wuss__menu  *parent,
                          struct wuss__menu **out);

#endif /* WUSS_MENU_PRIVATE_H */
