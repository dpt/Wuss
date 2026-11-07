/* wuss/component/colourpicker.c -- true-colour colour picker dialogue */

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/colourspace.h"
#include "geom/box.h"

#include "wuss/gadget/swatches.h"
#include "wuss/icon.h"
#include "wuss/icon-spec.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "wuss/component/colourpicker.h"
#include "wuss/component/dialogue.h"

#include "../core/types.h"
#include "../font/font.h"
#include "../icon.h"

/* ----------------------------------------------------------------------- */

#define CP_NMODELS   5
#define CP_NCHANNELS 4   /* most channels any model has (CMYK) */
#define CP_SLIDER_W  160
#define CP_SWATCH_W  48
#define CP_HEX_SIZE  8   /* "#RRGGBB" and terminator */

enum
{
  CP_RGB,
  CP_CMYK,
  CP_HSL,
  CP_HSV,
  CP_OKLAB
};

/* one slider: its label, its integer range and the factor from the
 * colourspace channel to the slider's units */
typedef struct cp_channel
{
  const char *label;
  int         min, max;
  double      scale;
}
cp_channel_t;

typedef struct cp_model
{
  const char  *name;
  int          nchannels;
  cp_channel_t channels[CP_NCHANNELS];
}
cp_model_t;

static const cp_model_t cp_models[CP_NMODELS] =
{
  [CP_RGB]   = { "RGB",   3, { { "Red",        0, 255, 255.0 },
                               { "Green",      0, 255, 255.0 },
                               { "Blue",       0, 255, 255.0 } } },
  [CP_CMYK]  = { "CMYK",  4, { { "Cyan",       0, 100, 100.0 },
                               { "Magenta",    0, 100, 100.0 },
                               { "Yellow",     0, 100, 100.0 },
                               { "Black",      0, 100, 100.0 } } },
  [CP_HSL]   = { "HSL",   3, { { "Hue",        0, 359,   1.0 },
                               { "Saturation", 0, 100, 100.0 },
                               { "Lightness",  0, 100, 100.0 } } },
  [CP_HSV]   = { "HSV",   3, { { "Hue",        0, 359,   1.0 },
                               { "Saturation", 0, 100, 100.0 },
                               { "Value",      0, 100, 100.0 } } },
  [CP_OKLAB] = { "OKLab", 3, { { "Lightness",  0, 100, 100.0 },
                               { "a",        -40,  40, 100.0 },
                               { "b",        -40,  40, 100.0 } } }
};

struct wuss_colourpicker
{
  alloc_t                         alloc;  /* copied hooks */
  wuss_t                         *wuss;
  wuss_task_t                    *task;
  wuss_window_t                  *window; /* ours, not the dialogue's */
  wuss_dialogue_t                *dialogue;
  wuss_colourpicker_changed_fn_t *fn;
  void                           *opaque;
  int                             model;  /* CP_RGB etc., kept across opens */
  double                          v[CP_NCHANNELS]; /* the colour, in model */
  colour_t                        old;
  colour_t                        colour; /* v, clamped and quantised */
  wuss_icon_t                    *radios[CP_NMODELS];
  wuss_icon_t                    *labels[CP_NMODELS][CP_NCHANNELS];
  wuss_slider_row_t               rows[CP_NMODELS][CP_NCHANNELS];
  wuss_icon_t                    *hex;
  wuss_icon_t                    *cancel;
  wuss_icon_t                    *ok;
  colour_t                        shown[2]; /* the swatches: old, new */
  wuss_swatches_t                 swatches;
};

enum { CP_SWATCH_OLD, CP_SWATCH_NEW };

/* ----------------------------------------------------------------------- */

/* every box in the window, for the current system font. Every model's rows
 * share the same boxes */
typedef struct
{
  size2d_t size;
  box_t    radios[CP_NMODELS];
  box_t    labels[CP_NCHANNELS];
  box_t    sliders[CP_NCHANNELS];
  box_t    values[CP_NCHANNELS];
  box_t    swatches;
  box_t    hex;
  box_t    cancel;
  box_t    ok;
}
cp_boxes_t;

/* pixel width of text in the desktop font */
static int cp_text_width(const wuss_t *wuss, const char *text)
{
  return (int) wuss__text_width(wuss_get_font(wuss), text);
}

/* ----------------------------------------------------------------------- */

/* the colour v in the given model, as unclamped sRGB */
static void cp_to_rgb(int model, const double v[], colourspace_rgb_t *rgb)
{
  colourspace_cmyk_t  cmyk;
  colourspace_hsl_t   hsl;
  colourspace_hsv_t   hsv;
  colourspace_oklab_t oklab;

  switch (model)
  {
  case CP_RGB:
    rgb->r = v[0];
    rgb->g = v[1];
    rgb->b = v[2];
    break;

  case CP_CMYK:
    cmyk.c = v[0];
    cmyk.m = v[1];
    cmyk.y = v[2];
    cmyk.k = v[3];
    colourspace_cmyk_to_rgb(&cmyk, rgb);
    break;

  case CP_HSL:
    hsl.h = v[0];
    hsl.s = v[1];
    hsl.l = v[2];
    colourspace_hsl_to_rgb(&hsl, rgb);
    break;

  case CP_HSV:
    hsv.h = v[0];
    hsv.s = v[1];
    hsv.v = v[2];
    colourspace_hsv_to_rgb(&hsv, rgb);
    break;

  default:
    oklab.l = v[0];
    oklab.a = v[1];
    oklab.b = v[2];
    colourspace_oklab_to_rgb(&oklab, rgb);
    break;
  }
}

static void cp_from_rgb(int model, const colourspace_rgb_t *rgb, double v[])
{
  colourspace_cmyk_t  cmyk;
  colourspace_hsl_t   hsl;
  colourspace_hsv_t   hsv;
  colourspace_oklab_t oklab;

  v[3] = 0.0;

  switch (model)
  {
  case CP_RGB:
    v[0] = rgb->r;
    v[1] = rgb->g;
    v[2] = rgb->b;
    break;

  case CP_CMYK:
    colourspace_rgb_to_cmyk(rgb, &cmyk);
    v[0] = cmyk.c;
    v[1] = cmyk.m;
    v[2] = cmyk.y;
    v[3] = cmyk.k;
    break;

  case CP_HSL:
    colourspace_rgb_to_hsl(rgb, &hsl);
    v[0] = hsl.h;
    v[1] = hsl.s;
    v[2] = hsl.l;
    break;

  case CP_HSV:
    colourspace_rgb_to_hsv(rgb, &hsv);
    v[0] = hsv.h;
    v[1] = hsv.s;
    v[2] = hsv.v;
    break;

  default:
    colourspace_rgb_to_oklab(rgb, &oklab);
    v[0] = oklab.l;
    v[1] = oklab.a;
    v[2] = oklab.b;
    break;
  }
}

/* ----------------------------------------------------------------------- */

static wuss_window_t *cp_window(const wuss_colourpicker_t *picker)
{
  return picker->window;
}

/* set the current model's sliders to v */
static void cp_sync_sliders(wuss_colourpicker_t *picker)
{
  const cp_model_t *model;
  int               i;

  model = &cp_models[picker->model];
  for (i = 0; i < model->nchannels; i++)
    (void) wuss_slider_row_set(cp_window(picker),
                               &picker->rows[picker->model][i],
                               (int) lround(picker->v[i] *
                                            model->channels[i].scale));
}

/* show only the current model's rows, and tick its radio */
static void cp_sync_model(wuss_colourpicker_t *picker)
{
  wuss_window_t *win;
  int            m, i, hidden;

  win = cp_window(picker);

  for (m = 0; m < CP_NMODELS; m++)
  {
    hidden = m != picker->model;
    wuss_icon_set_selected(win, picker->radios[m], !hidden);
    for (i = 0; i < cp_models[m].nchannels; i++)
    {
      wuss_icon_set_hidden(win, picker->labels[m][i], hidden);
      wuss_icon_set_hidden(win, picker->rows[m][i].slider, hidden);
      wuss_icon_set_hidden(win, picker->rows[m][i].value, hidden);
    }
  }
}

/* swatches are painted by the swatches gadget, so on a paletted screen a
 * colour the palette lacks shows as an ordered mix of two entries */
static void cp_sync_swatch(wuss_colourpicker_t *picker,
                           int                  which,
                           colour_t             colour)
{
  box_t box;

  picker->shown[which] = colour;
  box = picker->swatches.bbox;
  box.x0 += which * CP_SWATCH_W;
  box.x1  = box.x0 + CP_SWATCH_W;
  wuss_window_invalidate(cp_window(picker), &box);
}

static void cp_sync_hex(wuss_colourpicker_t *picker)
{
  char         hex[CP_HEX_SIZE];
  unsigned int r, g, b;

  colour_get_rgb(&picker->colour, &r, &g, &b);
  snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
  (void) wuss_icon_set_text(cp_window(picker), picker->hex, hex);
}

/* refresh everything from v */
static void cp_sync_all(wuss_colourpicker_t *picker)
{
  cp_sync_model(picker);
  cp_sync_sliders(picker);
  cp_sync_swatch(picker, CP_SWATCH_OLD, picker->old);
  cp_sync_swatch(picker, CP_SWATCH_NEW, picker->colour);
  cp_sync_hex(picker);
}

/* set v (in the current model) and the colour from an 8-bit colour */
static void cp_set_colour(wuss_colourpicker_t *picker, colour_t colour)
{
  colourspace_rgb_t rgb;

  colourspace_from_colour(colour, &rgb);
  cp_from_rgb(picker->model, &rgb, picker->v);
  picker->colour = colourspace_to_colour(&rgb);
}

/* v has changed: requantise, repaint and report if the colour moved. The
 * callback comes last: it may destroy the picker */
static void cp_changed(wuss_colourpicker_t *picker, int from_hex)
{
  colourspace_rgb_t rgb;
  colour_t          colour;

  cp_to_rgb(picker->model, picker->v, &rgb);
  colour = colourspace_to_colour(&rgb);
  if (colour.primary == picker->colour.primary)
    return;

  picker->colour = colour;
  cp_sync_swatch(picker, CP_SWATCH_NEW, colour);
  if (!from_hex)
    cp_sync_hex(picker); /* leave the user's typing alone */

  picker->fn(colour, picker->opaque);
}

/* ----------------------------------------------------------------------- */

static result_t cp_action_ok(void *opaque, wuss_button_t button)
{
  wuss_colourpicker_t *picker;

  picker = opaque;

  if (button & wuss_BUTTON_ADJUST)
  {
    picker->old = picker->colour;
    cp_sync_swatch(picker, CP_SWATCH_OLD, picker->old);
    return result_OK;
  }

  return wuss_dialogue_hide(picker->dialogue);
}

static result_t cp_action_cancel(void *opaque, wuss_button_t button)
{
  result_t             rc;
  wuss_colourpicker_t *picker;

  picker = opaque;

  cp_set_colour(picker, picker->old);
  cp_sync_all(picker);

  rc = result_OK;
  if (!(button & wuss_BUTTON_ADJUST))
    rc = wuss_dialogue_hide(picker->dialogue);

  picker->fn(picker->old, picker->opaque);

  return rc;
}

/* ----------------------------------------------------------------------- */

static result_t cp_icon(wuss_colourpicker_t *picker,
                        const wuss_event_t  *event)
{
  result_t          rc;
  wuss_icon_t      *icon;
  const cp_model_t *model;
  int               i, value;
  colourspace_rgb_t rgb;
  colour_t          colour;

  icon  = event->data.icon.icon;
  model = &cp_models[picker->model];

  for (i = 0; i < model->nchannels; i++)
  {
    if (!wuss_slider_row_event(cp_window(picker),
                               &picker->rows[picker->model][i], event,
                               &value))
      continue;

    picker->v[i] = value / model->channels[i].scale;
    cp_changed(picker, 0);

    return result_OK;
  }

  if (event->data.icon.action != wuss_MOUSE_UP)
    return result_OK;

  for (i = 0; i < CP_NMODELS; i++)
  {
    if (icon != picker->radios[i])
      continue;

    if (i != picker->model)
    {
      /* through unclamped RGB, so nothing is lost to quantising */
      cp_to_rgb(picker->model, picker->v, &rgb);
      picker->model = i;
      cp_from_rgb(picker->model, &rgb, picker->v);
    }
    cp_sync_model(picker); /* reticks the radio even if unchanged */
    cp_sync_sliders(picker);

    return result_OK;
  }

  if (icon == picker->hex)
  {
    if (event->data.icon.button != wuss_BUTTON_NONE)
      return result_OK; /* a click placing the caret, not an edit */

    if (colourspace_parse_hex(wuss_icon_get_text(icon), &colour) != result_OK)
      return result_OK; /* incomplete: wait for more */

    colourspace_from_colour(colour, &rgb);
    cp_from_rgb(picker->model, &rgb, picker->v);
    cp_sync_sliders(picker);
    cp_changed(picker, 1);

    return result_OK;
  }

  rc = result_OK;
  (void) wuss_dialogue_handle_icon(picker->dialogue, event, &rc);

  return rc;
}

static void cp_layout(const wuss_t *wuss, cp_boxes_t *b);

/* re-lay out for a new system font: move every icon and refit the window */
static result_t cp_relayout(wuss_colourpicker_t *picker)
{
  result_t       rc;
  cp_boxes_t     b;
  wuss_window_t *win;
  int            m, i;

  cp_layout(picker->wuss, &b);
  win = cp_window(picker);

  rc = wuss_window_set_min_size(win, b.size);
  if (rc != result_OK)
    return rc;

  rc = wuss_window_set_doc(win, b.size);
  if (rc != result_OK)
    return rc;

  rc = wuss_window_resize(win, b.size);
  if (rc != result_OK)
    return rc;

  for (m = 0; m < CP_NMODELS; m++)
  {
    wuss_icon_set_bbox(win, picker->radios[m], &b.radios[m]);
    for (i = 0; i < cp_models[m].nchannels; i++)
    {
      wuss_icon_set_bbox(win, picker->labels[m][i], &b.labels[i]);
      wuss_icon_set_bbox(win, picker->rows[m][i].slider, &b.sliders[i]);
      wuss_icon_set_bbox(win, picker->rows[m][i].value, &b.values[i]);
    }
  }
  wuss_icon_set_bbox(win, picker->hex, &b.hex);
  wuss_icon_set_bbox(win, picker->cancel, &b.cancel);
  wuss_icon_set_bbox(win, picker->ok, &b.ok);
  picker->swatches.bbox = b.swatches;

  return result_OK;
}

/* The picker's own task: not autoclose, so it survives its window being
 * hidden and shown again; wuss_colourpicker_destroy frees it. */
static result_t cp_task_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data)
{
  wuss_colourpicker_t *picker;
  result_t             rc;

  NOT_USED(window);

  picker = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    wuss_swatches_draw(&picker->swatches, event);
    return result_OK;

  case wuss_EVENT_ICON:
    return cp_icon(picker, event);

  case wuss_EVENT_KEY:
    rc = result_WUSS_KEY_UNCLAIMED;
    (void) wuss_dialogue_handle_key(picker->dialogue, event, &rc);
    return rc;

  case wuss_EVENT_PRE_CLOSE:
    return wuss_dialogue_handle_pre_close(picker->dialogue);

  case wuss_EVENT_RELAYOUT:
    return cp_relayout(picker);

  default:
    return result_OK;
  }
}

/* ----------------------------------------------------------------------- */

/* Lay out every box for the current system font. From the top: the model
 * radios, up to four slider rows, the swatches with the hex field, then
 * Cancel and OK. */
static void cp_layout(const wuss_t *wuss, cp_boxes_t *b)
{
  int  radio_h, slider_h, field_h, primary, secondary;
  int  label_w, value_w, radio_w, radios_w;
  int  m, i, x, y;
  char digit[2];
  int  cancel_w, ok_w, width;

  radio_h   = MAX(wuss_std_height(wuss, wuss_STD_FIELD), wuss_STD_GLYPH_SIZE);
  slider_h  = wuss_std_height(wuss, wuss_STD_SLIDER);
  field_h   = wuss_std_height(wuss, wuss_STD_FIELD);
  primary   = wuss_std_height(wuss, wuss_STD_PRIMARY_BUTTON);
  secondary = wuss_std_height(wuss, wuss_STD_SECONDARY_BUTTON);

  /* measure */
  label_w = 0;
  for (m = 0; m < CP_NMODELS; m++)
    for (i = 0; i < cp_models[m].nchannels; i++)
      label_w = MAX(label_w, cp_text_width(wuss,
                                           cp_models[m].channels[i].label));

  /* room for a sign and three of the widest digit: a hidden row's text
   * must not spill past its box, or hiding it leaves the spill behind */
  value_w  = 0;
  digit[1] = '\0';
  for (digit[0] = '0'; digit[0] <= '9'; digit[0]++)
    value_w = MAX(value_w, cp_text_width(wuss, digit));
  value_w = cp_text_width(wuss, "-") + 3 * value_w;

  /* model radios */
  x = wuss_STD_INSET;
  y = wuss_STD_INSET;
  for (m = 0; m < CP_NMODELS; m++)
  {
    radio_w = wuss_STD_GLYPH_SIZE + wuss_STD_GAP +
              cp_text_width(wuss, cp_models[m].name);
    b->radios[m] = (box_t) BOX_POS_SIZE(x, y, radio_w, radio_h);
    x += radio_w + wuss_STD_GAP;
  }
  radios_w = x - wuss_STD_GAP - wuss_STD_INSET;

  cancel_w = wuss__button_width(wuss, "Cancel",
                                wuss_STD_SECONDARY_BUTTON_BORDER);
  ok_w     = wuss__button_width(wuss, "OK", wuss_STD_PRIMARY_BUTTON_BORDER);
  ok_w     = MAX(ok_w, cancel_w);

  width  = label_w + CP_SLIDER_W + value_w + 2 * wuss_STD_GAP;
  width  = MAX(width, radios_w);
  width += 2 * wuss_STD_INSET;

  /* slider rows: label, slider, value echo */
  y += radio_h + wuss_STD_GAP;
  for (i = 0; i < CP_NCHANNELS; i++)
  {
    x = wuss_STD_INSET;
    b->labels[i]  = (box_t) BOX_POS_SIZE(x, y, label_w, slider_h);
    x += label_w + wuss_STD_GAP;
    b->sliders[i] = (box_t) BOX_POS_SIZE(x, y, CP_SLIDER_W, slider_h);
    x += CP_SLIDER_W + wuss_STD_GAP;
    b->values[i]  = (box_t) BOX_POS_SIZE(x, y, value_w, slider_h);
    y += slider_h + wuss_STD_GAP;
  }

  /* swatches, old then new, as tall as OK, and the hex field */
  x = wuss_STD_INSET;
  b->swatches = (box_t) BOX_POS_SIZE(x, y, 2 * CP_SWATCH_W, primary);
  x += 2 * CP_SWATCH_W + wuss_STD_GAP;
  b->hex = (box_t) BOX_POS_SIZE(x, y + (primary - field_h) / 2,
                                width - wuss_STD_INSET - x, field_h);

  /* Cancel and OK, at the right */
  y += primary + wuss_STD_GAP;
  x  = width - wuss_STD_INSET - ok_w;
  b->cancel = (box_t) BOX_POS_SIZE(x - wuss_STD_GAP - cancel_w,
                                   y + (primary - secondary) / 2,
                                   cancel_w, secondary);
  b->ok     = (box_t) BOX_POS_SIZE(x, y, ok_w, primary);

  b->size = SIZE2D(width, y + primary + wuss_STD_INSET);
}

/* Create every icon. Every model's rows share the same boxes; all but the
 * current model's are hidden. */
static result_t cp_build(wuss_colourpicker_t *picker, const char *title)
{
  enum
  {
    NRADIOS = CP_NMODELS,
    NROWS   = CP_NMODELS * CP_NCHANNELS,
    NICONS  = NRADIOS + 3 * NROWS + 3 /* hex, Cancel, OK */
  };

  result_t               rc;
  wuss_t                *wuss;
  cp_boxes_t             b;
  box_t                  content;
  int                    m, i, n;
  wuss_icon_spec_t       specs[NICONS];
  wuss_icon_t           *made[NICONS];
  char                   bufs[NROWS][WUSS_SLIDER_ROW_BUF];
  const cp_channel_t    *ch;
  wuss_dialogue_action_t actions[2];

  wuss = picker->wuss;

  cp_layout(wuss, &b);

  /* the window is made here, not by wuss_dialogue_create, since that one
   * skips the task's redraw and the swatches are painted from it */
  content = (box_t) BOX_POS_SIZE(0, 0, b.size.w, b.size.h);
  rc = wuss_window_create(picker->task,
                          &content,
                          title != NULL ? title : "Colour",
                          wuss_WINDOW_HIDDEN | wuss_WINDOW_FOCUSABLE,
                          wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                          b.size,
                          b.size,
                          &picker->window);
  if (rc != result_OK)
    return rc;

  rc = wuss_dialogue_create_on_window(&picker->dialogue, wuss, picker->window,
                                      NULL, picker);
  if (rc != result_OK)
    return rc;

  n = 0;
  for (m = 0; m < CP_NMODELS; m++)
    wuss_icon_spec_radio(&specs[n++], b.radios[m], cp_models[m].name, 1);

  for (m = 0; m < CP_NMODELS; m++)
  {
    for (i = 0; i < cp_models[m].nchannels; i++)
    {
      ch = &cp_models[m].channels[i];
      wuss_icon_spec_label(&specs[n++], b.labels[i], ch->label,
                           wuss_ICON_FLAGS_JUSTIFY_RIGHT);
      wuss_icon_spec_slider_row(&specs[n], &specs[n + 1], b.sliders[i],
                                b.values[i], wuss_SLIDER_HORIZONTAL, ch->min,
                                ch->max, ch->min, NULL, 0,
                                bufs[m * CP_NCHANNELS + i],
                                WUSS_SLIDER_ROW_BUF);
      n += 2;
    }
  }

  picker->swatches.bbox     = b.swatches;
  picker->swatches.columns  = 2;
  picker->swatches.gap      = 0;
  picker->swatches.outline  = 0;
  picker->swatches.colours  = picker->shown;
  picker->swatches.ncolours = 2;
  picker->swatches.selected = -1;

  wuss_icon_spec_writable(&specs[n++], b.hex, "", CP_HEX_SIZE, 0);
  wuss_icon_spec_action(&specs[n++], b.cancel, "Cancel", 0);
  wuss_icon_spec_action(&specs[n++], b.ok, "OK", 1);

  rc = wuss_icon_create_array(cp_window(picker), specs, n, made);
  if (rc != result_OK)
    return rc;

  /* pick the made icons back out, in the order specified */
  n = 0;
  for (m = 0; m < CP_NMODELS; m++)
    picker->radios[m] = made[n++];
  for (m = 0; m < CP_NMODELS; m++)
  {
    for (i = 0; i < cp_models[m].nchannels; i++)
    {
      ch = &cp_models[m].channels[i];
      picker->labels[m][i] = made[n];
      wuss_slider_row_bind(&picker->rows[m][i], made[n + 1], made[n + 2],
                           NULL, ch->min, ch->max, 0);
      n += 3;
    }
  }
  picker->hex    = made[n++];
  picker->cancel = made[n++];
  picker->ok     = made[n++];

  actions[0].icon = picker->cancel;
  actions[0].fn   = cp_action_cancel;
  actions[1].icon = picker->ok;
  actions[1].fn   = cp_action_ok;

  wuss_dialogue_set_cancel(picker->dialogue, actions[0].icon);

  return wuss_dialogue_set_actions(picker->dialogue, actions,
                                   NELEMS(actions));
}

/* ----------------------------------------------------------------------- */

result_t wuss_colourpicker_create(wuss_colourpicker_t           **out,
                                  wuss_t                         *wuss,
                                  const char                     *title,
                                  wuss_colourpicker_changed_fn_t *fn,
                                  void                           *opaque)
{
  result_t             rc;
  wuss_colourpicker_t *picker;
  wuss_task_desc_t     desc;

  if (out == NULL || wuss == NULL || fn == NULL)
    return result_NULL_ARG;

  picker = wuss->alloc.malloc(sizeof(*picker));
  if (picker == NULL)
    return result_OOM;

  memset(picker, 0, sizeof(*picker));
  picker->alloc  = wuss->alloc;
  picker->wuss   = wuss;
  picker->fn     = fn;
  picker->opaque = opaque;
  picker->model  = CP_RGB;
  picker->old    = colour_rgb(0, 0, 0);
  picker->colour = picker->old;

  desc.handle    = cp_task_handle;
  desc.task_data = picker;
  desc.name      = "ColourPicker";
  rc = wuss_task_create(wuss, &desc, &picker->task);
  if (rc != result_OK)
    goto failure;

  rc = cp_build(picker, title);
  if (rc != result_OK)
    goto failure;

  cp_sync_all(picker);

  *out = picker;
  return result_OK;

failure:
  wuss_colourpicker_destroy(picker);
  return rc;
}

void wuss_colourpicker_destroy(wuss_colourpicker_t *doomed)
{
  alloc_t alloc;

  if (doomed == NULL)
    return;

  alloc = doomed->alloc;

  wuss_dialogue_destroy(doomed->dialogue);
  wuss_window_close(doomed->window); /* and its icons */
  wuss_task_destroy(doomed->task);
  alloc.free(doomed);
}

/* ----------------------------------------------------------------------- */

result_t wuss_colourpicker_open(wuss_colourpicker_t *picker, colour_t colour)
{
  result_t       rc;
  wuss_window_t *win;
  box_t          outer;
  box_t          content;
  size2d_t       size;
  size2d_t       scr;
  point_t        at;

  cp_set_colour(picker, colour);
  picker->old = picker->colour;
  cp_sync_all(picker);

  win = cp_window(picker);

  if (win->flags & wuss_WINDOW_HIDDEN)
  {
    /* centred on the pointer, where the click that prompted it was, and
     * kept on screen, title bar and all */
    wuss_window_get_visible_bounds(win, &outer);
    wuss_window_get_content_bounds(win, &content);
    size = box_size(&outer);
    scr  = wuss_get_screen_size(picker->wuss);
    at   = wuss_get_pointer(picker->wuss);
    at.x = CLAMP(at.x - size.w / 2, 0, MAX(scr.w - size.w, 0));
    at.y = CLAMP(at.y - size.h / 2, 0, MAX(scr.h - size.h, 0));
    wuss_window_move(win, POINT(at.x + content.x0 - outer.x0,
                                at.y + content.y0 - outer.y0));

    rc = wuss_window_set_hidden(win, 0);
    if (rc != result_OK)
      return rc;
  }

  wuss_window_restack(win, wuss_ZORDER_FRONT);

  return wuss_set_focus(picker->wuss, win);
}

colour_t wuss_colourpicker_get_colour(const wuss_colourpicker_t *picker)
{
  return picker->colour;
}
