/* wuss/tasks/display/display.h -- desktop display mode picker task */

#ifndef TASKS_DISPLAY_H
#define TASKS_DISPLAY_H

#ifdef WUSS_APP

#include "wuss/gadget/stringset.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../common/taskapp.h"

/* window D's task: a small window, after RISC OS's Display Manager, with
 * string sets for colour depth, resolution and zoom (1x-4x), and options for
 * the CRT effect and Software pointer, showing the current settings. Nothing
 * takes effect until Change: it applies the zoom, options and mode (via
 * app_set_mode, which live-changes the frontend's surface and wuss's own
 * screen, nudging every open window back on-screen and shrinking any that no
 * longer fit), then closes the window (ADJUST leaves it open). Cancel closes
 * it, discarding the picks; ADJUST-Cancel instead reverts them to the
 * settings in force. */
typedef struct display_task
{
  taskapp_instance_t  base;
  wuss_stringset_t   *zoom;
  wuss_stringset_t   *colours;
  wuss_stringset_t   *resolution;
  wuss_icon_t        *crt;
  wuss_icon_t        *pointer;
  wuss_icon_t        *cancel;
  wuss_icon_t        *change;
}
display_task_t;

/* install the icon bar app that opens display windows on demand */
result_t display_app_create(wuss_t *wuss);

/* open a display window now; the app must be installed first */
result_t display_open(wuss_t *wuss);

#endif /* WUSS_APP */

#endif /* TASKS_DISPLAY_H */
