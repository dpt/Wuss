/* wuss/component/scaleview.c -- Scale view dialogue */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "geom/box.h"
#include "geom/stack.h"

#include "wuss/icon.h"
#include "wuss/icon-spec.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "wuss/component/dialogue.h"
#include "wuss/component/scaleview.h"

#include "../core/types.h"
#include "../font/font.h"
#include "../icon.h"

/* ----------------------------------------------------------------------- */

/* icons, in creation order */
enum
{
  SV_ICON_LABEL,
  SV_ICON_FIELD,
  SV_ICON_DOWN,
  SV_ICON_UP,
  SV_ICON_PERCENT,
  SV_ICON_P25,
  SV_ICON_P50,
  SV_ICON_P100,
  SV_ICON_P200,
  SV_ICON_FIT_SCREEN,
  SV_ICON_FIT_WINDOW,
  SV_ICON_CANCEL,
  SV_ICON_SCALE,
  SV_ICON__COUNT
};

/* layout items: a VBOX of four rows -- the field and its adjusters, the
 * presets, the fits, then Cancel and Scale pushed apart. Sizes are filled in
 * from the font. Buttons in a row share a size group so they line up. See
 * docs/windowing/wuss-layout.md */
enum
{
  SV_ST_ROOT,
  SV_ST_ROW_FIELD,
  SV_ST_LABEL,
  SV_ST_FIELD,
  SV_ST_DOWN,
  SV_ST_UP,
  SV_ST_PERCENT,
  SV_ST_ROW_PRESETS,
  SV_ST_P25,
  SV_ST_P50,
  SV_ST_P100,
  SV_ST_P200,
  SV_ST_ROW_FITS,
  SV_ST_FIT_SCREEN,
  SV_ST_FIT_WINDOW,
  SV_ST_ROW_BUTTONS,
  SV_ST_CANCEL,
  SV_ST_SPACER,
  SV_ST_SCALE,
  SV_ST__LIMIT
};

enum
{
  SV_GROUP_PRESETS = 1,
  SV_GROUP_FITS
};

static const stack_item_t sv_layout[SV_ST__LIMIT] =
{
  [SV_ST_ROOT]        = { .kind = stack_KIND_VBOX, .parent = -1,
                          .gap = wuss_STD_GAP, .pad = wuss_STD_INSETS },
  [SV_ST_ROW_FIELD]   = STACK_HBOX(SV_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_END),
  [SV_ST_LABEL]       = STACK_LEAF(SV_ST_ROW_FIELD, 0, 0, stack_ALIGN_CENTRE),
  [SV_ST_FIELD]       = STACK_LEAF(SV_ST_ROW_FIELD, 0, 0, stack_ALIGN_CENTRE),
  [SV_ST_DOWN]        = STACK_LEAF(SV_ST_ROW_FIELD, 0, 0, stack_ALIGN_CENTRE),
  [SV_ST_UP]          = STACK_LEAF(SV_ST_ROW_FIELD, 0, 0, stack_ALIGN_CENTRE),
  [SV_ST_PERCENT]     = STACK_LEAF(SV_ST_ROW_FIELD, 0, 0, stack_ALIGN_CENTRE),
  [SV_ST_ROW_PRESETS] = STACK_HBOX(SV_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_END),
  [SV_ST_P25]         = STACK_LEAF_GROUP(SV_ST_ROW_PRESETS, 0, 0, stack_ALIGN_CENTRE, SV_GROUP_PRESETS),
  [SV_ST_P50]         = STACK_LEAF_GROUP(SV_ST_ROW_PRESETS, 0, 0, stack_ALIGN_CENTRE, SV_GROUP_PRESETS),
  [SV_ST_P100]        = STACK_LEAF_GROUP(SV_ST_ROW_PRESETS, 0, 0, stack_ALIGN_CENTRE, SV_GROUP_PRESETS),
  [SV_ST_P200]        = STACK_LEAF_GROUP(SV_ST_ROW_PRESETS, 0, 0, stack_ALIGN_CENTRE, SV_GROUP_PRESETS),
  [SV_ST_ROW_FITS]    = STACK_HBOX(SV_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_END),
  [SV_ST_FIT_SCREEN]  = STACK_LEAF_GROUP(SV_ST_ROW_FITS, 0, 0, stack_ALIGN_CENTRE, SV_GROUP_FITS),
  [SV_ST_FIT_WINDOW]  = STACK_LEAF_GROUP(SV_ST_ROW_FITS, 0, 0, stack_ALIGN_CENTRE, SV_GROUP_FITS),
  [SV_ST_ROW_BUTTONS] = STACK_HBOX(SV_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_END),
  [SV_ST_CANCEL]      = STACK_LEAF(SV_ST_ROW_BUTTONS, 0, 0, stack_ALIGN_CENTRE),
  [SV_ST_SPACER]      = STACK_SPACER(SV_ST_ROW_BUTTONS, 1), /* push Scale right */
  [SV_ST_SCALE]       = STACK_LEAF(SV_ST_ROW_BUTTONS, 0, 0, stack_ALIGN_CENTRE)
};

/* the preset buttons, in icon order from SV_ICON_P25 */
static const struct
{
  const char *text;
  int         percent;
}
sv_presets[] =
{
  { "25%",  25  },
  { "50%",  50  },
  { "100%", 100 },
  { "200%", 200 }
};

#define SV_FIELD_SIZE 4 /* bytes: "999" and a terminator */

struct wuss_scaleview
{
  alloc_t                     alloc;       /* copied hooks; wuss_t itself not retained */
  wuss_t                     *wuss;
  wuss_task_t                *task;
  wuss_dialogue_t            *dialogue;
  wuss_scaleview_choice_fn_t *fn;
  void                       *opaque;
  wuss_menu_handle_t          menu_handle; /* live only while shown */
  wuss_menu_handle_t          leaf_handle; /* level showing the leaf; live
                                            * only while shown, and wuss
                                            * tells the menu's task, not us,
                                            * when it closes */
  wuss_icon_t               *icons[SV_ICON__COUNT];
  int                         nicons;      /* live entries in icons */
};

/* ----------------------------------------------------------------------- */

static int sv_get_percent(const wuss_scaleview_t *sv)
{
  long percent;

  percent = strtol(wuss_icon_get_text(sv->icons[SV_ICON_FIELD]), NULL, 10);

  return (int) CLAMP(percent, wuss_SCALEVIEW_MIN, wuss_SCALEVIEW_MAX);
}

result_t wuss_scaleview_set_percent(wuss_scaleview_t *sv, int percent)
{
  char text[SV_FIELD_SIZE];

  percent = CLAMP(percent, wuss_SCALEVIEW_MIN, wuss_SCALEVIEW_MAX);
  snprintf(text, sizeof(text), "%d", percent);

  return wuss_icon_set_text(wuss_dialogue_window(sv->dialogue),
                            sv->icons[SV_ICON_FIELD], text);
}

/* close whichever chain shows the dialogue: its own, or the one it is a leaf
 * of, telling that chain's owner so it drops its handle */
static void sv_dismiss(wuss_scaleview_t *sv)
{
  if (sv->menu_handle != NULL)
    wuss_menu_close(sv->menu_handle);
  else
    wuss_menu_dismiss(sv->leaf_handle);
  sv->menu_handle = NULL;
  sv->leaf_handle = NULL;
}

/* dismiss the dialogue and report the choice. The callback comes last: it
 * may destroy or reopen the dialogue */
static result_t sv_choose(wuss_scaleview_t       *sv,
                          wuss_scaleview_choice_t choice,
                          int                     percent)
{
  sv_dismiss(sv);
  sv->fn(choice, percent, sv->opaque);

  return result_OK;
}

static result_t sv_action_cancel(void *opaque, wuss_button_t button)
{
  NOT_USED(button);

  sv_dismiss(opaque);

  return result_OK;
}

static result_t sv_action_scale(void *opaque, wuss_button_t button)
{
  NOT_USED(button);

  return sv_choose(opaque, wuss_SCALEVIEW_SCALE, sv_get_percent(opaque));
}

static result_t sv_action_fit_screen(void *opaque, wuss_button_t button)
{
  NOT_USED(button);

  return sv_choose(opaque, wuss_SCALEVIEW_FIT_SCREEN, 0);
}

static result_t sv_action_fit_window(void *opaque, wuss_button_t button)
{
  NOT_USED(button);

  return sv_choose(opaque, wuss_SCALEVIEW_FIT_WINDOW, 0);
}

/* ----------------------------------------------------------------------- */

/* fill spec as an adjuster arrow from the icon set, or a text button where
 * there is no icon set */
static void sv_arrow_spec(const wuss_t     *wuss,
                          box_t             bbox,
                          int               up,
                          wuss_icon_spec_t *spec)
{
  const char *name;

  name = up ? "gup" : "gdown";
  if (wuss_iconpool_lookup(wuss, name) < 0)
  {
    wuss_icon_spec_action(spec, bbox, up ? "+" : "-", 0);
    return;
  }

  memset(spec, 0, sizeof(*spec));
  spec->bbox                  = bbox;
  spec->type                  = wuss_ICON_TYPE_BITMAP;
  spec->flags                 = wuss_ICON_FLAGS_INTERACTIVE;
  spec->u.bitmap.name         = name;
  spec->u.bitmap.pressed_name = up ? "gup-p" : "gdown-p";
}

/* solve the layout: the window size, and a box for every item */
static result_t sv_solve(const wuss_t *wuss,
                         size2d_t     *size,
                         box_t         boxes[SV_ST__LIMIT])
{
  result_t        rc;
  stack_item_t    items[SV_ST__LIMIT];
  bmfont_t       *font;
  const bitmap_t *bm;
  int             field, primary, secondary;
  int             glyph;
  int             i;
  box_t           root;

  memcpy(items, sv_layout, sizeof(items));

  font      = wuss_get_font(wuss);
  bm        = wuss_iconpool_bitmap(wuss, wuss_iconpool_lookup(wuss, "gup"));
  field     = wuss_std_height(wuss, wuss_STD_FIELD);
  primary   = wuss_std_height(wuss, wuss_STD_PRIMARY_BUTTON);
  secondary = wuss_std_height(wuss, wuss_STD_SECONDARY_BUTTON);
  glyph     = bm ? bm->size.h : field;

  items[SV_ST_ROW_FIELD].axis_size  = MAX(field, glyph);
  items[SV_ST_LABEL].axis_size      =
    (int) wuss__text_width(font, "Scale") + 2 * WUSS_LABEL_TEXT_PAD;
  items[SV_ST_LABEL].cross_size     = field;
  items[SV_ST_FIELD].axis_size      =
    (int) wuss__text_width(font, "0000") + 4 * WUSS_LABEL_TEXT_PAD;
  items[SV_ST_FIELD].cross_size     = field;
  items[SV_ST_DOWN].axis_size       = glyph;
  items[SV_ST_DOWN].cross_size      = glyph;
  items[SV_ST_UP].axis_size         = glyph;
  items[SV_ST_UP].cross_size        = glyph;
  items[SV_ST_PERCENT].axis_size    =
    (int) wuss__text_width(font, "%") + 2 * WUSS_LABEL_TEXT_PAD;
  items[SV_ST_PERCENT].cross_size   = field;

  items[SV_ST_ROW_PRESETS].axis_size = secondary;
  for (i = 0; i < (int) NELEMS(sv_presets); i++)
  {
    items[SV_ST_P25 + i].axis_size  =
      wuss__button_width(wuss, sv_presets[i].text,
                         wuss_STD_SECONDARY_BUTTON_BORDER);
    items[SV_ST_P25 + i].cross_size = secondary;
  }

  items[SV_ST_ROW_FITS].axis_size    = secondary;
  items[SV_ST_FIT_SCREEN].axis_size  =
    wuss__button_width(wuss, "Fit screen", wuss_STD_SECONDARY_BUTTON_BORDER);
  items[SV_ST_FIT_SCREEN].cross_size = secondary;
  items[SV_ST_FIT_WINDOW].axis_size  =
    wuss__button_width(wuss, "Fit window", wuss_STD_SECONDARY_BUTTON_BORDER);
  items[SV_ST_FIT_WINDOW].cross_size = secondary;

  items[SV_ST_ROW_BUTTONS].axis_size = primary;
  items[SV_ST_CANCEL].axis_size      =
    wuss__button_width(wuss, "Cancel", wuss_STD_SECONDARY_BUTTON_BORDER);
  items[SV_ST_CANCEL].cross_size     = secondary;
  items[SV_ST_SCALE].axis_size       =
    wuss__button_width(wuss, "Scale", wuss_STD_PRIMARY_BUTTON_BORDER);
  items[SV_ST_SCALE].cross_size      = primary;

  rc = stack_smallest(items, SV_ST__LIMIT, size);
  if (rc != result_OK)
    return rc;

  root = (box_t) BOX_POS_SIZE(0, 0, size->w, size->h);

  return stack_solve(items, SV_ST__LIMIT, &root, boxes);
}

/* make the icons for boxes with text in the field, replacing any already on
 * the window */
static result_t sv_build_icons(wuss_scaleview_t *sv,
                               const char       *text,
                               const box_t       boxes[SV_ST__LIMIT])
{
  result_t               rc;
  wuss_icon_spec_t       specs[SV_ICON__COUNT];
  wuss_icon_t           *icons[SV_ICON__COUNT];
  int                    i;
  wuss_dialogue_action_t actions[4];

  wuss_icon_spec_label(&specs[SV_ICON_LABEL], boxes[SV_ST_LABEL], "Scale", 0);
  wuss_icon_spec_writable(&specs[SV_ICON_FIELD], boxes[SV_ST_FIELD], text,
                          SV_FIELD_SIZE, 0);
  sv_arrow_spec(sv->wuss, boxes[SV_ST_DOWN], 0, &specs[SV_ICON_DOWN]);
  sv_arrow_spec(sv->wuss, boxes[SV_ST_UP], 1, &specs[SV_ICON_UP]);
  wuss_icon_spec_label(&specs[SV_ICON_PERCENT], boxes[SV_ST_PERCENT], "%", 0);
  for (i = 0; i < (int) NELEMS(sv_presets); i++)
    wuss_icon_spec_action(&specs[SV_ICON_P25 + i], boxes[SV_ST_P25 + i],
                          sv_presets[i].text, 0);
  wuss_icon_spec_action(&specs[SV_ICON_FIT_SCREEN], boxes[SV_ST_FIT_SCREEN],
                        "Fit screen", 0);
  wuss_icon_spec_action(&specs[SV_ICON_FIT_WINDOW], boxes[SV_ST_FIT_WINDOW],
                        "Fit window", 0);
  wuss_icon_spec_action(&specs[SV_ICON_CANCEL], boxes[SV_ST_CANCEL], "Cancel",
                        0);
  wuss_icon_spec_action(&specs[SV_ICON_SCALE], boxes[SV_ST_SCALE], "Scale", 1);

  rc = wuss_icon_create_array(wuss_dialogue_window(sv->dialogue), specs,
                              SV_ICON__COUNT, icons);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < sv->nicons; i++)
    wuss_icon_delete(wuss_dialogue_window(sv->dialogue), sv->icons[i]);
  memcpy(sv->icons, icons, sizeof(icons));
  sv->nicons = SV_ICON__COUNT;

  actions[0].icon = icons[SV_ICON_FIT_SCREEN];
  actions[0].fn   = sv_action_fit_screen;
  actions[1].icon = icons[SV_ICON_FIT_WINDOW];
  actions[1].fn   = sv_action_fit_window;
  actions[2].icon = icons[SV_ICON_CANCEL];
  actions[2].fn   = sv_action_cancel;
  actions[3].icon = icons[SV_ICON_SCALE];
  actions[3].fn   = sv_action_scale;

  wuss_dialogue_set_cancel(sv->dialogue, icons[SV_ICON_CANCEL]);

  return wuss_dialogue_set_actions(sv->dialogue, actions, 4);
}

/* a click on an adjuster or a preset: change the field. Returns non-zero if
 * the event was one */
static int sv_handle_field_icon(wuss_scaleview_t   *sv,
                                const wuss_event_t *event,
                                result_t           *rc)
{
  wuss_icon_t *icon;
  int          step;
  int          i;

  if (event->data.icon.action != wuss_MOUSE_UP ||
      !(event->data.icon.button & WUSS_BUTTON_CLICK))
    return 0;

  icon = event->data.icon.icon;

  if (icon == sv->icons[SV_ICON_DOWN] || icon == sv->icons[SV_ICON_UP])
  {
    /* ADJUST goes the other way, as in RISC OS */
    step = (icon == sv->icons[SV_ICON_UP]) ? wuss_SCALEVIEW_STEP
                                           : -wuss_SCALEVIEW_STEP;
    if (event->data.icon.button & wuss_BUTTON_ADJUST)
      step = -step;
    *rc = wuss_scaleview_set_percent(sv, sv_get_percent(sv) + step);
    return 1;
  }

  for (i = 0; i < (int) NELEMS(sv_presets); i++)
  {
    if (icon == sv->icons[SV_ICON_P25 + i])
    {
      *rc = wuss_scaleview_set_percent(sv, sv_presets[i].percent);
      return 1;
    }
  }

  return 0;
}

/* The dialogue's own task: not autoclose, so it survives its window being
 * dismissed and shown again; wuss_scaleview_destroy frees it. */
static result_t sv_task_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data)
{
  result_t          rc;
  wuss_scaleview_t *sv;
  size2d_t          size;
  box_t             boxes[SV_ST__LIMIT];

  NOT_USED(window);

  sv = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_ICON:
    rc = result_OK;
    if (!sv_handle_field_icon(sv, event, &rc))
      (void) wuss_dialogue_handle_icon(sv->dialogue, event, &rc);
    return rc;

  case wuss_EVENT_KEY:
    rc = result_WUSS_KEY_UNCLAIMED;
    (void) wuss_dialogue_handle_key(sv->dialogue, event, &rc);
    return rc;

  case wuss_EVENT_PRE_CLOSE:
    return wuss_dialogue_handle_pre_close(sv->dialogue);

  case wuss_EVENT_MENU_CLOSED:
    sv->menu_handle = NULL; /* wuss closed the chain under us */
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    if (event->data.pre_show.handle == NULL)
      return result_OK; /* shown by wuss_scaleview_open */
    sv->leaf_handle = event->data.pre_show.handle;
    rc = wuss_menu_open_window_now(event->data.pre_show.handle,
                                   event->data.pre_show.index);
    if (rc != result_OK)
      return rc;
    (void) wuss_icon_set_caret(wuss_dialogue_window(sv->dialogue),
                               sv->icons[SV_ICON_FIELD], -1);
    return result_OK;

  case wuss_EVENT_RELAYOUT:
    /* the font may have changed: lay out again around the same field text,
     * which the new writable copies before the old one goes */
    rc = sv_solve(sv->wuss, &size, boxes);
    if (rc != result_OK)
      return rc;
    rc = wuss_window_resize(wuss_dialogue_window(sv->dialogue), size);
    if (rc != result_OK)
      return rc;
    rc = wuss_window_set_doc(wuss_dialogue_window(sv->dialogue), size);
    if (rc != result_OK)
      return rc;
    return sv_build_icons(sv, wuss_icon_get_text(sv->icons[SV_ICON_FIELD]),
                          boxes);

  default:
    return result_OK;
  }
}

result_t wuss_scaleview_create(wuss_scaleview_t          **out,
                               wuss_t                     *wuss,
                               wuss_scaleview_choice_fn_t *fn,
                               void                       *opaque)
{
  result_t          rc;
  wuss_scaleview_t *sv;
  size2d_t          size;
  box_t             boxes[SV_ST__LIMIT];
  wuss_task_desc_t  desc;

  if (out == NULL || wuss == NULL || fn == NULL)
    return result_NULL_ARG;

  sv = wuss->alloc.malloc(sizeof(*sv));
  if (sv == NULL)
    return result_OOM;

  sv->alloc       = wuss->alloc;
  sv->wuss        = wuss;
  sv->task        = NULL;
  sv->dialogue    = NULL;
  sv->fn          = fn;
  sv->opaque      = opaque;
  sv->menu_handle = NULL;
  sv->leaf_handle = NULL;
  sv->nicons      = 0;

  rc = sv_solve(wuss, &size, boxes);
  if (rc != result_OK)
    goto failure;

  desc.handle    = sv_task_handle;
  desc.task_data = sv;
  desc.name      = "ScaleView";
  rc = wuss_task_create(wuss, &desc, &sv->task);
  if (rc != result_OK)
    goto failure;

  rc = wuss_dialogue_create(&sv->dialogue, sv->task, size, "Scale view",
                            NULL, sv);
  if (rc != result_OK)
    goto failure;

  rc = sv_build_icons(sv, "100", boxes);
  if (rc != result_OK)
    goto failure;

  *out = sv;
  return result_OK;

failure:
  wuss_scaleview_destroy(sv);
  return rc;
}

void wuss_scaleview_destroy(wuss_scaleview_t *doomed)
{
  alloc_t alloc;

  if (doomed == NULL)
    return;

  alloc = doomed->alloc;

  wuss_menu_close(doomed->menu_handle);
  wuss_dialogue_destroy(doomed->dialogue); /* frees the window and its icons */
  wuss_task_destroy(doomed->task);
  alloc.free(doomed);
}

/* ----------------------------------------------------------------------- */

result_t wuss_scaleview_open(wuss_scaleview_t *sv, int percent)
{
  result_t       rc;
  wuss_window_t *win;
  box_t          content;
  point_t        at;

  rc = wuss_scaleview_set_percent(sv, percent);
  if (rc != result_OK)
    return rc;

  sv->leaf_handle = NULL;

  win = wuss_dialogue_window(sv->dialogue);
  wuss_window_get_content_bounds(win, &content);

  /* centred on the pointer, where the click that prompted it was */
  at    = wuss_get_pointer(sv->wuss);
  at.x -= (content.x1 - content.x0) / 2;
  at.y -= (content.y1 - content.y0) / 2;

  rc = wuss_menu_open_window(sv->task, win, at, &sv->menu_handle);
  if (rc != result_OK)
    return rc;

  /* ready to type over: the caret at the end of the value */
  (void) wuss_icon_set_caret(win, sv->icons[SV_ICON_FIELD], -1);

  return result_OK;
}

wuss_window_t *wuss_scaleview_window(const wuss_scaleview_t *sv)
{
  return wuss_dialogue_window(sv->dialogue);
}
