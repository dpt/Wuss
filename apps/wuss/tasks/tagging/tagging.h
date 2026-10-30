/* wuss/tasks/tagging/tagging.h -- tag cloud demo task */

#ifndef TASKS_TAGGING_H
#define TASKS_TAGGING_H

#ifdef WUSS_APP

#include "wuss/component/dialogue.h"
#include "wuss/gadget/tagcloud.h"
#include "wuss/icon.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#define TAGGING_MAX_TAGS  200 /* room for the random set plus new ones */
#define TAGGING_NAME_LEN  32

/* Main menu rows */
enum
{
  TAGGING_MENU_DISPLAY,
  TAGGING_MENU_TAG,
  TAGGING_MENU_NEW,
  TAGGING_MENU__LIMIT
};

/* Display submenu rows */
enum
{
  TAGGING_DISPLAY_LIST,
  TAGGING_DISPLAY_CLOUD,
  TAGGING_DISPLAY_SCALING_OFF,
  TAGGING_DISPLAY_SCALING_ON,
  TAGGING_DISPLAY_SORT_NAME,
  TAGGING_DISPLAY_SORT_COUNT,
  TAGGING_DISPLAY_SELECTED_FIRST,
  TAGGING_DISPLAY__LIMIT
};

/* Tag submenu rows */
enum
{
  TAGGING_TAG_RENAME,
  TAGGING_TAG_DELETE,
  TAGGING_TAG__LIMIT
};

/* Name dialogue icons */
enum
{
  TAGGING_NAME_FIELD,
  TAGGING_NAME_CANCEL,
  TAGGING_NAME_OK,
  TAGGING_NAME__LIMIT
};

typedef struct tagging_tag
{
  char name[TAGGING_NAME_LEN];
  int  count;
  int  highlighted;
}
tagging_tag_t;

/* window's task: a tag cloud of random names. Clicks toggle a tag's
 * highlight; the menu changes the display and adds, renames and deletes
 * tags. */
typedef struct tagging_task
{
  wuss_window_t          *window;
  wuss_t                 *wuss;
  wuss_task_t            *delegate;
  wuss_tagcloud_t        *tagcloud;
  wuss_tagcloud_options_t options;

  tagging_tag_t           tags[TAGGING_MAX_TAGS];
  int                     ntags;
  int                     current; /* tag last clicked or menued, -1 for
                                    * none */
  int                     renaming; /* tag the name dialogue renames, -1 to
                                     * add a new one */

  char                    tag_text[TAGGING_NAME_LEN + 8]; /* "Tag 'name'" */
  wuss_menu_item_t        display_items[TAGGING_DISPLAY__LIMIT];
  wuss_menu_t             display_menu;
  wuss_menu_item_t        tag_items[TAGGING_TAG__LIMIT];
  wuss_menu_t             tag_menu;
  wuss_menu_item_t        menu_items[TAGGING_MENU__LIMIT];
  wuss_menu_t             menu;
  wuss_menu_handle_t      menu_handle; /* the open chain, if any */

  wuss_task_t            *name_task;   /* the name dialogue's own task */
  wuss_dialogue_t        *name_dialogue;
  wuss_icon_t            *name_icons[TAGGING_NAME__LIMIT];
  wuss_menu_handle_t      name_handle; /* live only while shown */
}
tagging_task_t;

wuss_window_fn_t tagging_handle;

/* create the window against the given wuss instance. if out is non-NULL,
 * the task block is also returned through it */
result_t tagging_create(wuss_t *wuss, tagging_task_t **out);

/* install the icon bar app that opens tagging windows on demand */
result_t tagging_app_create(wuss_t *wuss);

/* free a task block allocated by tagging_create; normally called by the
 * window's wuss_EVENT_QUIT handler, not by callers directly */
void tagging_destroy(tagging_task_t *task);

#endif /* WUSS_APP */

#endif /* TASKS_TAGGING_H */
