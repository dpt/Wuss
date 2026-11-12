/* wuss/test/api-test.c -- tests for small public queries and helpers */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "io/path.h"
#include "wuss/message.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"
#ifdef WUSS_MENUS
#include "wuss/menu.h"
#endif

#include "wuss/test/all-tests.h"
#include "harness.h"

/* ----------------------------------------------------------------------- */

#define API_TEST_W 320
#define API_TEST_H 240

#define API_TEST_ACTION (wuss_MESSAGE_APP_BASE + 7)

typedef struct api_task
{
  int            key_up_count;
  int            last_key_up;
  int            last_key_up_mods;
  int            message_count;
  int            last_action;
  wuss_window_t *last_window;
}
api_task_t;

static result_t api_handle(wuss_window_t      *window,
                           const wuss_event_t *event,
                           void               *task_data)
{
  api_task_t *at;

  NOT_USED(window);

  at = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_KEY_UP:
    at->key_up_count++;
    at->last_key_up      = event->data.key.code;
    at->last_key_up_mods = (int) event->data.key.modifiers;
    break;

  case wuss_EVENT_MESSAGE:
    at->message_count++;
    at->last_action = event->data.message->action;
    at->last_window = event->data.message->window;
    break;

  default:
    break;
  }

  return result_OK;
}

/* ----------------------------------------------------------------------- */

result_t wuss_api_test(const char *resources)
{
  result_t          rc;
  bmfont_t         *font;
  void             *pixels;
  bitmap_t          bm;
  screen_t          scr;
  wuss_font_desc_t  fdesc;
  colour_t          palette[3];
  wuss_t           *wuss;
  api_task_t        at;
  wuss_task_t      *task;
  wuss_task_t      *parent;
  wuss_task_t      *child;
  wuss_window_t    *win;
  wuss_window_t    *pwin;
  box_t             content;
  box_t             before;
  point_t           scroll;
  size2d_t          max;
  bmfont_width_t    w1;
  bmfont_width_t    w2;
  int               split;
  unsigned int      gen;
  wuss_msg_target_t target;
  const char       *text;

  font   = NULL;
  pixels = NULL;
  wuss   = NULL;
  memset(&at, 0, sizeof(at));

  forget_test_tasks();

  rc = bmfont_create(pathf("%s/resources/bmfonts/Tiny/Regular.png",
                           resources), &font);
  if (rc != result_OK)
    goto Failure;

  pixels = malloc((size_t) API_TEST_W * 4 * API_TEST_H);
  if (pixels == NULL)
  {
    rc = result_OOM;
    goto Failure;
  }
  rc = bitmap_init(&bm, SIZE2D(API_TEST_W, API_TEST_H), pixelfmt_bgrx8888,
                   API_TEST_W * 4, NULL, pixels);
  if (rc != result_OK)
    goto Failure;
  screen_for_bitmap(&scr, &bm);

  fdesc.font       = font;
  fdesc.font_class = wuss_FONT_CLASS_SYSTEM;
  fdesc.name       = "Tiny";

  palette[0] = colour_rgb(0, 0, 0);
  palette[1] = colour_rgb(255, 255, 255);
  palette[2] = colour_rgb(255, 0, 0);

  rc = wuss_create(&scr, &fdesc, 1, palette, NELEMS(palette), NULL, NULL,
                   resources, &wuss);
  if (rc != result_OK)
    goto Failure;

  printf("test: wuss_result_string names wuss codes and defers to DPTLib\n");
  text = wuss_result_string(result_WUSS_BAD_INDEX);
  if (text == NULL || text[0] == '\0' ||
      strcmp(text, wuss_result_string(result_WUSS_BAD_COLOUR)) == 0)
    goto Failure;
  if (strcmp(wuss_result_string(result_OOM), result_string(result_OOM)) != 0)
    goto Failure;

  printf("test: wuss_nearest_colour picks the closest entry\n");
  if (wuss_nearest_colour(wuss, 255, 0, 0)   != 2 ||
      wuss_nearest_colour(wuss, 240, 20, 10) != 2 ||
      wuss_nearest_colour(wuss, 10, 10, 10)  != 0 ||
      wuss_nearest_colour(wuss, 250, 250, 250) != 1)
    goto Failure;

  printf("test: wuss_get_resources returns the root given to wuss_create\n");
  if (wuss_get_resources(wuss) != resources)
    goto Failure;

  printf("test: wuss_get_font_n returns the slot's font, NULL outside\n");
  if (wuss_get_font_n(wuss, 0) != font || wuss_get_font_n(wuss, 1) != NULL ||
      wuss_get_font_n(wuss, -1) != NULL ||
      wuss_get_font_n(wuss, wuss_MAX_FONTS) != NULL)
    goto Failure;

  printf("test: wuss_get_font_name_n returns the slot's name, NULL outside\n");
  text = wuss_get_font_name_n(wuss, 0);
  if (text == NULL || strcmp(text, "Tiny") != 0 ||
      wuss_get_font_name_n(wuss, 1) != NULL ||
      wuss_get_font_name_n(wuss, -1) != NULL)
    goto Failure;

  printf("test: wuss_get_font_class_n returns the slot's class, NONE "
         "outside\n");
  if (wuss_get_font_class_n(wuss, 0) != wuss_FONT_CLASS_SYSTEM ||
      wuss_get_font_class_n(wuss, 1) != wuss_FONT_CLASS_NONE ||
      wuss_get_font_class_n(wuss, wuss_MAX_FONTS) != wuss_FONT_CLASS_NONE)
    goto Failure;

  printf("test: wuss_text_measure widens with the text and splits to fit\n");
  rc = wuss_text_measure(wuss, 0, "ab", 1, INT_MAX, NULL, &w1);
  if (rc == result_OK)
    rc = wuss_text_measure(wuss, 0, "ab", 2, INT_MAX, &split, &w2);
  if (rc != result_OK || w1 <= 0 || w2 <= w1 || split != 2)
    goto Failure;
  rc = wuss_text_measure(wuss, 0, "ab", 2, w2 - 1, &split, NULL);
  if (rc != result_OK || split != 1)
    goto Failure;
  if (wuss_text_measure(wuss, 1, "ab", 2, INT_MAX, NULL, NULL) !=
      result_WUSS_BAD_INDEX)
    goto Failure;

  printf("test: wuss_clipboard_set copies, bumps the generation and clears "
         "on NULL\n");
  gen = wuss_clipboard_generation(wuss);
  {
    char buf[] = "copied";

    rc = wuss_clipboard_set(wuss, buf);
    buf[0] = 'X'; /* the clipboard keeps its own copy */
  }
  if (rc != result_OK || strcmp(wuss_clipboard_get(wuss), "copied") != 0 ||
      wuss_clipboard_generation(wuss) != gen + 1)
    goto Failure;
  rc = wuss_clipboard_set(wuss, NULL);
  if (rc != result_OK || strcmp(wuss_clipboard_get(wuss), "") != 0 ||
      wuss_clipboard_generation(wuss) != gen + 2)
    goto Failure;

  printf("test: wuss_get_modifiers returns what wuss_set_modifiers set\n");
  wuss_set_modifiers(wuss, wuss_KEY_MOD_SHIFT | wuss_KEY_MOD_CTRL);
  if (wuss_get_modifiers(wuss) != (wuss_KEY_MOD_SHIFT | wuss_KEY_MOD_CTRL))
    goto Failure;
  wuss_set_modifiers(wuss, wuss_KEY_MOD_NONE);
  if (wuss_get_modifiers(wuss) != wuss_KEY_MOD_NONE)
    goto Failure;

  printf("test: wuss_get_pointer starts at the origin and follows moves\n");
  if (wuss_get_pointer(wuss).x != 0 || wuss_get_pointer(wuss).y != 0)
    goto Failure;
  rc = wuss_mouse_move(wuss, POINT(123, 45), NULL);
  if (rc != result_OK ||
      wuss_get_pointer(wuss).x != 123 || wuss_get_pointer(wuss).y != 45)
    goto Failure;

  task = mk_task(wuss, api_handle, &at);
  if (task == NULL)
    goto Failure;
  rc = wuss_window_create(task, &(box_t) BOX_POS_SIZE(20, 20, 60, 40), "Api",
                          wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                          wuss_NO_BACKDROP, SIZE2D(200, 150), SIZE2D(0, 0),
                          &win);
  if (rc != result_OK)
    goto Failure;

  printf("test: wuss_window_scroll_by scrolls and clamps to the document\n");
  wuss_window_scroll_by(win, POINT(10, 20));
  wuss_window_get_scroll(win, &scroll);
  if (scroll.x != 10 || scroll.y != 20)
    goto Failure;
  wuss_window_get_content_bounds(win, &content);
  wuss_window_scroll_by(win, POINT(10000, 10000));
  wuss_window_get_scroll(win, &scroll);
  if (scroll.x != 200 - (content.x1 - content.x0) ||
      scroll.y != 150 - (content.y1 - content.y0))
    goto Failure;
  wuss_window_scroll_by(win, POINT(-10000, -10000));
  wuss_window_get_scroll(win, &scroll);
  if (scroll.x != 0 || scroll.y != 0)
    goto Failure;

  printf("test: wuss_window_get_max_content fits within the screen\n");
  wuss_window_get_max_content(win, &max);
  if (max.w <= 0 || max.h <= 0 || max.w > API_TEST_W || max.h > API_TEST_H)
    goto Failure;
#ifdef WUSS_FURNITURE
  if (max.w == API_TEST_W || max.h == API_TEST_H)
    goto Failure; /* the outline and titlebar take their share */
#endif

#ifdef WUSS_FURNITURE
  printf("test: wuss_window_toggle_size grows the window and restores it\n");
  wuss_window_get_content_bounds(win, &before);
  wuss_window_toggle_size(win);
  wuss_window_get_content_bounds(win, &content);
  if (content.x1 - content.x0 <= before.x1 - before.x0 ||
      content.y1 - content.y0 <= before.y1 - before.y0)
    goto Failure;
  wuss_window_toggle_size(win);
  wuss_window_get_content_bounds(win, &content);
  if (content.x0 != before.x0 || content.y0 != before.y0 ||
      content.x1 != before.x1 || content.y1 != before.y1)
    goto Failure;
  wuss_window_toggle_size(NULL); /* safe */
#else
  NOT_USED(before);
#endif

  printf("test: wuss_key_up delivers KEY_UP to the focus window\n");
  rc = wuss_set_focus(wuss, win);
  if (rc == result_OK)
    rc = wuss_key_up(wuss, 'q', wuss_KEY_MOD_SHIFT);
  if (rc != result_OK || at.key_up_count != 1 || at.last_key_up != 'q' ||
      at.last_key_up_mods != wuss_KEY_MOD_SHIFT)
    goto Failure;

  printf("test: wuss_send_to queues a message for the target window\n");
  target.window = win;
  target.icon   = NULL;
  rc = wuss_send_to(wuss, task, &target, API_TEST_ACTION, NULL, 0, 0);
  if (rc != result_OK || at.message_count != 0)
    goto Failure; /* never delivered inline */
  rc = wuss_idle(wuss);
  if (rc != result_OK || at.message_count != 1 ||
      at.last_action != API_TEST_ACTION || at.last_window != win)
    goto Failure;

  printf("test: wuss_task_get_root finds the parent, else the task\n");
  parent = mk_task(wuss, api_handle, &at);
  if (parent == NULL)
    goto Failure;
  wuss_set_task_parent(wuss, parent);
  child = mk_task(wuss, api_handle, &at);
  wuss_set_task_parent(wuss, NULL);
  if (child == NULL)
    goto Failure;
  if (wuss_task_get_root(child) != parent ||
      wuss_task_get_root(parent) != parent ||
      wuss_task_get_root(task) != task)
    goto Failure;

  printf("test: wuss_task_raise_children reports whether there are "
         "children\n");
  if (!wuss_task_raise_children(wuss, parent) ||
      wuss_task_raise_children(wuss, child))
    goto Failure;

  printf("test: wuss_task_front needs a visible window and brings it "
         "forward\n");
  if (wuss_task_front(wuss, parent))
    goto Failure; /* no windows yet */
  rc = wuss_window_create(child, &(box_t) BOX_POS_SIZE(100, 100, 40, 30),
                          "Child", wuss_WINDOW_DEFAULT, wuss_NO_BACKDROP,
                          SIZE2D(40, 30), SIZE2D(0, 0), &pwin);
  if (rc != result_OK)
    goto Failure;
  wuss_window_restack(win, wuss_ZORDER_FRONT);
  if (wuss_window_is_front(pwin))
    goto Failure;
  if (!wuss_task_front(wuss, child) || !wuss_window_is_front(pwin))
    goto Failure;

  printf("test: wuss_task_is_hidden follows wuss_task_set_hidden\n");
  if (wuss_task_is_hidden(wuss, parent))
    goto Failure;
  rc = wuss_task_set_hidden(wuss, parent, 1);
  if (rc != result_OK || !wuss_task_is_hidden(wuss, parent) ||
      wuss_task_is_hidden(wuss, task))
    goto Failure;
  rc = wuss_task_set_hidden(wuss, parent, 0);
  if (rc != result_OK || wuss_task_is_hidden(wuss, parent))
    goto Failure;

  printf("test: wuss_clear_touched leaves no touched extent\n");
  wuss_clear_touched(wuss);
  if (wuss_get_touched_extent(wuss, &content))
    goto Failure;

#ifdef WUSS_MENUS
  printf("test: wuss_menu_select_index and wuss_menu_should_keep_open read "
         "a MENU_SELECT\n");
  {
    static wuss_menu_item_t items[3] =
    {
      { "One",   wuss_MENU_ITEM_NONE,   NULL },
      { "Two",   wuss_MENU_ITEM_TICKED, NULL },
      { "Three", wuss_MENU_ITEM_TICKED, NULL }
    };
    static wuss_menu_t menu  = { "Menu",  items, NELEMS(items) };
    static wuss_menu_t other = { "Other", items, NELEMS(items) };

    wuss_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind                     = wuss_EVENT_MENU_SELECT;
    ev.data.menu_select.menu    = &menu;
    ev.data.menu_select.index   = 1;
    ev.data.menu_select.button  = wuss_BUTTON_SELECT;

    if (wuss_menu_select_index(&ev, &menu) != 1 ||
        wuss_menu_select_index(&ev, &other) != -1 ||
        wuss_menu_select_index(NULL, &menu) != -1 ||
        wuss_menu_should_keep_open(&ev))
      goto Failure;
    ev.data.menu_select.index = NELEMS(items); /* no such row */
    if (wuss_menu_select_index(&ev, &menu) != -1)
      goto Failure;
    ev.data.menu_select.button = wuss_BUTTON_ADJUST;
    if (!wuss_menu_should_keep_open(&ev))
      goto Failure;
    ev.kind = wuss_EVENT_IDLE;
    if (wuss_menu_select_index(&ev, &menu) != -1 ||
        wuss_menu_should_keep_open(&ev))
      goto Failure;

    printf("test: wuss_menu_tick_exclusive ticks one row, or none\n");
    wuss_menu_tick_exclusive(&menu, 0);
    if (!(items[0].flags & wuss_MENU_ITEM_TICKED) ||
        (items[1].flags & wuss_MENU_ITEM_TICKED) ||
        (items[2].flags & wuss_MENU_ITEM_TICKED))
      goto Failure;
    wuss_menu_tick_exclusive(&menu, -1);
    if ((items[0].flags & wuss_MENU_ITEM_TICKED) ||
        (items[1].flags & wuss_MENU_ITEM_TICKED) ||
        (items[2].flags & wuss_MENU_ITEM_TICKED))
      goto Failure;
  }
#endif

  rc = result_OK;
  goto Cleanup;

Failure:
  printf("api_test: failed (rc=0x%X)\n", rc);
  rc = result_TEST_FAILED;

Cleanup:
  /* not reap_test_tasks: destroying parent also destroys child, which the
   * registry would then destroy again. at outlives the sweep */
  forget_test_tasks();
  wuss_destroy(wuss);
  free(pixels);
  bmfont_destroy(font);

  return rc == result_TEST_FAILED ? result_TEST_FAILED : result_TEST_PASSED;
}
