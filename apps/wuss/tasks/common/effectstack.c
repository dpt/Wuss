/* wuss/tasks/common/effectstack.c -- an editable stack of bitmap effects */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/effects.h"
#include "framebuf/tonemap.h"
#include "utils/array.h"
#include "wuss/component/colourmenu.h"
#include "wuss/gadget/gridview.h"
#include "wuss/menu.h"

#include "effectstack.h"
#include "effectstack-edit.h"
#include "common.h"

#define EFFECTSTACK_LABEL_CHARS 22
#define EFFECTSTACK_LIST_ROWS   8

static const char *const g_names[EFFECTSTACK__LIMIT] =
{
  "Clear",
  "Tone",
  "Grey",
  "Blur",
  "Sharpen",
  "Expand",
  "Equalise"
};

/* the effects' glyphs, in the shared icon pool */
static const char *const g_glyph_names[EFFECTSTACK__LIMIT] =
{
  "effect_clear",
  "effect_tone",
  "effect_grey",
  "effect_blur",
  "effect_sharpen",
  "effect_expand",
  "effect_equalise"
};

/* ----------------------------------------------------------------------- */

/* Run one element from src into dst, which may be the same bitmap. */
static result_t effectstack_apply_element(const effectstack_element_t *el,
                                          const bitmap_t              *src,
                                          bitmap_t                    *dst)
{
  switch (el->type)
  {
  case EFFECTSTACK_CLEAR:
    return effects_clear_apply(src, dst, el->u.clear);

  case EFFECTSTACK_TONE:
    return effects_tonemap_apply(src, dst, el->u.tone);

  case EFFECTSTACK_GREY:
    return effects_grey_apply(src, dst);

  case EFFECTSTACK_BLUR:
    return effects_blur_apply(src, dst, el->u.blur.method, el->u.blur.amount);

  case EFFECTSTACK_SHARPEN:
    return effects_sharpen_apply(src, dst);

  case EFFECTSTACK_EXPAND:
    return effects_expand_apply(src, dst, 0);

  case EFFECTSTACK_EQUALISE:
    return effects_equalise_apply(src, dst);

  default:
    return result_BAD_ARG;
  }
}

/* Make chain and result match source's size and format. */
static result_t effectstack_alloc(effectstack_t  *stack,
                                  const bitmap_t *source)
{
  size_t size;

  if (stack->chain.base != NULL &&
      stack->chain.size.w == source->size.w &&
      stack->chain.size.h == source->size.h &&
      stack->chain.format == source->format &&
      stack->chain.rowbytes == source->rowbytes)
    return result_OK;

  free(stack->chain.base);
  free(stack->result.base);

  size = (size_t) source->rowbytes * source->size.h;

  stack->chain       = *source;
  stack->chain.base  = malloc(size);
  stack->result      = *source;
  stack->result.base = malloc(size);
  if (stack->chain.base == NULL || stack->result.base == NULL)
  {
    free(stack->chain.base);
    free(stack->result.base);
    stack->chain.base  = NULL;
    stack->result.base = NULL;

    return result_OOM;
  }

  return result_OK;
}

result_t effectstack_run(effectstack_t  *stack,
                         const bitmap_t *source,
                         int             rechain)
{
  result_t rc;
  int      i;

  if (stack->chain.base == NULL)
    rechain = 1;

  rc = effectstack_alloc(stack, source);
  if (rc != result_OK)
    return rc;

  if (rechain)
  {
    if (stack->nelements == 0)
      memcpy(stack->chain.base, source->base,
             (size_t) source->rowbytes * source->size.h);

    /* rerunning the stack is the slow part: a level drag only blends */
    wuss_hourglass_on(stack->base->wuss);
    rc = result_OK;
    for (i = 0; i < stack->nelements && rc == result_OK; i++)
      rc = effectstack_apply_element(&stack->elements[i],
                                     i == 0 ? source : &stack->chain,
                                     &stack->chain);
    wuss_hourglass_off(stack->base->wuss);
    if (rc != result_OK)
      return rc;
  }

  return effects_blend_extrapolate(source, &stack->chain, &stack->result,
                                   stack->level * 256 / 100);
}

void effectstack_changed(effectstack_t *stack, int rechain)
{
  stack->changed(stack->opaque, rechain);
}

/* ----- the stack ----- */

effectstack_element_t *effectstack_selected(effectstack_t *stack, int *index)
{
  int i;

  if (stack->list == NULL)
    return NULL;

  for (i = 0; i < stack->nelements; i++)
  {
    if (!wuss_gridview_is_selected(stack->list, i))
      continue;

    if (index != NULL)
      *index = i;

    return &stack->elements[i];
  }

  return NULL;
}

/* wuss_gridview_item_fn_t: opaque is the effectstack_t */
static void effectstack_item(int                   index,
                             void                 *opaque,
                             wuss_gridview_item_t *out)
{
  effectstack_t               *stack;
  const effectstack_element_t *el;
  const char                  *name;

  stack = opaque;
  el    = &stack->elements[index];
  name  = g_names[el->type];

  switch (el->type)
  {
  case EFFECTSTACK_BLUR:
    snprintf(stack->label, sizeof(stack->label), "%s blur %d",
             el->u.blur.method == effects_blur_BOX ? "Box" : "Gaussian",
             el->u.blur.amount);
    break;

  case EFFECTSTACK_CLEAR:
  {
    unsigned int r, g, b;

    colour_get_rgb(&el->u.clear, &r, &g, &b);
    snprintf(stack->label, sizeof(stack->label), "%s %02X%02X%02X", name, r,
             g, b);
    break;
  }

  default:
    snprintf(stack->label, sizeof(stack->label), "%s", name);
    break;
  }

  out->glyph = stack->glyphs[el->type];
  out->label = stack->label;
}

/* Sizes the list window to its rows. The gridview's minimum width keeps it
 * wide enough for its title. */
static void effectstack_fit_list(effectstack_t *stack)
{
  wuss_gridview_fit_window(stack->list, 1, EFFECTSTACK_LIST_ROWS, NULL);
}

static void effectstack_list_changed(effectstack_t *stack)
{
  if (stack->list != NULL)
  {
    (void) wuss_gridview_set_count(stack->list, stack->nelements);
    /* shrink as well as grow, so a delete leaves no blank rows */
    effectstack_fit_list(stack);
  }

  /* an open menu must reflect the new stack and selection */
  effectstack_menu_update(stack);
}

/* Appends an element of the given type, selects it and re-runs. */
static result_t effectstack_add(effectstack_t     *stack,
                                effectstack_type_t type)
{
  effectstack_element_t el;

  memset(&el, 0, sizeof(el));
  el.type = type;

  switch (type)
  {
  case EFFECTSTACK_CLEAR:
    el.u.clear = colour_rgb(0, 0, 0);
    break;

  case EFFECTSTACK_TONE:
    el.u.tone = tonemap_create();
    if (el.u.tone == NULL)
      return result_OOM;
    break;

  case EFFECTSTACK_BLUR:
    el.u.blur.method = effects_blur_GAUSSIAN;
    el.u.blur.amount = 8;
    break;

  default:
    break;
  }

  if (array_grow((void **) &stack->elements, sizeof(stack->elements[0]),
                 stack->nelements, &stack->allocated, 1, 8))
  {
    if (type == EFFECTSTACK_TONE)
      tonemap_destroy(el.u.tone);

    return result_OOM;
  }

  stack->elements[stack->nelements++] = el;

  effectstack_list_changed(stack);
  if (stack->list != NULL)
    wuss_gridview_select(stack->list, stack->nelements - 1);

  /* the new element is selected only now */
  effectstack_menu_update(stack);

  effectstack_changed(stack, 1);

  return result_OK;
}

static void effectstack_free_element(effectstack_element_t *el)
{
  if (el->type == EFFECTSTACK_TONE)
    tonemap_destroy(el->u.tone);
}

static void effectstack_delete(effectstack_t *stack, int index)
{
  effectstack_free_element(&stack->elements[index]);
  array_delete_element(stack->elements, sizeof(stack->elements[0]),
                       stack->nelements, index);
  stack->nelements--;

  if (stack->list != NULL)
    wuss_gridview_clear_selection(stack->list);
  effectstack_list_changed(stack);
  effectstack_changed(stack, 1);
}

static void effectstack_delete_all(effectstack_t *stack)
{
  int i;

  for (i = 0; i < stack->nelements; i++)
    effectstack_free_element(&stack->elements[i]);
  stack->nelements = 0;

  if (stack->list != NULL)
    wuss_gridview_clear_selection(stack->list);
  effectstack_list_changed(stack);
  effectstack_changed(stack, 1);
}

void effectstack_reset(effectstack_t *stack)
{
  stack->level = 100;
  effectstack_delete_all(stack);
}

/* Moves element from to position to, keeping it selected. */
static void effectstack_move(effectstack_t *stack, int from, int to)
{
  effectstack_element_t el;

  if (from == to)
    return;

  el = stack->elements[from];
  if (from < to)
    memmove(&stack->elements[from], &stack->elements[from + 1],
            (size_t) (to - from) * sizeof(el));
  else
    memmove(&stack->elements[to + 1], &stack->elements[to],
            (size_t) (from - to) * sizeof(el));
  stack->elements[to] = el;

  if (stack->list != NULL)
  {
    effectstack_list_changed(stack);
    wuss_gridview_select(stack->list, to);
  }

  effectstack_changed(stack, 1);
}

/* wuss_gridview_drag_fn_t: opaque is the effectstack_t */
static int effectstack_drag(wuss_gridview_t *gridview,
                            int              index,
                            void            *opaque)
{
  effectstack_t *stack;

  NOT_USED(gridview);

  stack             = opaque;
  stack->drag_index = index;

  return 1;
}

/* The drag ended: a drop on another row of the stack moves the element
 * there. */
static void effectstack_drop(effectstack_t *stack, const wuss_event_t *event)
{
  box_t   content;
  point_t scroll;
  point_t point;
  int     target;
  int     from;

  from              = stack->drag_index;
  stack->drag_index = -1;

  if (from < 0 || stack->list == NULL || event->data.drag_end.cancelled ||
      event->data.drag_end.drop != stack->list_window)
    return;

  wuss_window_get_content_bounds(stack->list_window, &content);
  wuss_window_get_scroll(stack->list_window, &scroll);

  point  = event->data.drag_end.point;
  point  = POINT(point.x - content.x0 + scroll.x,
                 point.y - content.y0 + scroll.y);
  target = wuss_gridview_hit_test(stack->list, point);
  if (target < 0)
    return;

  effectstack_move(stack, from, target);
}

result_t effectstack_open(effectstack_t *stack, wuss_window_t *window)
{
  result_t     rc;
  wuss_place_t place;

  if (stack->list_window != NULL)
  {
    wuss_window_restack(stack->list_window, wuss_ZORDER_FRONT);
    return result_OK;
  }

  place = task_place_after(window);
  rc = wuss_gridview_window_create(&stack->list, stack->base->delegate,
                                   &place, "Effect stack", 0,
                                   effectstack_item, EFFECTSTACK_LABEL_CHARS,
                                   NULL, stack, wuss_WINDOW_CLOSE,
                                   &stack->list_window);
  if (rc != result_OK)
    return rc;

  wuss_gridview_set_layout(stack->list, 1, 0, 4);
  wuss_gridview_set_min_width(stack->list,
                              wuss_window_get_title_width(stack->list_window));
  wuss_gridview_set_drag(stack->list, effectstack_drag);
  (void) wuss_gridview_set_count(stack->list, stack->nelements);
  effectstack_fit_list(stack);

  /* for its fitted size */
  return wuss_window_place(stack->list_window, &place);
}

void effectstack_close(effectstack_t *stack)
{
  if (stack->list_window == NULL)
    return;

  /* a forced close sends no CLOSE, so free the gadget here, while its
   * window is still there */
  wuss_gridview_destroy(stack->list);
  stack->list = NULL;
  wuss_window_close(stack->list_window);
  stack->list_window = NULL;
}

/* ----------------------------------------------------------------------- */

result_t effectstack_init(effectstack_t            *stack,
                          taskapp_instance_t       *base,
                          effectstack_changed_fn_t *changed,
                          void                     *opaque)
{
  int i;

  memset(stack, 0, sizeof(*stack));
  stack->base       = base;
  stack->changed    = changed;
  stack->opaque     = opaque;
  stack->level      = 100;
  stack->drag_index = -1;

  for (i = 0; i < EFFECTSTACK__LIMIT; i++)
  {
    stack->glyphs[i] = wuss_task_iconpool_find(base->delegate,
                                               g_glyph_names[i]);
    WUSS_MENU_ITEM(stack->add_items, i, g_names[i], wuss_MENU_ITEM_NONE);
  }
  WUSS_MENU_TITLE(stack->add_menu, "Add", stack->add_items,
                  NELEMS(stack->add_items));

  return effectstack_edit_create(stack, &stack->edit);
}

void effectstack_destroy(effectstack_t *stack)
{
  int i;

  for (i = 0; i < stack->nelements; i++)
    effectstack_free_element(&stack->elements[i]);
  free(stack->elements);
  stack->elements  = NULL;
  stack->nelements = 0;

  wuss_gridview_destroy(stack->list);
  stack->list = NULL;
  effectstack_edit_destroy(stack->edit);
  stack->edit = NULL;
  free(stack->chain.base);
  free(stack->result.base);
  stack->chain.base  = NULL;
  stack->result.base = NULL;
}

void effectstack_menu_rows(effectstack_t    *stack,
                           wuss_menu_t      *menu,
                           wuss_menu_item_t *items,
                           int               first)
{
  stack->menu  = menu;
  stack->rows  = &items[first];
  stack->first = first;

  WUSS_MENU_ITEM_MENU(items, first + EFFECTSTACK_ROW_ADD, "Add",
                      wuss_MENU_ITEM_BORROWED_SUBMENU, &stack->add_menu);
  WUSS_MENU_ITEM(items, first + EFFECTSTACK_ROW_EDIT, "Edit",
                 wuss_MENU_ITEM_DISABLED);
  WUSS_MENU_ITEM(items, first + EFFECTSTACK_ROW_DELETE, "Delete",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(items, first + EFFECTSTACK_ROW_DELETE_ALL, "Delete all",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM_WINDOW(items, first + EFFECTSTACK_ROW_LEVEL, "Level",
                        wuss_MENU_ITEM_BORROWED_SUBMENU |
                        wuss_MENU_ITEM_PRE_OPEN,
                        effectstack_edit_window(stack->edit,
                                                EFFECTSTACK_EDIT_LEVEL));
}

void effectstack_menu_update(effectstack_t *stack)
{
  wuss_menu_handle_t     handle;
  wuss_menu_item_t      *edit_row;
  effectstack_element_t *el;

  if (stack->menu == NULL)
    return;

  handle   = stack->base->menu_handle;
  el       = effectstack_selected(stack, NULL);
  edit_row = &stack->rows[EFFECTSTACK_ROW_EDIT];

  edit_row->submenu = NULL;
  edit_row->window  = NULL;
  edit_row->flags   = wuss_MENU_ITEM_DISABLED;
  if (el != NULL)
  {
    switch (el->type)
    {
    case EFFECTSTACK_CLEAR:
      edit_row->submenu = wuss_colourmenu_menu(stack->base->wuss);
      edit_row->flags   = wuss_MENU_ITEM_PRE_OPEN;
      break;

    case EFFECTSTACK_TONE:
      edit_row->window = effectstack_edit_window(stack->edit,
                                                 EFFECTSTACK_EDIT_TONE);
      edit_row->flags  = wuss_MENU_ITEM_BORROWED_SUBMENU |
                         wuss_MENU_ITEM_PRE_OPEN;
      break;

    case EFFECTSTACK_BLUR:
      edit_row->window = effectstack_edit_window(stack->edit,
                                                 EFFECTSTACK_EDIT_BLUR);
      edit_row->flags  = wuss_MENU_ITEM_BORROWED_SUBMENU |
                         wuss_MENU_ITEM_PRE_OPEN;
      break;

    default:
      break;
    }
  }

  wuss_menu_disable_item_live(handle, stack->menu,
                              stack->first + EFFECTSTACK_ROW_EDIT,
                              edit_row->flags & wuss_MENU_ITEM_DISABLED);
  wuss_menu_disable_item_live(handle, stack->menu,
                              stack->first + EFFECTSTACK_ROW_DELETE,
                              el == NULL);
  wuss_menu_disable_item_live(handle, stack->menu,
                              stack->first + EFFECTSTACK_ROW_DELETE_ALL,
                              stack->nelements == 0);
}

/* ----------------------------------------------------------------------- */

static result_t effectstack_menu_select(effectstack_t      *stack,
                                        const wuss_event_t *event,
                                        int                *handled)
{
  effectstack_element_t *el;
  colour_t               colour;
  int                    selected;

  *handled = 1;

  if (wuss_colourmenu_selected_rgb(event, &colour))
  {
    el = effectstack_selected(stack, NULL);
    if (el != NULL && el->type == EFFECTSTACK_CLEAR)
    {
      el->u.clear = colour;
      effectstack_changed(stack, 1);
      if (stack->list != NULL)
        (void) wuss_gridview_set_count(stack->list, stack->nelements);
    }
    return result_OK;
  }

  if (event->data.menu_select.menu == &stack->add_menu)
    return effectstack_add(stack, (effectstack_type_t)
                           event->data.menu_select.index);

  if (wuss_menu_select_is(event, stack->menu,
                          stack->first + EFFECTSTACK_ROW_DELETE))
  {
    if (effectstack_selected(stack, &selected) != NULL)
      effectstack_delete(stack, selected);
    return result_OK;
  }

  if (wuss_menu_select_is(event, stack->menu,
                          stack->first + EFFECTSTACK_ROW_DELETE_ALL))
  {
    effectstack_delete_all(stack);
    return result_OK;
  }

  *handled = 0;
  return result_OK;
}

int effectstack_event(effectstack_t      *stack,
                      wuss_window_t      *window,
                      const wuss_event_t *event,
                      result_t           *rc)
{
  int in_list;
  int which;
  int handled;

  *rc     = result_OK;
  in_list = stack->list != NULL && window == stack->list_window;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
  case wuss_EVENT_OPEN:
  case wuss_EVENT_PALETTE:
  case wuss_EVENT_POINTER_EXIT:
  case wuss_EVENT_RELAYOUT:
    if (!in_list)
      return 0;
    (void) wuss_gridview_handle_event(stack->list, event);
    return 1;

  case wuss_EVENT_IDLE: /* window is NULL: forward, but leave it unclaimed */
    if (stack->list != NULL)
      (void) wuss_gridview_handle_event(stack->list, event);
    return 0;

  case wuss_EVENT_MOUSE:
    if (!in_list)
      return 0;
    (void) wuss_gridview_handle_event(stack->list, event);
    return !(event->data.mouse.action == wuss_MOUSE_DOWN &&
             (event->data.mouse.button & wuss_BUTTON_MENU));

  case wuss_EVENT_DRAG_END:
    if (stack->drag_index < 0)
      return 0;
    effectstack_drop(stack, event);
    return 1;

  case wuss_EVENT_ICON:
    which = effectstack_edit_find(stack->edit, window);
    if (which < 0)
      return 0;
    *rc = effectstack_edit_icon(stack->edit, (effectstack_edit_which_t) which,
                                event);
    return 1;

  case wuss_EVENT_PRE_SUBMENU_OPEN:
  {
    effectstack_element_t *el;

    /* the Edit row of a Clear element: the colour menu */
    el = effectstack_selected(stack, NULL);
    if (el == NULL || el->type != EFFECTSTACK_CLEAR)
      return 0;
    *rc = wuss_colourmenu_open_rgb(stack->base->wuss, event, "Clear colour",
                                   el->u.clear);
    return 1;
  }

  case wuss_EVENT_MENU_SELECT:
    *rc = effectstack_menu_select(stack, event, &handled);
    return handled;

  case wuss_EVENT_MENU_CLOSED:
    if (stack->list != NULL)
    {
      (void) wuss_gridview_handle_event(stack->list, event);
      effectstack_menu_update(stack);
    }
    return 0;

  case wuss_EVENT_CLOSE:
    effectstack_edit_closed(stack->edit, window);
    if (window != stack->list_window)
      return 0;
    wuss_gridview_destroy(stack->list);
    stack->list        = NULL;
    stack->list_window = NULL;
    return 1;

  case wuss_EVENT_PRE_SHOW:
    if (event->data.pre_show.handle == NULL)
      return 0;
    which = effectstack_edit_find(stack->edit, window);
    if (which < 0)
      return 0;
    *rc = effectstack_edit_pre_show(stack->edit,
                                    (effectstack_edit_which_t) which);
    if (*rc == result_OK)
      *rc = wuss_menu_open_window_now(event->data.pre_show.handle,
                                      event->data.pre_show.index);
    return 1;

  case wuss_EVENT_KEY:
    if (!in_list || event->data.key.code != wuss_KEY_DELETE)
      return 0;
    if (effectstack_selected(stack, &which) != NULL)
      effectstack_delete(stack, which);
    return 1;

  default:
    return 0;
  }
}

#endif /* WUSS_APP */
