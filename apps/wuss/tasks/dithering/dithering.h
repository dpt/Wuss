/* wuss/tasks/dithering/dithering.h -- error-diffusion dithering demo task */

#ifndef TASKS_DITHERING_H
#define TASKS_DITHERING_H

#ifdef WUSS_APP

#include "framebuf/bitmap.h"
#include "framebuf/dither.h"
#include "io/path.h"
#include "wuss/component/dialogue.h"
#include "wuss/component/saveas.h"
#include "wuss/icon-spec.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../palette/palette.h"

#define DITHERING_MAX_NAME_LEN 64
#define DITHERING_SLIDER_W     128 /* slider dialogues' slider */
#define DITHERING_SWATCHES_W   128 /* Show palette window's initial side */
#define DITHERING_COLOURS_STEPS 8 /* Colours slider positions per doubling */
#define DITHERING_STRENGTH_STEP 1 /* Animate strength: percent per tick */

/* dithering_task_t.palette: one of these two, or DITHERING_PALETTE_FIRST_HEX
 * plus an index into palettes[] */
#define DITHERING_PALETTE_OPTIMAL 0
#define DITHERING_PALETTE_DESKTOP 1
#define DITHERING_PALETTE_FIRST_HEX 2

/* Palette submenu rows: a submenu per kind of palette */
enum
{
  DITHERING_PALETTE_ROW_COMPUTED,
  DITHERING_PALETTE_ROW_FIXED,
  DITHERING_PALETTE_ROW__LIMIT
};

/* Computed submenu rows */
enum
{
  DITHERING_COMPUTED_OPTIMAL,
  DITHERING_COMPUTED_COLOURS,
  DITHERING_COMPUTED__LIMIT
};

/* Fixed submenu rows: Desktop, then one "N-colour" submenu per *.hex file
 * size */
#define DITHERING_FIXED_DESKTOP     0
#define DITHERING_FIXED_FIRST_GROUP 1

/* Method submenu rows: Nearest alone, then a submenu per family */
enum
{
  DITHERING_METHOD_NEAREST,
  DITHERING_METHOD_ORDERED,
  DITHERING_METHOD_DIFFUSION,
  DITHERING_METHOD__LIMIT
};

/* Error diffusion submenu rows: these options, then a row per method */
#define DITHERING_DIFFUSION_SERPENTINE 0
#define DITHERING_DIFFUSION_LINEAR     1
#define DITHERING_DIFFUSION_FIRST      2

/* ponytail: fixed cap on distinct *.hex sizes; files of any further size
 * are left out of the menu. Raise it if resources/palettes grows more. */
#define DITHERING_MAX_GROUPS 8

/* an "N-colour" submenu: palettes[start..start+n-1], all of ncolours */
typedef struct dithering_group
{
  int         ncolours;
  int         start;
  int         n;
  char        title[16]; /* "N-colour": the row's text and menu's title */
  wuss_menu_t menu;
}
dithering_group_t;

/* Main menu rows */
enum
{
  DITHERING_MENU_SAVE,
  DITHERING_MENU_METHOD,
  DITHERING_MENU_PALETTE,
  DITHERING_MENU_SHOW_PALETTE,
  DITHERING_MENU_STRENGTH,
  DITHERING_MENU_ANIMATE,
  DITHERING_MENU__LIMIT
};

/* window's task: a PNG dropped on its icon or window (DataLoad) reduced by
 * dither_bitmap, optionally to an octree palette. A Select click toggles
 * between the dithered result and the original; Menu picks the options, and
 * every change re-dithers at once; Colours and Strength are slider
 * dialogues. Show palette opens a second window of swatches for the palette
 * the last dither targeted. Save writes the dithered result as a PNG. */
typedef struct dithering_task
{
  wuss_window_t     *window;
  wuss_window_t     *palette_window; /* Show palette's swatches, or NULL */
  wuss_t            *wuss;
  wuss_task_t       *delegate;
  bitmap_t           source;   /* owned base: rgbx8888 or rgba8888; NULL
                                * base until the first DataLoad */
  bitmap_t          *dithered; /* owned; NULL if the last dither failed */
  int                show_original;
  colour_t           target[256]; /* the palette dithered to; ntarget is 0
                                   * if the last dither failed */
  int                ntarget;

  dither_method_t    method;
  int                palette;  /* a DITHERING_PALETTE_* row */
  int                colours_pos; /* Colours slider position: optimal
                                   * palette size is 2^(pos / STEPS) */
  int                strength; /* percent */
  int                animate;  /* cycle strength up and down while idle */
  int                strength_step; /* animated change per idle tick */
  int                serpentine;
  int                linear;   /* diffuse in linear light */

  char               path[DPTLIB_MAXPATH]; /* file shown; "" until the
                                * first DataLoad */
  char               name[DITHERING_MAX_NAME_LEN]; /* leafname of the shown
                                * image, less ".png" */
  char               palettes[PALETTE_MAX_FILES][32]; /* *.hex leafnames,
                                * by entry count then name */
  int                npalettes;
  dithering_group_t  groups[DITHERING_MAX_GROUPS];
  int                ngroups;

  wuss_menu_item_t   method_items[DITHERING_METHOD__LIMIT];
  wuss_menu_t        method_menu;
  wuss_menu_item_t   family_items[dither__LIMIT - 1 +
                                  DITHERING_DIFFUSION_FIRST]; /* every
                                * method but Nearest, plus the options
                                * heading the diffusion family; each family's
                                * menu is a slice */
  wuss_menu_t        ordered_menu;
  wuss_menu_t        diffusion_menu;
  wuss_menu_item_t   hex_items[PALETTE_MAX_FILES]; /* one per palettes[];
                                * each group's menu is a slice */
  wuss_menu_item_t   computed_items[DITHERING_COMPUTED__LIMIT];
  wuss_menu_t        computed_menu;
  wuss_menu_item_t   fixed_items[DITHERING_FIXED_FIRST_GROUP +
                                 DITHERING_MAX_GROUPS];
  wuss_menu_t        fixed_menu;
  wuss_menu_item_t   palette_items[DITHERING_PALETTE_ROW__LIMIT];
  wuss_menu_t        palette_menu;
  wuss_menu_item_t   menu_items[DITHERING_MENU__LIMIT];
  wuss_menu_t        menu;
  wuss_menu_handle_t menu_handle; /* the open chain, if any */
  wuss_saveas_t     *saveas;
  wuss_dialogue_t   *strength_dialogue; /* NULL once the window closes */
  wuss_slider_row_t  strength_row;
  wuss_dialogue_t   *colours_dialogue; /* NULL once the window closes */
  wuss_slider_row_t  colours_row;
}
dithering_task_t;

wuss_window_fn_t dithering_handle;

/* scan wuss_get_resources(wuss)/resources/palettes for *.hex files and create
 * an empty window against the given wuss instance; it shows an image once one
 * is dropped on it. if out is non-NULL, the task block is also returned
 * through it */
result_t dithering_create(wuss_t *wuss, dithering_task_t **out);

/* install the icon bar app that opens dithering windows on demand */
result_t dithering_app_create(wuss_t *wuss);

/* free a task block allocated by dithering_create; normally called by the
 * window's wuss_EVENT_QUIT handler, not by callers directly */
void dithering_destroy(dithering_task_t *task);

#endif /* WUSS_APP */

#endif /* TASKS_DITHERING_H */
