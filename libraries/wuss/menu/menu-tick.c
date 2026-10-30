/* wuss/menu/menu-tick.c -- wuss pop-up menu ticks and live row edits */

#include <assert.h>
#include <limits.h>
#include <stddef.h>

#include "wuss/icon.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

#include "../core/types.h"

#include "impl.h"

/* ----------------------------------------------------------------------- */

/* Set or clear item "index"'s tick: the one place the flag is written. */
static void wuss__menu_item_set_ticked(wuss_menu_t *menu,
                                       int          index,
                                       int          ticked)
{
  wuss_menu_item_t *item;

  item = (wuss_menu_item_t *) &menu->items[index];
  if (ticked)
    item->flags |= wuss_MENU_ITEM_TICKED;
  else
    item->flags &= ~(wuss_menu_item_flags_t) wuss_MENU_ITEM_TICKED;
}

void wuss_menu_tick_set(wuss_menu_t *menu, unsigned int ticked)
{
  int i;

  assert(menu != NULL);
  assert(menu->nitems <= (int) (sizeof(ticked) * CHAR_BIT));

  for (i = 0; i < menu->nitems; i++)
    wuss__menu_item_set_ticked(menu, i, (ticked & (1u << i)) != 0);
}

void wuss_menu_tick_exclusive(wuss_menu_t *menu, int index)
{
  int i;

  assert(menu != NULL);

  for (i = 0; i < menu->nitems; i++)
    wuss__menu_item_set_ticked(menu, i, i == index);
}

void wuss_menu_tick_item(wuss_menu_t *menu, int index, int ticked)
{
  assert(menu != NULL);

  if (index < 0 || index >= menu->nitems)
    return;

  wuss__menu_item_set_ticked(menu, index, ticked);
}

/* ----------------------------------------------------------------------- */

/* Find the open chain level showing `menu`, or NULL if `handle` is stale or
 * `menu` is not a level of its chain. Shared by the *_live setters. */
static struct wuss__menu *wuss__menu_open_level(wuss_menu_handle_t handle,
                                                const wuss_menu_t *menu)
{
  struct wuss__menu *root;
  struct wuss__menu *node;

  if (handle == NULL || menu == NULL)
    return NULL;

  root = handle;
  while (root->parent != NULL)
    root = root->parent;

  if (root->wuss->menu->chain != root)
    return NULL; /* stale handle */

  for (node = root; node != NULL; node = node->child)
    if (node->menu == menu)
      return node;

  return NULL; /* menu is not a level of this chain */
}

/* The live icon for item "index" of "menu" in the open chain "handle", or NULL
 * if the menu is not open or the index is out of range. "node" gets its
 * level. */
static wuss_icon_t *wuss__menu_live_icon(wuss_menu_handle_t  handle,
                                         const wuss_menu_t  *menu,
                                         int                 index,
                                         struct wuss__menu **node)
{
  *node = wuss__menu_open_level(handle, menu);
  if (*node == NULL || index < 0 || index >= menu->nitems)
    return NULL;

  return (*node)->icons[index];
}

void wuss_menu_tick_exclusive_live(wuss_menu_handle_t handle,
                                   const wuss_menu_t *menu,
                                   int                index)
{
  struct wuss__menu *node;
  int                i;

  node = wuss__menu_open_level(handle, menu);
  if (node == NULL)
    return;

  for (i = 0; i < menu->nitems; i++)
    wuss_icon_set_selected(node->window, node->icons[i], i == index);
}

void wuss_menu_tick_set_live(wuss_menu_handle_t handle,
                             const wuss_menu_t *menu,
                             unsigned int       ticked)
{
  struct wuss__menu *node;
  int                i;

  assert(menu->nitems <= (int) (sizeof(ticked) * CHAR_BIT));

  node = wuss__menu_open_level(handle, menu);
  if (node == NULL)
    return;

  for (i = 0; i < menu->nitems; i++)
    wuss_icon_set_selected(node->window, node->icons[i],
                           (ticked & (1u << i)) != 0);
}

void wuss_menu_tick_item_live(wuss_menu_handle_t handle,
                              const wuss_menu_t *menu,
                              int                index,
                              int                ticked)
{
  struct wuss__menu *node;
  wuss_icon_t       *icon;

  icon = wuss__menu_live_icon(handle, menu, index, &node);
  if (icon != NULL)
    wuss_icon_set_selected(node->window, icon, ticked);
}

void wuss_menu_disable_item_live(wuss_menu_handle_t handle,
                                 const wuss_menu_t *menu,
                                 int                index,
                                 int                disabled)
{
  struct wuss__menu *node;
  wuss_icon_t       *icon;

  icon = wuss__menu_live_icon(handle, menu, index, &node);
  if (icon != NULL)
    wuss_icon_set_disabled(node->window, icon, disabled);
}

void wuss_menu_set_item_text_live(wuss_menu_handle_t handle,
                                  const wuss_menu_t *menu,
                                  int                index,
                                  const char        *text)
{
  struct wuss__menu *node;
  wuss_icon_t       *icon;

  icon = wuss__menu_live_icon(handle, menu, index, &node);
  if (icon != NULL)
    (void) wuss_icon_set_text(node->window, icon, text);
}

void wuss_menu_set_title_live(wuss_menu_handle_t handle,
                              const wuss_menu_t *menu,
                              const char        *title)
{
  struct wuss__menu *node;

  node = wuss__menu_open_level(handle, menu);
  if (node == NULL)
    return;

  wuss_window_set_title(node->window, title);
}
