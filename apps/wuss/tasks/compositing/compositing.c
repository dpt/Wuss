/* wuss/tasks/compositing/compositing.c -- animated Porter-Duff compositing */

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
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* window's task: the two composite demo images blended under a cycling
 * Porter-Duff rule, over an alpha checkerboard. The source image's alpha is
 * ramped up and back down across each rule's turn, so every operator is seen
 * across its full range. Select steps to the next rule and Adjust to the
 * previous one; Menu > Rule jumps straight to any one; Menu > Swap images
 * exchanges source and destination; Menu > Save opens a Save As dialogue
 * (drag its icon onto a Filer window, or Save with a full path already
 * typed) that writes the window's contents out. Once a click has given the
 * window the input focus, Space pauses/resumes and Left/Right step the
 * rule. */
typedef struct compositing_task
{
  taskapp_instance_t  base;
  wuss_menu_item_t    menu_items[4]; /* per-instance: a shared static would
                                       * leak one instance's .window pointer
                                       * into another's menu */
  wuss_menu_item_t    rule_items[composite_RULE__LIMIT];
  wuss_menu_t         rule_menu;
  int                 paused; /* Menu > Pause; idle does nothing while set */
  int                 swapped; /* Menu > Swap images; a and b exchanged */
  bmfont_t        *font;
  bitmap_t         a;               /* owned: pristine source, BGRA */
  bitmap_t         b;               /* owned: pristine destination, BGRA */
  bitmap_t         src;             /* owned: scratch, rebuilt each frame */
  bitmap_t         dst;             /* owned: scratch, rebuilt each frame */
  composite_rule_t rule;
  int              frame;           /* frames elapsed in the current rule */
  int              frames_per_rule;
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
  COMPOSITING_MENU_PAUSE,
  COMPOSITING_MENU_SAVE,
  COMPOSITING_MENU_RULE,
  COMPOSITING_MENU_SWAP
};

#define PD_SIZE            (256) /* the demo images are 256x256 */
#define PD_LABEL_HEIGHT     (20) /* strip below the pane, for the rule name */
#define PD_CHECKER_BAND      (8) /* checkerboard square size, in pixels */
#define PD_FORMAT          (pixelfmt_bgra8888)

#define PD_FRAMES_DEFAULT  (120) /* ~2s per rule at 60 main-loop passes/sec */
#define PD_FRAMES_MIN       (10)
#define PD_FRAMES_MAX      (600)

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

static result_t compositing_init(compositing_task_t *task)
{
  result_t        rc;
  wuss_t         *wuss;
  const char     *resources;
  const colour_t *palette;
  int             i;

  wuss                  = task->base.wuss;
  palette               = wuss_get_palette(wuss, NULL);
  task->font            = wuss_get_font_n(wuss, 0);
  task->rule            = composite_RULE_CLEAR;
  task->frame           = 0;
  task->frames_per_rule = PD_FRAMES_DEFAULT;
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

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, COMPOSITING_MENU_PAUSE, "Pause",
                          wuss_MENU_ITEM_NONE, "SPACE");

  taskapp_save_row(&task->base, task->menu_items, COMPOSITING_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  for (i = 0; i < composite_RULE__LIMIT; i++)
    WUSS_MENU_ITEM(task->rule_items, i, rule_names[i], wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->rule_menu, "Rule", task->rule_items,
                 NELEMS(task->rule_items));

  WUSS_MENU_ITEM_MENU(task->menu_items, COMPOSITING_MENU_RULE, "Rule",
                      wuss_MENU_ITEM_DASHED, &task->rule_menu);

  WUSS_MENU_ITEM(task->menu_items, COMPOSITING_MENU_SWAP, "Swap images",
                 wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->base.menu, "Compositing", task->menu_items,
                 NELEMS(task->menu_items));

  return result_OK;
}

static void compositing_destroy(compositing_task_t *task)
{
  free(task->dst.base);
  free(task->src.base);
  free(task->b.base);
  free(task->a.base);
}

/* ----------------------------------------------------------------------- */

/* Triangle ramp: 0 at the start of the rule's turn, 255 at its midpoint, back
 * to 0 at its end. */
static int compositing_ramp(const compositing_task_t *pd)
{
  int half;

  half = pd->frames_per_rule / 2;
  if (half <= 0)
    return 255;

  if (pd->frame < half)
    return pd->frame * 255 / half;
  else
    return MAX(0, (pd->frames_per_rule - pd->frame) * 255 / half);
}

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

      alpha = PIXELFMT_xxxA8888(px) * ramp / 255;
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
   * bitmap and rebuild it only when the ramp value or rule actually changes. */
  rc = bitmap_copy_pixels(&pd->b, &pd->dst);
  if (rc != result_OK)
    return rc;

  compositing_ramp_src(pd, compositing_ramp(pd));

  rc = composite(pd->rule, &pd->src, &pd->dst);
  if (rc != result_OK)
    return rc;

  screen_copy_bitmap(scr, bounds->x0 - sx, bounds->y0 - sy, &pd->dst);

  {
    int ascent;

    bmfont_get_info(pd->font, NULL, NULL, &ascent, NULL);
    name  = rule_names[pd->rule];
    pos.x = bounds->x0 - sx + 2;
    pos.y = bounds->y0 - sy + PD_SIZE + 2 + ascent;
  }

  return bmfont_draw(pd->font, scr, name, (int) strlen(name),
                     pd->fg, pd->bg, NULL, &pos, NULL);
}

static result_t compositing_idle(void *task_data)
{
  compositing_task_t *pd;

  pd = task_data;

  /* the shared proginfo singleton is a second window on this same
   * (autoclose) delegate while its dialogue is open, so closing the main
   * window alone doesn't necessarily empty task->windows immediately --
   * guard against the dangling window in the meantime */
  if (pd->base.window == NULL)
    return result_OK;

  if (pd->paused)
    return result_OK;

  if (++pd->frame >= pd->frames_per_rule)
  {
    pd->frame = 0;
    pd->rule  = (pd->rule + 1) % composite_RULE__LIMIT;
  }

  /* the ramp changes every frame, so the whole pane is stale every frame */
  wuss_window_invalidate_visible(pd->base.window);

  return result_OK;
}

/* step the rule by dir (+1 or -1), wrapping round; paused, restart at the
 * ramp's midpoint, as compositing_pick_rule does */
static result_t compositing_mouse(wuss_window_t *window,
                                  int            dir,
                                  void          *task_data)
{
  compositing_task_t *pd;

  pd = task_data;

  pd->rule  = (pd->rule + composite_RULE__LIMIT + dir) %
              composite_RULE__LIMIT;
  pd->frame = pd->paused ? pd->frames_per_rule / 2 : 0;

  wuss_window_invalidate_visible(window);

  return result_OK;
}

static result_t compositing_scroll(wuss_window_t *window,
                                   int            delta,
                                   void          *task_data)
{
  compositing_task_t *pd;

  pd = task_data;

  pd->frames_per_rule += delta * 10;
  pd->frames_per_rule  = CLAMP(pd->frames_per_rule,
                               PD_FRAMES_MIN, PD_FRAMES_MAX);
  pd->frame            = MIN(pd->frame, pd->frames_per_rule);

  wuss_window_invalidate_visible(window);

  return result_OK;
}

/* Left/Right step to the previous/next rule, as Adjust and Select clicks
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

/* The "Pause" row stops or restarts the idle animation; the "Swap" row
 * exchanges the source and destination images (with their scratch bitmaps,
 * which are sized to match). An ADJUST pick keeps the menu open, so retick
 * the live row; a SELECT pick has already closed it. */
static result_t compositing_toggle(compositing_task_t *pd,
                                   const wuss_event_t *event)
{
  int      index;
  int     *flag;
  bitmap_t t;

  index = event->data.menu_select.index;

  if (index == COMPOSITING_MENU_SWAP)
  {
    t       = pd->a;
    pd->a   = pd->b;
    pd->b   = t;
    t       = pd->src;
    pd->src = pd->dst;
    pd->dst = t;
    flag    = &pd->swapped;
    wuss_window_invalidate_visible(pd->base.window);
  }
  else
  {
    flag = &pd->paused;
  }
  *flag = !*flag;

  wuss_menu_tick_item_live(pd->base.menu_handle, &pd->base.menu, index, *flag);

  return result_OK;
}

/* A "Rule" submenu pick: jump straight to that rule. Paused, restart it at
 * the ramp's midpoint rather than its start, where the source is fully
 * transparent and the rule would look like DST. */
static result_t compositing_pick_rule(compositing_task_t *pd,
                                      const wuss_event_t *event)
{
  pd->rule  = (composite_rule_t) event->data.menu_select.index;
  pd->frame = pd->paused ? pd->frames_per_rule / 2 : 0;

  wuss_menu_tick_exclusive_live(pd->base.menu_handle, &pd->rule_menu, pd->rule);

  wuss_window_invalidate_visible(pd->base.window);

  return result_OK;
}

static result_t compositing_handle(wuss_window_t      *window,
                                   const wuss_event_t *event,
                                   void               *task_data)
{
  compositing_task_t *pd;

  pd = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return compositing_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      wuss_menu_tick_item(&pd->base.menu, COMPOSITING_MENU_PAUSE, pd->paused);
      wuss_menu_tick_item(&pd->base.menu, COMPOSITING_MENU_SWAP, pd->swapped);
      wuss_menu_tick_exclusive(&pd->rule_menu, pd->rule);

      return wuss_menu_open_at_pointer(pd->base.delegate, &pd->base.menu,
                                       &pd->base.menu_handle);
    }
    if (event->data.mouse.button & wuss_BUTTON_SELECT)
      return compositing_mouse(window, +1, task_data);
    if (event->data.mouse.button & wuss_BUTTON_ADJUST)
      return compositing_mouse(window, -1, task_data);
    return result_OK;

  case wuss_EVENT_SCROLL:
    return compositing_scroll(window, event->data.scroll.delta, task_data);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(pd->base.delegate, &pd->base.menu, event, &rc))
      return rc;

    return compositing_key(pd, window, event->data.key.code);
  }

  case wuss_EVENT_IDLE:
    return compositing_idle(task_data);

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu == &pd->base.menu &&
        (event->data.menu_select.index == COMPOSITING_MENU_PAUSE ||
         event->data.menu_select.index == COMPOSITING_MENU_SWAP))
      return compositing_toggle(pd, event);
    if (event->data.menu_select.menu == &pd->rule_menu)
      return compositing_pick_rule(pd, event);
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
    .purpose       = "Animated Porter-Duff compositing",
    .instance_size = sizeof(compositing_task_t),
    .window_size   = { PD_SIZE, PD_SIZE + PD_LABEL_HEIGHT },
    .init          = (taskapp_init_fn_t) compositing_init,
    .handle        = compositing_handle,
    .save_name     = "compositing.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
