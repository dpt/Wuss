/* wuss/tasks/effecting/effecting.c -- bitmap effects task */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "io/path.h"
#include "wuss/menu.h"

#include "effecting.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* ----------------------------------------------------------------------- */

static void effecting_update(effecting_task_t *ec, int rechain)
{
  result_t rc;

  if (ec->source.base == NULL)
    return;

  rc = effectstack_run(&ec->stack, &ec->source, rechain);
  if (rc != result_OK)
    logf_warning("effecting: cannot apply effects (rc=0x%X)", rc);

  ec->show_original = 0;
  wuss_window_invalidate_visible(ec->base.window);
}

static void effecting_menu_enable(effecting_task_t *ec, int row, int enabled)
{
  wuss_menu_disable_item_live(ec->base.menu_handle, &ec->base.menu, row,
                              !enabled);
}

/* Sets the rows to match the selection and the stack. */
static void effecting_menu_update(effecting_task_t *ec)
{
  effectstack_menu_update(&ec->stack);
  effecting_menu_enable(ec, EFFECTING_MENU_APPLY, ec->stack.nelements > 0);
  effecting_menu_enable(ec, EFFECTING_MENU_CANCEL, ec->stack.nelements > 0);
}

/* effectstack_changed_fn_t: opaque is the effecting_task_t */
static void effecting_changed(void *opaque, int rechain)
{
  effecting_task_t *ec;

  ec = opaque;

  effecting_menu_update(ec);
  effecting_update(ec, rechain);
}

/* Makes the result the new source and empties the stack. */
static void effecting_apply(effecting_task_t *ec)
{
  if (ec->source.base == NULL || ec->stack.result.base == NULL)
    return;

  memcpy(ec->source.base, ec->stack.result.base,
         (size_t) ec->source.rowbytes * ec->source.size.h);

  effectstack_reset(&ec->stack);
}

/* ----- files ----- */

/* wuss_saveas_save_fn_t: opaque is the effecting_task_t */
static result_t effecting_saveas_save(const char *path, void *opaque)
{
  effecting_task_t *ec;

  ec = opaque;
  if (ec->stack.result.base == NULL)
    return result_BAD_ARG; /* nothing loaded yet; nothing to save */

  return bitmap_save_png(&ec->stack.result, path);
}

/* Seed Save As with "<image>-effects.png". */
static void effecting_saveas_name(effecting_task_t *ec)
{
  char leafname[EFFECTING_MAX_NAME_LEN + 16];

  snprintf(leafname, sizeof(leafname), "%s-effects.png", ec->name);
  wuss_saveas_set_leafname(ec->base.saveas, leafname);
}

/* Fit the image window to the image at the current scale, first opening it if
 * it is not open yet. */
static result_t effecting_fit(effecting_task_t *ec)
{
  return task_window_fit(ec->base.delegate, &ec->base.window, "Effecting",
                         wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                         wuss_NO_BACKDROP, /* the image covers it all */
                         dithermenu_scaled_size(ec->source.size, ec->scale),
                         SIZE2D(32, 32));
}

/* The "Scale view" dialogue's choice */
static void effecting_scaleview_choice(wuss_scaleview_choice_t choice,
                                       int                     percent,
                                       void                   *opaque)
{
  effecting_task_t *ec;

  ec = opaque;

  if (ec->base.window == NULL)
    return;

  ec->scale = dithermenu_scale_percent(ec->base.window, choice, percent,
                                       ec->source.size);
  task_set_doc_title(ec->base.window, path_leaf(ec->path), "Effecting", 0,
                     ec->scale);
  (void) wuss_scaleview_set_percent(ec->scaleview, ec->scale);
  wuss_window_invalidate_visible(ec->base.window);
  if (effecting_fit(ec) == result_OK && choice == wuss_SCALEVIEW_FIT_SCREEN)
    (void) wuss_window_place(ec->base.window, &dithermenu_fit_place);
}

/* Re-apply to next, an rgba8888 bitmap whose pixels it takes, named path and
 * name, resizing the window to fit. The image and list windows open with the
 * first image, sized and placed for it. */
static result_t effecting_take(effecting_task_t *ec,
                               const bitmap_t   *next,
                               const char       *path,
                               const char       *name)
{
  result_t rc;
  int      opening;

  free(ec->source.base);
  ec->source = *next;
  strcpy(ec->name, name);
  effecting_saveas_name(ec);

  rc = effectstack_run(&ec->stack, &ec->source, 1);
  if (rc != result_OK)
    return rc;

  opening = ec->base.window == NULL;
  rc = effecting_fit(ec);
  if (rc != result_OK)
    return rc;

  snprintf(ec->path, sizeof(ec->path), "%s", path);
  task_set_doc_title(ec->base.window, path_leaf(path), "Effecting", 0,
                     ec->scale);
  wuss_window_invalidate_visible(ec->base.window);
  if (opening)
    return effectstack_open(&ec->stack, ec->base.window);

  return result_OK;
}

/* Load the PNG or JPEG at path and re-apply, as effecting_take. A load
 * failure keeps the current image and is returned. */
static result_t effecting_show(effecting_task_t *ec, const char *path)
{
  result_t rc;
  char     name[EFFECTING_MAX_NAME_LEN];
  bitmap_t next;

  if (!task_image_name(path, name, sizeof(name)))
    return result_BAD_ARG;

  rc = bitmap_load_deep(&next, path, pixelfmt_rgba8888);
  if (rc != result_OK)
  {
    logf_warning("effecting: cannot load \"%s\" (rc=0x%X)", path, rc);
    return rc;
  }

  return effecting_take(ec, &next, path, name);
}

/* taskapp_data_load_bitmap_fn_t: re-apply to a RAM transfer, named
 * leafname */
static result_t effecting_show_bitmap(void       *instance,
                                      bitmap_t   *bm,
                                      const char *leafname)
{
  char name[EFFECTING_MAX_NAME_LEN];

  if (!task_image_name(leafname, name, sizeof(name)))
    return result_BAD_ARG;

  (void) effecting_take(instance, bm, leafname, name);
  free(bm); /* its pixels are taken */
  return result_OK;
}

/* wuss_saveas_bitmap_fn_t: opaque is the effecting_task_t */
static result_t effecting_saveas_bitmap(bitmap_t **out, void *opaque)
{
  effecting_task_t *ec;

  ec = opaque;
  if (ec->stack.result.base == NULL)
    return result_BAD_ARG;

  return bitmap_convert(&ec->stack.result, pixelfmt_rgba8888, out);
}

/* ----------------------------------------------------------------------- */

/* Builds the static menus; the Edit row and flags are set per open. */
static void effecting_build_menus(effecting_task_t *task)
{
  dithermenu_init(&task->dither, task->dithering);
  dithermenu_add_scale(&task->dither, wuss_scaleview_window(task->scaleview));

  WUSS_MENU_ITEM_MENU(task->menu_items, EFFECTING_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_BORROWED_SUBMENU, &task->dither.display);
  taskapp_save_row(&task->base, task->menu_items, EFFECTING_MENU_SAVE,
                   wuss_MENU_ITEM_DASHED);
  effectstack_menu_rows(&task->stack, &task->base.menu, task->menu_items,
                        EFFECTING_MENU_STACK);
  task->menu_items[EFFECTING_MENU_STACK + EFFECTSTACK_ROW_ADD].flags |=
    wuss_MENU_ITEM_DASHED;
  task->menu_items[EFFECTING_MENU_STACK + EFFECTSTACK_ROW_LEVEL].flags |=
    wuss_MENU_ITEM_DASHED;
  WUSS_MENU_ITEM(task->menu_items, EFFECTING_MENU_APPLY, "Apply",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM(task->menu_items, EFFECTING_MENU_CANCEL, "Cancel",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->base.menu, "Effects", task->menu_items,
                  NELEMS(task->menu_items));
}

static result_t effecting_init(effecting_task_t *task)
{
  result_t     rc;
  wuss_t      *wuss;
  wuss_task_t *delegate;

  wuss     = task->base.wuss;
  delegate = task->base.delegate;

  /* No autoclose: QUIT is delivered at wuss_destroy instead. The windows
   * open with the first image */
  wuss_task_set_autoclose(delegate, 0);

  task->dithering = screen_DITHER_BLUE_NOISE;
  task->scale     = 100;

  rc = effectstack_init(&task->stack, &task->base, effecting_changed, task);
  if (rc != result_OK)
    return rc;

  rc = wuss_scaleview_create(&task->scaleview, wuss,
                             effecting_scaleview_choice, task);
  if (rc != result_OK)
    return rc;

  effecting_build_menus(task);

  return result_OK;
}

static void effecting_quit(effecting_task_t *task)
{
  effectstack_destroy(&task->stack);
  wuss_scaleview_destroy(task->scaleview);
  free(task->source.base);
}

/* ----------------------------------------------------------------------- */

static result_t effecting_redraw(const wuss_event_t *event, void *task_data)
{
  effecting_task_t *ec;
  screen_t         *scr;
  const box_t      *bounds;
  const bitmap_t   *bm;

  ec = task_data;

  scr    = event->data.redraw.scr;
  bounds = event->data.redraw.bounds;
  if (ec->source.base == NULL)
    return result_OK; /* unreachable: the window opens with the first image */

  bm = ec->show_original ? &ec->source : &ec->stack.result;

  return dithermenu_draw(scr,
                         bounds->x0 - event->data.redraw.scroll.x,
                         bounds->y0 - event->data.redraw.scroll.y,
                         bm, ec->scale, ec->dithering);
}

static result_t effecting_open_menu(effecting_task_t *ec)
{
  result_t rc;

  effecting_menu_update(ec);

  rc = wuss_scaleview_set_percent(ec->scaleview, ec->scale);
  if (rc != result_OK)
    return rc;

  return wuss_menu_open_at_pointer(ec->base.delegate, &ec->base.menu,
                                   &ec->base.menu_handle);
}

static result_t effecting_menu_select(effecting_task_t   *ec,
                                      const wuss_event_t *event)
{
  const wuss_menu_t *menu;
  int                index;

  menu  = event->data.menu_select.menu;
  index = event->data.menu_select.index;

  if (menu == &ec->dither.menu)
  {
    ec->dithering = dithermenu_select(&ec->dither, ec->base.menu_handle,
                                      index);
    wuss_window_invalidate_visible(ec->base.window);
    return result_OK;
  }

  if (menu != &ec->base.menu)
    return result_OK;

  switch (index)
  {
  case EFFECTING_MENU_APPLY:
    effecting_apply(ec);
    return result_OK;

  case EFFECTING_MENU_CANCEL:
    effectstack_reset(&ec->stack);
    return result_OK;

  default:
    return result_OK;
  }
}

static result_t effecting_handle(wuss_window_t      *window,
                                 const wuss_event_t *event,
                                 void               *task_data)
{
  result_t          rc;
  effecting_task_t *ec;

  ec = task_data;

  if (event->kind == wuss_EVENT_QUIT)
  {
    effecting_quit(ec);
    return result_OK;
  }

  /* the stack is meaningless without its image */
  if (event->kind == wuss_EVENT_CLOSE && window == ec->base.window)
    effectstack_close(&ec->stack);

  if (effectstack_event(&ec->stack, window, event, &rc))
    return rc;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    if (window != ec->base.window)
      return result_OK;
    return effecting_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
      return effecting_open_menu(ec);
    if (window == ec->base.window &&
        (event->data.mouse.button & wuss_BUTTON_SELECT))
    {
      ec->show_original = !ec->show_original;
      wuss_window_invalidate_visible(ec->base.window);
    }
    return result_OK;

  case wuss_EVENT_MENU_SELECT:
    return effecting_menu_select(ec, event);

  case wuss_EVENT_MESSAGE:
    if (ec->base.window == NULL || window != ec->base.window)
      return result_OK;
    return taskapp_window_data_load(ec->base.wuss, ec->base.delegate, ec,
                                    (taskapp_data_load_fn_t) effecting_show,
                                    event->data.message);

  case wuss_EVENT_MENU_CLOSED:
    effecting_menu_update(ec);
    return result_OK;

  case wuss_EVENT_CLOSE_ADJUST:
    if (window != ec->base.window)
      return result_OK;
    return task_close_adjust(ec->base.wuss, ec->base.delegate, window, ec->path,
                             event);

  case wuss_EVENT_KEY:
    return wuss_menu_dispatch_shortcut(ec->base.delegate, &ec->base.menu,
                                       event);

  default:
    return result_OK;
  }
}

result_t effecting_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Effecting",
    .purpose       = "Apply effects to images",
    .data_load     = (taskapp_data_load_fn_t) effecting_show,
    .drop_only     = true,
    .instance_size = sizeof(effecting_task_t),
    .init          = (taskapp_init_fn_t) effecting_init,
    .handle        = effecting_handle,
    .all_windows   = true, /* the list window takes clicks and keys too */
    .save          = effecting_saveas_save,
    .save_bitmap   = effecting_saveas_bitmap,
    .data_load_bitmap = effecting_show_bitmap
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
