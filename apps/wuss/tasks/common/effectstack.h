/* wuss/tasks/common/effectstack.h -- an editable stack of bitmap effects */

#ifndef TASKS_EFFECTSTACK_H
#define TASKS_EFFECTSTACK_H

#ifdef WUSS_APP

#include "framebuf/bitmap.h"
#include "framebuf/colour.h"
#include "framebuf/effects.h"
#include "framebuf/tonemap.h"
#include "wuss/gadget/gridview.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "taskapp.h"

#define EFFECTSTACK_LABEL_LEN 32 /* longest list label, with NUL */
#define EFFECTSTACK_LEVEL_MIN (-100)
#define EFFECTSTACK_LEVEL_MAX 200

/* the kinds of element in the stack; also the rows of the Add submenu */
typedef enum effectstack_type
{
  EFFECTSTACK_CLEAR,
  EFFECTSTACK_TONE,
  EFFECTSTACK_GREY,
  EFFECTSTACK_BLUR,
  EFFECTSTACK_SHARPEN,
  EFFECTSTACK_EXPAND,
  EFFECTSTACK_EQUALISE,
  EFFECTSTACK__LIMIT
}
effectstack_type_t;

/* one effect in the stack */
typedef struct effectstack_element
{
  effectstack_type_t type;
  union
  {
    colour_t clear;
    struct
    {
      effects_blur_method_t method;
      int                   amount;
    }
    blur;
    tonemap_t *tone; /* owned */
  }
  u;
}
effectstack_element_t;

/* the rows effectstack_menu_rows fills in the owner's menu */
enum
{
  EFFECTSTACK_ROW_ADD,
  EFFECTSTACK_ROW_EDIT,
  EFFECTSTACK_ROW_DELETE,
  EFFECTSTACK_ROW_DELETE_ALL,
  EFFECTSTACK_ROW_LEVEL,
  EFFECTSTACK_NROWS
};

/* The stack or its level changed: the owner calls effectstack_run again
 * (rechain is zero when only the level moved) and redraws. */
typedef void (effectstack_changed_fn_t)(void *opaque, int rechain);

/* A task's stack of framebuf/effects functions, run top to bottom over a
 * 32bpp source, the outcome blended with the source by the level (-100% to
 * 200%). The stack is shown in a list window: pick an element, drag it to
 * reorder it, Delete removes it. Rows in the owner's menu add, edit and
 * delete elements and set the level. Embed one in the task and route its
 * events through effectstack_event. */
typedef struct effectstack
{
  taskapp_instance_t       *base;
  effectstack_changed_fn_t *changed;
  void                     *opaque;

  effectstack_element_t    *elements; /* owned */
  int                       nelements;
  int                       allocated;
  int                       level;  /* percent of the chain in the result */
  bitmap_t                  chain;  /* owned base: source through stack */
  bitmap_t                  result; /* owned base: chain blended */

  wuss_window_t            *list_window;
  wuss_gridview_t          *list;
  char                      label[EFFECTSTACK_LABEL_LEN]; /* item callback */
  const bitmap_t           *glyphs[EFFECTSTACK__LIMIT]; /* borrowed */
  int                       drag_index; /* element being dragged, or -1 */

  wuss_menu_item_t          add_items[EFFECTSTACK__LIMIT];
  wuss_menu_t               add_menu;
  wuss_menu_t              *menu;  /* the owner's menu holding the rows */
  wuss_menu_item_t         *rows;  /* EFFECTSTACK_NROWS of its items */
  int                       first; /* index of rows[0] in menu */
  struct effectstack_edit  *edit;  /* the editing dialogues */
}
effectstack_t;

/* Make the stack, empty at level 100%, and its hidden dialogues. */
result_t effectstack_init(effectstack_t            *stack,
                          taskapp_instance_t       *base,
                          effectstack_changed_fn_t *changed,
                          void                     *opaque);

/* Free the stack, its bitmaps, dialogues and list gadget. */
void effectstack_destroy(effectstack_t *stack);

/* Fill items[first..first+EFFECTSTACK_NROWS-1], rows of menu: Add, Edit,
 * Delete, Delete all and Level. The owner adds dividers afterwards. */
void effectstack_menu_rows(effectstack_t    *stack,
                           wuss_menu_t      *menu,
                           wuss_menu_item_t *items,
                           int               first);

/* Set the rows to match the selection and the stack, live if open. */
void effectstack_menu_update(effectstack_t *stack);

/* Run the stack over source into chain (if rechain), then blend source and
 * chain into result by the level. chain and result are reallocated to match
 * source's size and format when they differ. */
result_t effectstack_run(effectstack_t  *stack,
                         const bitmap_t *source,
                         int             rechain);

/* Open the list window beside window, or bring it to the front. */
result_t effectstack_open(effectstack_t *stack, wuss_window_t *window);

/* Close the list window, if open, as when its owner goes. */
void effectstack_close(effectstack_t *stack);

/* Empty the stack and reset the level to 100%. */
void effectstack_reset(effectstack_t *stack);

/* Handle event for the list window, the dialogues and the rows. Return
 * non-zero when it was the stack's, with *rc set. A MENU click on the list
 * window is left to the owner, to open the menu holding the rows. */
int effectstack_event(effectstack_t      *stack,
                      wuss_window_t      *window,
                      const wuss_event_t *event,
                      result_t           *rc);

#endif /* WUSS_APP */

#endif /* TASKS_EFFECTSTACK_H */
