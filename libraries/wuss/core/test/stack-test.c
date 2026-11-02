/* core/test/stack-test.c -- wuss window stack tests */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "geom/box.h"
#include "wuss/wuss.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "framebuf/screen.h"
#include "wuss/icon-spec.h"
#include "../../icon.h"

#include "wuss/test/all-tests.h"

#if defined(WUSS_FURNITURE) && defined(WUSS_ICONS)

#include "../../core/impl.h"
#include "../../test/harness.h"

/* ----------------------------------------------------------------------- */


/* ----------------------------------------------------------------------- */

/* Non-zero if want[0..n-1] appear in that front-to-back order across all
 * three stacks. Other windows may be interleaved. */
static int z_order_is(wuss_t *wuss, wuss_window_t *const *want, int n)
{
  wuss_window_t *w;
  int            i;

  i = 0;
  for (w = wuss__z_first(wuss); w != NULL && i < n; w = wuss__z_below(w))
    if (w == want[i])
      i++;

  return i == n;
}

result_t wuss_stack_test(const char *resources)
{
  result_t       rc;
  wuss_fixture_t fixture;
  wuss_t        *wuss;

  rc = wuss_fixture_open(&fixture, 200, 200);
  if (rc != result_OK)
    goto Failure;
  wuss = fixture.wuss;

  printf("test: window stacks: top over middle over back, restack stays within a stack, set_stack relinks\n");

  {
    wuss_task_t   *owner;
    wuss_window_t *back, *mid_a, *mid_b, *top, *bad;
    wuss_window_t *order[4];
    box_t          box;

    owner = mk_task(wuss, paint_handle, NULL);
    if (owner == NULL) goto Failure;

    /* four windows stacked on the same spot, created back stack first */
    box = (box_t) BOX_POS_SIZE(20, 40, 60, 60);
    rc  = wuss_window_create(owner, &box, "B", wuss_WINDOW_DEFAULT |
                             wuss_WINDOW_STACK_BACK, wuss_NO_BACKDROP,
                             SIZE2D(60, 60), SIZE2D(20, 20), &back);
    if (rc != result_OK) goto Failure;
    rc = wuss_window_create(owner, &box, "A", wuss_WINDOW_DEFAULT,
                            wuss_NO_BACKDROP, SIZE2D(60, 60), SIZE2D(20, 20),
                            &mid_a);
    if (rc != result_OK) goto Failure;
    rc = wuss_window_create(owner, &box, "M", wuss_WINDOW_DEFAULT,
                            wuss_NO_BACKDROP, SIZE2D(60, 60), SIZE2D(20, 20),
                            &mid_b);
    if (rc != result_OK) goto Failure;
    rc = wuss_window_create(owner, &box, "T", wuss_WINDOW_DEFAULT |
                            wuss_WINDOW_STACK_TOP, wuss_NO_BACKDROP,
                            SIZE2D(60, 60), SIZE2D(20, 20), &top);
    if (rc != result_OK) goto Failure;

    /* a window is frontmost in its stack, and every stack is in front of
     * the next regardless of creation order */
    order[0] = top; order[1] = mid_b; order[2] = mid_a; order[3] = back;
    if (!z_order_is(wuss, order, 4)) goto Failure;
    if (wuss__window_at(wuss, POINT(50, 80)) != top) goto Failure;

    /* front and back only move a window within its own stack */
    wuss_window_restack(mid_a, wuss_ZORDER_FRONT);
    order[1] = mid_a; order[2] = mid_b;
    if (!z_order_is(wuss, order, 4)) goto Failure;

    /* frontmost is per stack */
    if (!wuss_window_is_front(mid_a) || wuss_window_is_front(mid_b) ||
        !wuss_window_is_front(top) || !wuss_window_is_front(back))
      goto Failure;

    wuss_window_restack(back, wuss_ZORDER_FRONT);
    wuss_window_restack(top, wuss_ZORDER_BACK);
    if (!z_order_is(wuss, order, 4)) goto Failure; /* each alone in its stack */

    wuss_window_restack(mid_a, wuss_ZORDER_BACK);
    order[1] = mid_b; order[2] = mid_a;
    if (!z_order_is(wuss, order, 4)) goto Failure;

    /* set_stack lands at the front of the destination stack */
    wuss_window_set_stack(mid_a, wuss_STACK_TOP);
    order[0] = mid_a; order[1] = top; order[2] = mid_b;
    if (!z_order_is(wuss, order, 4)) goto Failure;
    if (wuss__window_at(wuss, POINT(50, 80)) != mid_a) goto Failure;

    wuss_window_set_stack(mid_a, wuss_STACK_BACK);
    order[0] = top; order[1] = mid_b; order[2] = mid_a; order[3] = back;
    if (!z_order_is(wuss, order, 4)) goto Failure;

    rc = wuss_redraw_dirty(wuss);
    if (rc != result_OK) goto Failure;

    /* a window cannot be in two stacks at once */
    rc = wuss_window_create(owner, &box, "X", wuss_WINDOW_STACK_TOP |
                            wuss_WINDOW_STACK_BACK, wuss_NO_BACKDROP,
                            SIZE2D(60, 60), SIZE2D(20, 20), &bad);
    if (rc != result_BAD_ARG) goto Failure;

    wuss_window_close(top);
    wuss_window_close(mid_a);
    wuss_window_close(mid_b);
    wuss_window_close(back);

    rc = wuss_redraw_dirty(wuss);
    if (rc != result_OK) goto Failure;
  }

  printf("test: window stacks: the backdrop window is full screen, behind everything and stays there\n");

  {
    wuss_task_t   *owner;
    wuss_window_t *bd, *back, *mid, *bad;
    wuss_window_t *order[3];
    box_t          box;

    owner = mk_task(wuss, paint_handle, NULL);
    if (owner == NULL) goto Failure;

    /* the content box passed is ignored */
    box = (box_t) BOX_POS_SIZE(20, 40, 60, 60);
    rc  = wuss_window_create(owner, &box, NULL, wuss_WINDOW_BACKDROP |
                             wuss_WINDOW_STACK_TOP, wuss_NO_BACKDROP,
                             SIZE2D(60, 60), SIZE2D(20, 20), &bd);
    if (rc != result_OK) goto Failure;
    if (bd->stack != wuss_STACK_BACK) goto Failure;
    if (bd->visible.x0 != 0 || bd->visible.y0 != 0 ||
        bd->visible.x1 != 200 || bd->visible.y1 != 200)
      goto Failure;
    if (bd->doc.w != 200 || bd->doc.h != 200) goto Failure;

    /* a back-stack window created later still sits in front of it */
    rc = wuss_window_create(owner, &box, "B", wuss_WINDOW_DEFAULT |
                            wuss_WINDOW_STACK_BACK, wuss_NO_BACKDROP,
                            SIZE2D(60, 60), SIZE2D(20, 20), &back);
    if (rc != result_OK) goto Failure;
    rc = wuss_window_create(owner, &box, "M", wuss_WINDOW_DEFAULT,
                            wuss_NO_BACKDROP, SIZE2D(60, 60), SIZE2D(20, 20),
                            &mid);
    if (rc != result_OK) goto Failure;

    order[0] = mid; order[1] = back; order[2] = bd;
    if (!z_order_is(wuss, order, 3)) goto Failure;
    if (wuss__window_at(wuss, POINT(150, 20)) != bd) goto Failure;
    if (wuss__window_at(wuss, POINT(50, 80)) != mid) goto Failure;

    /* restacking and changing stack leave it at the very back */
    wuss_window_restack(bd, wuss_ZORDER_FRONT);
    wuss_window_set_stack(bd, wuss_STACK_TOP);
    if (!z_order_is(wuss, order, 3) || bd->stack != wuss_STACK_BACK)
      goto Failure;

    /* pinned: its box is wuss's alone */
    if (wuss_window_resize(bd, SIZE2D(50, 50)) != result_WUSS_PINNED)
      goto Failure;
    wuss_window_move(bd, POINT(10, 10));
    if (bd->visible.x0 != 0 || bd->visible.y0 != 0) goto Failure;

    /* only one */
    rc = wuss_window_create(owner, &box, NULL, wuss_WINDOW_BACKDROP,
                            wuss_NO_BACKDROP, SIZE2D(60, 60), SIZE2D(20, 20),
                            &bad);
    if (rc != result_WUSS_BACKDROP_EXISTS) goto Failure;

    rc = wuss_redraw_dirty(wuss);
    if (rc != result_OK) goto Failure;

    /* once closed, another may be made */
    wuss_window_close(bd);
    rc = wuss_window_create(owner, &box, NULL, wuss_WINDOW_BACKDROP,
                            wuss_NO_BACKDROP, SIZE2D(60, 60), SIZE2D(20, 20),
                            &bd);
    if (rc != result_OK) goto Failure;

    wuss_window_close(bd);
    wuss_window_close(mid);
    wuss_window_close(back);

    rc = wuss_redraw_dirty(wuss);
    if (rc != result_OK) goto Failure;
  }

  wuss_fixture_close(&fixture);

  return result_TEST_PASSED;

Failure:
  printf("wuss_stack_test: failed (rc=0x%X)\n", rc);
  wuss_fixture_close(&fixture);

  return result_TEST_FAILED;
}

#else

result_t wuss_stack_test(const char *resources)
{
  NOT_USED(resources);
  return result_TEST_PASSED;
}

#endif
