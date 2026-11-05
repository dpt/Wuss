/* wuss/tasks/tiling/tiling.c -- tiled backdrop task */

#ifdef WUSS_APP

#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "geom/point.h"
#include "io/path.h"
#include "wuss/menu-desc.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* fills a screen-sized window with a PNG repeated edge to edge, the PNG
 * dropped on its icon or window (DataLoad). Select drag moves the tile
 * origin by the pointer's movement; Menu opens Info and Reset origin */
typedef struct tiling_task
{
  taskapp_instance_t base;
  bitmap_t       bitmap; /* owned: base freed when replaced or at QUIT; NULL
                          * base until the first DataLoad */
  char           path[DPTLIB_MAXPATH]; /* file shown; "" until the
                                       * first DataLoad */
  point_t        origin; /* window-local position of one tile's top-left */
  point_t        grab;   /* pointer position at the last drag event */
  int            dragging; /* Select held: pointer moves drag the origin */
  wuss_menu_t   *menu;      /* owned: built from a descriptor string on each
                             * open, freed at QUIT */
}
tiling_task_t;

static result_t tiling_init(tiling_task_t *task)
{
  size2d_t sz;

  /* No autoclose: QUIT is delivered at wuss_destroy instead. */
  wuss_task_set_autoclose(task->base.delegate, 0);

  sz = wuss_get_screen_size(task->base.wuss);

  return wuss_window_create_at(task->base.delegate, &task_place, sz, "Tiling",
                               wuss_WINDOW_CLOSE | wuss_WINDOW_BACK |
                               wuss_WINDOW_FOCUSABLE,
                               wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW), sz,
                               SIZE2D(32, 32),
                               &task->base.window);
}

/* Load the PNG at path and show it tiled. A PNG that fails to load keeps the
 * current one and the error is returned. */
static result_t tiling_show(tiling_task_t *tc, const char *path)
{
  result_t rc;
  bitmap_t next;
  char     name[64];

  if (!path_leaf_strip_ext(path_leaf(path), ".png", name, sizeof(name)))
    return result_BAD_ARG;

  rc = bitmap_load_png_deep(&next, path, pixelfmt_rgbx8888);
  if (rc != result_OK)
  {
    logf_warning("tiling: cannot load \"%s\" (rc=0x%X)", path, rc);
    return rc;
  }

  free(tc->bitmap.base);
  tc->bitmap = next;

  snprintf(tc->path, sizeof(tc->path), "%s", path);
  task_set_doc_title(tc->base.window, path_leaf(path), "Tiling", 0);

  /* the tiles now cover the content, so drop the backdrop rather than fill
   * under them (this invalidates the content) */
  return wuss_window_set_background(tc->base.window, wuss_NO_BACKDROP);
}

/* Menu shape built from a descriptor string. The tree must outlive the open
 * chain, so it is kept on the task and rebuilt (previous one freed) on each
 * open. */
static result_t tiling_open_menu(tiling_task_t *tc)
{
  result_t     rc;
  wuss_menu_t *m;

  rc = wuss_menu_create_from_desc(&m, "Tiling, Reset origin");
  if (rc != result_OK)
    return rc;

  wuss_menu_destroy(tc->menu);
  tc->menu = m;

  return wuss_menu_open_at_pointer(tc->base.delegate, tc->menu,
                                   &tc->base.menu_handle);
}

static result_t tiling_redraw(const wuss_event_t *event, void *task_data)
{
  tiling_task_t *tc;
  const box_t   *bounds;
  point_t        origin;

  tc     = task_data;
  bounds = event->data.redraw.bounds;

  if (tc->bitmap.base == NULL)
    return result_OK; /* the backdrop shows */

  origin = POINT(bounds->x0 - event->data.redraw.scroll.x + tc->origin.x,
                 bounds->y0 - event->data.redraw.scroll.y + tc->origin.y);

  return screen_copy_bitmap_tiled(event->data.redraw.scr, bounds, &tc->bitmap,
                                  origin);
}

static result_t tiling_handle(wuss_window_t      *window,
                              const wuss_event_t *event,
                              void               *task_data)
{
  tiling_task_t *tc;

  tc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return tiling_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action == wuss_MOUSE_UP)
    {
      tc->dragging = 0;
    }
    else if (event->data.mouse.action == wuss_MOUSE_DOWN)
    {
      if (event->data.mouse.button & wuss_BUTTON_MENU)
      {
        return tiling_open_menu(tc);
      }
      else if (event->data.mouse.button & wuss_BUTTON_SELECT)
      {
        tc->dragging = 1;
        tc->grab     = event->data.mouse.point;
      }
    }
    else if (tc->dragging) /* wuss_MOUSE_MOVE */
    {
      tc->origin.x += event->data.mouse.point.x - tc->grab.x;
      tc->origin.y += event->data.mouse.point.y - tc->grab.y;
      tc->grab      = event->data.mouse.point;
      wuss_window_invalidate_visible(tc->base.window);
    }
    return result_OK;

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu == tc->menu &&
        strcmp(tc->menu->items[event->data.menu_select.index].text,
                    "Reset origin") == 0)
    {
      tc->origin = POINT(0, 0);
      wuss_window_invalidate_visible(tc->base.window);
    }
    return result_OK;

  case wuss_EVENT_MESSAGE:
    if (tc->base.window == NULL || window != tc->base.window)
      return result_OK;
    return taskapp_window_data_load(tc->base.wuss, tc->base.delegate, tc,
                                    (taskapp_data_load_fn_t) tiling_show,
                                    event->data.message);

  case wuss_EVENT_CLOSE_ADJUST:
    if (window != tc->base.window)
      return result_OK;
    return task_close_adjust(tc->base.wuss, tc->base.delegate, window,
                             tc->path, event);

  case wuss_EVENT_QUIT:
    wuss_menu_destroy(tc->menu);
    free(tc->bitmap.base);
    return result_OK;

  case wuss_EVENT_KEY:
    return result_WUSS_KEY_UNCLAIMED;

  default:
    return result_OK;
  }
}

result_t tiling_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Tiling",
    .purpose       = "Tile screen with PNG",
    .data_load     = (taskapp_data_load_fn_t) tiling_show,
    .drop_only     = true,
    .instance_size = sizeof(tiling_task_t),
    .init          = (taskapp_init_fn_t) tiling_init,
    .handle        = tiling_handle
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
