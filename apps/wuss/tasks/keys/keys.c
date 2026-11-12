/* wuss/tasks/keys/keys.c -- key input demo task */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "geom/box.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define KEYS_HISTORY 6 /* most recent keys shown, newest first */

/* a focusable window that shows whether it holds the input focus and the
 * last few keys it was sent, each with its modifiers. A SELECT click gives
 * it the focus; Menu > Clear empties the history. */
typedef struct keys_task
{
  taskapp_instance_t   base;
  wuss_menu_item_t     menu_items[1]; /* per-instance: a shared static would
                                        * leak one instance's .window
                                        * pointer into another's menu */
  bmfont_t            *font; /* borrowed */
  colour_t             bg, fg;
  int                  keys[KEYS_HISTORY]; /* newest first */
  wuss_key_modifiers_t mods[KEYS_HISTORY]; /* keys[i]'s modifiers */
  int                  nkeys; /* used entries of keys[] */
}
keys_task_t;

/* MENU click pops this menu; the item table lives per-instance in
 * keys_task_t so each window's Info row can hold its own .window pointer to
 * the shared proginfo singleton, retargeted just before wuss_menu_open */
enum
{
  KEYS_MENU_CLEAR = 0
};

static result_t keys_init(keys_task_t *task)
{
  result_t rc;
  int      fh;
  size2d_t size;

  task->font = wuss_get_font_n(task->base.wuss, 0);
  task->bg   = colour_rgb(0xFF, 0xFF, 0xFF);
  task->fg   = colour_rgb(0x00, 0x00, 0x00);

  /* a Focus line, then one line per history entry */
  bmfont_get_info(task->font, NULL, &fh, NULL, NULL);
  size = SIZE2D(160, 8 + (1 + KEYS_HISTORY) * fh);

  rc = task_window_create(task->base.delegate, size, "Keys",
                          &task->base.window);
  if (rc != result_OK)
    return rc;

  WUSS_MENU_ITEM(task->menu_items, KEYS_MENU_CLEAR, "Clear",
                 wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->base.menu, "Keys", task->menu_items,
                 NELEMS(task->menu_items));

  return result_OK;
}

/* describe a key code: the character itself if printable ASCII, a name for
 * the wuss_KEY_* specials and control keys, otherwise U+hex */
static void keys_describe(int key, char *buf, size_t bufsz)
{
  static const char *const specials[] =
  {
    "Up", "Down", "Left", "Right", "Home", "End", "PageUp", "PageDown",
    "Insert", "Delete"
  };

  if (key >= wuss_KEY_UP && key < wuss_KEY_UP + (int) NELEMS(specials))
    snprintf(buf, bufsz, "%s", specials[key - wuss_KEY_UP]);
  else if (key >= wuss_KEY_F1 && key <= wuss_KEY_F12)
    snprintf(buf, bufsz, "F%d", key - wuss_KEY_F1 + 1);
  else if (key == 13)
    snprintf(buf, bufsz, "Return");
  else if (key == 8)
    snprintf(buf, bufsz, "Backspace");
  else if (key == 9)
    snprintf(buf, bufsz, "Tab");
  else if (key == 27)
    snprintf(buf, bufsz, "Escape");
  else if (key >= 0x20 && key < 0x7F)
    snprintf(buf, bufsz, "'%c'", key);
  else
    snprintf(buf, bufsz, "U+%04X", key);
}

static result_t keys_redraw(wuss_window_t      *window,
                            const wuss_event_t *event,
                            keys_task_t        *kt)
{
  screen_t    *scr;
  const box_t *content, *bounds;
  char         name[32];
  char         lines[1 + KEYS_HISTORY][48];
  int          nlines;
  int          fh, ascent;
  int          i;
  point_t      pos;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;

  screen_fill_rect(scr, content->x0, content->y0,
                   box_size(content), kt->bg);

  snprintf(lines[0], sizeof(lines[0]), "Focus: %s",
           (wuss_get_focus(kt->base.wuss) == window)
             ? "yes" : "no (click)");
  nlines = 1;

  if (kt->nkeys == 0)
    snprintf(lines[nlines++], sizeof(lines[0]), "Keys: (none)");

  for (i = 0; i < kt->nkeys; i++)
  {
    keys_describe(kt->keys[i], name, sizeof(name));
    snprintf(lines[nlines++], sizeof(lines[0]), "%s%s%s%s",
             (kt->mods[i] & wuss_KEY_MOD_SHIFT) ? "Shift+" : "",
             (kt->mods[i] & wuss_KEY_MOD_CTRL)  ? "Ctrl+"  : "",
             (kt->mods[i] & wuss_KEY_MOD_ALT)   ? "Alt+"   : "",
             name);
  }

  bmfont_get_info(kt->font, NULL, &fh, &ascent, NULL);
  for (i = 0; i < nlines; i++)
  {
    pos.x = bounds->x0 + 4;
    pos.y = bounds->y0 + 4 + i * fh + ascent;
    bmfont_draw(kt->font, scr, lines[i], (int) strlen(lines[i]), kt->fg,
                colour_rgba(0, 0, 0, 0), NULL, &pos, NULL);
  }

  return result_OK;
}

static result_t keys_mouse(keys_task_t *kt, const wuss_event_t *event)
{

  if (event->data.mouse.action != wuss_MOUSE_DOWN ||
      !(event->data.mouse.button & wuss_BUTTON_MENU))
    return result_OK;

  /* Clear has nothing to do with an empty history */
  if (kt->nkeys > 0)
    kt->menu_items[KEYS_MENU_CLEAR].flags &= ~wuss_MENU_ITEM_DISABLED;
  else
    kt->menu_items[KEYS_MENU_CLEAR].flags |= wuss_MENU_ITEM_DISABLED;

  return wuss_menu_open_at_pointer(kt->base.delegate, &kt->base.menu,
                                   &kt->base.menu_handle);
}

static result_t keys_handle(wuss_window_t      *window,
                            const wuss_event_t *event,
                            void               *task_data)
{
  keys_task_t *kt;

  kt = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return keys_redraw(window, event, kt);

  case wuss_EVENT_KEY:
    /* pass the driver's F-key hotkeys through so they keep working */
    if (event->data.key.code >= wuss_KEY_F1 &&
        event->data.key.code <= wuss_KEY_F12)
      return result_WUSS_KEY_UNCLAIMED;

    /* push onto the front of the history, dropping the oldest when full */
    kt->nkeys = MIN(kt->nkeys + 1, KEYS_HISTORY);
    memmove(&kt->keys[1], &kt->keys[0],
            (size_t) (kt->nkeys - 1) * sizeof(kt->keys[0]));
    memmove(&kt->mods[1], &kt->mods[0],
            (size_t) (kt->nkeys - 1) * sizeof(kt->mods[0]));
    kt->keys[0] = event->data.key.code;
    kt->mods[0] = event->data.key.modifiers;
    wuss_window_invalidate_visible(window);
    return result_OK;

  case wuss_EVENT_MOUSE:
    return keys_mouse(kt, event);

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu == &kt->base.menu &&
        event->data.menu_select.index == KEYS_MENU_CLEAR &&
        kt->base.window != NULL)
    {
      kt->nkeys = 0;
      wuss_window_invalidate_visible(kt->base.window);
    }
    return result_OK;

  case wuss_EVENT_GAIN_FOCUS:
  case wuss_EVENT_LOSE_FOCUS:
    wuss_window_invalidate_visible(window);
    return result_OK;

  default:
    return result_OK;
  }
}

result_t keys_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Keys",
    .purpose       = "Key input and focus",
    .singleton     = true,
    .instance_size = sizeof(keys_task_t),
    .init          = (taskapp_init_fn_t) keys_init,
    .handle        = keys_handle
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
