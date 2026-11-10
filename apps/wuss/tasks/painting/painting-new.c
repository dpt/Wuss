/* wuss/tasks/painting/painting-new.c -- Painting's New dialogue */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "geom/box.h"
#include "geom/stack.h"
#include "wuss/component/dialogue.h"
#include "wuss/icon.h"
#include "wuss/icon-spec.h"
#include "wuss/iconbar.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../common/common.h"
#include "painting-new.h"

/* ----------------------------------------------------------------------- */

#define PN_DIGITS   4    /* PAINTINGNEW_SIZE_MAX is four digits */
#define PN_G_LABEL  1    /* size group, see stack_item_t::group */

#define PN_DEFAULT_W 640
#define PN_DEFAULT_H 480

/* the background choices, in the order of the radios */
enum
{
  PN_BG_WHITE,
  PN_BG_BLACK,
  PN_BG_GREY,
  PN_BG_TRANSPARENT,
  PN_BG__COUNT
};

static const char *const paintingnew_bg_names[PN_BG__COUNT] =
{
  "White", "Black", "Grey", "Transparent"
};

/* icons, in creation order */
enum
{
  PN_ICON_DIM_LABEL,
  PN_ICON_WIDTH,
  PN_ICON_X_LABEL,
  PN_ICON_HEIGHT,
  PN_ICON_BG_LABEL,
  PN_ICON_BG,        /* first of PN_BG__COUNT radios */
  PN_ICON_CANCEL = PN_ICON_BG + PN_BG__COUNT,
  PN_ICON_CREATE,
  PN_ICON__COUNT
};

/* layout items, in creation order. See docs/windowing/wuss-layout.md */
enum
{
  PN_ST_ROOT,
  PN_ST_DIM_ROW,
  PN_ST_DIM_LABEL,
  PN_ST_WIDTH,
  PN_ST_X_LABEL,
  PN_ST_HEIGHT,
  PN_ST_BG_ROW,
  PN_ST_BG_LABEL,
  PN_ST_BG,          /* first of PN_BG__COUNT radios */
  PN_ST_BUTTONS = PN_ST_BG + PN_BG__COUNT,
  PN_ST_SPACER,
  PN_ST_CANCEL,
  PN_ST_CREATE,
  PN_ST__LIMIT
};

#define PN_ST_RADIO(i) \
  [PN_ST_BG + (i)] = STACK_LEAF(PN_ST_BG_ROW, 0, 0, stack_ALIGN_CENTRE)

/* sizes are zero here: paintingnew_layout sets them from the font */
static const stack_item_t g_paintingnew_stack[PN_ST__LIMIT] =
{
  [PN_ST_ROOT] = { .kind = stack_KIND_VBOX, .parent = -1,
                   .gap = wuss_STD_GAP, .pad = wuss_STD_INSETS },

  [PN_ST_DIM_ROW]      = STACK_HBOX(PN_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_START),
  [PN_ST_DIM_LABEL]    = STACK_LEAF_GROUP(PN_ST_DIM_ROW, 0, 0, stack_ALIGN_CENTRE, PN_G_LABEL),
  [PN_ST_WIDTH]        = STACK_LEAF(PN_ST_DIM_ROW, 0, 0, stack_ALIGN_CENTRE),
  [PN_ST_X_LABEL]      = STACK_LEAF(PN_ST_DIM_ROW, 0, 0, stack_ALIGN_CENTRE),
  [PN_ST_HEIGHT]       = STACK_LEAF(PN_ST_DIM_ROW, 0, 0, stack_ALIGN_CENTRE),

  [PN_ST_BG_ROW]       = STACK_HBOX(PN_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_START),
  [PN_ST_BG_LABEL]     = STACK_LEAF_GROUP(PN_ST_BG_ROW, 0, 0, stack_ALIGN_CENTRE, PN_G_LABEL),
  PN_ST_RADIO(0),
  PN_ST_RADIO(1),
  PN_ST_RADIO(2),
  PN_ST_RADIO(3),

  [PN_ST_BUTTONS]      = STACK_HBOX(PN_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_END),
  [PN_ST_SPACER]       = STACK_SPACER(PN_ST_BUTTONS, 1), /* push the buttons right */
  [PN_ST_CANCEL]       = STACK_LEAF(PN_ST_BUTTONS, 0, 0, stack_ALIGN_CENTRE),
  [PN_ST_CREATE]       = STACK_LEAF(PN_ST_BUTTONS, 0, 0, stack_ALIGN_CENTRE),
};

/* the stack item laying out each icon */
static const int g_paintingnew_item[PN_ICON__COUNT] =
{
  [PN_ICON_DIM_LABEL]      = PN_ST_DIM_LABEL,
  [PN_ICON_WIDTH]          = PN_ST_WIDTH,
  [PN_ICON_X_LABEL]        = PN_ST_X_LABEL,
  [PN_ICON_HEIGHT]         = PN_ST_HEIGHT,
  [PN_ICON_BG_LABEL]       = PN_ST_BG_LABEL,
  [PN_ICON_BG + 0]         = PN_ST_BG + 0,
  [PN_ICON_BG + 1]         = PN_ST_BG + 1,
  [PN_ICON_BG + 2]         = PN_ST_BG + 2,
  [PN_ICON_BG + 3]         = PN_ST_BG + 3,
  [PN_ICON_CANCEL]         = PN_ST_CANCEL,
  [PN_ICON_CREATE]         = PN_ST_CREATE
};

struct paintingnew
{
  paintingnew_t           **slot;     /* cleared on QUIT */
  wuss_t                *wuss;
  wuss_task_t           *task;
  wuss_dialogue_t       *dialogue;
  paintingnew_accept_fn_t  *accept;
  void                  *opaque;
  const char            *action;      /* the accepting button's text */
  wuss_menu_handle_t     menu_handle; /* live only while shown on its own */
  wuss_menu_handle_t     leaf_handle; /* level showing it as a menu leaf;
                                       * live only while so shown */
  wuss_icon_t           *icons[PN_ICON__COUNT];
};

/* ----------------------------------------------------------------------- */

/* the colours for a background choice: the canvas, and a pen that shows on it */
static void paintingnew_colours(int which, paintingnew_params_t *params)
{
  switch (which)
  {
  case PN_BG_BLACK:
    params->bg = colour_rgb(0x00, 0x00, 0x00);
    params->fg = colour_rgb(0xFF, 0xFF, 0xFF);
    break;

  case PN_BG_GREY:
    params->bg = colour_rgb(0xC0, 0xC0, 0xC0);
    params->fg = colour_rgb(0x00, 0x00, 0x00);
    break;

  case PN_BG_TRANSPARENT:
    params->bg = colour_rgba(0x00, 0x00, 0x00, 0x00);
    params->fg = colour_rgb(0x00, 0x00, 0x00);
    break;

  default:
    params->bg = colour_rgb(0xFF, 0xFF, 0xFF);
    params->fg = colour_rgb(0x00, 0x00, 0x00);
    break;
  }
}

/* the number in a size field, clamped to the allowed range, with the field
 * rewritten to match. Anything that is not a number reads as the minimum */
static int paintingnew_read_size(const paintingnew_t *pn, int which)
{
  wuss_icon_t *icon;
  int          size;
  char         text[PN_DIGITS + 2];

  icon = pn->icons[which];
  size = (int) strtol(wuss_icon_get_text(icon), NULL, 10);
  size = CLAMP(size, PAINTINGNEW_SIZE_MIN, PAINTINGNEW_SIZE_MAX);

  snprintf(text, sizeof(text), "%d", size);
  (void) wuss_icon_set_text(wuss_dialogue_window(pn->dialogue), icon, text);

  return size;
}

static int paintingnew_read_bg(const paintingnew_t *pn)
{
  int i;

  for (i = 0; i < PN_BG__COUNT; i++)
    if (wuss_icon_get_selected(pn->icons[PN_ICON_BG + i]))
      return i;

  return PN_BG_WHITE;
}

/* close whichever chain shows the dialogue: its own, or the one it is a leaf
 * of */
static void paintingnew_dismiss(paintingnew_t *pn)
{
  if (pn->menu_handle != NULL)
    wuss_menu_close(pn->menu_handle);
  else
    wuss_menu_dismiss(pn->leaf_handle);
  pn->menu_handle = NULL;
  pn->leaf_handle = NULL;
}

static result_t paintingnew_action_cancel(void *opaque, wuss_button_t button)
{
  paintingnew_t *pn;

  pn = opaque;

  /* ADJUST leaves the dialogue open, as on RISC OS */
  if (!(button & wuss_BUTTON_ADJUST))
    paintingnew_dismiss(pn);

  return result_OK;
}

static result_t paintingnew_action_create(void *opaque, wuss_button_t button)
{
  paintingnew_t       *pn;
  paintingnew_params_t params;

  pn = opaque;

  params.size.w = paintingnew_read_size(pn, PN_ICON_WIDTH);
  params.size.h = paintingnew_read_size(pn, PN_ICON_HEIGHT);
  paintingnew_colours(paintingnew_read_bg(pn), &params);

  if (!(button & wuss_BUTTON_ADJUST))
    paintingnew_dismiss(pn);

  return pn->accept(&params, pn->opaque);
}

/* ----------------------------------------------------------------------- */

/* solve the layout: the window size, and a box for every item */
static result_t paintingnew_layout(const wuss_t *wuss,
                                   const char   *action,
                                   size2d_t     *size,
                                   box_t         boxes[PN_ST__LIMIT])
{
  result_t     rc;
  stack_item_t items[PN_ST__LIMIT];
  int          row_h;
  int          field_w;
  int          i;
  box_t        root;

  memcpy(items, g_paintingnew_stack, sizeof(items));

  /* rows fit a field or a radio glyph, whichever is taller */
  row_h = MAX(wuss_std_height(wuss, wuss_STD_FIELD), wuss_STD_GLYPH_SIZE);
  for (i = PN_ST_DIM_ROW; i < PN_ST_BUTTONS; i++)
  {
    if (items[i].kind == stack_KIND_HBOX)
      items[i].axis_size = row_h;
    else
      items[i].cross_size = row_h;
  }
  items[PN_ST_BUTTONS].axis_size = wuss_std_height(wuss, wuss_STD_PRIMARY_BUTTON);
  items[PN_ST_CANCEL].cross_size =
    wuss_std_height(wuss, wuss_STD_SECONDARY_BUTTON);
  items[PN_ST_CREATE].cross_size =
    wuss_std_height(wuss, wuss_STD_PRIMARY_BUTTON);

  /* each label's natural width; the size group widens the others to match.
   * a field fits PN_DIGITS + 1 digits with a space either side */
  field_w = task_button_width(wuss, "99999", 0);
  items[PN_ST_DIM_LABEL].axis_size    = task_text_width(wuss, "Dimensions");
  items[PN_ST_WIDTH].axis_size        = field_w;
  items[PN_ST_X_LABEL].axis_size      = task_text_width(wuss, "x");
  items[PN_ST_HEIGHT].axis_size       = field_w;
  items[PN_ST_BG_LABEL].axis_size     = task_text_width(wuss, "Background");
  for (i = 0; i < PN_BG__COUNT; i++)
    items[PN_ST_BG + i].axis_size = wuss_STD_GLYPH_SIZE + wuss_STD_GAP +
                                    task_text_width(wuss, paintingnew_bg_names[i]);
  items[PN_ST_CANCEL].axis_size =
    task_button_width(wuss, "Cancel", wuss_STD_SECONDARY_BUTTON_BORDER);
  items[PN_ST_CREATE].axis_size =
    task_button_width(wuss, action, wuss_STD_PRIMARY_BUTTON_BORDER);

  rc = stack_smallest(items, NELEMS(items), size);
  if (rc != result_OK)
    return rc;

  root = (box_t) BOX_POS_SIZE(0, 0, size->w, size->h);

  return stack_solve(items, NELEMS(items), &root, boxes);
}

static result_t paintingnew_build_icons(paintingnew_t *pn,
                                        const box_t    boxes[PN_ST__LIMIT])
{
  result_t               rc;
  wuss_icon_spec_t       specs[PN_ICON__COUNT];
  wuss_dialogue_action_t actions[2];
  char                   width_text[PN_DIGITS + 2];
  char                   height_text[PN_DIGITS + 2];
  int                    i;

  wuss_icon_spec_label(&specs[PN_ICON_DIM_LABEL], boxes[PN_ST_DIM_LABEL],
                       "Dimensions", wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  snprintf(width_text, sizeof(width_text), "%d", PN_DEFAULT_W);
  wuss_icon_spec_writable(&specs[PN_ICON_WIDTH], boxes[PN_ST_WIDTH],
                          width_text, sizeof(width_text),
                          wuss_ICON_FLAGS_JUSTIFY_CENTRE);

  wuss_icon_spec_label(&specs[PN_ICON_X_LABEL], boxes[PN_ST_X_LABEL],
                       "x", wuss_ICON_FLAGS_JUSTIFY_CENTRE);
  snprintf(height_text, sizeof(height_text), "%d", PN_DEFAULT_H);
  wuss_icon_spec_writable(&specs[PN_ICON_HEIGHT], boxes[PN_ST_HEIGHT],
                          height_text, sizeof(height_text),
                          wuss_ICON_FLAGS_JUSTIFY_CENTRE);

  wuss_icon_spec_label(&specs[PN_ICON_BG_LABEL], boxes[PN_ST_BG_LABEL],
                       "Background", wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  for (i = 0; i < PN_BG__COUNT; i++)
    wuss_icon_spec_radio(&specs[PN_ICON_BG + i], boxes[PN_ST_BG + i],
                         paintingnew_bg_names[i], 1);

  wuss_icon_spec_action(&specs[PN_ICON_CANCEL], boxes[PN_ST_CANCEL],
                        "Cancel", 0);
  wuss_icon_spec_action(&specs[PN_ICON_CREATE], boxes[PN_ST_CREATE],
                        pn->action, 1);

  rc = wuss_icon_create_array(wuss_dialogue_window(pn->dialogue), specs,
                              PN_ICON__COUNT, pn->icons);
  if (rc != result_OK)
    return rc;

  wuss_icon_set_selected(wuss_dialogue_window(pn->dialogue),
                         pn->icons[PN_ICON_BG + PN_BG_WHITE], 1);

  actions[0].icon = pn->icons[PN_ICON_CANCEL];
  actions[0].fn   = paintingnew_action_cancel;
  actions[1].icon = pn->icons[PN_ICON_CREATE];
  actions[1].fn   = paintingnew_action_create;

  wuss_dialogue_set_cancel(pn->dialogue, pn->icons[PN_ICON_CANCEL]);

  return wuss_dialogue_set_actions(pn->dialogue, actions, 2);
}

static result_t paintingnew_relayout(paintingnew_t *pn)
{
  result_t       rc;
  size2d_t       size;
  box_t          boxes[PN_ST__LIMIT];
  wuss_window_t *window;
  int            i;

  rc = paintingnew_layout(pn->wuss, pn->action, &size, boxes);
  if (rc != result_OK)
    return rc;

  window = wuss_dialogue_window(pn->dialogue);

  rc = task_fit_window(window, size);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < PN_ICON__COUNT; i++)
    wuss_icon_set_bbox(window, pn->icons[i], &boxes[g_paintingnew_item[i]]);

  return result_OK;
}

/* the dialogue's own task: not autoclose, so it survives its window being
 * dismissed and shown again. Its QUIT, while the window is still alive,
 * frees everything */
static result_t paintingnew_handle(wuss_window_t      *window,
                                   const wuss_event_t *event,
                                   void               *task_data)
{
  paintingnew_t *pn;
  result_t       rc;

  pn = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_ICON:
    rc = result_OK;
    (void) wuss_dialogue_handle_icon(pn->dialogue, event, &rc);
    return rc;

  case wuss_EVENT_KEY:
    rc = result_WUSS_KEY_UNCLAIMED;
    (void) wuss_dialogue_handle_key(pn->dialogue, event, &rc);
    return rc;

  case wuss_EVENT_CLOSE:
    wuss_dialogue_handle_close(pn->dialogue, window);
    return result_OK;

  case wuss_EVENT_MENU_CLOSED:
    pn->menu_handle = NULL; /* wuss closed the chain under us */
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    if (event->data.pre_show.handle == NULL)
      return result_OK; /* shown by paintingnew_open */
    pn->leaf_handle = event->data.pre_show.handle;
    return wuss_menu_open_window_now(event->data.pre_show.handle,
                                     event->data.pre_show.index);

  case wuss_EVENT_SHOW:
    /* not on PRE_SHOW: a still-hidden window cannot take the focus */
    (void) wuss_icon_set_caret(window, pn->icons[PN_ICON_WIDTH], -1);
    return result_OK;

  case wuss_EVENT_RELAYOUT:
    return paintingnew_relayout(pn);

  case wuss_EVENT_QUIT:
    wuss_dialogue_destroy(pn->dialogue);
    if (pn->slot != NULL)
      *pn->slot = NULL;
    free(pn);
    return result_OK;

  default:
    return result_OK;
  }
}

result_t paintingnew_create(paintingnew_t          **slot,
                            wuss_t                  *wuss,
                            const char              *title,
                            const char              *action,
                            paintingnew_accept_fn_t *accept,
                            void                    *opaque)
{
  result_t         rc;
  paintingnew_t   *pn;
  wuss_task_desc_t desc;
  size2d_t         size;
  box_t            boxes[PN_ST__LIMIT];

  pn = calloc(1, sizeof(*pn));
  if (pn == NULL)
    return result_OOM;

  pn->slot   = slot;
  pn->wuss   = wuss;
  pn->accept = accept;
  pn->opaque = opaque;
  pn->action = action;

  desc.handle    = paintingnew_handle;
  desc.task_data = pn;
  desc.name      = "Painting-New";
  rc = wuss_task_create(wuss, &desc, &pn->task);
  if (rc != result_OK)
  {
    free(pn); /* nothing registered yet */
    return rc;
  }

  rc = paintingnew_layout(wuss, action, &size, boxes);
  if (rc == result_OK)
    rc = wuss_dialogue_create(&pn->dialogue, pn->task, size, title, NULL, pn);
  if (rc == result_OK)
    rc = paintingnew_build_icons(pn, boxes);
  if (rc != result_OK)
  {
    wuss_task_destroy(pn->task); /* its QUIT frees pn */
    return rc;
  }

  *slot = pn;

  return result_OK;
}

result_t paintingnew_open(paintingnew_t *pn)
{
  result_t       rc;
  wuss_window_t *win;

  win = wuss_dialogue_window(pn->dialogue);

  /* where an icon bar menu would open, as it is opened from the icon bar */
#ifdef WUSS_ICONBAR
  rc = wuss_iconbar_window_open(pn->task, win, &pn->menu_handle);
#else
  /* unreachable: taskapp only offers this dialogue from an icon bar menu */
  rc = wuss_menu_open_window(pn->task, win, POINT(0, 0), &pn->menu_handle);
#endif
  if (rc != result_OK)
    return rc;

  (void) wuss_icon_set_caret(win, pn->icons[PN_ICON_WIDTH], -1);

  return result_OK;
}

void paintingnew_destroy(paintingnew_t *pn)
{
  if (pn == NULL)
    return;

  /* the slot's owner may be freed before the task's QUIT arrives, as when
   * wuss itself shuts down, so clear it now */
  *pn->slot = NULL;
  pn->slot  = NULL;

  paintingnew_dismiss(pn);
  wuss_task_destroy(pn->task); /* its QUIT frees pn */
}

void paintingnew_set_size(paintingnew_t *pn, size2d_t size)
{
  wuss_window_t *win;
  char           text[PN_DIGITS + 2];

  win = wuss_dialogue_window(pn->dialogue);

  snprintf(text, sizeof(text), "%d", size.w);
  (void) wuss_icon_set_text(win, pn->icons[PN_ICON_WIDTH], text);
  snprintf(text, sizeof(text), "%d", size.h);
  (void) wuss_icon_set_text(win, pn->icons[PN_ICON_HEIGHT], text);
}

wuss_window_t *paintingnew_window(const paintingnew_t *pn)
{
  return wuss_dialogue_window(pn->dialogue);
}

#endif /* WUSS_APP */
