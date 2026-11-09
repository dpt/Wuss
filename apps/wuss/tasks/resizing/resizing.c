/* wuss/tasks/resizing/resizing.c -- image resizing demo task */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "io/path.h"
#include "wuss/component/saveas.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../common/common.h"
#include "../common/dithermenu.h"

#include "../apps.h"
#include "../common/taskapp.h"

#define RESIZING_MAX_NAME_LEN 64
#define RESIZING_NSCALES      8  /* rows in the Scale submenu */

/* Main menu rows */
enum
{
  RESIZING_MENU_DISPLAY,
  RESIZING_MENU_SAVE,
  RESIZING_MENU_METHOD,
  RESIZING_MENU_SCALE,
  RESIZING_MENU_LINEAR,
  RESIZING_MENU__LIMIT
};

/* window's task: a PNG or JPEG dropped on its icon or window (DataLoad)
 * resampled by
 * bitmap_resize to the chosen scale. MENU picks the method and scale, and
 * every change resamples at once. Save writes the resized result as a
 * PNG. */
typedef struct resizing_task
{
  taskapp_instance_t base;
  bitmap_t           source;  /* owned base: rgbx8888 or rgba8888; NULL
                               * base until the first DataLoad */
  bitmap_t          *resized; /* owned; NULL if the last resize failed */

  bitmap_resample_t  method;
  int                scale;   /* index into the Scale rows */
  int                linear;  /* blend in linear light */
  screen_dither_t    dithering; /* how the image is blitted to the screen */

  char               path[DPTLIB_MAXPATH]; /* file shown; "" until the first
                               * DataLoad */
  char               name[RESIZING_MAX_NAME_LEN]; /* leafname of the shown
                               * image, less ".png" */

  wuss_menu_item_t   method_items[bitmap_resample__LIMIT];
  wuss_menu_t        method_menu;
  wuss_menu_item_t   scale_items[RESIZING_NSCALES];
  wuss_menu_t        scale_menu;
  dithermenu_t       dither;  /* "Display" submenu: screen dithering */
  wuss_menu_item_t   menu_items[RESIZING_MENU__LIMIT];
}
resizing_task_t;

/* ----------------------------------------------------------------------- */

/* indexed by bitmap_resample_t */
static const char *const resizing_method_names[bitmap_resample__LIMIT] =
{
  "Nearest",
  "Bilinear",
  "Box",
  "B-spline",
  "Gaussian",
  "Hermite",
  "Mitchell",
  "Catmull-Rom",
  "Lanczos2",
  "Lanczos3"
};

static const int resizing_scales[RESIZING_NSCALES] =
{
  25, 50, 75, 100, 150, 200, 300, 400
};
static const char *const resizing_scale_names[RESIZING_NSCALES] =
{
  "25%", "50%", "75%", "100%", "150%", "200%", "300%", "400%"
};

/* ----------------------------------------------------------------------- */

static void resizing_free_resized(resizing_task_t *rt)
{
  bitmap_destroy(rt->resized);
  rt->resized = NULL;
}

/* Resample the source to the chosen scale and fit the window to it. On
 * failure the window shows the original instead. */
static result_t resizing_apply(resizing_task_t *rt)
{
  result_t  rc;
  size2d_t  size;
  bitmap_t *out;
  int       percent;

  if (rt->source.base == NULL)
    return result_OK;

  percent = resizing_scales[rt->scale];
  size.w  = MAX(1, rt->source.size.w * percent / 100);
  size.h  = MAX(1, rt->source.size.h * percent / 100);

  wuss_hourglass_on(rt->base.wuss);
  rc = bitmap_resize(&rt->source, size, rt->method, rt->linear, &out);
  wuss_hourglass_off(rt->base.wuss);

  resizing_free_resized(rt);
  if (rc == result_OK)
    rt->resized = out;
  else
    logf_warning("resizing: resize failed (rc=0x%X)", rc);

  /* the window opens with the first image, sized and placed for it */
  size = rt->resized ? rt->resized->size : rt->source.size;
  rc   = task_window_fit(rt->base.delegate, &rt->base.window, "Resizing",
                         wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                         wuss_NO_BACKDROP, /* the image covers it all */
                         size, SIZE2D(0, 0));
  if (rc != result_OK)
    return rc;

  wuss_window_invalidate_visible(rt->base.window);

  return result_OK;
}

/* wuss_saveas_save_fn_t: opaque is the resizing_task_t */
static result_t resizing_saveas_save(const char *path, void *opaque)
{
  resizing_task_t *rt;

  rt = opaque;
  if (rt->resized == NULL)
    return result_BAD_ARG; /* the last resize failed; nothing to save */

  return bitmap_save_png(rt->resized, path);
}

/* Seed Save As with "<image>-resized.png". */
static void resizing_saveas_name(resizing_task_t *rt)
{
  char leafname[RESIZING_MAX_NAME_LEN + 16];

  snprintf(leafname, sizeof(leafname), "%s-resized.png", rt->name);
  wuss_saveas_set_leafname(rt->base.saveas, leafname);
}

/* Load the PNG or JPEG at path and resize it. A load failure keeps the
 * current image and is returned. */
static result_t resizing_show(resizing_task_t *rt, const char *path)
{
  result_t rc;
  bitmap_t next;
  char     name[RESIZING_MAX_NAME_LEN];

  if (!task_image_name(path, name, sizeof(name)))
    return result_BAD_ARG;

  rc = bitmap_load_deep(&next, path, pixelfmt_rgbx8888);
  if (rc != result_OK)
  {
    logf_warning("resizing: cannot load \"%s\" (rc=0x%X)", path, rc);
    return rc;
  }

  free(rt->source.base);
  rt->source = next;
  strcpy(rt->name, name);
  resizing_saveas_name(rt);

  rc = resizing_apply(rt);
  if (rc != result_OK)
    return rc;

  snprintf(rt->path, sizeof(rt->path), "%s", path);
  task_set_doc_title(rt->base.window, path_leaf(path), "Resizing", 0);
  return result_OK;
}

/* ----------------------------------------------------------------------- */

/* The window opens with the first image. */
static result_t resizing_init(resizing_task_t *task)
{
  int i;

  task->method    = bitmap_resample_LANCZOS3;
  task->scale     = 3; /* 100% */
  task->linear    = 1;
  task->dithering = screen_DITHER_BLUE_NOISE;

  for (i = 0; i < bitmap_resample__LIMIT; i++)
    WUSS_MENU_ITEM(task->method_items, i, resizing_method_names[i],
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->method_menu, "Method", task->method_items,
                  bitmap_resample__LIMIT);

  for (i = 0; i < RESIZING_NSCALES; i++)
    WUSS_MENU_ITEM(task->scale_items, i, resizing_scale_names[i],
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->scale_menu, "Scale", task->scale_items,
                  RESIZING_NSCALES);

  dithermenu_init(&task->dither, task->dithering);
  WUSS_MENU_ITEM_MENU(task->menu_items, RESIZING_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_NONE, &task->dither.display);
  taskapp_save_row(&task->base, task->menu_items, RESIZING_MENU_SAVE,
                   wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM_MENU(task->menu_items, RESIZING_MENU_METHOD, "Method",
                      wuss_MENU_ITEM_DASHED, &task->method_menu);
  WUSS_MENU_ITEM_MENU(task->menu_items, RESIZING_MENU_SCALE, "Scale",
                      wuss_MENU_ITEM_NONE, &task->scale_menu);
  WUSS_MENU_ITEM(task->menu_items, RESIZING_MENU_LINEAR, "Linear light",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->base.menu, "Resizing", task->menu_items,
                  RESIZING_MENU__LIMIT);

  return result_OK;
}

/* ----------------------------------------------------------------------- */

static result_t resizing_redraw(resizing_task_t    *rt,
                                const wuss_event_t *event)
{
  const box_t    *bounds;
  const bitmap_t *bm;

  bounds = event->data.redraw.bounds;
  if (rt->source.base == NULL)
    return result_OK; /* unreachable: the window opens with the first image */

  bm = rt->resized != NULL ? rt->resized : &rt->source;

  return screen_copy_bitmap_dithered(event->data.redraw.scr,
                                     bounds->x0 - event->data.redraw.scroll.x,
                                     bounds->y0 - event->data.redraw.scroll.y,
                                     bm, rt->dithering);
}

static result_t resizing_menu_open(resizing_task_t *rt)
{
  wuss_menu_tick_exclusive(&rt->method_menu, (int) rt->method);
  wuss_menu_tick_exclusive(&rt->scale_menu, rt->scale);

  return wuss_menu_open_ticked(rt->base.delegate, &rt->base.menu,
                               rt->linear ? 1u << RESIZING_MENU_LINEAR : 0,
                               wuss_get_pointer(rt->base.wuss),
                               &rt->base.menu_handle);
}

static result_t resizing_menu_select(resizing_task_t    *rt,
                                     const wuss_event_t *event)
{
  const wuss_menu_t *menu;
  int                index;

  menu  = event->data.menu_select.menu;
  index = event->data.menu_select.index;

  /* display only: the resize itself is unchanged */
  if (menu == &rt->dither.menu)
  {
    rt->dithering = dithermenu_select(&rt->dither, rt->base.menu_handle, index);
    wuss_window_invalidate_visible(rt->base.window);
    return result_OK;
  }

  if (menu == &rt->method_menu)
    rt->method = (bitmap_resample_t) index;
  else if (menu == &rt->scale_menu)
    rt->scale = index;
  else if (menu == &rt->base.menu && index == RESIZING_MENU_LINEAR)
    rt->linear = !rt->linear;
  else
    return result_OK;

  if (menu == &rt->base.menu)
    wuss_menu_tick_item_live(rt->base.menu_handle, menu, index, rt->linear);
  else
    wuss_menu_tick_exclusive_live(rt->base.menu_handle, menu, index);

  return resizing_apply(rt);
}

static result_t resizing_handle(wuss_window_t      *window,
                                const wuss_event_t *event,
                                void               *task_data)
{
  resizing_task_t *rt;

  rt = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return resizing_redraw(rt, event);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;

    if (event->data.mouse.button & wuss_BUTTON_MENU)
      return resizing_menu_open(rt);

    return wuss_drag_start_grab(rt->base.wuss, rt->base.window);

  case wuss_EVENT_MENU_SELECT:
    return resizing_menu_select(rt, event);

  case wuss_EVENT_MESSAGE:
    if (rt->base.window == NULL || window != rt->base.window)
      return result_OK;
    return taskapp_window_data_load(rt->base.wuss, rt->base.delegate, rt,
                                    (taskapp_data_load_fn_t) resizing_show,
                                    event->data.message);

  case wuss_EVENT_CLOSE_ADJUST:
    if (window != rt->base.window)
      return result_OK;
    return task_close_adjust(rt->base.wuss, rt->base.delegate, window, rt->path,
                             event);

  case wuss_EVENT_QUIT:
    resizing_free_resized(rt);
    free(rt->source.base);
    return result_OK;

  case wuss_EVENT_KEY:
    return wuss_menu_dispatch_shortcut(rt->base.delegate, &rt->base.menu, event);

  default:
    return result_OK;
  }
}

result_t resizing_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Resizing",
    .purpose       = "Image resizing",
    .data_load     = (taskapp_data_load_fn_t) resizing_show,
    .drop_only     = true,
    .instance_size = sizeof(resizing_task_t),
    .init          = (taskapp_init_fn_t) resizing_init,
    .handle        = resizing_handle,
    .save          = resizing_saveas_save
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
