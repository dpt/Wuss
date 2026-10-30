/* wuss/menu/menu-session.c -- wuss pop-up menu session entry points for core */

#include <stddef.h>
#include <string.h>

#include "base/result.h"

#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

#include "../core/types.h"
#include "../core/alloc.h"
#include "../core/input.h"

#include "impl.h"

/* ----------------------------------------------------------------------- */

int wuss__menu_row_pinned(const wuss_t *wuss, const wuss_icon_t *icon)
{
  const struct wuss__menu *node;

  for (node = wuss->menu->chain; node != NULL; node = node->child)
  {
    if ((node->flags & wuss_MENU__BORROWED) || node->child == NULL ||
        node->open_index < 0)
      continue;
    if (node->icons[node->open_index] == icon)
      return 1;
  }

  return 0;
}

int wuss__menu_contains(wuss_t *wuss, const wuss_window_t *window)
{
  return wuss__menu_node_for(wuss, window) != NULL;
}

/* ----------------------------------------------------------------------- */

result_t wuss__menu_session_create(wuss_t *wuss)
{
  wuss->menu = wuss__malloc(wuss, sizeof(*wuss->menu));
  if (wuss->menu == NULL)
    return result_OOM;

  memset(wuss->menu, 0, sizeof(*wuss->menu));

  return result_OK;
}

void wuss__menu_session_free(wuss_t *wuss)
{
  wuss__free(wuss, wuss->menu);
  wuss->menu = NULL;
}

void wuss__menu_teardown(wuss_t *wuss)
{
  wuss_task_t *task;

  /* Drop any chain a QUIT handler left open: its windows are freed by the
   * caller's z_order sweep, but the chain nodes and their icon-handle arrays
   * are not.
   *
   * ponytail: a bare wuss_menu_close, not wuss__menu_abandon -- every client
   * task is already freed by the caller's sweep, so the chain's owner (and
   * any flashing pick's owner) is dangling and must not be delivered to. No
   * path leaves a mid-flash pick standing at wuss_destroy today; track the
   * flash owner per task if one ever does. */
  wuss_menu_close(wuss->menu->chain);

  /* Now the menu task has nothing left to own -- QUIT and free it. */
  task = wuss->menu->task;
  if (task != NULL)
  {
    (void) wuss__deliver_kind(task, NULL, wuss_EVENT_QUIT);
    wuss__free(wuss, &task->link);
    wuss->menu->task = NULL;
  }
}

int wuss__menu_is_task(const wuss_t *wuss, const wuss_task_t *task)
{
  return task != NULL && wuss->menu->task == task;
}

struct wuss__menu *wuss__menu_chain(const wuss_t *wuss)
{
  return wuss->menu->chain;
}

int wuss__menu_press(wuss_t              *wuss,
                     wuss_mouse_action_t  action,
                     wuss_button_t        button,
                     const wuss_window_t *hit)
{
  /* The MOUSE_UP that follows the MENU press which opened the chain is
   * spent: without this it would land on the new menu's row 0 and pick it. */
  if (action == wuss_MOUSE_UP && wuss->menu->eat_up)
  {
    wuss->menu->eat_up = 0;
    return 1;
  }

  if (action != wuss_MOUSE_DOWN)
    return 0;

  /* Any fresh press ends the eat-up window: the release it pairs with is a
   * real click, not the tail of the menu-opening press. */
  wuss->menu->eat_up = 0;

  /* A press outside every window in the open chain dismisses the chain.
   * Presses inside the chain fall through to the menu window's own delegate.
   * A MENU press that lands on another window is not spent by the dismissal:
   * it falls through so that window's delegate can open its own menu at the
   * new spot, matching the RISC OS "MENU moves the menu" feel. */
  if (wuss->menu->chain == NULL || wuss__menu_contains(wuss, hit))
    return 0;

  wuss__menu_abandon(wuss);

  return !(hit != NULL && (button & wuss_BUTTON_MENU));
}

int wuss__menu_key(wuss_t *wuss, int code)
{
  /* Escape dismisses an open chain, as a click outside it does: menus never
   * take the input focus, so no menu window would ever see the key. */
  if (code != wuss_KEY_ESCAPE || wuss->menu->chain == NULL)
    return 0;

  wuss__menu_abandon(wuss);

  return 1;
}

void wuss__menu_window_closing(wuss_t *wuss, wuss_window_t *doomed)
{
  struct wuss__menu *m;

  /* the doomed window may be a borrowed level's focus lender; forget it */
  for (m = wuss->menu->chain; m != NULL; m = m->child)
    if (m->saved_focus == doomed)
      m->saved_focus = NULL;

  /* A window closed while in the open chain -- typically a borrowed level
   * such as a Save As dialogue its client is freeing -- would leave a chain
   * node pointing at freed memory. Tear the chain down first, while the
   * window is still alive to be hidden and restacked. The chain's own menu
   * windows are unhooked from their nodes before being closed (see
   * wuss__menu_close_from), so this never recurses. */
  if (wuss__menu_contains(wuss, doomed))
    wuss__menu_abandon(wuss);
}

void wuss__menu_task_closing(wuss_t *wuss, const wuss_task_t *task)
{
  /* A chain the task still owns would outlive it: tear it down, delivering
   * MENU_CLOSED, before the task's QUIT. */
  if (wuss->menu->chain != NULL && wuss->menu->chain->owner == task)
    wuss__menu_abandon(wuss);
}

/* ----------------------------------------------------------------------- */

struct wuss__menu *wuss__menu_level_child(const struct wuss__menu *level)
{
  return level->child;
}

int wuss__menu_level_open_index(const struct wuss__menu *level)
{
  return level->open_index;
}

wuss_window_t *wuss__menu_level_window(const struct wuss__menu *level)
{
  return level->window;
}

wuss_icon_t *wuss__menu_level_icon(const struct wuss__menu *level, int index)
{
  return level->icons != NULL ? level->icons[index] : NULL;
}

int wuss__menu_level_flashing(const struct wuss__menu *level)
{
  return level->flash.frames > 0;
}
