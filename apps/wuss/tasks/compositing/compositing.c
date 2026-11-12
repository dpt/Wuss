/* wuss/tasks/compositing/compositing.c -- Porter-Duff compositing */

#ifdef WUSS_APP

#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "framebuf/composite.h"
#include "framebuf/palettes.h"
#include "framebuf/pixelfmt.h"
#include "geom/box.h"
#include "geom/point.h"
#include "io/path.h"
#include "utils/maths.h"
#include "wuss/component/dialogue.h"
#include "wuss/icon-spec.h"
#include "wuss/icon.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"
#include "../common/dithermenu.h"

/* window's task: the two composite demo images blended under a Porter-Duff
 * rule, over an alpha checkerboard. Menu > Alpha opens a slider scaling the
 * source image's alpha, so each operator can be seen across its full range.
 * SELECT steps to the next rule and ADJUST to the previous one; Menu > Rule
 * jumps straight to any one; Menu > Swap images exchanges source and
 * destination; Menu > Display > Dithering picks how the result is blitted
 * to a shallow screen; Menu > Save opens a Save As dialogue (drag its icon onto a
 * Filer window, or Save with a full path already typed) that writes the
 * window's contents out. Once a click has given the window the input focus,
 * Left/Right step the rule. */
typedef struct compositing_task
{
  taskapp_instance_t  base;
  wuss_menu_item_t    menu_items[5]; /* per-instance: a shared static would
                                       * leak one instance's .window pointer
                                       * into another's menu */
  wuss_menu_item_t    rule_items[composite_RULE__LIMIT];
  wuss_menu_t         rule_menu;
  dithermenu_t        dither;    /* "Display" submenu */
  screen_dither_t     dithering; /* how the composite is blitted */
  wuss_dialogue_t    *alpha_dialogue; /* NULL once the window closes */
  wuss_slider_row_t   alpha_row;
  int                 alpha; /* Menu > Alpha; source alpha scale, 0..255 */
  int                 swapped; /* Menu > Swap images; a and b exchanged */
  bitmap_t         a;              /* owned: pristine source, BGRA */
  bitmap_t         b;               /* owned: pristine destination, BGRA */
  bitmap_t         src;             /* owned: scratch, rebuilt each redraw */
  bitmap_t         dst;             /* owned: scratch, rebuilt each redraw */
  composite_rule_t rule;
  colour_t         light;           /* checkerboard */
  colour_t         dark;            /* checkerboard */
  colour_t         fg;              /* rule name label */
  colour_t         bg;              /* rule name label */
}
compositing_task_t;

/* MENU click pops this menu; the item table and wuss_menu_t live
 * per-instance in compositing_task_t, not as a file-scope static */
enum
{
  COMPOSITING_MENU_SAVE,
  COMPOSITING_MENU_DISPLAY,
  COMPOSITING_MENU_RULE,
  COMPOSITING_MENU_ALPHA,
  COMPOSITING_MENU_SWAP
};

#define PD_SIZE            (256) /* the demo images are 256x256 */
#define PD_CHECKER_BAND      (8) /* checkerboard square size, in pixels */
#define PD_FORMAT          (pixelfmt_bgra8888)

#define PD_ALPHA_DEFAULT   (192) /* part-transparent, so rules differ */
#define PD_SLIDER_W        (128)

/* ----------------------------------------------------------------------- */

static const char *const rule_names[composite_RULE__LIMIT] =
{
  "CLEAR",
  "SRC",
  "DST",
  "SRC OVER",
  "DST OVER",
  "SRC IN",
  "DST IN",
  "SRC OUT",
  "DST OUT",
  "SRC ATOP",
  "DST ATOP",
  "XOR"
};

static result_t load_demo_png(bitmap_t   *bm,
                              const char *resources,
                              const char *leafname)
{
  result_t    rc;
  const char *filename;

  filename = pathf("%s/resources/wuss-tasks/compositing/%s.png", resources,
                   leafname);

  rc = bitmap_load_png(bm, filename);
  if (rc != result_OK)
    return rc;

  rc = bitmap_convert_in_place(bm, PD_FORMAT);
  if (rc != result_OK)
  {
    free(bm->base);
    bm->base = NULL;
    return rc;
  }

  return result_OK;
}

/* ----------------------------------------------------------------------- */

/* size the window to the pane plus a strip below it for the rule name, as
 * tall as the font */
static result_t compositing_fit(compositing_task_t *task)
{
  result_t rc;
  size2d_t size;

  size = SIZE2D(PD_SIZE,
                PD_SIZE + wuss_std_height(task->base.wuss, wuss_STD_LABEL) + 4);

  rc = wuss_window_set_doc(task->base.window, size);
  if (rc != result_OK)
    return rc;

  return wuss_window_resize(task->base.window, size);
}

static result_t compositing_alpha_fillout(void *opaque)
{
  compositing_task_t *task;

  task = opaque;

  return wuss_slider_row_set(wuss_dialogue_window(task->alpha_dialogue),
                             &task->alpha_row, task->alpha);
}

/* Build the Alpha dialogue: a label, a 0..255 slider and its value echo in
 * one row. Hover on the menu row opens it as a submenu; every drag redraws
 * at once. */
static result_t compositing_alpha_create(compositing_task_t *task)
{
  static const char title[] = "Alpha";

  result_t         rc;
  wuss_icon_spec_t specs[3];
  wuss_icon_t     *made[3];
  char             buf[WUSS_SLIDER_ROW_BUF];
  int              label_w, value_w, slider_h, width, height;
  box_t            label_box, slider_box, value_box;

  label_w  = task_text_width(task->base.wuss, title);
  /* +2: a label insets its text a pixel either side, and an overhang would
   * be left behind when the text shrinks */
  value_w  = task_text_width(task->base.wuss, "255") + 2;
  slider_h = wuss_std_height(task->base.wuss, wuss_STD_SLIDER);
  width    = 2 * wuss_STD_INSET + label_w + PD_SLIDER_W + value_w +
             2 * wuss_STD_GAP;
  height   = 2 * wuss_STD_INSET + slider_h;

  rc = wuss_dialogue_create(&task->alpha_dialogue, task->base.delegate,
                            SIZE2D(width, height), title,
                            compositing_alpha_fillout, task);
  if (rc != result_OK)
    return rc;

  wuss_dialogue_set_focusable(task->alpha_dialogue, 0);

  label_box  = (box_t) BOX_POS_SIZE(wuss_STD_INSET, wuss_STD_INSET, label_w,
                                    slider_h);
  slider_box = (box_t) BOX_POS_SIZE(label_box.x1 + wuss_STD_GAP,
                                    wuss_STD_INSET, PD_SLIDER_W, slider_h);
  value_box  = (box_t) BOX_POS_SIZE(slider_box.x1 + wuss_STD_GAP,
                                    wuss_STD_INSET, value_w, slider_h);

  wuss_icon_spec_label(&specs[0], label_box, title,
                       wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  wuss_icon_spec_slider_row(&specs[1], &specs[2], slider_box, value_box,
                            wuss_SLIDER_HORIZONTAL, 0, 255, task->alpha, "%d",
                            0, buf, sizeof(buf));

  rc = wuss_icon_create_array(wuss_dialogue_window(task->alpha_dialogue),
                              specs, NELEMS(specs), made);
  if (rc != result_OK)
  {
    wuss_dialogue_destroy(task->alpha_dialogue);
    task->alpha_dialogue = NULL;
    return rc;
  }

  wuss_slider_row_bind(&task->alpha_row, made[1], made[2], "%d", 0, 255, 0);

  return result_OK;
}

static result_t compositing_init(compositing_task_t *task)
{
  result_t        rc;
  wuss_t         *wuss;
  const char     *resources;
  const colour_t *palette;
  int             i;

  wuss                  = task->base.wuss;
  palette               = wuss_get_palette(wuss, NULL);
  task->rule            = composite_RULE_SRC_OVER;
  task->alpha           = PD_ALPHA_DEFAULT;
  task->dithering       = screen_DITHER_BLUE_NOISE;
  task->light           = palette[palette_PICO8_LIGHT_GREY];
  task->dark            = palette[palette_PICO8_DARK_GREY];
  task->fg              = palette[palette_PICO8_WHITE];
  task->bg              = palette[palette_PICO8_BLACK];

  resources = wuss_get_resources(wuss);

  rc = load_demo_png(&task->a, resources, "A");
  if (rc != result_OK)
    return rc;

  rc = load_demo_png(&task->b, resources, "B");
  if (rc != result_OK)
    return rc;

  rc = bitmap_clone(&task->a, &task->src);
  if (rc != result_OK)
    return rc;

  rc = bitmap_clone(&task->b, &task->dst);
  if (rc != result_OK)
    return rc;

  taskapp_save_row(&task->base, task->menu_items, COMPOSITING_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  dithermenu_init(&task->dither, task->dithering);
  WUSS_MENU_ITEM_MENU(task->menu_items, COMPOSITING_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_NONE, &task->dither.display);

  for (i = 0; i < composite_RULE__LIMIT; i++)
    WUSS_MENU_ITEM(task->rule_items, i, rule_names[i], wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->rule_menu, "Rule", task->rule_items,
                 NELEMS(task->rule_items));

  WUSS_MENU_ITEM_MENU(task->menu_items, COMPOSITING_MENU_RULE, "Rule",
                      wuss_MENU_ITEM_NONE, &task->rule_menu);

  /* window set once the dialogue exists */
  WUSS_MENU_ITEM(task->menu_items, COMPOSITING_MENU_ALPHA, "Alpha",
                 wuss_MENU_ITEM_BORROWED_SUBMENU | wuss_MENU_ITEM_PRE_OPEN);

  WUSS_MENU_ITEM(task->menu_items, COMPOSITING_MENU_SWAP, "Swap images",
                 wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->base.menu, "Compositing", task->menu_items,
                 NELEMS(task->menu_items));

  rc = compositing_alpha_create(task);
  if (rc != result_OK)
    return rc;
  task->menu_items[COMPOSITING_MENU_ALPHA].window =
    wuss_dialogue_window(task->alpha_dialogue);

  return compositing_fit(task);
}

static void compositing_destroy(compositing_task_t *task)
{
  wuss_dialogue_destroy(task->alpha_dialogue);
  free(task->dst.base);
  free(task->src.base);
  free(task->b.base);
  free(task->a.base);
}

/* ----------------------------------------------------------------------- */

/* Copy "a" into "src", scaling its alpha channel by "ramp" (0..255). The
 * library's compositing works on non-premultiplied values, so the colour
 * components are left alone. */
static void compositing_ramp_src(compositing_task_t *pd, int ramp)
{
  pixelfmt_any_t *sp;
  pixelfmt_any_t *dp;
  int             x, y;

  sp = pd->a.base;
  dp = pd->src.base;

  for (y = 0; y < pd->a.size.h; y++)
    for (x = 0; x < pd->a.size.w; x++)
    {
      pixelfmt_any_t px = *sp++;
      unsigned int   alpha;

      alpha = div255(PIXELFMT_xxxA8888(px) * ramp);
      *dp++ = (px & ~PIXELFMT_xxxA8888_MASK) |
              ((pixelfmt_any_t) alpha << PIXELFMT_xxxA8888_SHIFT);
    }
}

static result_t compositing_redraw(const wuss_event_t *event,
                                   void               *task_data)
{
  result_t            rc;
  compositing_task_t *pd;
  screen_t           *scr;
  const box_t        *content, *bounds;
  const char         *name;
  point_t             pos;
  int                 sx, sy;
  bmfont_t           *font;

  pd = task_data;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;
  sx      = event->data.redraw.scroll.x;
  sy      = event->data.redraw.scroll.y;

  screen_fill_checks(scr, content, POINT(bounds->x0 - sx, bounds->y0 - sy),
                     PD_CHECKER_BAND, pd->light, pd->dark);

  /* ponytail: the whole 256x256 pane is recomposited on every redraw -- two
   * full-image memcpys plus two full-image passes. Fine for one window in a
   * test harness; if the main loop ever gets tight, cache the composited
   * bitmap and rebuild it only when the alpha or rule actually changes. */
  rc = bitmap_copy_pixels(&pd->b, &pd->dst);
  if (rc != result_OK)
    return rc;

  compositing_ramp_src(pd, pd->alpha);

  rc = composite(pd->rule, &pd->src, &pd->dst);
  if (rc != result_OK)
    return rc;

  rc = screen_copy_bitmap_dithered(scr, bounds->x0 - sx, bounds->y0 - sy,
                                   &pd->dst, pd->dithering);
  if (rc != result_OK)
    return rc;

  /* fetched each time: a font change releases the old one */
  font = wuss_get_font_n(pd->base.wuss, 0);
  {
    int ascent;

    bmfont_get_info(font, NULL, NULL, &ascent, NULL);
    name  = rule_names[pd->rule];
    pos.x = bounds->x0 - sx + 2;
    pos.y = bounds->y0 - sy + PD_SIZE + 2 + ascent;
  }

  return bmfont_draw(font, scr, name, (int) strlen(name),
                     pd->fg, pd->bg, NULL, &pos, NULL);
}

/* step the rule by dir (+1 or -1), wrapping round */
static result_t compositing_mouse(wuss_window_t *window,
                                  int            dir,
                                  void          *task_data)
{
  compositing_task_t *pd;

  pd = task_data;

  pd->rule = (pd->rule + composite_RULE__LIMIT + dir) % composite_RULE__LIMIT;

  wuss_window_invalidate_visible(window);

  return result_OK;
}

/* Left/Right step to the previous/next rule, as ADJUST and SELECT clicks
 * do. Anything else is passed back unclaimed. */
static result_t compositing_key(compositing_task_t *pd,
                                wuss_window_t      *window,
                                int                 code)
{
  switch (code)
  {
  case wuss_KEY_LEFT:  return compositing_mouse(window, -1, pd);
  case wuss_KEY_RIGHT: return compositing_mouse(window, +1, pd);
  default:             return result_WUSS_KEY_UNCLAIMED;
  }
}

/* The "Swap" row exchanges the source and destination images (with their
 * scratch bitmaps, which are sized to match). An ADJUST pick keeps the menu
 * open, so retick the live row; a SELECT pick has already closed it. */
static result_t compositing_swap(compositing_task_t *pd)
{
  bitmap_t t;

  t           = pd->a;
  pd->a       = pd->b;
  pd->b       = t;
  t           = pd->src;
  pd->src     = pd->dst;
  pd->dst     = t;
  pd->swapped = !pd->swapped;

  wuss_window_invalidate_visible(pd->base.window);

  wuss_menu_tick_item_live(pd->base.menu_handle, &pd->base.menu,
                           COMPOSITING_MENU_SWAP, pd->swapped);

  return result_OK;
}

/* A "Rule" submenu pick: jump straight to that rule. */
static result_t compositing_pick_rule(compositing_task_t *pd,
                                      const wuss_event_t *event)
{
  pd->rule = (composite_rule_t) event->data.menu_select.index;

  wuss_menu_tick_exclusive_live(pd->base.menu_handle, &pd->rule_menu, pd->rule);

  wuss_window_invalidate_visible(pd->base.window);

  return result_OK;
}

static result_t compositing_handle(wuss_window_t      *window,
                                   const wuss_event_t *event,
                                   void               *task_data)
{
  result_t            rc;
  compositing_task_t *pd;
  int                 old;

  pd = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    if (window != pd->base.window)
      return result_OK;
    return compositing_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN ||
        window != pd->base.window)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      wuss_menu_tick_item(&pd->base.menu, COMPOSITING_MENU_SWAP, pd->swapped);
      wuss_menu_tick_exclusive(&pd->rule_menu, pd->rule);
      wuss_menu_tick_exclusive(&pd->dither.menu, (int) pd->dithering);

      return wuss_menu_open_at_pointer(pd->base.delegate, &pd->base.menu,
                                       &pd->base.menu_handle);
    }
    if (event->data.mouse.button & wuss_BUTTON_SELECT)
      return compositing_mouse(window, +1, task_data);
    if (event->data.mouse.button & wuss_BUTTON_ADJUST)
      return compositing_mouse(window, -1, task_data);
    return result_OK;

  case wuss_EVENT_KEY:
    if (window != pd->base.window)
      return result_WUSS_KEY_UNCLAIMED;
    if (!task_key_is_plain(pd->base.delegate, &pd->base.menu, event, &rc))
      return rc;
    return compositing_key(pd, window, event->data.key.code);

  case wuss_EVENT_RELAYOUT:
    return window == pd->base.window ? compositing_fit(pd) : result_OK;

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu == &pd->base.menu &&
        event->data.menu_select.index == COMPOSITING_MENU_SWAP)
      return compositing_swap(pd);
    if (event->data.menu_select.menu == &pd->rule_menu)
      return compositing_pick_rule(pd, event);
    if (event->data.menu_select.menu == &pd->dither.menu)
    {
      pd->dithering = dithermenu_select(&pd->dither, pd->base.menu_handle,
                                        event->data.menu_select.index);
      wuss_window_invalidate_visible(pd->base.window);
    }
    return result_OK;

  case wuss_EVENT_ICON:
    /* a press or a move needn't change the slider's value */
    old = pd->alpha;
    if (window == wuss_dialogue_window(pd->alpha_dialogue) &&
        wuss_slider_row_event(window, &pd->alpha_row, event, &pd->alpha) &&
        pd->alpha != old && pd->base.window != NULL)
      wuss_window_invalidate_visible(pd->base.window);
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    if (window != wuss_dialogue_window(pd->alpha_dialogue))
      return result_OK;
    rc = wuss_dialogue_handle_pre_show(pd->alpha_dialogue);
    if (rc != result_OK || event->data.pre_show.handle == NULL)
      return rc;
    return wuss_menu_open_window_now(event->data.pre_show.handle,
                                     event->data.pre_show.index);

  case wuss_EVENT_CLOSE:
    wuss_dialogue_handle_close(pd->alpha_dialogue, window);
    if (window == pd->base.window)
    {
      /* the hidden dialogue is another window on this autoclose task:
       * close it too, else the task outlives its main window */
      wuss_dialogue_destroy(pd->alpha_dialogue);
      pd->alpha_dialogue = NULL;
    }
    return result_OK;

  case wuss_EVENT_QUIT:
    compositing_destroy(pd);
    return result_OK;

  default:
    return result_OK;
  }
}

result_t compositing_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Compositing",
    .purpose       = "Porter-Duff compositing",
    .instance_size = sizeof(compositing_task_t),
    .window_size   = { PD_SIZE, PD_SIZE }, /* compositing_fit adds the label */
    .init          = (taskapp_init_fn_t) compositing_init,
    .handle        = compositing_handle,
    .save_name     = "compositing.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
