/* wuss/tasks/common/dithermenu.h -- "Display" submenu holding "Dithering" */

#ifndef TASKS_DITHERMENU_H
#define TASKS_DITHERMENU_H

#include "base/utils.h"
#include "framebuf/screen.h"
#include "geom/size.h"
#include "wuss/component/scaleview.h"
#include "wuss/menu.h"
#include "wuss/window.h"

/* a "Display" submenu whose one row opens a "Dithering" submenu with one row
 * per screen_dither_t, for a task to embed and hang off the top row of its
 * main menu. dithermenu_add_scale adds a second row holding a "Scale view"
 * dialogue */
typedef struct dithermenu
{
  wuss_menu_item_t items[3]; /* indexed by screen_dither_t */
  wuss_menu_t      menu;     /* "Dithering" */
  wuss_menu_item_t display_items[2];
  wuss_menu_t      display;  /* "Display": the main menu's submenu */
}
dithermenu_t;

/* row of the "Display" submenu that dithermenu_add_scale adds */
#define DITHERMENU_SCALE 1

/* build dm's rows and tick the one for dithering */
static inline void dithermenu_init(dithermenu_t   *dm,
                                   screen_dither_t dithering)
{
  WUSS_MENU_ITEM(dm->items, screen_DITHER_NEAREST, "Nearest",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(dm->items, screen_DITHER_BAYER, "Bayer",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(dm->items, screen_DITHER_BLUE_NOISE, "Blue noise",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(dm->menu, "Dithering", dm->items, NELEMS(dm->items));
  wuss_menu_tick_exclusive(&dm->menu, (int) dithering);

  WUSS_MENU_ITEM_MENU(dm->display_items, 0, "Dithering", wuss_MENU_ITEM_NONE,
                      &dm->menu);
  WUSS_MENU_TITLE(dm->display, "Display", dm->display_items, 1);
}

/* after dithermenu_init: add a "Scale view" row to the "Display" submenu
 * opening scaleview's dialogue (from wuss_scaleview_window) as its leaf */
static inline void dithermenu_add_scale(dithermenu_t  *dm,
                                        wuss_window_t *scaleview)
{
  WUSS_MENU_ITEM_WINDOW(dm->display_items, DITHERMENU_SCALE, "Scale view",
                        wuss_MENU_ITEM_PRE_OPEN, scaleview);
  WUSS_MENU_TITLE(dm->display, "Display", dm->display_items,
                  NELEMS(dm->display_items));
}

/* tick row index, both in dm and in the open chain handle, and return the
 * method it names */
static inline screen_dither_t dithermenu_select(dithermenu_t      *dm,
                                                wuss_menu_handle_t handle,
                                                int                index)
{
  wuss_menu_tick_exclusive_live(handle, &dm->menu, index);

  return (screen_dither_t) index;
}

/* ----- scaling, for a window whose content is exactly one image ----- */

/* size at percent, rounded up as screen_copy_bitmap_scaled rounds its
 * extent */
static inline size2d_t dithermenu_scaled_size(size2d_t size, int percent)
{
  return SIZE2D(DIVIDE_ROUNDING_UP(size.w * percent, 100),
                DIVIDE_ROUNDING_UP(size.h * percent, 100));
}

/* blit bm at percent with its top left at (x, y) */
static inline result_t dithermenu_draw(screen_t       *scr,
                                       int             x,
                                       int             y,
                                       const bitmap_t *bm,
                                       int             percent,
                                       screen_dither_t dithering)
{
  size2d_t       sz;
  box_t          box;
  screen_scale_t scale;

  sz         = dithermenu_scaled_size(bm->size, percent);
  box        = (box_t) { x, y, x + sz.w, y + sz.h };
  scale.xmul = scale.ymul = percent;
  scale.xdiv = scale.ydiv = 100;

  return screen_copy_bitmap_scaled_dithered(scr, &box, bm, &scale, dithering);
}

/* where a window fitted to the screen goes once resized: centred on the
 * whole screen, as the safe area is smaller than the fit */
static const wuss_place_t dithermenu_fit_place =
{
  wuss_PLACE_IGNORE_SAFE_AREA, wuss_ALIGN_CENTRE, NULL, { 0, 0 },
  { 0, 0, 0, 0 }
};

/* the percent a "Scale view" choice asks of window, whose content is an
 * image of size at 1:1. Fit screen first moves window to the screen's top
 * left, as a resize stops at the screen's edge: place it with
 * dithermenu_fit_place after resizing */
static inline int dithermenu_scale_percent(wuss_window_t          *window,
                                           wuss_scaleview_choice_t choice,
                                           int                     percent,
                                           size2d_t                size)
{
  box_t    visible;
  box_t    content;
  size2d_t avail;

  if (choice == wuss_SCALEVIEW_FIT_SCREEN)
  {
    wuss_window_get_visible_bounds(window, &visible);
    wuss_window_get_content_bounds(window, &content);
    wuss_window_move(window, POINT(content.x0 - visible.x0,
                                   content.y0 - visible.y0));
    wuss_window_get_max_content(window, &avail);
  }
  else if (choice == wuss_SCALEVIEW_FIT_WINDOW)
  {
    wuss_window_get_content_bounds(window, &content);
    avail = SIZE2D(content.x1 - content.x0, content.y1 - content.y0);
  }
  else
  {
    return percent;
  }

  percent = MIN(avail.w * 100 / size.w, avail.h * 100 / size.h);
  return CLAMP(percent, wuss_SCALEVIEW_MIN, wuss_SCALEVIEW_MAX);
}

#endif /* TASKS_DITHERMENU_H */
