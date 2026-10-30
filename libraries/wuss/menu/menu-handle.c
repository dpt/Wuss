/* wuss/menu/menu-handle.c -- wuss pop-up menu window delegate */

#include <stddef.h>

#include "base/result.h"
#include "geom/box.h"
#include "geom/point.h"

#include "wuss/icon.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

#include "../core/types.h"
#include "../core/geometry.h"
#include "../core/input.h"
#include "../core/windows.h"

#include "impl.h"

/* SELECT-pick flash: toggle the row highlight every WUSS_MENU_FLASH_PERIOD
 * IDLE frames, for WUSS_MENU_FLASH_FRAMES frames total. At ~60 Hz that is
 * three quick on/off blinks in about 0.2s -- the RISC OS feel. */
#define WUSS_MENU_FLASH_FRAMES  12
#define WUSS_MENU_FLASH_PERIOD  2

/* ----------------------------------------------------------------------- */

/* True if the wuss pointer sits over `window`'s chrome above its content area
 * -- the titlebar strip -- and `window` is the frontmost window there. Core
 * routes furniture hovers straight to the window manager and delivers no event
 * to the menu delegate (see wuss_mouse_move), so a titlebar hover on a parent
 * level cannot be caught in the ICON handler; the IDLE tick polls this instead
 * to close a submenu the pointer has slid up onto the parent's titlebar to
 * reach (e.g. to drag the parent). The frontmost check stops a child menu
 * dragged back over its parent's titlebar -- the child window is then on top
 * at that point -- from being read as a parent-titlebar hover and closed. */
static int wuss__pointer_over_titlebar(const wuss_window_t *window)
{
  point_t p;
  box_t   content;

  p = wuss_get_pointer(window->wuss);
  wuss__content_box(window, &content);

  return box_contains_point(&window->visible, p.x, p.y)
      && p.y < content.y0
      && wuss__window_at(window->wuss, p) == window;
}

/* True if the wuss pointer currently sits over icon `icon` in `window`. */
static int wuss__pointer_over_icon(wuss_window_t     *window,
                                   const wuss_icon_t *icon)
{
  return wuss__icon_hit_test(window,
                             wuss__screen_to_doc(window,
                                                 wuss_get_pointer(window->wuss)))
      == icon;
}

/* True if the wuss pointer sits over the submenu-arrow gutter of row `icon`
 * in `window`: the pointer is within the row vertically and inside the
 * rightmost wuss__menu_entry_gutter_right pixels of it. A submenu opens only
 * from here, so re-entering the parent anywhere else closes the child. */
static int wuss__pointer_over_row_arrow(wuss_window_t     *window,
                                        const wuss_icon_t *icon)
{
  point_t doc;
  box_t   bbox;
  int     gutter;

  doc = wuss__screen_to_doc(window, wuss_get_pointer(window->wuss));

  wuss_icon_get_bbox(icon, &bbox);
  gutter = wuss__menu_entry_gutter_right(
             wuss__fontset_height(&window->wuss->fonts, 0));

  return doc.y >= bbox.y0 && doc.y < bbox.y1
      && doc.x >= bbox.x1 - gutter && doc.x < bbox.x1;
}

/* ----------------------------------------------------------------------- */

/* End the pick flash on `self` now: leave the flashed row un-highlit unless the
 * pointer is still over it (on an ADJUST re-pick no MOUSE_MOVE follows to bring
 * the highlight back), then deliver the MENU_SELECT the flash stands in for --
 * tearing the chain down first unless the pick was an ADJUST (keep_open). On
 * a non-keep_open return `self` has been freed. Callers guard against calling
 * this with no flash armed. */
static void wuss__menu_flash_finish(struct wuss__menu *self)
{
  wuss_t            *wuss      = self->wuss;
  struct wuss__menu *root;
  wuss_icon_t       *row       = self->icons[self->flash.index];
  wuss_task_t       *owner     = self->flash.owner;
  const wuss_menu_t *menu      = self->flash.menu;
  int                index     = self->flash.index;
  wuss_button_t      button    = self->flash.button;
  int                keep_open = (self->flags & wuss_MENU__FLASH_KEEP_OPEN) != 0;
  int                on;

  self->flash.frames = 0;

  on = keep_open && wuss__pointer_over_icon(self->window, row);
  wuss__icon_set_state(row, wuss_ICON_STATE_HOVERED, on);
  wuss__icon_invalidate(self->window, row);

  root = self;
  while (root->parent != NULL)
    root = root->parent;

  if (!keep_open)
  {
    wuss->menu->chain = NULL;
    wuss__menu_close_from(root); /* frees `self` */

    /* The chain is gone and the owner's wuss_menu_handle_t with it: tell it
     * so, same as wuss__menu_abandon, before MENU_SELECT below -- otherwise
     * the handle sits stale until a later wuss_menu_close (e.g. from the
     * owner's own QUIT handler) walks freed nodes. */
    (void) wuss__menu_deliver_closed(owner, 1);
  }

  (void) wuss__menu_deliver_select(owner, menu, index, button);
}

/* Advance a running pick flash by one IDLE frame; hand off to
 * wuss__menu_flash_finish when it runs out. `self` must be a real
 * (non-borrowed) level. */
static void wuss__menu_flash_step(struct wuss__menu *self)
{
  wuss_icon_t *row;

  if (self->flash.frames <= 0)
    return;

  row = self->icons[self->flash.index];

  self->flash.frames--;

  if (self->flash.frames == 0)
  {
    wuss__menu_flash_finish(self); /* delivers MENU_SELECT; may free `self` */
    return;
  }

  /* toggle the highlight at each period boundary; the pick frame already did
   * the first (off) blink, so phase here starts the row coming back on */
  if (self->flash.frames % WUSS_MENU_FLASH_PERIOD == 0)
  {
    int on = ((self->flash.frames / WUSS_MENU_FLASH_PERIOD) & 1) == 0;

    wuss__icon_set_state(row, wuss_ICON_STATE_HOVERED, on);
    wuss__icon_invalidate(self->window, row);
  }
}

/* ----------------------------------------------------------------------- */

/* The menu window's task delegate. Shared across every borderless menu
 * window; the firing level is located by its window. */
static result_t wuss__menu_handle(wuss_window_t      *window,
                                  const wuss_event_t *event,
                                  void               *task_data)
{
  struct wuss__menu      *self;
  const wuss_icon_t      *icon;
  const wuss_menu_item_t *item;
  wuss_t                 *wuss;
  int                     index;
  int                     i;

  /* IDLE arrives with window == NULL; task_data is the wuss_t (see
   * wuss__menu_task). Drive any running SELECT-pick flash off it, and close a
   * submenu the pointer has slid up onto its parent's titlebar. */
  if (event->kind == wuss_EVENT_IDLE)
  {
    struct wuss__menu *node;

    wuss = task_data;

    for (node = wuss->menu->chain; node != NULL; node = node->child)
      if (!(node->flags & wuss_MENU__BORROWED) && node->flash.frames > 0)
      {
        wuss__menu_flash_step(node);
        break; /* node may be freed; the chain is gone if the flash ended */
      }

    /* a core drag (e.g. a Save As file icon) owns the pointer until it ends:
     * the pointer crossing a titlebar mid-drag must not collapse anything */
    if (wuss->drag.window != NULL)
      return result_OK;

    for (node = wuss->menu->chain; node != NULL; node = node->child)
      if (node->child != NULL
          && node->flash.frames == 0
          && wuss__pointer_over_titlebar(node->window))
      {
        wuss_icon_t *was_parent;

        was_parent = (node->open_index >= 0) ? node->icons[node->open_index]
                                             : NULL;

        wuss__menu_close_from(node->child);
        node->child      = NULL;
        node->open_index = -1;

        if (was_parent != NULL)
        {
          wuss__icon_set_state(was_parent, wuss_ICON_STATE_HOVERED, 0);
          wuss__icon_invalidate(node->window, was_parent);
        }
        break; /* closing the child re-shaped the chain past this node */
      }

    return result_OK;
  }

  if (event->kind != wuss_EVENT_ICON)
    return result_OK;

  wuss = window->wuss;
  self = wuss__menu_node_for(wuss, window);
  if (self == NULL)
    return result_OK;

  icon  = event->data.icon.icon;
  index = -1;
  for (i = 0; i < self->menu->nitems; i++)
  {
    if (self->icons[i] == icon)
    {
      index = i;
      break;
    }
  }
  if (index < 0)
    return result_OK;

  item = &self->menu->items[index];

  if (event->data.icon.action == wuss_MOUSE_MOVE)
  {
    int has_child_row;
    int on_arrow;

    /* A disabled row still has to run the close-on-move-away logic below --
     * hovering off an open submenu onto a disabled sibling must collapse it
     * -- but can never itself open a submenu. */
    has_child_row = !(item->flags & wuss_MENU_ITEM_DISABLED)
                 && (item->submenu != NULL || item->window != NULL);
    on_arrow      = has_child_row
                 && wuss__pointer_over_row_arrow(self->window, icon);

    /* A submenu opens only while the pointer is over its row's arrow. Any
     * other move that re-enters this level -- onto a different row, or onto
     * this row's text off the arrow -- closes the child it opened. */
    if (self->child != NULL && !(self->open_index == index && on_arrow))
    {
      wuss_icon_t *was_parent;

      was_parent = (self->open_index >= 0) ? self->icons[self->open_index]
                                           : NULL;

      wuss__menu_close_from(self->child);
      self->child      = NULL;
      self->open_index = -1;

      /* The ex-parent row was held highlit while its submenu was open (see
       * wuss__menu_row_pinned). Now the submenu is gone, drop that highlight
       * unless the pointer has landed back on that very row. */
      if (was_parent != NULL && was_parent != icon)
      {
        wuss__icon_set_state(was_parent, wuss_ICON_STATE_HOVERED, 0);
        wuss__icon_invalidate(self->window, was_parent);
      }
    }

    if (self->child != NULL)
      return result_OK; /* still on the open row's arrow: leave it up */

    if (!on_arrow)
      return result_OK;

    /* A borrowed window opens where a submenu would; a submenu spawns as a
     * fresh menu level. Either lines its row 0 up with this row -- the
     * child's own titlebar sits above the row's arrow. */
    if (item->window != NULL)
    {
      if (wuss__menu_open_window(self, index) == result_OK && self->child != NULL)
        self->open_index = index;
      return result_OK;
    }

    if (!(item->flags & wuss_MENU_ITEM_PRE_OPEN))
    {
      /* unflagged: no event, just open the row's own static submenu */
      point_t at;

      at = wuss__submenu_anchor(self, self->icons[index]);
      if (wuss__menu_spawn(self->wuss, self->owner, item->submenu, at,
                           self, &self->child) == result_OK)
        self->open_index = index;

      return result_OK;
    }

    {
      result_t     rc;
      wuss_event_t pre_open;

      self->pending_index                = index;
      pre_open.kind                      = wuss_EVENT_PRE_SUBMENU_OPEN;
      pre_open.data.pre_submenu_open.handle = self;
      pre_open.data.pre_submenu_open.index  = index;
      rc = wuss__deliver(self->owner, NULL, &pre_open);
      self->pending_index = -1;
      if (rc != result_OK)
        return rc;

      /* flagged but the handler did not call wuss_menu_open_submenu_now:
       * row stays inert */
    }

    return result_OK;
  }

  if (event->data.icon.action == wuss_MOUSE_UP)
  {
    wuss_button_t      button;
    wuss_task_t       *owner;
    const wuss_menu_t *menu;

    if (item->flags & wuss_MENU_ITEM_DISABLED)
      return result_OK;

    button = event->data.icon.button;

    if (!(button & WUSS_BUTTON_CLICK))
      return result_OK; /* MENU release picks nothing -- core never arms the
                         * row's press for it, so this UP is unpaired */

    if (item->submenu != NULL || item->window != NULL)
      return result_OK; /* a submenu/window row opens on hover, not a pick */

    /* owner (the task that opened the chain) is copied onto every level */
    owner = self->owner;
    menu  = self->menu;

    /* Flash the picked row, then deliver MENU_SELECT from the IDLE handler.
     * SELECT tears the whole chain down first; ADJUST keeps it open so the
     * row can be re-picked. Capture everything the notification needs now --
     * a SELECT flash frees `self` when it ends. A re-pick while a flash on
     * this level is still running (fast clicks) finishes that flash first so
     * its row is cleared and its MENU_SELECT is not lost; if that previous
     * pick was a SELECT the finish frees the whole chain (including `self`),
     * so this pick is spent on it and we must not touch `self` again. */
    {
      wuss_icon_t *row;

      if (self->flash.frames > 0)
      {
        wuss__menu_flash_finish(self);

        /* the finish's own SELECT tears the chain down when it wasn't a
         * keep_open pick, and even a keep_open pick's MENU_SELECT delivery
         * may re-enter and close the chain from the client side (e.g. the
         * owner calling wuss_menu_close()) -- either way `self` is gone the
         * moment the chain no longer starts at it */
        if (wuss__menu_node_for(wuss, window) != self)
          return result_OK; /* `self` and the chain are gone */
      }

      row = self->icons[index];

      self->flash.frames = WUSS_MENU_FLASH_FRAMES;
      self->flash.index  = index;
      self->flash.owner  = owner;
      self->flash.menu   = menu;
      self->flash.button = button;
      if (button & wuss_BUTTON_ADJUST)
        self->flags |= wuss_MENU__FLASH_KEEP_OPEN;
      else
        self->flags &= ~wuss_MENU__FLASH_KEEP_OPEN;

      /* first blink now: the row is already highlit under the pointer, so
       * drop the highlight this frame for an immediate visible change */
      wuss__icon_set_state(row, wuss_ICON_STATE_HOVERED, 0);
      wuss__icon_invalidate(self->window, row);
    }
    return result_OK;
  }

  return result_OK;
}

wuss_task_t *wuss__menu_task(wuss_t *wuss)
{
  wuss_task_desc_t desc;

  if (wuss->menu->task != NULL)
    return wuss->menu->task;

  desc.handle    = wuss__menu_handle;
  desc.task_data = wuss; /* the ICON path walks the chain by window; the IDLE
                          * path (window == NULL) needs the wuss_t directly */
  desc.name      = "wuss:menu";

  if (wuss_task_create(wuss, &desc, &wuss->menu->task) != result_OK)
    return NULL;

  return wuss->menu->task;
}
