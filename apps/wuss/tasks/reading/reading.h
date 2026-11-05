/* wuss/tasks/reading/reading.h -- Reading task */

#ifndef TASKS_READING_H
#define TASKS_READING_H

#ifdef WUSS_APP

#include <stdbool.h>

#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "io/path.h"
#include "text/mdtext.h"
#include "wuss/component/colourmenu.h"
#include "wuss/component/fontmenu.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../common/taskapp.h"

/* window B's task: flows a text file dropped on its icon or window
 * (DataLoad) over its wuss-filled background, one line per bmfont_draw call.
 * A MENU click on the window opens a top-level menu with three submenus --
 * "Font" (the shared wuss_fontmenu singleton listing the font families
 * under resources/bmfonts, swapping the paragraph's family in place),
 * "Spacing" (swaps the letter/word spacing), "Alignment" (Left, Centre or
 * Right per line) and "Colours" (a submenu of "Foreground"/"Background",
 * each retargeting the shared wuss_colourmenu singleton on hover) -- whose
 * "Background" row also offers a "None" chip that unsets the paragraph's
 * background colour so glyphs blend straight onto whatever is already
 * behind the window's content. */
typedef struct reading_task
{
  taskapp_instance_t  base;
  mdtext_t            md;         /* the shown text, its fonts, spacing,
                                   * alignment and cached layout; md.font is
                                   * the current family's Regular face
                                   * (acquired from the font cache) or the
                                   * borrowed sysfont, md.bold its Bold face
                                   * (acquired) or NULL; md.text is NULL until
                                   * the first DataLoad */
  int                 doc_h;      /* document height last set */
  char               path[DPTLIB_MAXPATH]; /* file shown; "" until one
                                   * is loaded */
  int                 current;    /* font menu row of the current family, or
                                   * -1 for sysfont */
  wuss_colour_t      *colourmenu_target; /* &task->fg_index or
                                   * &task->bg_index: which field the open
                                   * colourmenu picks into, set by
                                   * reading_pre_submenu_open */
  wuss_menu_item_t    spacing_items[4]; /* "Spacing" submenu rows; per-
                                   * instance for the same reason -- each
                                   * carries its own tick state */
  wuss_menu_t         spacing_menu;
  wuss_menu_item_t    align_items[3]; /* "Alignment" submenu rows: Left,
                                   * Centre, Right */
  wuss_menu_t         align_menu;
  wuss_menu_item_t    colours_items[2]; /* "Foreground"/"Background" rows;
                                   * both share the colourmenu singleton,
                                   * retargeted per hover in
                                   * reading_pre_submenu_open */
  wuss_menu_t         colours_menu;
  wuss_menu_item_t    top_items[4]; /* "Font", "Spacing", "Alignment",
                                   * "Colours", built once the
                                   * fontmenu exists so items[0].submenu can
                                   * borrow its live wuss_menu_t */
  wuss_menu_t         top_menu;   /* root menu passed to wuss_menu_open */
  int               spacing_idx; /* index into reading_spacing_presets[]
                                   * currently applied as md.spacing */
  wuss_colour_t       fg_index;   /* system palette index picked from
                                   * "Foreground" */
  wuss_colour_t       bg_index;   /* system palette index picked from
                                   * "Background" */
  bool                bg_transparent; /* set from the "Background" menu's
                                   * "None" row; bg drawn transparent while
                                   * set, overriding bg_index */
}
reading_task_t;

/* install the icon bar app that opens Reading windows on demand */
result_t reading_app_create(wuss_t *wuss);

/* open a Reading window showing the file at path; the app must be installed
 * (reading_app_create) first */
result_t reading_open_file(wuss_t *wuss, const char *path);

#endif /* WUSS_APP */

#endif /* TASKS_READING_H */
