/* wuss/menu/menu.c -- wuss pop-up menu helper */

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "geom/box.h"
#include "geom/point.h"

#include "wuss/icon.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

#include "../core/types.h"
#include "../core/alloc.h"
#include "../core/geometry.h"
#include "../core/input.h"
#include "../core/windows.h"

#include "impl.h"

#define WUSS_MENU_SUBMENU_OVERLAP 2 /* px a submenu overlaps its parent's right edge */

/* ----------------------------------------------------------------------- */

/* Find the open chain node whose window is `window` (the menu window whose
 * delegate fired), or NULL. Borrowed-window levels never fire this handler
 * (their windows keep the caller's task), so a match is always a real menu
 * level with icons. */
struct wuss__menu *wuss__menu_node_for(wuss_t              *wuss,
                                       const wuss_window_t *window)
{
  struct wuss__menu *node;

  for (node = wuss->menu->chain; node != NULL; node = node->child)
    if (node->window == window)
      return node;

  return NULL;
}

/* Close `node` and everything it opened, leaf-first. The caller clears any
 * parent's child pointer. */
result_t wuss__menu_deliver_select(wuss_task_t       *task,
                                   const wuss_menu_t *menu,
                                   int                index,
                                   wuss_button_t      button)
{
  wuss_event_t sel;

  sel.kind                    = wuss_EVENT_MENU_SELECT;
  sel.data.menu_select.menu   = menu;
  sel.data.menu_select.index  = index;
  sel.data.menu_select.button = button;
  return wuss__deliver(task, NULL, &sel);
}

result_t wuss__menu_deliver_closed(wuss_task_t *task, int picked)
{
  wuss_event_t closed;

  closed.kind                    = wuss_EVENT_MENU_CLOSED;
  closed.data.menu_closed.picked = picked;
  return wuss__deliver(task, NULL, &closed);
}

void wuss__menu_close_from(struct wuss__menu *node)
{
  if (node == NULL)
    return;

  wuss__menu_close_from(node->child);
  node->child = NULL;

  /* A pending pick flash still owes its owner a MENU_SELECT; the chain is
   * about to be freed out from under it (the caller unlinks it from
   * wuss->menu->chain first), so delivering the notification here -- rather
   * than via wuss__menu_flash_finish, whose own teardown would race this
   * one -- is the only chance the pick has of reaching its owner. */
  if (node->flash.frames > 0)
  {
    node->flash.frames = 0;
    (void) wuss__menu_deliver_select(node->flash.owner, node->flash.menu,
                                     node->flash.index, node->flash.button);
  }

  {
    wuss_t *w;

    w = node->wuss;
    if (node->flags & wuss_MENU__BORROWED)
    {
      /* caller's window: hide, keep, and put back in the stack it came from */
      wuss_window_set_hidden(node->window, 1);
      wuss_window_set_stack(node->window, node->saved_stack);

      /* hiding dropped the focus it held; hand it back to its lender */
      if (node->saved_focus != NULL && w->focus == NULL)
        (void) wuss_set_focus(w, node->saved_focus);
    }
    else
    {
      wuss_window_t *win;

      /* unhook first: wuss_window_close abandons the chain for a window it
       * still contains, and a submenu is closed with the chain linked */
      win          = node->window;
      node->window = NULL;
      wuss_window_close(win);
    }
    wuss__free(w, node->icons); /* NULL for a borrowed level */
    wuss__free(w, node);
  }
}

/* Tear the whole open chain down because wuss decided to, not the client: a
 * click outside every menu window, or another wuss_menu_open. The task that
 * opened it still holds the handle wuss_menu_open handed back, so tell it the
 * chain is gone (wuss_EVENT_MENU_CLOSED) before the nodes are freed -- a pick
 * has wuss_EVENT_MENU_SELECT for that, but these paths have nothing. Unlinks
 * wuss->menu->chain first so a re-entrant wuss_menu_close from the handler is a
 * no-op. Also used by wuss_task_destroy when the chain's owner is the task
 * going away, so declared in core impl.h. */
void wuss__menu_abandon(wuss_t *wuss)
{
  struct wuss__menu *root;
  wuss_task_t       *owner;

  root = wuss->menu->chain;
  if (root == NULL)
    return;

  owner            = root->owner;
  wuss->menu->chain = NULL;

  if (owner != NULL)
    (void) wuss__menu_deliver_closed(owner, 0);

  wuss__menu_close_from(root);
}

/* Compute where a submenu (or a borrowed window standing in for one) opens
 * off the row `icon` in parent level `self`: content top-left in screen
 * space, the child's own titlebar then sitting above it so the two rows line
 * up. Mirrors the maths in wuss__menu_handle's MOUSE_MOVE branch. */
point_t wuss__submenu_anchor(struct wuss__menu *self,
                             const wuss_icon_t *icon)
{
  box_t   wb;
  box_t   ib;
  point_t scroll;
  point_t at;

  wuss_window_get_content_bounds(self->window, &wb);
  wuss_icon_get_bbox(icon, &ib);
  wuss_window_get_scroll(self->window, &scroll);

  at.x = wb.x1 - WUSS_MENU_SUBMENU_OVERLAP;
  at.y = wb.y0 + ib.y0 - scroll.y;
  return at;
}

/* Allocate a borrowed-window level owned by `owner`, link it onto
 * parent->child (or leave it unlinked as a root if `parent` is NULL), then
 * bring its window to front. Shared by wuss_menu_open_window_now (the
 * flagged row's explicit opt-in), wuss__menu_open_window's direct-open path
 * for an unflagged row, and wuss_menu_open_window (a root). */
static result_t wuss__menu_new_borrowed(wuss_t             *wuss,
                                        wuss_task_t        *owner,
                                        struct wuss__menu  *parent,
                                        wuss_window_t      *win,
                                        struct wuss__menu **out)
{
  struct wuss__menu *node;
  int                i;
  wuss_icon_t       *icon;

  node = wuss__malloc(wuss, sizeof(*node));
  if (node == NULL)
    return result_OOM;

  node->flags         = wuss_MENU__BORROWED;
  node->wuss          = wuss;
  node->owner         = owner;
  node->window        = win;
  node->saved_stack   = win->stack;
  node->saved_focus   = NULL;
  node->menu          = NULL;
  node->icons         = NULL;
  node->parent        = parent;
  node->child         = NULL;
  node->open_index    = -1;
  node->pending_index = -1;
  memset(&node->flash, 0, sizeof(node->flash));

  if (parent != NULL)
    parent->child = node;

  wuss_window_set_stack(win, wuss_STACK_TOP);
  wuss_window_restack(win, wuss_ZORDER_FRONT); /* for a window already in TOP */

#ifdef WUSS_ICONS
  /* a dialogue takes the focus, with the caret in its first writable; a
   * window with no writable is left alone */
  for (i = 0; i < win->nicons; i++)
  {
    icon = win->icons[i];
    if (icon->spec.type != wuss_ICON_TYPE_WRITABLE ||
        (icon->spec.flags & (wuss_ICON_FLAGS_HIDDEN | wuss_ICON_FLAGS_DISABLED)))
      continue;

    node->saved_focus = wuss->focus;
    if (wuss_set_focus(wuss, win) == result_OK)
      wuss__writable_place_caret(win, icon, (int) strlen(icon->spec.text));
    break;
  }
#endif

  if (out != NULL)
    *out = node;
  return result_OK;
}

/* Link a borrowed-window child level onto self->child. */
static result_t wuss__menu_link_borrowed(struct wuss__menu *self,
                                         wuss_window_t     *win)
{
  return wuss__menu_new_borrowed(self->wuss, self->owner, self, win, NULL);
}

/* Open item `index`'s borrowed window as level `self->child`: position it
 * where a submenu would appear, then reveal it. Unflagged
 * (wuss_MENU_ITEM_PRE_OPEN unset), this reveals and links directly with no
 * event fired. Flagged, wuss_window_set_hidden fires wuss_EVENT_PRE_SHOW to
 * the window's own task with `self` and `index` in the payload, and the
 * handler must call wuss_menu_open_window_now to proceed and link the
 * child level -- not calling it leaves the row inert. */
result_t wuss__menu_open_window(struct wuss__menu *self, int index)
{
  result_t                rc;
  const wuss_menu_item_t *item;
  wuss_window_t           *win;
  point_t                  at;

  item = &self->menu->items[index];
  win  = item->window;

  at = wuss__submenu_anchor(self, self->icons[index]);
  wuss_window_move(win, at);
  /* the anchor sits off the parent's right edge; if the parent menu is near
   * the screen edge that puts the window (partly) off-screen. wuss_window_move
   * does not clamp -- drags rely on it not clamping -- so pull it back here. */
  wuss__nudge_visible_onscreen(win);

  if (!(item->flags & wuss_MENU_ITEM_PRE_OPEN))
  {
    rc = wuss_window_set_hidden(win, 0);
    if (rc != result_OK)
      return rc;

    if (self->child == NULL && !(win->flags & wuss_WINDOW_HIDDEN))
      return wuss__menu_link_borrowed(self, win);

    return result_OK;
  }

  self->pending_index = index;
  rc                  = wuss__window_set_hidden_ex(win, 0, self, index);
  self->pending_index = -1;

  return rc;
}

/* Called back from wuss_EVENT_PRE_SHOW, synchronously, to opt in to
 * revealing `index`'s borrowed window: tells wuss__window_set_hidden_ex to
 * proceed with the actual flag-flip/SHOW once this handler returns, and
 * links the child level. Positioning has already been done by
 * wuss__menu_open_window before PRE_SHOW was fired. */
result_t wuss_menu_open_window_now(wuss_menu_handle_t handle, int index)
{
  struct wuss__menu *self;
  wuss_window_t     *win;

  self = handle;

  assert(self != NULL);
  assert(self->pending_index == index);

  win = self->menu->items[index].window;

  self->wuss->pre_show_proceed = 1;

  return wuss__menu_link_borrowed(self, win);
}

/* Called back from wuss_EVENT_PRE_SUBMENU_OPEN, synchronously, to opt in to
 * opening `index`'s submenu: spawns `menu` (the item's own submenu, or
 * another one retitled/retargeted for this row) as the child level, anchored
 * off the row's arrow exactly as a plain submenu would be. */
result_t wuss_menu_open_submenu_now(wuss_menu_handle_t handle,
                                    int                index,
                                    const wuss_menu_t *menu)
{
  result_t           rc;
  struct wuss__menu *self;
  point_t            at;

  self = handle;

  assert(self != NULL);
  assert(self->pending_index == index);

  at = wuss__submenu_anchor(self, self->icons[index]);

  rc = wuss__menu_spawn(self->wuss, self->owner, menu, at, self, &self->child);
  if (rc == result_OK)
    self->open_index = index;

  return rc;
}

/* ----------------------------------------------------------------------- */

/* Make "root" the open chain and report it to the caller. */
static void wuss__menu_install(wuss_t             *wuss,
                               struct wuss__menu  *root,
                               wuss_menu_handle_t *out)
{
  wuss->menu->chain = root;

  if (out != NULL)
    *out = root;
}

result_t wuss_menu_open(wuss_task_t        *task,
                        const wuss_menu_t  *menu,
                        point_t             at,
                        wuss_menu_handle_t *out)
{
  result_t           rc;
  struct wuss__menu *root;
  wuss_t            *wuss;

  assert(task != NULL);
  assert(menu != NULL);

  wuss = task->wuss;

  wuss__menu_abandon(wuss);

  /* RISC OS convention: the pointer opens the menu sitting a little inside its
   * first item, not on the top-left corner. Shift the content top-left up and
   * left so `at` (the pointer) lands over row 0. */
  at.x -= wuss__menu_entry_gutter_left(
            wuss__fontset_height(&wuss->fonts, 0));
  at.y -= WUSS_MENU_ROW_PAD;

  rc = wuss__menu_spawn(wuss, task, menu, at, NULL, &root);
  if (rc != result_OK)
    return rc;

  /* A menu is opened from a task's MOUSE_DOWN handler; the matching MOUSE_UP is
   * still to come and would land on the fresh menu's row 0. Mark it to be
   * eaten. wuss_mouse_click clears this on the next MOUSE_UP whether or not it
   * hit the menu. */
  wuss->menu->eat_up = 1;

  wuss__menu_install(wuss, root, out);
  return result_OK;
}

result_t wuss_menu_open_above(wuss_task_t        *task,
                              const wuss_menu_t  *menu,
                              int                 x,
                              int                 gap,
                              wuss_menu_handle_t *out)
{
  result_t           rc;
  struct wuss__menu *root;
  wuss_t            *wuss;

  assert(task != NULL);
  assert(menu != NULL);

  wuss = task->wuss;

  wuss__menu_abandon(wuss);

  wuss->menu->bottom_gap = gap;
  rc = wuss__menu_spawn(wuss, task, menu, POINT(x, 0), NULL, &root);
  wuss->menu->bottom_gap = 0;
  if (rc != result_OK)
    return rc;

  wuss__menu_install(wuss, root, out);
  return result_OK;
}

result_t wuss_menu_open_window(wuss_task_t        *task,
                               wuss_window_t      *window,
                               point_t             at,
                               wuss_menu_handle_t *out)
{
  result_t           rc;
  struct wuss__menu *root;
  wuss_t            *wuss;

  assert(task != NULL);
  assert(window != NULL);

  wuss = task->wuss;

  wuss__menu_abandon(wuss);

  wuss_window_move(window, at);
  wuss__nudge_visible_onscreen(window);

  rc = wuss_window_set_hidden(window, 0);
  if (rc != result_OK)
    return rc;

  rc = wuss__menu_new_borrowed(wuss, task, NULL, window, &root);
  if (rc != result_OK)
  {
    wuss_window_set_hidden(window, 1);
    return rc;
  }

  wuss__menu_install(wuss, root, out);
  return result_OK;
}

int wuss_menu_should_keep_open(const wuss_event_t *ev)
{
  if (ev == NULL || ev->kind != wuss_EVENT_MENU_SELECT)
    return 0;

  return (ev->data.menu_select.button & wuss_BUTTON_ADJUST) != 0;
}

int wuss_menu_select_index(const wuss_event_t *ev, const wuss_menu_t *menu)
{
  int index;

  if (ev == NULL || menu == NULL || ev->kind != wuss_EVENT_MENU_SELECT)
    return -1;

  if (ev->data.menu_select.menu != menu)
    return -1;

  index = ev->data.menu_select.index;
  if (index < 0 || index >= menu->nitems)
    return -1;

  return index;
}

int wuss_menu_select_is(const wuss_event_t *ev,
                        const wuss_menu_t  *menu,
                        int                 index)
{
  return index >= 0 && wuss_menu_select_index(ev, menu) == index;
}

result_t wuss_menu_open_ticked(wuss_task_t        *task,
                               wuss_menu_t        *menu,
                               unsigned int        ticked,
                               point_t             at,
                               wuss_menu_handle_t *out)
{
  wuss_menu_tick_set(menu, ticked);

  return wuss_menu_open(task, menu, at, out);
}

result_t wuss_menu_open_at_pointer(wuss_task_t        *task,
                                   const wuss_menu_t  *menu,
                                   wuss_menu_handle_t *out)
{
  return wuss_menu_open(task, menu, wuss_get_pointer(task->wuss), out);
}

void wuss_menu_close(wuss_menu_handle_t handle)
{
  struct wuss__menu *root;

  if (handle == NULL)
    return;

  root = handle;
  while (root->parent != NULL)
    root = root->parent;

  if (root->wuss->menu->chain != root)
    return; /* stale handle */

  root->wuss->menu->chain = NULL;
  wuss__menu_close_from(root);
}

int wuss_menu_is_open(wuss_menu_handle_t handle)
{
  struct wuss__menu *root;

  if (handle == NULL)
    return 0;

  root = handle;
  while (root->parent != NULL)
    root = root->parent;

  return root->wuss->menu->chain == root;
}

const wuss_menu_t *wuss_menu_handle_menu(wuss_menu_handle_t handle)
{
  struct wuss__menu *self;

  self = handle;

  return self ? self->menu : NULL;
}
