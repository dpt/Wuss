/* wuss/tasks/checker/checker.c -- checkerboard task */

#ifdef WUSS_APP

#include <stdlib.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/colour.h"
#include "framebuf/palettes.h"
#include "geom/box.h"
#include "utils/rng.h"
#include "wuss/component/colourmenu.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* cycled by a content click, in this order, or picked from Menu > Pattern */
typedef enum checker_pattern
{
  checker_PATTERN_CHECKERBOARD,
  checker_PATTERN_HORIZONTAL,
  checker_PATTERN_VERTICAL,
  checker_PATTERN_DIAGONAL,
  checker_PATTERN__COUNT
}
checker_pattern_t;

/* fills the whole content area with a two-tone pattern, black and white
 * until Menu > Colours recolours it (Menu > Swap colours exchanges the
 * two); the pattern cycles forward on a SELECT click and back on ADJUST, or
 * is picked from Menu > Pattern; Menu > Save opens a Save As dialogue
 * (drag its icon onto a Filer window, or Save with a full path already
 * typed) that writes the window out; Menu > Randomise (^R) picks a pattern,
 * band width and colours afresh and Menu > Reset (R) restores the starting
 * ones. Once a click has given the window the
 * input focus, Left/Right step the pattern and Up/Down widen/narrow its
 * bands */
typedef struct checker_task
{
  taskapp_instance_t   base;
  wuss_menu_item_t     menu_items[6]; /* per-instance: a shared static would
                                        * leak one instance's .window pointer
                                        * into another's menu */
  wuss_menu_item_t     pattern_items[checker_PATTERN__COUNT];
  wuss_menu_t          pattern_menu;
  colour_t             black, white;
  wuss_colourlist_t    colours; /* Ink/Paper submenu */
  checker_pattern_t    pattern;
  int                  band; /* pixels per band, scroll-adjustable */
  rng_t                rng;  /* Randomise */
}
checker_task_t;

#define CHECKER_BAND_DEFAULT 8  /* pixels per band, so each pattern reads clearly */
#define CHECKER_BAND_MIN     1
#define CHECKER_BAND_MAX     32

/* MENU click pops this menu; the item table and wuss_menu_t live
 * per-instance in checker_task_t, not as a file-scope static, so that each
 * window's Info row can hold its own .window pointer to the shared proginfo
 * singleton, retargeted just before wuss_menu_open */
enum
{
  CHECKER_MENU_SAVE = 0,
  CHECKER_MENU_PATTERN,
  CHECKER_MENU_COLOURS,
  CHECKER_MENU_SWAP,
  CHECKER_MENU_RANDOMISE,
  CHECKER_MENU_RESET
};

/* Colours submenu rows: the black and white fields, in that order */
static const char *const checker_colour_names[] = { "Ink", "Paper" };

/* Pattern submenu rows, in checker_pattern_t order */
static const char *checker_pattern_names[checker_PATTERN__COUNT] =
{
  "Checkerboard",
  "Horizontal",
  "Vertical",
  "Diagonal"
};

/* the starting pattern, as checker_create and Reset restore it */
static void checker_defaults(checker_task_t *task)
{
  task->black   = colour_rgb(0x00, 0x00, 0x00);
  task->white   = colour_rgb(0xFF, 0xFF, 0xFF);
  task->pattern = checker_PATTERN_CHECKERBOARD;
  task->band    = CHECKER_BAND_DEFAULT;
}

/* pick the pattern, band width and both colours afresh */
static void checker_randomise(checker_task_t *task)
{
  task->pattern = (checker_pattern_t) rng_range(&task->rng,
                                                checker_PATTERN__COUNT);
  task->band    = CHECKER_BAND_MIN +
                  rng_range(&task->rng,
                            CHECKER_BAND_MAX - CHECKER_BAND_MIN + 1);
  task->black   = colour_random(&task->rng);
  task->white   = colour_random_contrast(&task->rng, task->black);
}

static result_t checker_init(checker_task_t *task)
{
  result_t  rc;
  colour_t *fields[2];
  int       i;

  rng_seed(&task->rng, (uint32_t) rand());
  checker_defaults(task);

  fields[0] = &task->black;
  fields[1] = &task->white;
  rc = wuss_colourlist_init(&task->colours, task->base.wuss, NELEMS(fields),
                            fields, checker_colour_names);
  if (rc != result_OK)
    return rc;

  taskapp_save_row(&task->base, task->menu_items, CHECKER_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  for (i = 0; i < checker_PATTERN__COUNT; i++)
    WUSS_MENU_ITEM(task->pattern_items, i, checker_pattern_names[i],
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->pattern_menu, "Pattern", task->pattern_items,
                 NELEMS(task->pattern_items));

  WUSS_MENU_ITEM_MENU(task->menu_items, CHECKER_MENU_PATTERN, "Pattern",
                      wuss_MENU_ITEM_DASHED, &task->pattern_menu);

  WUSS_MENU_ITEM_MENU(task->menu_items, CHECKER_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->colours.menu);

  WUSS_MENU_ITEM(task->menu_items, CHECKER_MENU_SWAP, "Swap colours",
                 wuss_MENU_ITEM_NONE);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, CHECKER_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_DASHED, "^R");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, CHECKER_MENU_RESET, "Reset",
                          wuss_MENU_ITEM_NONE, "R");

  WUSS_MENU_TITLE(task->base.menu, "Checker", task->menu_items,
                 NELEMS(task->menu_items));

  return result_OK;
}

static result_t checker_redraw(const wuss_event_t *event, void *task_data)
{
  checker_task_t   *cc;
  screen_t         *scr;
  const box_t      *content, *bounds;
  checker_pattern_t pattern;
  int               x, y, lx, ly, sx, sy, band_px, v, dv, band, r;

  cc = task_data;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;
  sx      = event->data.redraw.scroll.x;
  sy      = event->data.redraw.scroll.y;

  pattern = cc->pattern;
  band_px = cc->band;

  if (pattern == checker_PATTERN_CHECKERBOARD)
  {
    screen_fill_checks(scr, content, POINT(bounds->x0 - sx, bounds->y0 - sy),
                       band_px, cc->white, cc->black);
    return result_OK;
  }

  /* divide once per row to find the band at the row's first pixel, then
   * count along the row: v rises by dv per pixel */
  for (y = content->y0; y < content->y1; y++)
  {
    lx = content->x0 - bounds->x0 + sx;
    ly = y - bounds->y0 + sy;

    switch (pattern)
    {
    case checker_PATTERN_HORIZONTAL: v = ly;      dv = 0; break;
    case checker_PATTERN_VERTICAL:   v = lx;      dv = 1; break;
    default:                         v = lx + ly; dv = 1; break; /* DIAGONAL */
    }

    band = v / band_px;
    r    = v % band_px;

    for (x = content->x0; x < content->x1; x++)
    {
      screen_set_pixel(scr, x, y, (band & 1) ? cc->black : cc->white);

      r += dv;
      if (r == band_px)
      {
        r = 0;
        band++;
      }
    }
  }

  return result_OK;
}

/* step the pattern by dir (+1 or -1), wrapping round */
static result_t checker_mouse(int dir, void *task_data)
{
  checker_task_t *cc;

  cc = task_data;

  cc->pattern = (cc->pattern + checker_PATTERN__COUNT + dir) %
                checker_PATTERN__COUNT;

  wuss_window_invalidate_visible(cc->base.window);

  return result_OK;
}

static result_t checker_scroll(int delta, void *task_data)
{
  checker_task_t *cc;

  cc = task_data;

  cc->band += delta;
  cc->band  = CLAMP(cc->band, CHECKER_BAND_MIN, CHECKER_BAND_MAX);

  wuss_window_invalidate_visible(cc->base.window);

  return result_OK;
}

/* Left/Right step the pattern back/forward, as ADJUST and SELECT clicks do;
 * Up/Down widen/narrow its bands, as the wheel does. Anything else is passed
 * back unclaimed. */
static result_t checker_key(checker_task_t *cc, int code)
{
  switch (code)
  {
  case wuss_KEY_LEFT:  return checker_mouse(-1, cc);
  case wuss_KEY_RIGHT: return checker_mouse(+1, cc);
  case wuss_KEY_UP:    return checker_scroll(+1, cc);
  case wuss_KEY_DOWN:  return checker_scroll(-1, cc);
  default:             return result_WUSS_KEY_UNCLAIMED;
  }
}

/* Every submenu leaf fires wuss_EVENT_PRE_SUBMENU_OPEN; only the Ink and
 * Paper rows retarget the shared colourmenu. */
static result_t checker_pre_submenu_open(checker_task_t     *cc,
                                         const wuss_event_t *event)
{
  result_t rc;
  int      index;

  if (wuss_colourlist_pre_open(&cc->colours, cc->base.wuss, event, &rc))
    return rc;

  index = event->data.pre_submenu_open.index;

  return wuss_menu_open_submenu_now(event->data.pre_submenu_open.handle, index,
                                    cc->menu_items[index].submenu);
}

/* A colourmenu pick lands in whichever colour last opened it. */
static result_t checker_menu_select(checker_task_t     *cc,
                                    const wuss_event_t *event)
{
  colour_t        tmp;

  if (event->data.menu_select.menu == &cc->pattern_menu)
  {
    cc->pattern = event->data.menu_select.index;
    wuss_menu_tick_exclusive_live(cc->base.menu_handle, &cc->pattern_menu,
                                  event->data.menu_select.index);
    wuss_window_invalidate_visible(cc->base.window);
    return result_OK;
  }

  if (event->data.menu_select.menu == &cc->base.menu &&
      (event->data.menu_select.index == CHECKER_MENU_RANDOMISE ||
       event->data.menu_select.index == CHECKER_MENU_RESET))
  {
    if (event->data.menu_select.index == CHECKER_MENU_RANDOMISE)
      checker_randomise(cc);
    else
      checker_defaults(cc);

    wuss_menu_tick_exclusive_live(cc->base.menu_handle, &cc->pattern_menu,
                                  cc->pattern);
    wuss_window_invalidate_visible(cc->base.window);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &cc->base.menu, CHECKER_MENU_SWAP))
  {
    tmp       = cc->black;
    cc->black = cc->white;
    cc->white = tmp;
  }
  else if (!wuss_colourlist_selected(&cc->colours, event))
  {
    return result_OK;
  }

  wuss_window_invalidate_visible(cc->base.window);

  return result_OK;
}

static result_t checker_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data)
{
  checker_task_t *cc;

  NOT_USED(window);

  cc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return checker_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      wuss_menu_tick_exclusive(&cc->pattern_menu, cc->pattern);

      return wuss_menu_open_at_pointer(cc->base.delegate, &cc->base.menu,
                                       &cc->base.menu_handle);
    }
    if (event->data.mouse.button & wuss_BUTTON_SELECT)
      return checker_mouse(+1, task_data);
    if (event->data.mouse.button & wuss_BUTTON_ADJUST)
      return checker_mouse(-1, task_data);
    return result_OK;

  case wuss_EVENT_SCROLL:
    return checker_scroll(event->data.scroll.delta, task_data);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(cc->base.delegate, &cc->base.menu, event, &rc))
      return rc;

    return checker_key(cc, event->data.key.code);
  }

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return checker_pre_submenu_open(cc, event);

  case wuss_EVENT_MENU_SELECT:
    return checker_menu_select(cc, event);

  default:
    return result_OK;
  }
}

result_t checker_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Checker",
    .purpose       = "Cycling checkerboard pattern",
    .instance_size = sizeof(checker_task_t),
    .window_size   = { 160, 160 },
    .init          = (taskapp_init_fn_t) checker_init,
    .handle        = checker_handle,
    .save_name     = "checker.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
