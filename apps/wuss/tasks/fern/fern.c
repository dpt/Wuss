/* wuss/tasks/fern/fern.c -- parameterised Barnsley fern */

#ifdef WUSS_APP

#include <stddef.h>
#include <stdlib.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/colour.h"
#include "geom/box.h"
#include "geom/size.h"
#include "utils/rng.h"
#include "wuss/component/colourmenu.h"
#include "wuss/component/dialogue.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/confdlg.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* window task: plots Michael Barnsley's iterated-function-system fern.
 * Menu > Configuration sets the window size, the number of points plotted
 * and the curl (a percentage scaling the small shear terms of the main frond
 * transform: 100 is Barnsley's fern, 0 a straight stem, higher bends it
 * over). SELECT re-seeds for a fresh fern and ADJUST steps back; Left/Right
 * step back and forward once the window has the input focus, and Space toggles
 * Menu > Pause; unticked, it re-seeds every null event. The wheel zooms in and
 * out about the centre. Menu > Colours sets the
 * foreground and background. Menu > Randomise (^R) picks a fresh seed,
 * curl and colours; Menu > Reset (R) restores the starting configuration,
 * colours and seed. Menu > Save writes the window's content
 * out. The plot is deterministic in the seed. */
typedef struct fern_config
{
  int size;   /* window size (both axes) */
  int points; /* points plotted */
  int curl;   /* percent, 100 = Barnsley's coefficients */
}
fern_config_t;

#define FERN_CONFIG_DEFAULT { 256, 20000, 100 }

#define FERN_SIZE_MIN  128
#define FERN_SIZE_MAX  512
#define FERN_SIZE_STEP 64

/* zoom range/step for the wheel: 1.0 fits the window, larger zooms in about
 * the centre */
#define FERN_ZOOM_MIN  0.3
#define FERN_ZOOM_MAX  4.0
#define FERN_ZOOM_STEP 0.1

typedef struct fern_task
{
  taskapp_instance_t base;
  fern_config_t      config;
  colour_t           bg, fg;
  unsigned long      seed;        /* RNG state; SELECT/ADJUST step it */
  confdlg_t          conf;
  wuss_colourlist_t  colours; /* per-instance, see saturn.c */
  wuss_menu_item_t   menu_items[6];
  int             animate;     /* Menu > Pause, inverted: re-seed on idle */
  double             zoom;        /* wheel-adjustable */
  rng_t              rng;         /* Randomise */
}
fern_task_t;

/* Barnsley's fern: start at (0,0) and repeatedly apply one of four affine
 * maps chosen at random (1%, 85%, 7%, 7%), plotting each result. The fern
 * lies in x -2.182..2.656, y 0..9.998. config.curl scales the 0.04 shear
 * terms of the 85% map. */
#define FERN_X_MIN (-2.2)
#define FERN_Y_MAX 10.0
#define FERN_SEED_STEP 0x9E3779B9UL /* golden-ratio step between ferns */

enum
{
  FERN_MENU_PAUSE = 0,
  FERN_MENU_SAVE,
  FERN_MENU_SIZE,
  FERN_MENU_COLOURS,
  FERN_MENU_RANDOMISE,
  FERN_MENU_RESET
};

static const fern_config_t g_fern_default_config = FERN_CONFIG_DEFAULT;

/* Size stays first: confdlg's applied callback gets its old value */
static const confdlg_row_t g_fern_rows[] =
{
  { "Size",   FERN_SIZE_MIN, FERN_SIZE_MAX, FERN_SIZE_STEP, offsetof(fern_config_t, size) },
  { "Points", 100, 100000, 0, offsetof(fern_config_t, points) },
  { "Curl",   -300, 300, 0, offsetof(fern_config_t, curl) },
};

static result_t fern_resize(fern_task_t *task, int old_size);

static const confdlg_desc_t g_fern_conf =
{
  g_fern_rows, NELEMS(g_fern_rows),
  &g_fern_default_config, sizeof(g_fern_default_config),
  (confdlg_applied_fn_t) fern_resize
};

/* the starting fern, as fern_init and Reset restore it */
static void fern_defaults(fern_task_t *task)
{
  task->config  = g_fern_default_config;
  task->bg      = colour_rgb(0x00, 0x00, 0x00);
  task->fg      = colour_rgb(0x40, 0xE0, 0x40);
  task->seed    = 1;
  task->animate = 0;
  task->zoom    = 1.0;
}

static result_t fern_init(fern_task_t *task)
{
  result_t rc;
  wuss_t  *wuss;

  wuss = task->base.wuss;
  rng_seed(&task->rng, (uint32_t) rand());
  fern_defaults(task);

  wuss_colourlist_init_pair(&task->colours, wuss, &task->fg, &task->bg);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, FERN_MENU_PAUSE, "Pause",
                          wuss_MENU_ITEM_NONE, "SPACE");

  taskapp_save_row(&task->base, task->menu_items, FERN_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_ITEM(task->menu_items, FERN_MENU_SIZE, "Configuration",
                 wuss_MENU_ITEM_DASHED | wuss_MENU_ITEM_BORROWED_SUBMENU |
                 wuss_MENU_ITEM_PRE_OPEN);

  WUSS_MENU_ITEM_MENU(task->menu_items, FERN_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->colours.menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, FERN_MENU_RANDOMISE, "Randomise",
                          wuss_MENU_ITEM_DASHED, "^R");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, FERN_MENU_RESET, "Reset",
                          wuss_MENU_ITEM_NONE, "R");

  WUSS_MENU_TITLE(task->base.menu, "Fern", task->menu_items,
                  NELEMS(task->menu_items));

  /* the window's size comes from the config */
  rc = task_window_create(task->base.delegate,
                          SIZE2D(task->config.size, task->config.size),
                          "Fern", &task->base.window);
  if (rc != result_OK)
    return rc;

  rc = confdlg_create(&task->conf, &task->base, &g_fern_conf, &task->config);
  if (rc != result_OK)
    return rc;

  task->menu_items[FERN_MENU_SIZE].window =
    wuss_dialogue_window(task->conf.dialogue);

  return result_OK;
}

static result_t fern_redraw(const wuss_event_t *event, fern_task_t *task)
{
  rng_t          rng;
  screen_t      *scr;
  const box_t   *content, *bounds;
  int            size, ox, oy, i, px, py, r;
  double         x, y, nx, ny, shear, scale;
  pixelfmt_any_t pix;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;

  screen_fill_rect(scr, content->x0, content->y0, box_size(content),
                   task->bg);

  size  = task->config.size;
  ox    = bounds->x0 - event->data.redraw.scroll.x;
  oy    = bounds->y0 - event->data.redraw.scroll.y;
  shear = 0.04 * task->config.curl / 100.0;
  scale = size / FERN_Y_MAX; /* fern is taller than wide: fit its height */
  pix   = screen_colour_to_pixel(scr, task->fg);

  rng_seed(&rng, (uint32_t) task->seed);
  x = y = 0.0;
  for (i = 0; i < task->config.points; i++)
  {
    r = (int) ((rng_lcg32(&rng) >> 16) % 100);
    if (r < 1)
    {
      nx = 0.0;
      ny = 0.16 * y;
    }
    else if (r < 86)
    {
      nx = 0.85 * x + shear * y;
      ny = -shear * x + 0.85 * y + 1.6;
    }
    else if (r < 93)
    {
      nx = 0.2 * x - 0.26 * y;
      ny = 0.23 * x + 0.22 * y + 1.6;
    }
    else
    {
      nx = -0.15 * x + 0.28 * y;
      ny = 0.26 * x + 0.24 * y + 0.44;
    }
    x = nx;
    y = ny;

    px = (int) (((x - FERN_X_MIN) * scale - size / 2.0) * task->zoom +
                size / 2.0);
    py = (int) ((size - 1 - y * scale - size / 2.0) * task->zoom +
                size / 2.0);
    if (px >= 0 && px < size && py >= 0 && py < size)
      screen_set_pixel_value(scr, ox + px, oy + py, pix);
  }

  return result_OK;
}

static result_t fern_scroll(fern_task_t *task, int delta)
{
  task->zoom += (delta > 0 ? 1 : -1) * FERN_ZOOM_STEP;
  task->zoom  = CLAMP(task->zoom, FERN_ZOOM_MIN, FERN_ZOOM_MAX);

  wuss_window_invalidate_visible(task->base.window);

  return result_OK;
}

static result_t fern_mouse(fern_task_t        *task,
                           wuss_mouse_action_t action,
                           wuss_button_t       button)
{
  if (action != wuss_MOUSE_DOWN)
    return result_OK;

  if (button & wuss_BUTTON_MENU)
  {
    wuss_menu_tick_item(&task->base.menu, FERN_MENU_PAUSE, !task->animate);

    return wuss_menu_open_at_pointer(task->base.delegate, &task->base.menu,
                                     &task->base.menu_handle);
  }

  /* the seed step is invertible, so ADJUST walks back through the ferns */
  if (button & wuss_BUTTON_SELECT)
    task->seed += FERN_SEED_STEP;
  else if (button & wuss_BUTTON_ADJUST)
    task->seed -= FERN_SEED_STEP;
  else
    return result_OK;

  wuss_window_invalidate_visible(task->base.window);

  return result_OK;
}

/* Left/Right step back/forward through the ferns; anything else is passed
 * back unclaimed. */
static result_t fern_key(fern_task_t *task, wuss_window_t *window, int code)
{
  switch (code)
  {
  case wuss_KEY_LEFT:  task->seed -= FERN_SEED_STEP; break;
  case wuss_KEY_RIGHT: task->seed += FERN_SEED_STEP; break;

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }

  wuss_window_invalidate_visible(window);

  return result_OK;
}

/* Unless Menu > Pause is ticked: a fresh fern every null event. The
 * Configuration window shares this delegate, so the task can outlive the fern
 * window. */
static result_t fern_idle(fern_task_t *task)
{
  if (!task->animate || task->base.window == NULL)
    return result_OK;

  task->seed += FERN_SEED_STEP;
  wuss_window_invalidate_visible(task->base.window);

  return result_OK;
}

/* Every submenu leaf fires wuss_EVENT_PRE_SUBMENU_OPEN; only the colour
 * pair's retargets the shared colourmenu. */
static result_t fern_pre_submenu_open(fern_task_t        *task,
                                      const wuss_event_t *event)
{
  result_t rc;
  int      index;

  if (wuss_colourlist_pre_open(&task->colours, task->base.wuss, event, &rc))
    return rc;

  index = event->data.pre_submenu_open.index;

  return wuss_menu_open_submenu_now(event->data.pre_submenu_open.handle, index,
                                    task->menu_items[index].submenu);
}

/* resize the window if config.size has moved off old_size, then redraw */
static result_t fern_resize(fern_task_t *task, int old_size)
{
  result_t rc;
  int      size;

  if (task->base.window == NULL)
    return result_OK; /* fern window closed while the dialogue lingers */

  size = task->config.size;
  if (size != old_size)
  {
    rc = wuss_window_resize(task->base.window, SIZE2D(size, size));
    if (rc != result_OK)
      return rc;
    rc = wuss_window_set_doc(task->base.window, SIZE2D(size, size));
    if (rc != result_OK)
      return rc;
  }

  wuss_window_invalidate_visible(task->base.window);

  return result_OK;
}

static result_t fern_menu_select(fern_task_t        *task,
                                 const wuss_event_t *event)
{
  if (wuss_menu_select_is(event, &task->base.menu, FERN_MENU_PAUSE))
  {
    task->animate = !task->animate;
    wuss_menu_tick_item_live(task->base.menu_handle, &task->base.menu,
                             FERN_MENU_PAUSE, !task->animate);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &task->base.menu, FERN_MENU_RANDOMISE))
  {
    task->seed        = rng_xorshift32(&task->rng);
    task->config.curl = rng_range(&task->rng, 601) - 300;
    task->bg          = colour_random(&task->rng);
    task->fg          = colour_random_contrast(&task->rng, task->bg);
    wuss_window_invalidate_visible(task->base.window);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &task->base.menu, FERN_MENU_RESET))
  {
    int old_size;

    old_size = task->config.size;
    fern_defaults(task);
    wuss_menu_tick_item_live(task->base.menu_handle, &task->base.menu,
                             FERN_MENU_PAUSE, !task->animate);
    return fern_resize(task, old_size);
  }

  if (wuss_colourlist_selected(&task->colours, event))
    wuss_window_invalidate_visible(task->base.window);

  return result_OK;
}

static result_t fern_handle(wuss_window_t      *window,
                            const wuss_event_t *event,
                            void               *task_data)
{
  fern_task_t *task;

  task = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return fern_redraw(event, task);

  case wuss_EVENT_MOUSE:
    return fern_mouse(task, event->data.mouse.action,
                      event->data.mouse.button);

  case wuss_EVENT_SCROLL:
    return fern_scroll(task, event->data.scroll.delta);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(task->base.delegate, &task->base.menu, event, &rc))
      return rc;

    return fern_key(task, window, event->data.key.code);
  }

  case wuss_EVENT_ICON:
  case wuss_EVENT_PRE_SHOW:
  case wuss_EVENT_CLOSE:
  case wuss_EVENT_QUIT:
    return confdlg_event(&task->conf, window, event);

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return fern_pre_submenu_open(task, event);

  case wuss_EVENT_MENU_SELECT:
    return fern_menu_select(task, event);

  case wuss_EVENT_IDLE:
    return fern_idle(task);

  default:
    return result_OK;
  }
}

result_t fern_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Fern",
    .purpose       = "Barnsley fern",
    .instance_size = sizeof(fern_task_t),
    .init          = (taskapp_init_fn_t) fern_init,
    .handle        = fern_handle,
    .save_name     = "fern.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
