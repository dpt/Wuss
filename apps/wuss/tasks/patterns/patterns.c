/* wuss/tasks/patterns/patterns.c -- dithered colour gradient task */

#ifdef WUSS_APP

#include <stdlib.h>
#include <time.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/colour.h"
#include "framebuf/pattern.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "utils/rng.h"
#include "wuss/component/colourmenu.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* window C's task: fills the window with a left-to-right gradient from
 * colour a to colour b in PATTERNS_STEP-pixel steps, each step an
 * ordered-dither pattern from pattern_from_colour against the system
 * palette. a and b start random and are chosen from the Colour A / Colour B
 * menu rows; Menu > Randomise (^R) picks both afresh and Menu > Reset (R)
 * makes it black to white; Menu > Save opens a Save As dialogue (drag its
 * icon onto a Filer window, or Save with a full path already typed) that
 * writes the window's content out */

typedef struct patterns_task
{
  taskapp_instance_t  base;
  wuss_menu_item_t    menu_items[4]; /* per-instance: a shared static would
                                       * leak state between instances */
  colour_t            a;           /* left edge */
  colour_t            b;            /* right edge */
  wuss_colourlist_t   colours;      /* Colour A/Colour B submenu */
  rng_t               rng;          /* Randomise */
}
patterns_task_t;

#define PATTERNS_STEP 4 /* pixels per gradient step */

/* MENU click pops this menu; the item table and wuss_menu_t live
 * per-instance in patterns_task_t, not as a file-scope static */
enum
{
  PATTERNS_MENU_SAVE,
  PATTERNS_MENU_COLOURS,
  PATTERNS_MENU_RANDOMISE,
  PATTERNS_MENU_RESET
};

/* Colours submenu rows: the a and b fields, in that order */
static const char *const patterns_colour_names[] = { "Colour A", "Colour B" };

/* channel c0 -> c1, f of n frames into the blend */
static int patterns_lerp(unsigned int c0, unsigned int c1, int f, int n)
{
  return (int) (c0 * (n - f) + c1 * f) / n;
}

/* a dither of the colour f of n steps of the way from a to b */
static pattern_t patterns_step_pattern(patterns_task_t *bc, int f, int n)
{
  unsigned int    ar, ag, ab;
  unsigned int    br, bg, bb;
  colour_t        mix;
  const colour_t *palette;
  int             npalette;

  colour_get_rgb(&bc->a, &ar, &ag, &ab);
  colour_get_rgb(&bc->b, &br, &bg, &bb);

  mix = colour_rgb(patterns_lerp(ar, br, f, n),
                   patterns_lerp(ag, bg, f, n),
                   patterns_lerp(ab, bb, f, n));

  palette = wuss_get_palette(bc->base.wuss, &npalette);

  return pattern_from_colour(palette, npalette, mix);
}

/* pick both ends of the gradient afresh, the second standing out from the
 * first */
static void patterns_randomise(patterns_task_t *task)
{
  task->a = colour_random(&task->rng);
  task->b = colour_random_contrast(&task->rng, task->a);
}

static result_t patterns_init(patterns_task_t *task)
{
  result_t  rc;
  wuss_t   *wuss;
  colour_t *fields[2];

  wuss = task->base.wuss;
  rng_seed(&task->rng, (uint32_t) time(NULL));
  patterns_randomise(task);

  fields[0] = &task->a;
  fields[1] = &task->b;
  rc = wuss_colourlist_init(&task->colours, wuss, NELEMS(fields), fields,
                            patterns_colour_names);
  if (rc != result_OK)
    return rc;

  taskapp_save_row(&task->base, task->menu_items, PATTERNS_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_ITEM_MENU(task->menu_items, PATTERNS_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_DASHED, &task->colours.menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, PATTERNS_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_NONE, "^R");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, PATTERNS_MENU_RESET, "Reset",
                          wuss_MENU_ITEM_NONE, "R");

  WUSS_MENU_TITLE(task->base.menu, "Patterns", task->menu_items,
                 NELEMS(task->menu_items));

  return result_OK;
}

/* Every submenu leaf fires wuss_EVENT_PRE_SUBMENU_OPEN; only the colour
 * rows retarget the shared colourmenu. */
static result_t patterns_pre_submenu_open(patterns_task_t    *bc,
                                          const wuss_event_t *event)
{
  result_t rc;
  int      index;

  if (wuss_colourlist_pre_open(&bc->colours, bc->base.wuss, event, &rc))
    return rc;

  index = event->data.pre_submenu_open.index;

  return wuss_menu_open_submenu_now(event->data.pre_submenu_open.handle, index,
                                    bc->menu_items[index].submenu);
}

static result_t patterns_redraw(const wuss_event_t *event, void *task_data)
{
  patterns_task_t *bc;
  const box_t     *content;
  const box_t     *bounds;
  int              ox;
  int              nsteps;
  int              s;
  int              s0;
  int              s1;
  pattern_t        pat;
  box_t            col;
  box_t            clip;

  bc = task_data;

  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;

  /* the gradient spans the work area, anchored at its (0,0) in screen space
   * so it scrolls with the content and every dirty rectangle shares one
   * dither phase */
  ox     = bounds->x0 - event->data.redraw.scroll.x;
  nsteps = DIVIDE_ROUNDING_UP(bounds->x1 - bounds->x0, PATTERNS_STEP);

  s0 = MAX(0, (content->x0 - ox) / PATTERNS_STEP);
  s1 = MIN(nsteps - 1, (content->x1 - 1 - ox) / PATTERNS_STEP);

  for (s = s0; s <= s1; s++)
  {
    pat          = patterns_step_pattern(bc, s, MAX(nsteps - 1, 1));
    pat.origin.x = ox;
    pat.origin.y = bounds->y0 - event->data.redraw.scroll.y;

    col.x0 = ox + s * PATTERNS_STEP;
    col.x1 = col.x0 + PATTERNS_STEP;
    col.y0 = content->y0;
    col.y1 = content->y1;
    if (!box_intersection(&col, content, &clip))
      screen_fill_pattern(event->data.redraw.scr, &clip, &pat);
  }

  return result_OK;
}

static result_t patterns_handle(wuss_window_t      *window,
                                const wuss_event_t *event,
                                void               *task_data)
{
  patterns_task_t *bc;

  NOT_USED(window);

  bc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return patterns_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;

    if (!(event->data.mouse.button & wuss_BUTTON_MENU))
      return result_OK;

    return wuss_menu_open_at_pointer(bc->base.delegate, &bc->base.menu,
                                     &bc->base.menu_handle);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(bc->base.delegate, &bc->base.menu, event, &rc))
      return rc;

    return result_WUSS_KEY_UNCLAIMED;
  }

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return patterns_pre_submenu_open(bc, event);

  case wuss_EVENT_MENU_SELECT:
    {
      result_t rc;

      rc = result_OK;
      if (wuss_menu_select_is(event, &bc->base.menu, PATTERNS_MENU_RANDOMISE))
      {
        patterns_randomise(bc);
        wuss_window_invalidate_visible(bc->base.window);
      }
      else if (wuss_menu_select_is(event, &bc->base.menu, PATTERNS_MENU_RESET))
      {
        bc->a = colour_rgb(0x00, 0x00, 0x00);
        bc->b = colour_rgb(0xFF, 0xFF, 0xFF);
        wuss_window_invalidate_visible(bc->base.window);
      }
      else if (wuss_colourlist_selected(&bc->colours, event))
      {
        wuss_window_invalidate_visible(bc->base.window);
      }
      return rc;
    }

  default:
    return result_OK;
  }
}

result_t patterns_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Patterns",
    .purpose       = "Dithered two-colour gradient",
    .instance_size = sizeof(patterns_task_t),
    .window_size   = { 200, 160 },
    .init          = (taskapp_init_fn_t) patterns_init,
    .handle        = patterns_handle,
    .save_name     = "patterns.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
