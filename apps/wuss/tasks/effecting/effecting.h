/* wuss/tasks/effecting/effecting.h -- bitmap effects task */

#ifndef TASKS_EFFECTING_H
#define TASKS_EFFECTING_H

#ifdef WUSS_APP

#include "framebuf/bitmap.h"
#include "framebuf/screen.h"
#include "io/path.h"
#include "wuss/component/saveas.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../common/dithermenu.h"
#include "../common/effectstack.h"
#include "../common/taskapp.h"

#define EFFECTING_MAX_NAME_LEN 64

/* menu rows: the stack's rows from EFFECTING_MENU_STACK */
enum
{
  EFFECTING_MENU_DISPLAY,
  EFFECTING_MENU_SAVE,
  EFFECTING_MENU_STACK,
  EFFECTING_MENU_APPLY = EFFECTING_MENU_STACK + EFFECTSTACK_NROWS,
  EFFECTING_MENU_CANCEL,
  EFFECTING_MENU__LIMIT
};

/* window's task: a PNG or JPEG dropped on its icon or window (DataLoad) is
 * run through an effect stack (see effectstack.h). SELECT toggles between
 * the result and the original. Menu edits the stack, sets the dithering used
 * on paletted screens, makes the result permanent (Apply) or discards it
 * (Cancel) or writes it as a PNG. Every change re-applies at once. */
typedef struct effecting_task
{
  taskapp_instance_t base;
  bitmap_t           source;  /* owned base; NULL until DataLoad */
  int                show_original;
  effectstack_t      stack;

  char               path[DPTLIB_MAXPATH]; /* file shown; "" until
                                * DataLoad */
  char               name[EFFECTING_MAX_NAME_LEN]; /* leafname of the shown
                                * image, less ".png" */

  dithermenu_t       dither;
  screen_dither_t    dithering; /* method for paletted screens */
  int                scale;     /* percent the image is shown at */
  wuss_scaleview_t  *scaleview; /* owned: "Display"'s "Scale view" leaf;
                                 * freed at QUIT */
  wuss_menu_item_t   menu_items[EFFECTING_MENU__LIMIT];
}
effecting_task_t;

/* install the icon bar app that opens effects windows on demand */
result_t effecting_app_create(wuss_t *wuss);

#endif /* WUSS_APP */

#endif /* TASKS_EFFECTING_H */
