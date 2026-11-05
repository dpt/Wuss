/* wuss/tasks/common/confdlg.c -- a task's slider configuration dialogue */

#ifdef WUSS_APP

#include <stdio.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "geom/box.h"
#include "geom/size.h"
#include "geom/stack.h"
#include "wuss/icon.h"
#include "wuss/menu.h"

#include "common.h"
#include "confdlg.h"

#define ST_G_LABEL      1 /* size groups, see stack_item_t::group */
#define ST_G_VALUE      2
#define ST_SLIDER_MIN_W 64
#define ST_PER_ROW      4 /* row, label, slider and value */
#define ICONS_PER_ROW   3 /* label, slider and value */
#define NBUTTONS        3
#define ST_MAX          (1 + CONFDLG_MAX_ROWS * ST_PER_ROW + 3 + NBUTTONS)
#define ICONS_MAX       (CONFDLG_MAX_ROWS * ICONS_PER_ROW + NBUTTONS)

static int *confdlg_field(confdlg_t *conf, int row)
{
  return (int *) ((char *) conf->config + conf->desc->rows[row].offset);
}

/* resync the sliders to the config; called on every reveal */
static result_t confdlg_fillout(void *opaque)
{
  result_t   rc;
  confdlg_t *conf;
  int        row;

  conf = opaque;

  rc = result_OK;
  for (row = 0; row < conf->desc->nrows; row++)
    rc = wuss_slider_row_set(wuss_dialogue_window(conf->dialogue),
                             &conf->rows[row], *confdlg_field(conf, row));

  return rc;
}

static void confdlg_dismiss(confdlg_t *conf)
{
  wuss_menu_close(conf->instance->menu_handle);
  conf->instance->menu_handle = NULL;
}

/* Select dismisses the menu chain; Adjust resets the sliders instead */
static result_t confdlg_cancel(void *opaque, wuss_button_t button)
{
  confdlg_t *conf;

  conf = opaque;

  if (button & wuss_BUTTON_SELECT)
    confdlg_dismiss(conf);
  else if (button & wuss_BUTTON_ADJUST)
    return confdlg_fillout(conf);

  return result_OK;
}

/* Select applies and dismisses; Adjust applies and leaves the dialogue open.
 * old is the value rows[0] held before the change. */
static result_t confdlg_commit(confdlg_t    *conf,
                               wuss_button_t button,
                               int           old)
{
  int row;

  if (button & wuss_BUTTON_SELECT)
    confdlg_dismiss(conf);
  else if (!(button & wuss_BUTTON_ADJUST))
    return result_OK;

  for (row = 0; row < conf->desc->nrows; row++)
    *confdlg_field(conf, row) = wuss_icon_get_value(conf->rows[row].slider);

  return conf->desc->applied(conf->instance, old);
}

static result_t confdlg_apply(void *opaque, wuss_button_t button)
{
  confdlg_t *conf;

  conf = opaque;

  return confdlg_commit(conf, button, *confdlg_field(conf, 0));
}

static result_t confdlg_default(void *opaque, wuss_button_t button)
{
  result_t   rc;
  confdlg_t *conf;
  int        old;

  conf = opaque;

  old = *confdlg_field(conf, 0);
  memcpy(conf->config, conf->desc->defaults, conf->desc->config_size);
  rc = confdlg_fillout(conf);
  if (rc != result_OK)
    return rc;

  return confdlg_commit(conf, button, old);
}

result_t confdlg_create(confdlg_t            *conf,
                        taskapp_instance_t   *instance,
                        const confdlg_desc_t *desc,
                        void                 *config)
{
  result_t               rc;
  stack_item_t           items[ST_MAX];
  box_t                  boxes[ST_MAX];
  wuss_icon_spec_t       specs[ICONS_MAX];
  wuss_icon_t           *made[ICONS_MAX];
  char                   bufs[CONFDLG_MAX_ROWS][WUSS_SLIDER_ROW_BUF];
  char                   text[WUSS_SLIDER_ROW_BUF];
  wuss_dialogue_action_t actions[NBUTTONS];
  wuss_t                *wuss;
  const confdlg_row_t   *r;
  size2d_t               min_sz;
  box_t                  root;
  int                    row;
  int                    st;
  int                    icon;
  int                    btns;
  int                    bbox;
  int                    value_w;
  int                    button_w;

  if (desc->nrows > CONFDLG_MAX_ROWS)
    return result_BAD_ARG;

  conf->instance = instance;
  conf->desc     = desc;
  conf->config   = config;
  wuss           = instance->wuss;

  items[0] = (stack_item_t) { .kind = stack_KIND_VBOX, .parent = -1,
                              .gap = wuss_STD_GAP, .pad = wuss_STD_INSETS };

  for (row = 0; row < desc->nrows; row++)
  {
    r  = &desc->rows[row];
    st = 1 + row * ST_PER_ROW;

    snprintf(text, sizeof(text), "%d", r->max);
    value_w = task_text_width(wuss, text);
    snprintf(text, sizeof(text), "%d", r->min);
    value_w = MAX(value_w, task_text_width(wuss, text));

    items[st]     = (stack_item_t) STACK_HBOX(0, wuss_STD_SLIDER_HEIGHT, wuss_STD_GAP, stack_ALIGN_START);
    items[st + 1] = (stack_item_t) STACK_LEAF_GROUP(st, task_text_width(wuss, r->label), 16, stack_ALIGN_CENTRE, ST_G_LABEL);
    items[st + 2] = (stack_item_t) STACK_LEAF_EX(st, 0, wuss_STD_SLIDER_HEIGHT, stack_ALIGN_CENTRE, 1, ST_SLIDER_MIN_W, 0);
    items[st + 3] = (stack_item_t) STACK_LEAF_GROUP(st, value_w, 16, stack_ALIGN_CENTRE, ST_G_VALUE);
  }

  /* a spacer in the label group puts the buttons under the sliders */
  button_w = task_button_width(wuss, "Default", wuss_STD_SECONDARY_BUTTON_BORDER);
  btns     = 1 + desc->nrows * ST_PER_ROW;
  bbox     = btns + 2;

  items[btns]     = (stack_item_t) STACK_HBOX(0, wuss_STD_SECONDARY_BUTTON_HEIGHT, wuss_STD_GAP, stack_ALIGN_START);
  items[btns + 1] = (stack_item_t) STACK_SPACER_GROUP(btns, 0, ST_G_LABEL);
  items[bbox]     = (stack_item_t) { .kind = stack_KIND_HBOX, .parent = btns,
                                     .gap = wuss_STD_GAP, .flex = 1 };
  for (st = bbox + 1; st <= bbox + NBUTTONS; st++)
    items[st] = (stack_item_t) STACK_LEAF_EX(bbox, 0, wuss_STD_SECONDARY_BUTTON_HEIGHT, stack_ALIGN_CENTRE, 1, button_w, 0);

  rc = stack_smallest(items, bbox + NBUTTONS + 1, &min_sz);
  if (rc != result_OK)
    return rc;

  rc = wuss_dialogue_create(&conf->dialogue, instance->delegate, min_sz,
                            "Configuration", confdlg_fillout, conf);
  if (rc != result_OK)
    return rc;

  wuss_dialogue_set_focusable(conf->dialogue, 0); /* no writables */

  root = (box_t) BOX_POS_SIZE(0, 0, min_sz.w, min_sz.h);
  rc = stack_solve(items, bbox + NBUTTONS + 1, &root, boxes);
  if (rc != result_OK)
    goto exit;

  for (row = 0; row < desc->nrows; row++)
  {
    r    = &desc->rows[row];
    st   = 1 + row * ST_PER_ROW;
    icon = row * ICONS_PER_ROW;

    wuss_icon_spec_label(&specs[icon], boxes[st + 1], r->label,
                         wuss_ICON_FLAGS_JUSTIFY_RIGHT);
    wuss_icon_spec_slider_row(&specs[icon + 1], &specs[icon + 2],
                              boxes[st + 2], boxes[st + 3],
                              wuss_SLIDER_HORIZONTAL,
                              r->min, r->max, *confdlg_field(conf, row), NULL,
                              r->step, bufs[row], sizeof(bufs[row]));
  }

  icon = desc->nrows * ICONS_PER_ROW;
  wuss_icon_spec_action(&specs[icon], boxes[bbox + 1], "Default", 0);
  wuss_icon_spec_action(&specs[icon + 1], boxes[bbox + 2], "Cancel", 0);
  wuss_icon_spec_action(&specs[icon + 2], boxes[bbox + 3], "Apply", 0);

  rc = wuss_icon_create_array(wuss_dialogue_window(conf->dialogue), specs,
                              icon + NBUTTONS, made);
  if (rc != result_OK)
    goto exit;

  for (row = 0; row < desc->nrows; row++)
    wuss_slider_row_bind(&conf->rows[row], made[row * ICONS_PER_ROW + 1],
                         made[row * ICONS_PER_ROW + 2], NULL,
                         desc->rows[row].min, desc->rows[row].max,
                         desc->rows[row].step);

  actions[0].icon = made[icon];
  actions[0].fn   = confdlg_default;
  actions[1].icon = made[icon + 1];
  actions[1].fn   = confdlg_cancel;
  actions[2].icon = made[icon + 2];
  actions[2].fn   = confdlg_apply;

  rc = wuss_dialogue_set_actions(conf->dialogue, actions, NBUTTONS);
  if (rc != result_OK)
    goto exit;

  return result_OK;

exit:
  wuss_dialogue_destroy(conf->dialogue);
  conf->dialogue = NULL;
  return rc;
}

result_t confdlg_event(confdlg_t          *conf,
                       wuss_window_t      *window,
                       const wuss_event_t *event)
{
  result_t rc;
  int      row;

  switch (event->kind)
  {
  case wuss_EVENT_ICON:
    if (window != wuss_dialogue_window(conf->dialogue))
      return result_OK;

    for (row = 0; row < conf->desc->nrows; row++)
      if (wuss_slider_row_event(window, &conf->rows[row], event, NULL))
        return result_OK;

    if (wuss_dialogue_handle_icon(conf->dialogue, event, &rc))
      return rc;

    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    if (window == wuss_dialogue_window(conf->dialogue))
    {
      rc = wuss_dialogue_handle_pre_show(conf->dialogue);
      if (rc != result_OK)
        return rc;
    }

    if (event->data.pre_show.handle == NULL)
      return result_OK;

    return wuss_menu_open_window_now(event->data.pre_show.handle,
                                     event->data.pre_show.index);

  case wuss_EVENT_CLOSE:
    wuss_dialogue_handle_close(conf->dialogue, window);
    return result_OK;

  case wuss_EVENT_QUIT:
    wuss_dialogue_destroy(conf->dialogue);
    return result_OK;

  default:
    return result_OK;
  }
}

#endif /* WUSS_APP */
