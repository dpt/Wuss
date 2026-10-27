/* wuss/iconbar/create.c -- add an icon to the icon bar */

#include <assert.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "../core/types.h"
#include "../core/alloc.h"
#include "../core/invalidate.h"

#ifdef WUSS_ICONBAR

/* The bar's window never receives events of its own -- icons are delivered
 * to their individual owner tasks, not this one -- so its handle is never
 * actually called; it exists only to satisfy wuss_task_create's contract. */
static result_t iconbar_window_handle(wuss_window_t      *window,
                                      const wuss_event_t *event,
                                      void               *task_data)
{
  (void) window;
  (void) event;
  (void) task_data;

  return result_OK;
}

/* The internal task that owns the bar's window, created on the first
 * wuss_iconbar_icon_create of a session. Mirrors wuss__menu_task in
 * libraries/wuss/menu/menu.c. */
static wuss_task_t *iconbar_task(wuss_t *wuss)
{
  wuss_task_desc_t desc;

  if (wuss->iconbar.task != NULL)
    return wuss->iconbar.task;

  desc.handle    = iconbar_window_handle;
  desc.task_data = wuss;
  desc.name      = "wuss:iconbar";

  if (wuss_task_create(wuss, &desc, &wuss->iconbar.task) != result_OK)
    return NULL;

  return wuss->iconbar.task;
}

/* Lazily create the bar's own pinned, chromeless window on the first icon.
 * No-op if it already exists. */
static result_t iconbar_window_ensure(wuss_t *wuss)
{
  wuss_task_t   *task;
  box_t          content;
  wuss_window_t *win;
  result_t       rc;

  if (wuss->iconbar.window != NULL)
    return result_OK;

  task = iconbar_task(wuss);
  if (task == NULL)
    return result_OOM;

  /* inset by WUSS_ICONBAR_OUTLINE so the outline furniture wuss_window_create
   * grows the box by lands exactly on the strip's edges, keeping the bar's
   * visible box flush to the screen's bottom/sides as before. */
  content.x0 = WUSS_ICONBAR_OUTLINE;
  content.y0 = wuss->scr->size.h - WUSS_ICONBAR_HEIGHT + WUSS_ICONBAR_OUTLINE;
  content.x1 = wuss->scr->size.w - WUSS_ICONBAR_OUTLINE;
  content.y1 = wuss->scr->size.h - WUSS_ICONBAR_OUTLINE;

  rc = wuss_window_create(task, &content, NULL,
                          wuss_WINDOW_NO_TITLEBAR |
                          wuss_WINDOW_NO_REDRAW   | wuss_WINDOW_PINNED |
                          wuss_WINDOW_STACK_BACK,
                          wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                          SIZE2D(content.x1 - content.x0,
                                 content.y1 - content.y0),
                          SIZE2D(content.x1 - content.x0,
                                 content.y1 - content.y0),
                          &win);
  if (rc != result_OK)
    return rc;

  wuss->iconbar.window = win;

  return result_OK;
}

/* Array index at which to insert an icon of side and priority. The array
 * holds each side's icons from its outer edge inward, so scanning from the
 * edge stops at the first lower priority and scanning from the middle at the
 * first higher one (walking back from the end). */
static int iconbar_scan(const wuss_t       *wuss,
                        wuss_iconbar_side_t side,
                        int                 priority,
                        wuss_iconbar_scan_t scan)
{
  int                        pos;
  int                        i;
  const wuss_iconbar_icon_t *other;

  pos = wuss->iconbar.n;
  if (scan == wuss_ICONBAR_SCAN_FROM_EDGE)
  {
    for (i = 0; i < wuss->iconbar.n; i++)
    {
      other = wuss->iconbar.icons[i];
      if (other->spec.side == side && other->spec.priority < priority)
        return i;
    }
  }
  else
  {
    for (i = wuss->iconbar.n - 1; i >= 0; i--)
    {
      other = wuss->iconbar.icons[i];
      if (other->spec.side != side)
        continue;
      if (other->spec.priority > priority)
        break;
      pos = i;
    }
  }

  return pos;
}

/* Array index at which to insert an icon beside anchor, or -1 if anchor is
 * not on the bar. On the left side outward is leftward, on the right side
 * rightward. */
static int iconbar_beside(const wuss_t              *wuss,
                          const wuss_iconbar_icon_t *anchor,
                          wuss_iconbar_beside_t      beside)
{
  int i;
  int outward;

  for (i = 0; i < wuss->iconbar.n; i++)
  {
    if (wuss->iconbar.icons[i] == anchor)
    {
      outward = (anchor->spec.side == wuss_ICONBAR_RIGHT)
                  ? (beside == wuss_ICONBAR_RIGHT_OF)
                  : (beside == wuss_ICONBAR_LEFT_OF);
      return outward ? i : i + 1;
    }
  }

  return -1;
}

result_t wuss_iconbar_icon_create(wuss_t                         *wuss,
                                  wuss_task_t                    *owner,
                                  const wuss_iconbar_icon_spec_t *spec,
                                  wuss_iconbar_icon_t           **icon)
{
  result_t             rc;
  int                  pos;
  wuss_iconbar_icon_t *it;
  box_t                slot;

  assert(wuss  != NULL);
  assert(owner != NULL);
  assert(spec  != NULL);

  if (spec->anchor != NULL)
  {
    pos = iconbar_beside(wuss, spec->anchor, spec->beside);
    if (pos < 0)
      return result_BAD_ARG;
  }
  else
  {
    pos = iconbar_scan(wuss, spec->side, spec->priority, spec->scan);
  }

  rc = iconbar_window_ensure(wuss);
  if (rc != result_OK)
    return rc;

  if (array_grow_alloc(&wuss->alloc, (void **) &wuss->iconbar.icons,
                       sizeof(*wuss->iconbar.icons), wuss->iconbar.n,
                       &wuss->iconbar.cap, 1, 4) != 0)
    return result_OOM;

  it = wuss__malloc(wuss, sizeof(*it));
  if (it == NULL)
    return result_OOM;

  it->spec        = *spec;
  it->spec.text   = alloc_strdup(&wuss->alloc,
                                 spec->text != NULL ? spec->text : "");
  it->owner       = owner;

  if (it->spec.text == NULL)
  {
    wuss__free(wuss, it);
    return result_OOM;
  }

  /* an associated icon joins its anchor's side as a peer; the link itself
   * is not kept */
  if (spec->anchor != NULL)
  {
    it->spec.side     = spec->anchor->spec.side;
    it->spec.priority = spec->anchor->spec.priority;
    it->spec.anchor   = NULL;
  }

  memmove(&wuss->iconbar.icons[pos + 1], &wuss->iconbar.icons[pos],
          (size_t) (wuss->iconbar.n - pos) * sizeof(wuss->iconbar.icons[0]));
  wuss->iconbar.icons[pos] = it;
  wuss->iconbar.n++;

  /* the whole bar: a full bar re-lays out (strip mode) when an icon is added */
  wuss__iconbar_box(wuss, &slot);
  wuss__invalidate_clipped(wuss->iconbar.window, &slot);

  if (icon != NULL)
    *icon = it;

  return result_OK;
}

#endif /* WUSS_ICONBAR */
