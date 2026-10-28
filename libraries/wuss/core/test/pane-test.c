/* core/test/pane-test.c -- wuss pane window tests */

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
#include "../../furniture.h"
#include "../../test/harness.h"

/* ----------------------------------------------------------------------- */

#define PANE_FLAGS (wuss_WINDOW_NO_TITLEBAR | wuss_WINDOW_NO_OUTLINE)

static result_t mk_pane(wuss_task_t    *task,
                        int             w,
                        int             h,
                        wuss_window_t **window)
{
  box_t box;

  box = (box_t) BOX_POS_SIZE(10, 10, w, h);

  return wuss_window_create(task, &box, NULL, PANE_FLAGS, wuss_NO_BACKDROP,
                            SIZE2D(w, h), SIZE2D(0, 0), window);
}

static int width(const box_t *b)
{
  return b->x1 - b->x0;
}

static int height(const box_t *b)
{
  return b->y1 - b->y0;
}

/* Non-zero if the group runs toolbar, then the panes in any order, then
 * parent, front to back with nothing between. */
static int group_order_ok(wuss_t        *wuss,
                          wuss_window_t *toolbar,
                          wuss_window_t *parent)
{
  wuss_window_t *w;

  for (w = wuss__z_first(wuss); w != NULL && w != toolbar;
       w = wuss__z_below(w))
    ;
  if (w == NULL)
    return 0;

  for (w = wuss__z_below(w); w != NULL && w != parent; w = wuss__z_below(w))
    if (!wuss__window_is_pane(w) || w->toolbar.parent != parent)
      return 0;

  return w == parent;
}

result_t wuss_pane_test(const char *resources)
{
  result_t       rc;
  wuss_fixture_t fixture;
  wuss_t        *wuss;

  NOT_USED(resources);

  rc = wuss_fixture_open(&fixture, 300, 300);
  if (rc != result_OK)
    goto Failure;
  wuss = fixture.wuss;

  printf("test: panes: carving, attach order, hiding, resizing, toggle-size and refusals\n");

  {
    wuss_task_t   *owner;
    wuss_window_t *parent, *top, *left, *bottom, *bad, *toolbar;
    box_t          box, inner, pv0, pv, pc, vt, vl, vb;
    int            dw, dh;
    size2d_t       min;

    owner = mk_task(wuss, paint_handle, NULL);
    if (owner == NULL) goto Failure;

    box = (box_t) BOX_POS_SIZE(100, 100, 100, 80);
    rc  = wuss_window_create(owner, &box, "P", wuss_WINDOW_DEFAULT,
                             wuss_NO_BACKDROP, SIZE2D(400, 400), SIZE2D(0, 0),
                             &parent);
    if (rc != result_OK) goto Failure;

    wuss_window_get_visible_bounds(parent, &pv0);
    wuss_window_get_content_bounds(parent, &inner);
    dw = width(&pv0) - width(&inner);
    dh = height(&pv0) - height(&inner);

    if (mk_pane(owner, 5, 10, &top)    != result_OK) goto Failure;
    if (mk_pane(owner, 12, 5, &left)   != result_OK) goto Failure;
    if (mk_pane(owner, 5, 8, &bottom)  != result_OK) goto Failure;

    /* refused: titlebar furniture, too thick for the parent */
    box = (box_t) BOX_POS_SIZE(10, 10, 20, 20);
    rc  = wuss_window_create(owner, &box, "B", wuss_WINDOW_DEFAULT,
                             wuss_NO_BACKDROP, SIZE2D(20, 20), SIZE2D(0, 0),
                             &bad);
    if (rc != result_OK) goto Failure;
    if (wuss_window_attach_pane(bad, parent, wuss_EDGE_TOP) != result_BAD_ARG)
      goto Failure;
    wuss_window_close(bad);

    if (mk_pane(owner, 5, 70, &bad) != result_OK) goto Failure;
    if (wuss_window_attach_pane(bad, parent, wuss_EDGE_TOP) !=
        result_WUSS_TOO_SMALL) goto Failure;
    wuss_window_close(bad);

    /* attach order: the first pane spans the corners */
    if (wuss_window_attach_pane(top, parent, wuss_EDGE_TOP) != result_OK)
      goto Failure;
    if (wuss_window_attach_pane(left, parent, wuss_EDGE_LEFT) != result_OK)
      goto Failure;
    if (wuss_window_attach_pane(bottom, parent, wuss_EDGE_BOTTOM) !=
        result_OK) goto Failure;

    /* refused: panes or toolbars on a pane, a pane on a toolbar */
    if (mk_pane(owner, 5, 5, &bad) != result_OK) goto Failure;
    if (wuss_window_attach_pane(bad, top, wuss_EDGE_TOP) != result_BAD_ARG)
      goto Failure;
    if (wuss_window_attach(bad, top, wuss_EDGE_TOP, wuss_ALIGN_LEFT, 0) !=
        result_BAD_ARG) goto Failure;
    box = (box_t) BOX_POS_SIZE(10, 10, 20, 20);
    rc  = wuss_window_create(owner, &box, NULL, wuss_WINDOW_NO_TITLEBAR,
                             wuss_NO_BACKDROP, SIZE2D(20, 20), SIZE2D(0, 0),
                             &toolbar);
    if (rc != result_OK) goto Failure;
    if (wuss_window_attach(toolbar, parent, wuss_EDGE_RIGHT, wuss_ALIGN_TOP,
                           0) != result_OK) goto Failure;
    if (wuss_window_attach_pane(bad, toolbar, wuss_EDGE_TOP) != result_BAD_ARG)
      goto Failure;
    wuss_window_close(bad);

    /* the footprint holds; the panes carve the content */
    wuss_window_get_visible_bounds(parent, &pv);
    wuss_window_get_content_bounds(parent, &pc);
    wuss_window_get_visible_bounds(top, &vt);
    wuss_window_get_visible_bounds(left, &vl);
    wuss_window_get_visible_bounds(bottom, &vb);
    if (!box_equal(&pv, &pv0)) goto Failure;
    if (vt.x0 != inner.x0 || vt.x1 != inner.x1 ||
        vt.y0 != inner.y0 || height(&vt) != 10) goto Failure;
    if (vl.x0 != inner.x0 || width(&vl) != 12 ||
        vl.y0 != vt.y1 + 1 || vl.y1 != inner.y1) goto Failure;
    if (vb.x0 != vl.x1 + 1 || vb.x1 != inner.x1 ||
        vb.y1 != inner.y1 || height(&vb) != 8) goto Failure;
    if (pc.x0 != vl.x1 + 1 || pc.y0 != vt.y1 + 1 ||
        pc.x1 != inner.x1  || pc.y1 != vb.y0 - 1) goto Failure;

    /* an outside toolbar aligns to the un-carved box */
    wuss_window_get_content_bounds(toolbar, &box);
    if (box.y0 != inner.y0) goto Failure;

    /* panes are part of the parent: its toolbars stay in front of them */
    if (!group_order_ok(wuss, toolbar, parent)) goto Failure;
    wuss_window_restack(parent, wuss_ZORDER_BACK);
    if (!group_order_ok(wuss, toolbar, parent)) goto Failure;
    wuss_window_restack(top, wuss_ZORDER_FRONT);
    if (!group_order_ok(wuss, toolbar, parent)) goto Failure;
    wuss_window_set_stack(parent, wuss_STACK_TOP);
    if (!group_order_ok(wuss, toolbar, parent)) goto Failure;
    wuss_window_set_stack(parent, wuss_STACK_MIDDLE);
    wuss_window_close(toolbar);

    /* a pane can't be moved by itself; resizing changes only its thickness */
    wuss_window_move(left, POINT(5, 5));
    wuss_window_get_visible_bounds(left, &vl);
    if (vl.x0 != inner.x0) goto Failure;
    rc = wuss_window_resize(left, SIZE2D(16, 3));
    if (rc != result_OK) goto Failure;
    wuss_window_get_visible_bounds(left, &vl);
    wuss_window_get_content_bounds(parent, &pc);
    if (width(&vl) != 16 || vl.y1 != inner.y1 || pc.x0 != vl.x1 + 1)
      goto Failure;

    /* hiding a pane gives its room back */
    rc = wuss_window_set_hidden(left, 1);
    if (rc != result_OK) goto Failure;
    wuss_window_get_content_bounds(parent, &pc);
    wuss_window_get_visible_bounds(bottom, &vb);
    if (pc.x0 != inner.x0 || vb.x0 != inner.x0) goto Failure;
    rc = wuss_window_set_hidden(left, 0);
    if (rc != result_OK) goto Failure;
    wuss_window_get_content_bounds(parent, &pc);
    wuss_window_get_visible_bounds(bottom, &vb);
    if (pc.x0 != inner.x0 + 17 || vb.x0 != inner.x0 + 17) goto Failure;

    /* moving places the carved content */
    wuss_window_move(parent, POINT(80, 90));
    wuss_window_get_content_bounds(parent, &pc);
    wuss_window_get_visible_bounds(left, &vl);
    if (pc.x0 != 80 || pc.y0 != 90 || pc.x0 != vl.x1 + 1) goto Failure;

    /* resizing sizes the carved content; the panes add on top */
    rc = wuss_window_resize(parent, SIZE2D(40, 30));
    if (rc != result_OK) goto Failure;
    wuss_window_get_visible_bounds(parent, &pv);
    wuss_window_get_content_bounds(parent, &pc);
    wuss_window_get_visible_bounds(top, &vt);
    if (width(&pc) != 40 || height(&pc) != 30) goto Failure;
    if (width(&pv) != dw + 40 + 17 || height(&pv) != dh + 30 + 11 + 9)
      goto Failure;
    if (width(&vt) != 40 + 17) goto Failure;

    /* toggle-size keeps the whole footprint on screen */
    wuss__furniture_toggle_size(parent);
    wuss_window_get_visible_bounds(parent, &pv);
    wuss_window_get_content_bounds(parent, &pc);
    if (pv.x0 < 0 || pv.y0 < 0 || pv.x1 > 300 || pv.y1 > 300) goto Failure;
    if (width(&pv) != dw + width(&pc) + 17) goto Failure;
    wuss__furniture_toggle_size(parent);
    wuss_window_get_content_bounds(parent, &pc);
    if (width(&pc) != 40 || height(&pc) != 30) goto Failure;

    /* a divider is frame, not content */
    wuss_window_get_visible_bounds(left, &vl);
    if (wuss__furniture_hit_test(parent, POINT(vl.x1, vl.y0)) !=
        wuss_FURNITURE_TITLE) goto Failure;

    /* hiding the parent keeps its panes' room */
    rc = wuss_window_set_hidden(parent, 1);
    if (rc != result_OK) goto Failure;
    wuss_window_get_content_bounds(parent, &box);
    if (!box_equal(&box, &pc)) goto Failure;
    rc = wuss_window_set_hidden(parent, 0);
    if (rc != result_OK) goto Failure;
    wuss_window_get_content_bounds(parent, &box);
    if (!box_equal(&box, &pc) || (left->flags & wuss_WINDOW_HIDDEN))
      goto Failure;

    /* a pane overhanging the screen edge keeps its length */
    wuss_window_move(parent, POINT(270, 90));
    rc = wuss_window_resize(bottom, SIZE2D(5, 7));
    if (rc != result_OK) goto Failure;
    wuss_window_get_visible_bounds(top, &vt);
    wuss_window_get_visible_bounds(bottom, &vb);
    if (vb.x1 != vt.x1 || height(&vb) != 7) goto Failure;
    rc = wuss_window_resize(bottom, SIZE2D(5, 8));
    if (rc != result_OK) goto Failure;
    wuss_window_move(parent, POINT(80, 90));

    /* a pane too thick for the parent is thinned to leave the content its
     * minimum */
    rc = wuss_window_resize(left, SIZE2D(200, 3));
    if (rc != result_OK) goto Failure;
    wuss_window_get_content_bounds(parent, &box);
    wuss__min_content(parent, &min);
    if (width(&box) != min.w) goto Failure;
    rc = wuss_window_resize(left, SIZE2D(16, 3));
    if (rc != result_OK) goto Failure;
    wuss_window_get_content_bounds(parent, &box);
    if (!box_equal(&box, &pc)) goto Failure;

    rc = wuss_redraw_dirty(wuss);
    if (rc != result_OK) goto Failure;

    /* a detached pane gives its room back and becomes an ordinary window */
    wuss_window_get_visible_bounds(parent, &pv0);
    wuss_window_get_content_bounds(parent, &pc);
    box = pc;
    wuss_window_detach(left);
    wuss_window_get_visible_bounds(parent, &pv);
    wuss_window_get_content_bounds(parent, &pc);
    if (!box_equal(&pv, &pv0) || pc.x0 != box.x0 - 17) goto Failure;
    if (wuss__window_is_pane(left)) goto Failure;
    wuss_window_close(left);

    /* closing the parent takes the rest with it */
    wuss_window_close(parent);
    if (owner->windows.next != NULL) goto Failure;

    rc = wuss_redraw_dirty(wuss);
    if (rc != result_OK) goto Failure;
  }

  wuss_fixture_close(&fixture);

  return result_TEST_PASSED;

Failure:
  printf("wuss_pane_test: failed (rc=0x%X)\n", rc);
  wuss_fixture_close(&fixture);

  return result_TEST_FAILED;
}

#else

result_t wuss_pane_test(const char *resources)
{
  NOT_USED(resources);
  return result_TEST_PASSED;
}

#endif
