/* wuss/tasks/palette/palette.h -- desktop palette swatch grid task */

#ifndef TASKS_PALETTE_H
#define TASKS_PALETTE_H

#ifdef WUSS_APP

#include <stdbool.h>

#include "framebuf/colour.h"
#include "wuss/component/colourpicker.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../common/taskapp.h"

/* ponytail: fixed cap on the number of *.hex files the picker menu will
 * list, so the menu_items/names arrays can live inline in palette_task_t
 * with no malloc bookkeeping. Raise it if resources/palettes ever grows
 * past this. */
#define PALETTE_MAX_FILES 64

/* window D's task: draws every entry of the desktop palette as a square
 * in a grid, so the palette is visible at a glance. A MENU click over the
 * grid opens a picker for every *.hex file under resources/palettes, plus
 * Invert and Greyscale toggles. A pick installs the loaded/inverted array via
 * wuss_set_palette; there is no callback here, since every other interested
 * party (framebuffer bitmap, physical palette) reacts to the resulting
 * wuss_EVENT_PALETTE and reads the array back with wuss_get_palette.
 *
 * In either window SELECT on a swatch selects it; Menu > Edit colour... (or a
 * double-click) opens that window's own colour picker on it, each change
 * installing the edited palette live: the desktop palette via
 * wuss_set_palette, the Screen's via tasks_set_screen_palette. A later Load,
 * Invert or Greyscale starts again from the file, dropping the edits, and a
 * desktop palette change rebuilds the Screen's.
 *
 * A second window ("Screen") draws the same kind of grid but for the
 * physical screen bitmap's own palette (event->data.redraw.scr->palette),
 * which is whatever size the screen's pixel format needs (2/4/16/256
 * entries for 1/2/4/8bpp) rather than the fixed wuss_SYSTEM_PALETTE_LENGTH
 * system palette above; a NULL screen palette (32bpp, no palette) just
 * shows a label. Its MENU click opens a "Screen" menu of the *.hex files
 * whose entry count matches the screen's depth, plus Edit colour...; a pick
 * replaces the screen palette (tasks_set_screen_palette) until the next
 * desktop palette or mode change. */
typedef struct palette_task
{
  taskapp_instance_t   base;    /* window: desktop palette; NULL in a Screen
                                 * task */
  wuss_window_t       *window2; /* Screen task's window, else NULL:
                                  * the physical screen's own palette
                                  * (1/2/4/8bpp) or a "none" label (32bpp
                                  * has no palette) */
  bool                 invert;
  bool                 grey; /* Menu > Greyscale */

  /* the selected swatch (-1 for none), and the entry the picker edits,
   * fixed when it opens. Each task has its own picker, made on first use,
   * so the desktop and Screen palettes are edited separately. */
  int                  cell;
  int                  editing;
  wuss_colourpicker_t *picker;

  /* *.hex files found under resources/palettes at create time, leafname
   * with the extension stripped (e.g. "PICO-8"), sorted. selected indexes
   * both this and menu_items. A Screen task keeps every file; the desktop
   * only the PALETTE_NCOLOURS-entry ones. */
  char                 names[PALETTE_MAX_FILES][32];
  int                  nnames;
  int                  selected;

  /* Screen task: load_items row i shows names[screen_map[i]], rebuilt on
   * each open to the files matching the screen's entry count */
  int                  screen_map[PALETTE_MAX_FILES];
  int                  nscreen;

  /* the picker menu: per-instance (not static const) because its ticks
   * track selected/invert and it must outlive the open chain, so a stack
   * copy built fresh on each open would not do */
  wuss_menu_item_t     load_items[PALETTE_MAX_FILES]; /* one row per
                                                        * *.hex file */
  wuss_menu_t          load_menu; /* "Load" submenu, opened off menu_items */
  wuss_menu_item_t     menu_items[4]; /* Load, Invert, Greyscale, Edit
                                       * colour; a Screen task uses only
                                       * Load and Edit colour */
}
palette_task_t;

/* load a named *.hex file (leafname, no extension, e.g. "PICO-8") from
 * resources/palettes into a wuss_SYSTEM_PALETTE_LENGTH-entry colour_t
 * array. Used both by palette_create's picker and by callers choosing the
 * startup system palette before any window exists. */
result_t palette_load_hex(const char *resources,
                          const char *name,
                          colour_t   *out);

/* as palette_load_hex, but accepts any 1..max entries and returns the
 * count through n (for dithering to a short palette such as CMYK) */
result_t palette_load_hex_n(const char *resources,
                            const char *name,
                            colour_t   *out,
                            int         max,
                            int        *n);

/* install the icon bar app that opens palette windows on demand */
result_t palette_app_create(wuss_t *wuss);

#endif /* WUSS_APP */

#endif /* TASKS_PALETTE_H */
