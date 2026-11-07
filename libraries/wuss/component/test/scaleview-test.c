/* component/test/scaleview-test.c -- wuss_scaleview tests */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "geom/box.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "io/path.h"
#include "wuss/task.h"
#include "wuss/wuss.h"
#include "wuss/window.h"
#include "wuss/component/scaleview.h"

#include "wuss/test/all-tests.h"

#if defined(WUSS_FURNITURE) && defined(WUSS_COMPONENTS)

#include "../../core/impl.h"
#include "../../icon.h"
#include "../../test/harness.h"

/* ----------------------------------------------------------------------- */

#define SV_TEST_W 320
#define SV_TEST_H 240

typedef struct sv_test_state
{
  int calls;
  int choice;
  int percent;
}
sv_test_state_t;

static void sv_test_choice(wuss_scaleview_choice_t choice,
                           int                     percent,
                           void                   *opaque)
{
  sv_test_state_t *state;

  state = opaque;

  state->calls++;
  state->choice  = (int) choice;
  state->percent = percent;
}

/* SELECT-click the button labelled text on the window under the pointer */
static result_t sv_test_click(wuss_t *wuss, const char *text)
{
  result_t       rc;
  wuss_window_t *win;
  wuss_icon_t   *icon;
  int            i;
  box_t          content;
  point_t        p;

  win = NULL;
  rc  = wuss_mouse_move(wuss, POINT(160, 120), &win);
  if (rc != result_OK || win == NULL)
    return result_BAD_ARG;

  icon = NULL;
  for (i = 0; i < win->nicons; i++)
    if (strcmp(wuss_icon_get_text(win->icons[i]), text) == 0)
      icon = win->icons[i];
  if (icon == NULL)
    return result_BAD_ARG;

  wuss_window_get_content_bounds(win, &content);
  p = POINT(content.x0 + (icon->spec.bbox.x0 + icon->spec.bbox.x1) / 2,
            content.y0 + (icon->spec.bbox.y0 + icon->spec.bbox.y1) / 2);

  rc = wuss_mouse_click(wuss, p, wuss_BUTTON_SELECT, wuss_MOUSE_DOWN, NULL);
  if (rc != result_OK)
    return rc;

  return wuss_mouse_click(wuss, p, wuss_BUTTON_SELECT, wuss_MOUSE_UP, NULL);
}

/* ----------------------------------------------------------------------- */

result_t scaleview_test(const char *resources)
{
  result_t          rc;
  bmfont_t         *font;
  void             *pixels;
  bitmap_t          bm;
  screen_t          scr;
  wuss_font_desc_t  fdesc;
  wuss_t           *wuss;
  wuss_scaleview_t *sv;
  sv_test_state_t   state;
  int               claimed;

  memset(&state, 0, sizeof(state));
  sv     = NULL;
  font   = NULL;
  pixels = NULL;
  wuss   = NULL;

  /* the layout measures text, so the wuss needs a real font */
  forget_test_tasks();
  rc = bmfont_create(pathf("%s/resources/bmfonts/Tiny/Regular.png",
                           resources), &font);
  if (rc != result_OK)
    goto Failure;

  pixels = malloc((size_t) SV_TEST_W * 4 * SV_TEST_H);
  if (pixels == NULL)
  {
    rc = result_OOM;
    goto Failure;
  }
  rc = bitmap_init(&bm, SIZE2D(SV_TEST_W, SV_TEST_H), pixelfmt_bgrx8888,
                   SV_TEST_W * 4, NULL, pixels);
  if (rc != result_OK)
    goto Failure;
  screen_for_bitmap(&scr, &bm);

  fdesc.font       = font;
  fdesc.font_class = wuss_FONT_CLASS_NONE;
  fdesc.name       = NULL;
  rc = wuss_create(&scr, &fdesc, 1, NULL, 0, NULL, NULL, resources, &wuss);
  if (rc != result_OK)
    goto Failure;

  printf("test: wuss_scaleview_create rejects NULL arguments\n");
  if (wuss_scaleview_create(NULL, wuss, sv_test_choice, &state) !=
      result_NULL_ARG ||
      wuss_scaleview_create(&sv, wuss, NULL, &state) != result_NULL_ARG ||
      sv != NULL)
    goto Failure;
  wuss_scaleview_destroy(NULL); /* safe */

  rc = wuss_scaleview_create(&sv, wuss, sv_test_choice, &state);
  if (rc != result_OK)
    goto Failure;
  rc = wuss_mouse_move(wuss, POINT(160, 120), NULL);
  if (rc != result_OK)
    goto Failure;

  printf("test: RETURN reports the opening scale\n");
  rc = wuss_scaleview_open(sv, 150);
  if (rc != result_OK)
    goto Failure;
  rc = wuss_key(wuss, wuss_KEY_RETURN, wuss_KEY_MOD_NONE, &claimed);
  if (rc != result_OK || !claimed)
    goto Failure;
  if (state.calls != 1 || state.choice != wuss_SCALEVIEW_SCALE ||
      state.percent != 150)
    goto Failure;

  printf("test: a preset fills the field and Scale reports it\n");
  rc = wuss_scaleview_open(sv, 100);
  if (rc == result_OK)
    rc = sv_test_click(wuss, "200%");
  if (rc != result_OK || state.calls != 1)
    goto Failure; /* a preset does not dismiss */
  rc = sv_test_click(wuss, "Scale");
  if (rc != result_OK)
    goto Failure;
  if (state.calls != 2 || state.choice != wuss_SCALEVIEW_SCALE ||
      state.percent != 200)
    goto Failure;

  printf("test: Fit window reports a fit\n");
  rc = wuss_scaleview_open(sv, 100);
  if (rc == result_OK)
    rc = sv_test_click(wuss, "Fit window");
  if (rc != result_OK || state.calls != 3 ||
      state.choice != wuss_SCALEVIEW_FIT_WINDOW || state.percent != 0)
    goto Failure;

  printf("test: Cancel reports nothing; the scale is clamped\n");
  rc = wuss_scaleview_open(sv, 5000);
  if (rc == result_OK)
    rc = sv_test_click(wuss, "999");
  if (rc != result_OK)
    goto Failure; /* the field shows the clamped value */
  rc = sv_test_click(wuss, "Cancel");
  if (rc != result_OK || state.calls != 3)
    goto Failure;

  wuss_scaleview_destroy(sv);
  sv = NULL;
  goto Cleanup;

Failure:
  printf("scaleview_test: failed (rc=0x%X)\n", rc);
  rc = result_TEST_FAILED;

Cleanup:
  wuss_scaleview_destroy(sv);
  wuss_destroy(wuss);
  free(pixels);
  bmfont_destroy(font);

  return rc == result_TEST_FAILED ? result_TEST_FAILED : result_TEST_PASSED;
}

#else

result_t scaleview_test(const char *resources)
{
  NOT_USED(resources);

  return result_TEST_PASSED;
}

#endif
