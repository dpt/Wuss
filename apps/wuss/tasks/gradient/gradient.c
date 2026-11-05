/* wuss/tasks/gradient/gradient.c -- gradient fill task */

#ifdef WUSS_APP

#include <math.h>
#include <stdlib.h>
#include <time.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include <stdio.h>
#include <string.h>

#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/colourspace.h"
#include "framebuf/pattern.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "utils/rng.h"
#include "wuss/component/colourmenu.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* fill shapes offered on the "Shape" menu */
typedef enum gradient_shape
{
  gradient_SHAPE_LINEAR,  /* the original two-axis RGB ramp */
  gradient_SHAPE_RADIAL,  /* hue by distance from the document centre */
  gradient_SHAPE_CONICAL, /* hue by angle about the document centre */
  gradient_SHAPE_TWO_COLOUR, /* colour a to b, as palette dither patterns */
  gradient_NSHAPES
}
gradient_shape_t;

/* fills its whole content area with a two-axis colour gradient; opens
 * small (100x100) against a large (400x400) document, so scrollbars
 * appear and the fill can be scrolled around. SELECT/ADJUST clicks cycle
 * the ordered-dither matrix forward/backward through 2x2, 4x4 and 8x8. An
 * ADJUST drag sets brightness (vertical, up is brighter) and saturation
 * (horizontal, right is more saturated). Menu > Shape swaps the two-axis
 * ramp for a radial or conical hue sweep, or a left-to-right blend between
 * two colours. That blend is drawn in GRADIENT_STEP-pixel columns, each an
 * ordered-dither pattern from pattern_from_colour against the system
 * palette, so the dither matrix does not apply to it. Its colours start
 * random; choosing one from Menu > Colours or picking Menu > Randomise (^R)
 * switches to that shape. Menu > Reset restores the shape, dither,
 * brightness and saturation and makes the two colours black and white;
 * Menu > Save opens a Save As dialogue (drag its icon onto a Filer window,
 * or Save with a full path already typed) that writes the window's content
 * out. Once a click has given the window the input focus, S steps the shape,
 * D steps the dither matrix and R resets. */
typedef struct gradient_task
{
  taskapp_instance_t  base;
  wuss_menu_item_t    menu_items[5]; /* per-instance: a shared static would
                                       * leak one instance's .window pointer
                                       * into another's menu */
  wuss_menu_item_t    shape_items[gradient_NSHAPES];
  wuss_menu_t         shape_menu;
  wuss_colourlist_t    colours;        /* Colour A/Colour B submenu */
  rng_t                rng;            /* Randomise */
  colour_t             a;              /* two colour blend's left edge */
  colour_t             b;              /* two colour blend's right edge */
  gradient_shape_t     shape;
  int                  dither_index;
  int                  brightness;     /* 0..512, 256 is unchanged */
  int                  saturation;     /* 0..512, 256 is unchanged */
  int                  dragging;       /* non-zero while an Adjust drag is live */
  int                  dragged;        /* non-zero once that drag has moved */
  int                  drag_x, drag_y; /* last drag point, content space */
}
gradient_task_t;

/* MENU click pops this menu; the item table and wuss_menu_t live
 * per-instance in gradient_task_t, not as a file-scope static */
enum
{
  GRADIENT_MENU_SAVE,
  GRADIENT_MENU_SHAPE,
  GRADIENT_MENU_COLOURS,
  GRADIENT_MENU_RANDOMISE,
  GRADIENT_MENU_RESET
};

/* "Shape" submenu rows, indexed by gradient_shape_t */
static const char *const gradient_shape_names[gradient_NSHAPES] =
{
  "Linear", "Radial", "Conical", "Two colour"
};

/* Colours submenu rows: the a and b fields, in that order */
static const char *const gradient_colour_names[] = { "Colour A", "Colour B" };

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GRADIENT_DOC_WIDTH  400
#define GRADIENT_DOC_HEIGHT 400

#define GRADIENT_DEFAULT_DITHER   1 /* 4x4, matching the original */
#define GRADIENT_UNITY          256 /* brightness/saturation of 1.0 */
#define GRADIENT_ADJUST_MAX     512
#define GRADIENT_DRAG_SCALE       2 /* adjust units per pixel dragged */
#define GRADIENT_STEP             4 /* two colour pixels per column */

/* ordered (Bayer) dither matrix sizes, as log2 of the side: 2x2, 4x4, 8x8,
 * then 0 for dithering off (plain truncation to the target format).
 * SELECT/ADJUST clicks cycle task->dither_index forward/backward through
 * this table. */
static const int gradient_dithers[] = { 1, 2, 3, 0 };

/* offset "v" by the dither cell for (x, y), spread across one "step" of the
 * target channel (-step/2 .. +step/2) whatever the matrix size, so a larger
 * matrix just gives a finer pattern rather than a louder one. A step of 1
 * (an 8-bit channel) adds nothing and only clamps. */
static int dither(int index, int step, int v, int x, int y)
{
  int log2n, m;

  log2n = gradient_dithers[index];
  if (log2n == 0)
    return CLAMP(v, 0, 255);

  m = pattern_bayer_threshold_n(x, y, log2n);

  /* m * step is never negative, so a shift divides by the matrix's cells */
  return CLAMP(v + ((m * step) >> (2 * log2n)) - step / 2, 0, 255);
}

/* the gap between adjacent representable 8-bit values of each R, G, B
 * channel in "fmt" */
static void channel_steps(pixelfmt_t fmt, int step[3])
{
  int i;

  for (i = 0; i < 3; i++)
  {
    step[i] = pixelfmt_channel_step(fmt, i);
    /* ponytail: paletted gaps depend on the palette; keep the original
     * fixed 16 (+/-8) swing rather than analysing it */
    if (step[i] == 0)
      step[i] = 16;
  }
}

/* the unadjusted colour of document point (lx, ly) under "shape" */
static void shade(gradient_shape_t shape, int lx, int ly, int rgb[3])
{
  static const double max_r = GRADIENT_DOC_WIDTH / 2 * 1.4142136;

  int               dx, dy;
  double            t;
  colourspace_hsv_t hsv;
  colourspace_rgb_t c;

  dx = lx - GRADIENT_DOC_WIDTH  / 2;
  dy = ly - GRADIENT_DOC_HEIGHT / 2;

  switch (shape)
  {
  default:
  case gradient_SHAPE_LINEAR:
    rgb[0] = lx * 255 / GRADIENT_DOC_WIDTH;
    rgb[1] = ly * 255 / GRADIENT_DOC_HEIGHT;
    rgb[2] = 255 - (lx + ly) * 255 / (GRADIENT_DOC_WIDTH + GRADIENT_DOC_HEIGHT);
    return;

  case gradient_SHAPE_RADIAL:
    /* 0 at the centre, 1 at a corner */
    t = sqrt((double) (dx * dx + dy * dy)) / max_r;
    break;

  case gradient_SHAPE_CONICAL:
    t = (atan2(dy, dx) + M_PI) / (2.0 * M_PI); /* 0..1 round the centre */
    break;
  }

  /* fully saturated, round the colour wheel from red back to red */
  hsv.h = CLAMP(t, 0.0, 1.0) * 360.0;
  hsv.s = 1.0;
  hsv.v = 1.0;
  colourspace_hsv_to_rgb(&hsv, &c);
  rgb[0] = (int) (c.r * 255.0 + 0.5);
  rgb[1] = (int) (c.g * 255.0 + 0.5);
  rgb[2] = (int) (c.b * 255.0 + 0.5);
}

/* apply the task's saturation (lerp away from luma) then brightness (scale)
 * to an 8-bit RGB triple, in place; results may exceed 0..255 until
 * dither() clamps them */
static void adjust(const gradient_task_t *gc, int rgb[3])
{
  int luma, i;

  luma = (rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) >> 8;

  for (i = 0; i < 3; i++)
  {
    rgb[i] = luma + (rgb[i] - luma) * gc->saturation / GRADIENT_UNITY;
    rgb[i] = rgb[i] * gc->brightness / GRADIENT_UNITY;
  }
}

/* channel c0 -> c1, f of n steps into the blend */
static int gradient_lerp(unsigned int c0, unsigned int c1, int f, int n)
{
  return (int) (c0 * (n - f) + c1 * f) / n;
}

/* a dither of the adjusted colour f of n steps of the way from a to b */
static pattern_t gradient_step_pattern(gradient_task_t *gc, int f, int n)
{
  unsigned int    ar, ag, ab;
  unsigned int    br, bg, bb;
  int             rgb[3];
  colour_t        mix;
  const colour_t *palette;
  int             npalette;

  colour_get_rgb(&gc->a, &ar, &ag, &ab);
  colour_get_rgb(&gc->b, &br, &bg, &bb);

  rgb[0] = gradient_lerp(ar, br, f, n);
  rgb[1] = gradient_lerp(ag, bg, f, n);
  rgb[2] = gradient_lerp(ab, bb, f, n);
  adjust(gc, rgb);

  mix = colour_rgb(CLAMP(rgb[0], 0, 255),
                   CLAMP(rgb[1], 0, 255),
                   CLAMP(rgb[2], 0, 255));

  palette = wuss_get_palette(gc->base.wuss, &npalette);

  return pattern_from_colour(palette, npalette, mix);
}

/* the two colour blend spans the work area, anchored at its (0,0) in screen
 * space so it scrolls with the content and every dirty rectangle shares one
 * dither phase */
static void gradient_redraw_two_colour(gradient_task_t    *gc,
                                       const wuss_event_t *event)
{
  const box_t *content;
  const box_t *bounds;
  int          ox;
  int          nsteps;
  int          s;
  int          s0;
  int          s1;
  pattern_t    pat;
  box_t        col;
  box_t        clip;

  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;

  ox     = bounds->x0 - event->data.redraw.scroll.x;
  nsteps = DIVIDE_ROUNDING_UP(bounds->x1 - bounds->x0, GRADIENT_STEP);

  s0 = MAX(0, (content->x0 - ox) / GRADIENT_STEP);
  s1 = MIN(nsteps - 1, (content->x1 - 1 - ox) / GRADIENT_STEP);

  for (s = s0; s <= s1; s++)
  {
    pat          = gradient_step_pattern(gc, s, MAX(nsteps - 1, 1));
    pat.origin.x = ox;
    pat.origin.y = bounds->y0 - event->data.redraw.scroll.y;

    col.x0 = ox + s * GRADIENT_STEP;
    col.x1 = col.x0 + GRADIENT_STEP;
    col.y0 = content->y0;
    col.y1 = content->y1;
    if (!box_intersection(&col, content, &clip))
      screen_fill_pattern(event->data.redraw.scr, &clip, &pat);
  }
}

/* pick both ends of the two colour blend afresh, the second standing out from
 * the first */
static void gradient_randomise(gradient_task_t *gc)
{
  gc->a = colour_random(&gc->rng);
  gc->b = colour_random_contrast(&gc->rng, gc->a);
}

static result_t gradient_init(gradient_task_t *task)
{
  result_t  rc;
  colour_t *fields[2];
  int       i;

  task->dither_index = GRADIENT_DEFAULT_DITHER;
  task->brightness   = GRADIENT_UNITY;
  task->saturation   = GRADIENT_UNITY;

  rng_seed(&task->rng, (uint32_t) time(NULL));
  gradient_randomise(task);

  fields[0] = &task->a;
  fields[1] = &task->b;
  rc = wuss_colourlist_init(&task->colours, task->base.wuss, NELEMS(fields),
                            fields, gradient_colour_names);
  if (rc != result_OK)
    return rc;

  /* gradient_redraw paints every pixel itself */
  rc = wuss_window_create_at(task->base.delegate,
                             &task_place,
                             SIZE2D(GRADIENT_DOC_WIDTH, GRADIENT_DOC_HEIGHT),
                             "Gradient",
                             wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                             wuss_NO_BACKDROP,
                             SIZE2D(GRADIENT_DOC_WIDTH, GRADIENT_DOC_HEIGHT),
                             SIZE2D(0, 0),
                             &task->base.window);
  if (rc != result_OK)
    return rc;

  taskapp_save_row(&task->base, task->menu_items, GRADIENT_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  for (i = 0; i < gradient_NSHAPES; i++)
    WUSS_MENU_ITEM(task->shape_items, i, gradient_shape_names[i],
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->shape_menu, "Shape", task->shape_items,
                 NELEMS(task->shape_items));

  WUSS_MENU_ITEM_MENU(task->menu_items, GRADIENT_MENU_SHAPE, "Shape",
                      wuss_MENU_ITEM_NONE, &task->shape_menu);

  WUSS_MENU_ITEM_MENU(task->menu_items, GRADIENT_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_DASHED, &task->colours.menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, GRADIENT_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_NONE, "^R");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, GRADIENT_MENU_RESET, "Reset",
                          wuss_MENU_ITEM_NONE, "R");

  WUSS_MENU_TITLE(task->base.menu, "Gradient", task->menu_items,
                 NELEMS(task->menu_items));

  return result_OK;
}

static result_t gradient_redraw(const wuss_event_t *event, void *task_data)
{
  gradient_task_t *gc;
  screen_t        *scr;
  const box_t     *content, *bounds;
  int              di, step[3], sx, sy, x, y, lx, ly;
  int              rgb[3];

  gc  = task_data;
  scr = event->data.redraw.scr;

  di  = gc->dither_index;

  channel_steps(pixelfmt_base(scr->format), step);

  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;
  sx      = event->data.redraw.scroll.x;
  sy      = event->data.redraw.scroll.y;

  if (gc->shape == gradient_SHAPE_TWO_COLOUR)
  {
    gradient_redraw_two_colour(gc, event);
  }
  else
  {
    for (y = content->y0; y < content->y1; y++)
    {
      for (x = content->x0; x < content->x1; x++)
      {
        lx = x - bounds->x0 + sx;
        ly = y - bounds->y0 + sy;

        shade(gc->shape, lx, ly, rgb);
        adjust(gc, rgb);

        screen_set_pixel(scr, x, y,
                         colour_rgb(dither(di, step[0], rgb[0], lx, ly),
                                    dither(di, step[1], rgb[1], lx, ly),
                                    dither(di, step[2], rgb[2], lx, ly)));
      }
    }
  }

  /* matrix-size label, anchored at document (2, 2) so it scrolls with the
   * content: screen pos = content top-left - scroll + doc offset. Uses bounds,
   * not the per-redraw dirty piece, so it is drawn whole on a partial redraw */
  {
    bmfont_t   *font = wuss_get_font(gc->base.wuss);
    int       dim  = 1 << gradient_dithers[gc->dither_index];
    char      label[32];
    colour_t    ink  = colour_rgb(0xFF, 0xFF, 0xFF);
    colour_t    bg   = colour_rgba(0, 0, 0, 0); /* transparent */

    char        matrix[8];

    if (gc->shape == gradient_SHAPE_TWO_COLOUR)
      snprintf(matrix, sizeof(matrix), "pattern");
    else if (dim == 1)
      snprintf(matrix, sizeof(matrix), "off");
    else
      snprintf(matrix, sizeof(matrix), "%dx%d", dim, dim);

    snprintf(label, sizeof(label), "%s%s b%d%% s%d%%",
             matrix,
             gc->shape != gradient_SHAPE_TWO_COLOUR && step[1] == 1 ? " (n/a)"
                                                                    : "",
             gc->brightness * 100 / GRADIENT_UNITY,
             gc->saturation * 100 / GRADIENT_UNITY);

    if (font != NULL)
    {
      int     ascent;
      point_t pos;

      bmfont_get_info(font, NULL, NULL, &ascent, NULL);
      pos = POINT(bounds->x0 - sx + 2, bounds->y0 - sy + 2 + ascent);
      wuss_text_draw(gc->base.wuss, 0, scr, label, (int) strlen(label), ink, bg,
                     &pos, NULL);
    }
  }

  return result_OK;
}

static result_t gradient_mouse(const wuss_event_t *event, void *task_data)
{
  gradient_task_t *gc;
  point_t          p;
  wuss_button_t    button;
  int              n;

  gc = task_data;
  p  = event->data.mouse.point;
  n  = (int) NELEMS(gradient_dithers);

  /* an ADJUST drag moves brightness/saturation; an ADJUST click with no
   * movement still steps the dither matrix backward, on release */
  if (event->data.mouse.action == wuss_MOUSE_MOVE)
  {
    if (!gc->dragging)
      return result_OK;

    gc->saturation = CLAMP(gc->saturation + (p.x - gc->drag_x) * GRADIENT_DRAG_SCALE,
                           0, GRADIENT_ADJUST_MAX);
    gc->brightness = CLAMP(gc->brightness - (p.y - gc->drag_y) * GRADIENT_DRAG_SCALE,
                           0, GRADIENT_ADJUST_MAX);
    gc->drag_x  = p.x;
    gc->drag_y  = p.y;
    gc->dragged = 1;
    wuss_window_invalidate_visible(gc->base.window);

    return result_OK;
  }

  if (event->data.mouse.action == wuss_MOUSE_UP)
  {
    if (!gc->dragging)
      return result_OK;

    gc->dragging = 0;
    if (gc->dragged)
      return result_OK;

    gc->dither_index = (gc->dither_index + n - 1) % n;
    wuss_window_invalidate_visible(gc->base.window);

    return result_OK;
  }

  button = event->data.mouse.button;

  if (button & wuss_BUTTON_MENU)
  {
    wuss_menu_tick_exclusive(&gc->shape_menu, gc->shape);

    return wuss_menu_open_at_pointer(gc->base.delegate, &gc->base.menu,
                                     &gc->base.menu_handle);
  }

  if (button & wuss_BUTTON_ADJUST)
  {
    gc->dragging = 1;
    gc->dragged  = 0;
    gc->drag_x   = p.x;
    gc->drag_y   = p.y;

    return result_OK;
  }

  if (!(button & wuss_BUTTON_SELECT))
    return result_OK;

  gc->dither_index = (gc->dither_index + 1) % n;

  wuss_window_invalidate_visible(gc->base.window); /* whole fill changes */

  return result_OK;
}

/* switch the fill, keeping the Shape submenu's tick right if it is open */
static void gradient_set_shape(gradient_task_t *gc, gradient_shape_t shape)
{
  gc->shape = shape;
  wuss_menu_tick_exclusive_live(gc->base.menu_handle, &gc->shape_menu,
                                gc->shape);
  wuss_window_invalidate_visible(gc->base.window);
}

/* restore the default shape, dither matrix, brightness, saturation and two
 * colours */
static void gradient_reset(gradient_task_t *gc)
{
  gc->dither_index = GRADIENT_DEFAULT_DITHER;
  gc->brightness   = GRADIENT_UNITY;
  gc->saturation   = GRADIENT_UNITY;
  gc->a            = colour_rgb(0x00, 0x00, 0x00);
  gc->b            = colour_rgb(0xFF, 0xFF, 0xFF);
  gradient_set_shape(gc, gradient_SHAPE_LINEAR);
}

/* Menu pick: a Shape row switches the fill; a Colours row or Randomise
 * changes the two colours and shows them; Reset restores the defaults; Save
 * shows the Save As dialogue. A SELECT pick has already closed and freed the
 * chain, so drop the handle then. */
static result_t gradient_menu_select(gradient_task_t    *gc,
                                     const wuss_event_t *event)
{
  if (event->data.menu_select.menu == &gc->shape_menu)
  {
    gradient_set_shape(gc, (gradient_shape_t) event->data.menu_select.index);

    return result_OK;
  }

  if (wuss_colourlist_selected(&gc->colours, event))
  {
    gradient_set_shape(gc, gradient_SHAPE_TWO_COLOUR);

    return result_OK;
  }

  if (wuss_menu_select_is(event, &gc->base.menu, GRADIENT_MENU_RANDOMISE))
  {
    gradient_randomise(gc);
    gradient_set_shape(gc, gradient_SHAPE_TWO_COLOUR);
  }
  else if (wuss_menu_select_is(event, &gc->base.menu, GRADIENT_MENU_RESET))
  {
    gradient_reset(gc);
  }

  return result_OK;
}

/* Every submenu leaf fires wuss_EVENT_PRE_SUBMENU_OPEN; only the colour rows
 * retarget the shared colourmenu. */
static result_t gradient_pre_submenu_open(gradient_task_t    *gc,
                                          const wuss_event_t *event)
{
  result_t rc;
  int      index;

  if (wuss_colourlist_pre_open(&gc->colours, gc->base.wuss, event, &rc))
    return rc;

  index = event->data.pre_submenu_open.index;

  return wuss_menu_open_submenu_now(event->data.pre_submenu_open.handle, index,
                                    gc->menu_items[index].submenu);
}

/* S steps the shape and D the dither matrix forward. The arrow keys are
 * left unclaimed for scrolling, as is anything else. */
static result_t gradient_key(gradient_task_t *gc,
                             wuss_window_t   *window,
                             int              code)
{
  switch (code)
  {
  case 'S':
  case 's':
    gc->shape = (gradient_shape_t) ((gc->shape + 1) % gradient_NSHAPES);
    break;

  case 'D':
  case 'd':
    gc->dither_index = (gc->dither_index + 1) %
                       (int) NELEMS(gradient_dithers);
    break;

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }

  wuss_window_invalidate_visible(window);

  return result_OK;
}

static result_t gradient_handle(wuss_window_t      *window,
                                const wuss_event_t *event,
                                void               *task_data)
{
  gradient_task_t *gc;

  gc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return gradient_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    return gradient_mouse(event, task_data);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(gc->base.delegate, &gc->base.menu, event, &rc))
      return rc;

    return gradient_key(gc, window, event->data.key.code);
  }

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return gradient_pre_submenu_open(gc, event);

  case wuss_EVENT_MENU_SELECT:
    return gradient_menu_select(gc, event);

  default:
    return result_OK;
  }
}

result_t gradient_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Gradient",
    .purpose       = "Dithered colour gradients",
    .instance_size = sizeof(gradient_task_t),
    .init          = (taskapp_init_fn_t) gradient_init,
    .handle        = gradient_handle,
    .save_name     = "gradient.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
