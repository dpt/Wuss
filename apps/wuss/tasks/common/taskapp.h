/* wuss/tasks/common/taskapp.h -- icon bar app wrapper for the demo tasks */

#ifndef TASKS_TASKAPP_H
#define TASKS_TASKAPP_H

#ifdef WUSS_APP

#include <stdbool.h>
#include <stddef.h>

#include "base/result.h"
#include "framebuf/bitmap.h"
#include "geom/size.h"
#include "wuss/component/saveas.h"
#include "wuss/menu.h"
#include "wuss/message.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

typedef struct taskapp_desc taskapp_desc_t;

/* The first member of an instance block that taskapp owns: see
 * taskapp_desc_t::instance_size. Filled in by taskapp, apart from what the
 * comments say the task sets. */
typedef struct taskapp_instance
{
  wuss_t               *wuss;
  const taskapp_desc_t *desc;
  wuss_task_t          *delegate;
  wuss_window_t        *window;      /* the main window, NULL once closed */
  wuss_menu_t           menu;        /* the task sets this, if it has a menu */
  wuss_menu_handle_t    menu_handle; /* the task sets this when opening a menu;
                                      * NULL again once it closes */
  wuss_saveas_t        *saveas;      /* see taskapp_desc_t::save_name;
                                      * destroyed with the instance */
  int                   save_row;    /* see taskapp_save_row; -1 for none */
}
taskapp_instance_t;

/* Open an instance as desc->create does for a task that manages its own
 * block. Such a block is owned by its window and freed from the task's
 * wuss_EVENT_QUIT handler, so an app only ever calls create with NULL for the
 * block, which is type-compatible however the second parameter is spelt. */
typedef result_t (*taskapp_create_fn_t)(wuss_t *wuss, void **out);

/* Set up an instance whose block taskapp has allocated, its task registered
 * and its window (if it has a window_size) open. On failure, the block goes
 * through the task's wuss_EVENT_QUIT as usual, so that must cope with an
 * instance set up only in part. The parameter is the task's own block type,
 * whose first member is a taskapp_instance_t. */
typedef result_t (*taskapp_init_fn_t)(void *instance);

/* Load the file at path into an instance create has just returned. Optional
 * in a taskapp_desc: with none, a file dropped on the app's icon is not
 * acknowledged. */
typedef result_t (*taskapp_data_load_fn_t)(void *instance, const char *path);

/* Open the app's "new instance" dialogue. Optional in a taskapp_desc: with
 * one, SELECT on the icon of the app with no windows and the "New..." row in
 * its menu call it in place of create. The dialogue opens an instance
 * itself, once the user accepts it, through taskapp_spawn. */
typedef result_t (*taskapp_new_fn_t)(wuss_t *wuss);

/* Make an instance as create does, but from fn with the caller's param; see
 * taskapp_spawn */
typedef result_t (*taskapp_spawn_fn_t)(wuss_t *wuss, void *param, void **out);

/* A task's description of its app. Each task keeps one as a static const and
 * passes it to taskapp_install from its X_app_create. */
struct taskapp_desc
{
  const char          *name;    /* icon label, menu title, proginfo name */
  const char          *purpose; /* proginfo "Purpose" row */
  taskapp_create_fn_t  create;  /* opens an instance of the main window; NULL
                                 * for taskapp_instance_create to do so */
  taskapp_data_load_fn_t data_load; /* optional: file dropped on the icon */
  bool                 drop_only;   /* SELECT on the icon opens nothing and
                                     * the menu has no "New" row */
  taskapp_new_fn_t     new_dialogue; /* optional: see taskapp_new_fn_t */
  bool                 singleton;    /* one instance only: the menu has no
                                      * "New" row */
  taskapp_create_fn_t  adjust;       /* optional: the menu gets a row for
                                      * each of create and adjust, labelled
                                      * create_label and adjust_label */
  const char          *create_label; /* menu row for create, if adjust */
  const char          *adjust_label; /* menu row for adjust, if adjust */
  const unsigned short *opens;       /* optional: 0-terminated RISC OS
                                      * filetypes this app claims from a
                                      * wuss_MESSAGE_DATA_OPEN; needs
                                      * data_load */
  int                  iconbar_priority; /* icon bar placement on the right;
                                          * higher is further outward, and
                                          * ordinary apps leave it 0 */
  bool               (*claims)(const char *path); /* optional: with opens,
                                      * claim only the paths this accepts,
                                      * leaving the rest to other apps */
  bool                 no_quit;      /* the menu has no "Quit" row: a system
                                      * app that stays on the icon bar */

  /* for taskapp_instance_create: */
  size_t               instance_size; /* bytes in the task's block, whose first
                                       * member is a taskapp_instance_t */
  size2d_t             window_size;  /* of the main window; zero for init to
                                      * open its own */
  const char          *window_title; /* optional: name if NULL */
  taskapp_init_fn_t    init;         /* optional: see taskapp_init_fn_t */
  wuss_window_fn_t    *handle;       /* the task's events; see
                                      * taskapp_instance_create */
  bool                 all_windows;  /* pass handle wuss_EVENT_MOUSE,
                                      * wuss_EVENT_SCROLL and wuss_EVENT_KEY
                                      * for every window */
  const char          *save_name;    /* Save As's initial leafname; with this
                                      * or save, the instance gets a PNG saveas
                                      * before init runs */
  wuss_saveas_save_fn_t *save;       /* optional: writes the file, its opaque
                                      * the block; if NULL, a snapshot of the
                                      * main window through handle */
};

/* Install the app for desc: a task with an icon bar icon (labelled name,
 * drawn with the application glyph) and a menu of Info, New (unless
 * desc->singleton or desc->drop_only), Hide and Quit (unless desc->no_quit).
 * SELECT or ADJUST on
 * the icon of a hidden app shows it again. Otherwise SELECT brings every
 * window of the app to the front or, when it has none, calls desc->create
 * for an instance of the main window and focuses it, each instance a child
 * task of the app's, so no window opens until asked for; ADJUST does
 * nothing.
 * A wuss_MESSAGE_DATA_LOAD sent to the icon (a file dropped on it) opens an
 * instance the same way and passes it the path through desc->data_load; it
 * is acknowledged only if that returns result_OK. A wuss_MESSAGE_DATA_OPEN
 * broadcast (a file double-clicked in a Filer) whose filetype is in
 * desc->opens is acknowledged at once and then opened the same way; a load
 * that fails is left for desc->data_load to report. With desc->drop_only,
 * SELECT does nothing, so a drop is the only way to open an instance.
 * Does nothing, returning result_OK, if desc is already installed. Without
 * the icon bar, calls desc->create at once instead. */
result_t taskapp_install(wuss_t *wuss, const taskapp_desc_t *desc);

/* Allocate a zeroed block of desc->instance_size, register its task, open its
 * main window, which reaps the task when it closes, create its saveas if it
 * has one, then call desc->init. desc->handle receives the block as task_data,
 * and every event except wuss_EVENT_MOUSE, wuss_EVENT_SCROLL and
 * wuss_EVENT_KEY on a window other than the main one, unless
 * desc->all_windows, and the selection of a row made by taskapp_save_row,
 * which opens the saveas instead.
 * taskapp clears menu_handle on wuss_EVENT_MENU_CLOSED first and, after handle
 * returns, clears window on its wuss_EVENT_CLOSE and destroys saveas and frees
 * the block on wuss_EVENT_QUIT. */
result_t taskapp_instance_create(wuss_t               *wuss,
                                 const taskapp_desc_t *desc,
                                 void                **out);

/* Make row of items, the rows of instance->menu, the standard Save row:
 * hovering opens the Save As dialogue as a submenu, and selecting it or F3
 * shows the dialogue standalone. */
void taskapp_save_row(taskapp_instance_t    *instance,
                      wuss_menu_item_t      *items,
                      int                    row,
                      wuss_menu_item_flags_t flags);

/* Open an instance of desc's app by calling fn(wuss, param, out), as create
 * would be called: the instance becomes a child task of the app's, so Quit
 * takes it too, and it takes the focus. For a "new instance" dialogue to
 * call once accepted. Fails with result_NOT_FOUND if desc is not installed.
 * Without the icon bar, just calls fn. */
result_t taskapp_spawn(wuss_t               *wuss,
                       const taskapp_desc_t *desc,
                       taskapp_spawn_fn_t    fn,
                       void                 *param,
                       void                **out);

/* A DataLoad on an instance's window: load the file it names through load
 * and acknowledge it. A file that will not load is not acknowledged, so the
 * send bounces to the sender. Messages of any other action are ignored. Call
 * it from the instance's wuss_EVENT_MESSAGE handler, after checking the
 * message is for the instance's main window. */
result_t taskapp_window_data_load(wuss_t                *wuss,
                                  wuss_task_t           *delegate,
                                  void                  *instance,
                                  taskapp_data_load_fn_t load,
                                  const wuss_message_t  *msg);

/* Size of the buffer taskapp_glyph_name fills. */
#define TASKAPP_GLYPH_NAME_MAX 32

/* The icon pool entry name for the app called name: "app_" plus name
 * lowercased, keeping only its letters and digits ("Chase H.Q." gives
 * "app_chasehq"). */
void taskapp_glyph_name(const char *name, char leaf[TASKAPP_GLYPH_NAME_MAX]);

/* The glyph for the app called name: "app_" plus name lowercased, keeping
 * only its letters and digits ("Chase H.Q." finds "app_chasehq"), else the
 * generic "file_app". Looked up through task's pools; NULL if neither is
 * there. Icon bar builds only. */
const bitmap_t *taskapp_glyph(const wuss_task_t *task, const char *name);

/* Destroy every app whose Quit was picked, with its instances. Call it after
 * wuss_idle, never from inside it: destroying an app takes its child tasks
 * with it, so it cannot happen while wuss_idle walks the task list. */
void taskapp_reap(void);

#endif /* WUSS_APP */

#endif /* TASKS_TASKAPP_H */
