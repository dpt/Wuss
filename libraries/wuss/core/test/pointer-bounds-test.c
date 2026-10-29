/* wuss/core/test/pointer-bounds-test.c -- wuss pointer lock tests */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "geom/box.h"
#include "framebuf/screen.h"
#include "wuss/wuss.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "wuss/test/all-tests.h"

#if defined(WUSS_FURNITURE) && defined(WUSS_ICONS)

#include "wuss/icon-spec.h"
#include "../../icon.h"
#include "../impl.h"
#include "../../test/harness.h"

/* ----------------------------------------------------------------------- */

#define FAIL(what) \
  do { printf("  failed: %s\n", what); goto Failure; } while (0)

static result_t quiet_handle(wuss_window_t      *window,
                             const wuss_event_t *event,
                             void               *task_data)
{
  NOT_USED(window);
  NOT_USED(event);
  NOT_USED(task_data);

  return result_OK;
}

static void press(wuss_t *wuss, point_t p)
{
  wuss_mouse_click(wuss, p, wuss_BUTTON_SELECT, wuss_MOUSE_DOWN, NULL);
}

static void release(wuss_t *wuss, point_t p)
{
  wuss_mouse_click(wuss, p, wuss_BUTTON_SELECT, wuss_MOUSE_UP, NULL);
}

static int scroll_y_at(wuss_t *wuss, wuss_window_t *window, point_t p)
{
  point_t scroll;

  wuss_mouse_move(wuss, p, NULL);
  wuss_window_get_scroll(window, &scroll);
  return scroll.y;
}

static size2d_t content_size_at(wuss_t        *wuss,
                                wuss_window_t *window,
                                point_t        p)
{
  box_t content;

  wuss_mouse_move(wuss, p, NULL);
  wuss_window_get_content_bounds(window, &content);
  return SIZE2D(content.x1 - content.x0, content.y1 - content.y0);
}

/* ----------------------------------------------------------------------- */

result_t wuss_pointer_bounds_test(const char *resources)
{
  result_t         rc;
  wuss_fixture_t   fixture;
  wuss_t          *wuss;
  wuss_task_t     *task;
  box_t            winbox;
  wuss_window_t   *window;
  wuss_icon_spec_t spec;
  wuss_icon_t     *slider;
  box_t            bounds;
  box_t            want;
  box_t            content;
  box_t            screen_box;
  box_t            well;
  box_t            sausage;
  box_t            resize;
  point_t          at;
  int              x;
  size2d_t         min;
  size2d_t         size;

  NOT_USED(resources);

  rc = wuss_fixture_open(&fixture, 240, 240);
  if (rc != result_OK)
    goto Failure;
  wuss = fixture.wuss;

  task = mk_task(wuss, quiet_handle, NULL);
  if (task == NULL)
    goto Failure;

  /* a document 60 taller than the content, small enough that a full-size
   * resize stays on screen */
  winbox.x0 = 10; winbox.y0 = 30; winbox.x1 = 110; winbox.y1 = 90;
  rc = wuss_window_create(task, &winbox, "B", wuss_WINDOW_DEFAULT,
                          wuss_NO_BACKDROP, SIZE2D(160, 120), SIZE2D(0, 0),
                          &window);
  if (rc != result_OK)
    goto Failure;

  wuss_icon_spec_slider(&spec, (box_t) { 0, 0, 58, 20 },
                        wuss_SLIDER_HORIZONTAL, 0, 255, 0);
  rc = wuss_icon_create(window, &spec, &slider);
  if (rc != result_OK)
    goto Failure;

  printf("test: nothing held, nothing locked, and no bounds can be set\n");

  if (wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("idle lock");
  if (wuss_set_pointer_bounds(wuss, &(box_t) { 0, 0, 10, 10 }) !=
      result_BAD_ARG)
    FAIL("idle set");

  printf("test: a slider press locks to its groove, every value reachable\n");

  wuss_window_get_content_bounds(window, &content);
  wuss__icon_box_to_screen(&content, POINT(0, 0), &slider->spec.bbox,
                           &screen_box);
  wuss__slider_groove_box(&screen_box, &want);

  press(wuss, POINT(want.x0 + 5, want.y0 + 5));
  if (!wuss_get_pointer_bounds(wuss, &bounds) || !box_equal(&bounds, &want))
    FAIL("slider bounds");

  /* more values than pixels: only the groove's last pixel reads the
   * maximum */
  wuss_mouse_move(wuss, POINT(bounds.x1 - 1, bounds.y0), NULL);
  if (wuss_icon_get_value(slider) != 255)
    FAIL("slider maximum out of bounds");
  wuss_mouse_move(wuss, POINT(bounds.x0, bounds.y0), NULL);
  if (wuss_icon_get_value(slider) != 0)
    FAIL("slider minimum out of bounds");

  release(wuss, POINT(bounds.x0, bounds.y0));
  if (wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("slider lock outlived its release");

  printf("test: the next press drops a stale lock; a capture's task can set "
         "one\n");

  at = POINT(content.x0 + 40, content.y0 + 40);
  press(wuss, at);
  if (wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("slider lock leaked into a capture");
  if (wuss_set_pointer_bounds(wuss, &(box_t) { 1, 2, 3, 4 }) != result_OK ||
      !wuss_get_pointer_bounds(wuss, &bounds) ||
      !box_equal(&bounds, &(box_t) { 1, 2, 3, 4 }))
    FAIL("capture set");
  if (wuss_set_pointer_bounds(wuss, NULL) != result_OK ||
      wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("capture clear");

  (void) wuss_set_pointer_bounds(wuss, &(box_t) { 1, 2, 3, 4 });

  printf("test: a core drag starts unbounded and Escape ends its lock\n");

  rc = wuss_drag_start(wuss, window, SIZE2D(8, 8), POINT(0, 0));
  if (rc != result_OK)
    goto Failure;
  if (wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("capture lock leaked into a drag");
  if (wuss_set_pointer_bounds(wuss, &(box_t) { 5, 6, 7, 8 }) != result_OK ||
      !wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("drag set");
  (void) wuss_key(wuss, wuss_KEY_ESCAPE, wuss_KEY_MOD_NONE, NULL);
  if (wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("drag lock outlived Escape");

  printf("test: a sausage press locks to the sausage's travel, exactly\n");

  wuss_window_set_scroll(window, POINT(0, 20));
  wuss__vscroll_well_box(window, &well);
  wuss__vscroll_sausage_box(window, &sausage);
  x = (sausage.x0 + sausage.x1) / 2;
  press(wuss, POINT(x, (sausage.y0 + sausage.y1) / 2));
  if (!wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("no sausage lock");
  if (bounds.x0 != well.x0 || bounds.x1 != well.x1)
    FAIL("sausage cross-axis bounds");
  if (scroll_y_at(wuss, window, POINT(x, bounds.y0)) != 0 ||
      scroll_y_at(wuss, window, POINT(x, bounds.y0 + 1)) == 0)
    FAIL("sausage top bound");
  if (scroll_y_at(wuss, window, POINT(x, bounds.y1 - 1)) != 60 ||
      scroll_y_at(wuss, window, POINT(x, bounds.y1 - 2)) == 60)
    FAIL("sausage bottom bound");
  release(wuss, POINT(x, bounds.y1 - 1));

  printf("test: a resize press locks to the sizes the window allows\n");

  wuss__min_content(window, &min);
  wuss__resize_box(window, &resize);
  at = POINT((resize.x0 + resize.x1) / 2, (resize.y0 + resize.y1) / 2);
  press(wuss, at);
  if (!wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("no resize lock");
  size = content_size_at(wuss, window, POINT(bounds.x0, bounds.y0));
  if (size.w != min.w || size.h != min.h)
    FAIL("resize minimum bound");
  size = content_size_at(wuss, window, POINT(bounds.x1 - 1, bounds.y1 - 1));
  if (size.w != 160 || size.h != 120)
    FAIL("resize maximum bound");
  release(wuss, POINT(bounds.x1 - 1, bounds.y1 - 1));
  if (wuss_get_pointer_bounds(wuss, &bounds))
    FAIL("resize lock outlived its release");

  wuss_window_close(window);
  wuss_fixture_close(&fixture);

  return result_TEST_PASSED;


Failure:
  printf("wuss_pointer_bounds_test: failed (rc=0x%X)\n", rc);
  wuss_fixture_close(&fixture);

  return result_TEST_FAILED;
}

#else

result_t wuss_pointer_bounds_test(const char *resources)
{
  NOT_USED(resources);
  return result_TEST_PASSED;
}

#endif
