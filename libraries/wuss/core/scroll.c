/* wuss/scroll.c -- wuss - minimal window manager */

#include <stdlib.h>

#include "impl.h"

/* Scrolling moves content under a stationary pointer, so the icon the pointer
 * now sits over may differ from before. Re-resolve the hovered icon so a
 * highlighted wuss_ICON_TYPE_MENU_ENTRY does not keep its highlight after
 * scrolling out from under the pointer. */
static void wuss__scroll_rehover(wuss_t        *wuss,
                                 wuss_window_t *win,
                                 point_t        screen_point)
{
#ifdef WUSS_ICONS
  wuss__icon_set_hover(wuss, win,
                       wuss__icon_hit_test(win,
                                           wuss__screen_to_doc(win,
                                                               screen_point)));
#else
  NOT_USED(wuss);
  NOT_USED(win);
  NOT_USED(screen_point);
#endif
}

#ifdef WUSS_ICONS
/* Bump slider "icon" of "win" by "delta" wheel notches of spec.u.slider.step
 * (0 meaning 1), a positive delta moving towards max, and tell the task. */
static result_t wuss__scroll_slider(wuss_window_t *win,
                                    wuss_icon_t   *icon,
                                    int            delta)
{
  int step;

  step = icon->spec.u.slider.step ? abs(icon->spec.u.slider.step) : 1;
  if (icon->spec.u.slider.min > icon->spec.u.slider.max)
    step = -step;

  wuss__icon_set_value(win, icon, icon->value + delta * step);

  return wuss__icon_deliver(win, icon, wuss_MOUSE_MOVE, wuss_BUTTON_NONE);
}

/* The topmost visible, enabled slider of "win" whose full bbox -- groove and
 * decorative surround alike, unlike wuss__icon_hit_test -- contains
 * "doc_point", or NULL. */
static wuss_icon_t *wuss__scroll_slider_at(wuss_window_t *win,
                                           point_t        doc_point)
{
  int          i;
  wuss_icon_t *it;

  for (i = win->nicons - 1; i >= 0; i--)
  {
    it = win->icons[i];
    if (it->spec.type != wuss_ICON_TYPE_SLIDER ||
        (it->spec.flags & (wuss_ICON_FLAGS_HIDDEN | wuss_ICON_FLAGS_DISABLED)))
      continue;

    if (box_contains_point(&it->spec.bbox, doc_point.x, doc_point.y))
      return it;
  }

  return NULL;
}
#endif

result_t wuss_scroll(wuss_t *wuss, point_t p, int delta, wuss_window_t **hit)
{
  wuss_window_t *win;
  int            x, y;

  x = p.x;
  y = p.y;

  win = wuss__window_at(wuss, p);
  if (hit != NULL)
    *hit = win;

  /* core owns all input while a drag is active, as for clicks and keys */
  if (win == NULL || wuss->drag.window != NULL)
    return result_OK;

#ifdef WUSS_FURNITURE
  {
    box_t titlebar;

    wuss__titlebar_box(win, &titlebar);
    if (box_contains_point(&titlebar, x, y))
      return result_OK;
  }
#endif

  {
    wuss_event_t event;

    /* Note the pointer position before scrolling, so the event describes the
     * content the pointer was over when the wheel turned, not the content the
     * step below brings under it. */
    event.kind               = wuss_EVENT_SCROLL;
    event.data.scroll.point  = wuss__screen_to_doc(win, POINT(x, y));
    event.data.scroll.delta   = delta;

#ifdef WUSS_ICONS
    {
      wuss_icon_t *icon;

      /* a wheel anywhere over a slider bumps its value instead of scrolling
       * the window, raised as a MOVE just like a drag step */
      icon = wuss__scroll_slider_at(win, event.data.scroll.point);
      if (icon != NULL)
        return wuss__scroll_slider(win, icon, delta);
    }
#endif

    wuss__scroll_step(win, POINT(0, delta));
    wuss__scroll_rehover(wuss, win, POINT(x, y));

    return wuss__deliver(win->task, win, &event);
  }
}

/* ----------------------------------------------------------------------- */

/* The step to scroll by along one axis of a toolbar's content, lo to hi,
 * with the pointer at p: back near the start, on near the end, else none. */
static int wuss__toolbar_scroll_delta(int p, int lo, int hi)
{
  if (p < lo + WUSS_TOOLBAR_SCROLL_ZONE)
    return -WUSS_TOOLBAR_SCROLL_STEP;
  if (p >= hi - WUSS_TOOLBAR_SCROLL_ZONE)
    return WUSS_TOOLBAR_SCROLL_STEP;
  return 0;
}

/* The extent of a toolbar's content: its shown icons, with a trailing margin
 * matching the leading one, so a document left wider than what is in it
 * gives no blank space to scroll into. Without icons, the document. */
static size2d_t wuss__toolbar_extent(const wuss_window_t *win)
{
#ifdef WUSS_ICONS
  box_t u = BOX_INIT;
  int   i;

  for (i = 0; i < win->nicons; i++)
    if (!(win->icons[i]->spec.flags & wuss_ICON_FLAGS_HIDDEN))
      box_union(&u, &win->icons[i]->spec.bbox, &u);

  if (!box_is_empty(&u))
    return SIZE2D(MIN(win->doc.w, u.x1 + MAX(u.x0, 0)),
                  MIN(win->doc.h, u.y1 + MAX(u.y0, 0)));
#endif

  return win->doc;
}

void wuss__toolbar_scroll_tick(wuss_t *wuss)
{
  wuss_window_t *win;
  box_t          content;
  point_t        p;
  point_t        scroll;
  size2d_t       extent;

  /* a press in progress owns the pointer: a slider being dragged must not
   * slide away under it */
  if (wuss->capture != NULL || wuss->drag.window != NULL)
    return;
#ifdef WUSS_ICONS
  if (wuss->pressed.icon != NULL)
    return;
#endif
#ifdef WUSS_FURNITURE
  if (wuss->furniture.dragging != NULL)
    return;
#endif

  p   = wuss->pointer;
  win = wuss__window_at(wuss, p);
  if (win == NULL || win->toolbar.parent == NULL)
    return;

  /* an axis with a scrollbar scrolls by that instead. an overrun shrunk away
   * since the last scroll springs back once the pointer is over it */
  wuss__content_box(win, &content);
  scroll = win->scroll;
  extent = wuss__toolbar_extent(win);
  if (!(win->flags & wuss_WINDOW_HSCROLL))
    scroll.x = CLAMP(scroll.x + wuss__toolbar_scroll_delta(p.x, content.x0,
                                                           content.x1),
                     0, MAX(extent.w - (content.x1 - content.x0), 0));
  if (!(win->flags & wuss_WINDOW_VSCROLL))
    scroll.y = CLAMP(scroll.y + wuss__toolbar_scroll_delta(p.y, content.y0,
                                                           content.y1),
                     0, MAX(extent.h - (content.y1 - content.y0), 0));
  if (scroll.x == win->scroll.x && scroll.y == win->scroll.y)
    return;

  wuss_window_set_scroll(win, scroll);
  wuss__scroll_rehover(wuss, win, p);
}
