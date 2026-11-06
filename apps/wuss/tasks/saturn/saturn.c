/* wuss/tasks/saturn/saturn.c -- Elite loading-screen planet, recreated */

#ifdef WUSS_APP

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/colour.h"
#include "framebuf/palettes.h"
#include "geom/box.h"
#include "geom/size.h"
#include "utils/maths.h"
#include "utils/rng.h"
/* ponytail: no #ifdef WUSS_COMPONENTS guard -- it is PUBLIC on WussLib and ON
 * by default, so the wuss app always has the colourmenu component. */
#include "wuss/component/colourmenu.h"
#include "wuss/component/dialogue.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/confdlg.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* window task: recreates the ringed planet from the loading screen of
 * Elite (Ian Bell and David Braben, Acornsoft, 1984). A cryptic BBC BASIC
 * one-liner stipples it out of three rejection-sampled point clouds - a
 * ring of dots, a sheared streak across it and a filled disc for the
 * planet body. SELECT re-seeds the RNG for a fresh sketch and ADJUST
 * steps back to the previous one; Menu > Pause, unticked, re-seeds every
 * null event so it churns; Menu > Randomise (^R) picks a fresh seed and
 * colours; Menu > Reset (R) restores the starting configuration, colours and seed;
 * Menu > Save opens a Save As dialogue (drag its
 * icon onto a Filer window, or Save with a full path already typed) that
 * writes the window's content out. Once a click has given the window the
 * input focus, Left/Right step back and forward through the sketches and
 * Space toggles Pause. The wheel zooms in and out about the centre. The plot
 * is deterministic in the seed. */
/* iteration counts for the three rejection-sampling loops, plus the window's
 * size, in document pixels (the window is always square); saturn_init
 * fills in SATURN_CONFIG_DEFAULT values when the caller passes NULL */
typedef struct saturn_config
{
  int stars_iters; /* loop 1: the ring */
  int ring_iters;  /* loop 2: ring shadow band */
  int body_iters;  /* loop 3: planet body */
  int size;        /* window size (both axes) */
}
saturn_config_t;

#define SATURN_CONFIG_DEFAULT { 477, 1280, 1280, 256 }

/* the "Size..." dialogue's slider: [min,max] and the step it snaps to */
#define SATURN_SIZE_MIN  128
#define SATURN_SIZE_MAX  512
#define SATURN_SIZE_STEP 64

/* zoom range/step for the wheel: 1.0 fits the window, larger zooms in about
 * the centre */
#define SATURN_ZOOM_MIN  0.3
#define SATURN_ZOOM_MAX  4.0
#define SATURN_ZOOM_STEP 0.1

typedef struct saturn_task
{
  taskapp_instance_t base;
  colour_t           bg, fg;
  unsigned long      seed;     /* RNG state; SELECT/ADJUST step it */
  saturn_config_t    config;
  confdlg_t          conf;
  wuss_colourlist_t  colours; /* per-instance: Foreground/Background rows */
  wuss_menu_item_t   menu_items[6];
  int                animate; /* Menu > Pause, inverted: re-seed on idle */
  double             zoom;    /* wheel-adjustable */
  rng_t              rng;     /* Randomise */
}
saturn_task_t;

/* A recreation of the ringed planet from the loading screen of Elite (Ian
 * Bell and David Braben, Acornsoft, 1984), the stippled Saturn-like world
 * the BBC Micro drew while the game loaded from tape.
 *
 * The source is one line of BBC BASIC (MODE 4, VDU5) that seeds the RNG from
 * TIME then runs three FOR loops, each rejection-sampling random points and
 * PLOT 69 (plot single point) at the survivors. In MODE 4 the plot area is
 * 1280x1024 OS units, so the original scales its -128..127 sample space by 4
 * and plots each point at y = (255 - (v + 128)). RISC OS screen coords run
 * bottom-up and wuss's run top-down, so that same expression - kept verbatim
 * here, only without the *4 - lands the sketch the same way up as it drew on
 * a BBC/RISC OS screen. The shear in loop 2 is asymmetric in y, so this is
 * the term that actually decides which way the ring's shadow tilts.
 *
 *   loop 1: X%,Y% in -127..127; keep if (X*X+Y*Y)/256 > 17  -> the stars
 *   loop 2: sheared X% = R6+R5/4, Y% = R6; keep if 32<=E<80 and
 *           (R5<0 or (X*X+Y*Y)/256 > 16)                    -> the ring
 *   loop 3: R1,R2 random; keep if R1*R1+R2*R2 < 16384;
 *           x = sqrt(16384-P)/2                             -> planet body
 */

/* the sample space matches task->config.size (square), scaling the original
 * -128..127 signed-byte range up or down with it; SATURN_SIZE_DEFAULT is
 * only the value saturn_init falls back to when the caller passes NULL
 * (see SATURN_CONFIG_DEFAULT in saturn.h). */
#define SATURN_SIZE_DEFAULT 256

/* The original works in a -128..127 sample space (BBC BASIC signed byte),
 * here scaled to whatever size is live; saturn_redraw derives these per-call
 * as half/range/flip/energy_shift from task->config.size. */

#define SATURN_BODY_R2(half) ((half) * (half)) /* planet body: keep if r1*r1+r2*r2 < this */

/* BBC BASIC RND(n>0) returns an integer 1..n. utils/rng's LCG stands in for
 * it (its high bits, which is where an LCG's usable randomness sits) so a
 * SELECT click can re-seed for a fresh sketch. Every draw in a redraw shares
 * n, so the seed fixes n too and keeps its reciprocal, letting div_recip take
 * the remainder without a division per sample. */
static struct
{
  rng_t        rnd_state;
  unsigned int n;
  unsigned int recip;
}
g;

static void saturn_rnd_seed(unsigned long seed, int n)
{
  rng_seed(&g.rnd_state, (uint32_t) seed);
  g.n     = (unsigned int) n;
  g.recip = DIV_RECIP(g.n);
}

static int saturn_rnd(void)
{
  unsigned int v;

  v = rng_lcg32(&g.rnd_state) >> 16;
  return (int) (v - div_recip(v, g.n, g.recip) * g.n) + 1;
}

/* one signed sample in the original -128..127 space, scaled to half */
#define SATURN_SAMPLE(half) (saturn_rnd() - (half))

/* MENU click over the content pops this. "Colours" leads to a submenu with
 * one row per task->fg/task->bg (see task->colours). "Configuration" hover-opens
 * task->conf.dialogue's window, a borrowed window built once in
 * saturn_init and stored into task->menu_items[SATURN_MENU_SIZE].window
 * there. */
enum
{
  SATURN_MENU_PAUSE = 0,
  SATURN_MENU_SAVE,
  SATURN_MENU_SIZE,
  SATURN_MENU_COLOURS,
  SATURN_MENU_RANDOMISE,
  SATURN_MENU_RESET
};

#define SATURN_SEED_STEP 0x9E3779B9UL /* golden-ratio step between sketches */

static const saturn_config_t g_saturn_default_config = SATURN_CONFIG_DEFAULT;

/* Size stays first: confdlg's applied callback gets its old value */
static const confdlg_row_t g_saturn_rows[] =
{
  { "Size",  SATURN_SIZE_MIN, SATURN_SIZE_MAX, SATURN_SIZE_STEP, offsetof(saturn_config_t, size) },
  { "Stars", 1, 9999, 0, offsetof(saturn_config_t, stars_iters) },
  { "Ring",  1, 9999, 0, offsetof(saturn_config_t, ring_iters) },
  { "Body",  1, 9999, 0, offsetof(saturn_config_t, body_iters) },
};

static result_t saturn_resize(saturn_task_t *task, int old_size);

static const confdlg_desc_t g_saturn_conf =
{
  g_saturn_rows, NELEMS(g_saturn_rows),
  &g_saturn_default_config, sizeof(g_saturn_default_config),
  (confdlg_applied_fn_t) saturn_resize
};

/* The item table and wuss_menu_t values live per-instance in saturn_task_t,
 * not as file-scope statics, so that each window's Configuration row points
 * at its own .window pointer rather than every instance sharing (and
 * overwriting) one global .window pointer. */

/* the starting sketch, as saturn_init and Reset restore it */
static void saturn_defaults(saturn_task_t *task)
{
  task->bg     = colour_rgb(0x00, 0x00, 0x00);
  task->fg      = colour_rgb(0xFF, 0xFF, 0xFF);
  task->seed    = 1;
  task->config  = g_saturn_default_config;
  task->animate = 0;
  task->zoom    = 1.0;
}

static result_t saturn_init(saturn_task_t *task)
{
  result_t rc;
  wuss_t  *wuss;

  wuss = task->base.wuss;

  rng_seed(&task->rng, (uint32_t) rand());
  saturn_defaults(task);

  wuss_colourlist_init_pair(&task->colours, wuss, &task->fg, &task->bg);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, SATURN_MENU_PAUSE, "Pause",
                          wuss_MENU_ITEM_NONE, "SPACE");

  taskapp_save_row(&task->base, task->menu_items, SATURN_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_ITEM(task->menu_items, SATURN_MENU_SIZE, "Configuration",
                wuss_MENU_ITEM_DASHED | wuss_MENU_ITEM_BORROWED_SUBMENU |
                wuss_MENU_ITEM_PRE_OPEN);

  WUSS_MENU_ITEM_MENU(task->menu_items, SATURN_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->colours.menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, SATURN_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_DASHED, "^R");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, SATURN_MENU_RESET, "Reset",
                          wuss_MENU_ITEM_NONE, "R");

  WUSS_MENU_TITLE(task->base.menu, "Saturn", task->menu_items,
                 NELEMS(task->menu_items));

  /* saturn_redraw paints its own background */
  rc = task_window_create(task->base.delegate,
                          SIZE2D(task->config.size, task->config.size),
                          "Saturn",
                          &task->base.window);
  if (rc != result_OK)
    return rc;

  rc = confdlg_create(&task->conf, &task->base, &g_saturn_conf, &task->config);
  if (rc != result_OK)
    return rc;

  task->menu_items[SATURN_MENU_SIZE].window =
    wuss_dialogue_window(task->conf.dialogue);

  return result_OK;
}

/* plot one point in window content space, clipped to the window. x,y are
 * already in the window's top-down pixel space (callers apply the RISC OS
 * (255 - ...) flip). */
static void saturn_plot(screen_t      *scr,
                        int            ox,
                        int            oy,
                        int            size,
                        int            x,
                        int            y,
                        double         zoom,
                        pixelfmt_any_t pxl)
{
  x = (int) ((x - size / 2.0) * zoom + size / 2.0);
  y = (int) ((y - size / 2.0) * zoom + size / 2.0);

  if (x < 0 || x >= size || y < 0 || y >= size)
    return;

  screen_set_pixel_value(scr, ox + x, oy + y, pxl);
}

static result_t saturn_redraw(const wuss_event_t *event, saturn_task_t *task)
{
  screen_t      *scr;
  const box_t   *content, *bounds;
  int            size, half, range, flip, energy_shift;
  int            stars_p, ring_p, e_lo, e_hi;
  int            ox, oy;
  int            i;
  int            x, y, p;
  pixelfmt_any_t fgpix;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;

  screen_fill_rect(scr, content->x0, content->y0, box_size(content), task->bg);

  /* sample space scales with the window: -size/2..size/2-1, matching the
   * original's -128..127 at task->config.size == SATURN_SIZE_DEFAULT */
  size         = task->config.size;
  half         = size / 2;
  range        = size - 1;
  flip         = size - 1;
  energy_shift = size;

  /* p's range grows linearly with size (p_max = size/2), so the original's
   * fixed energy-gate constants (17, 16, 32, 80 at size==256) must scale
   * with size too, or the exclusion disc shrinks relative to half as size
   * grows and the stars/ring creep into the planet body. */
  stars_p = 17 * size / SATURN_SIZE_DEFAULT;
  ring_p  = 16 * size / SATURN_SIZE_DEFAULT;
  e_lo    = 32 * size / SATURN_SIZE_DEFAULT;
  e_hi    = 80 * size / SATURN_SIZE_DEFAULT;

  /* plot in doc space (saturn_plot clips to 0..size); origin carries the
   * scroll so a scrolled/shrunk window shows the right slice */
  ox = bounds->x0 - event->data.redraw.scroll.x;
  oy = bounds->y0 - event->data.redraw.scroll.y;

  /* resolve the colour once, not once per plotted point */
  fgpix = screen_colour_to_pixel(scr, task->fg);

  saturn_rnd_seed(task->seed, range);

  /* The gates compare energies divided by energy_shift, rounded down. The
   * energies are sums of squares, so never negative, and
   * floor(s / k) > p exactly when s >= (p + 1) * k: scale the thresholds
   * once and compare the sums undivided. */
  stars_p = (stars_p + 1) * energy_shift;
  ring_p  = (ring_p + 1) * energy_shift;
  e_lo    = e_lo * energy_shift;
  e_hi    = e_hi * energy_shift;

  /* loop 1 - the stars: keep points outside the inner disc */
  for (i = 0; i <= task->config.stars_iters; i++)
  {
    x = SATURN_SAMPLE(half);
    y = SATURN_SAMPLE(half);
    p = x * x + y * y;
    if (p >= stars_p)
      saturn_plot(scr, ox, oy, size, x + half, flip - (y + half), task->zoom,
                  fgpix);
  }

  /* loop 2 - the ring: sheared sample with a banded energy gate */
  for (i = 0; i <= task->config.ring_iters; i++)
  {
    int r5, r6, r7, e;

    r5 = SATURN_SAMPLE(half);
    r6 = SATURN_SAMPLE(half);
    r7 = r5 / 4;
    x  = r6 + r7;
    y  = r6;
    p  = x * x + y * y;
    e  = (r6 + r7) * (r6 + r7) + r5 * r5 + r6 * r6;
    if (e >= e_lo && e < e_hi && (r5 < 0 || p >= ring_p))
      saturn_plot(scr, ox, oy, size, x + half, y + half, task->zoom, fgpix);
  }

  /* loop 3 - planet body: filled half-disc offset right */
  for (i = 0; i <= task->config.body_iters; i++)
  {
    int r1, r2, body_r2;

    r1      = SATURN_SAMPLE(half);
    r2      = SATURN_SAMPLE(half);
    p       = r1 * r1 + r2 * r2;
    body_r2 = SATURN_BODY_R2(half);
    if (p < body_r2)
    {
      x = (int) (sqrt((double) (body_r2 - p)) / 2.0) + half;
      y = flip - (r2 / 2 + half);
      saturn_plot(scr, ox, oy, size, x, y, task->zoom, fgpix);
    }
  }

  return result_OK;
}

static result_t saturn_scroll(saturn_task_t *task, int delta)
{
  task->zoom += (delta > 0 ? 1 : -1) * SATURN_ZOOM_STEP;
  task->zoom  = CLAMP(task->zoom, SATURN_ZOOM_MIN, SATURN_ZOOM_MAX);

  wuss_window_invalidate_visible(task->base.window);

  return result_OK;
}

static result_t saturn_mouse(saturn_task_t      *task,
                             wuss_mouse_action_t action,
                             wuss_button_t       button,
                             wuss_window_t      *window)
{
  if (action != wuss_MOUSE_DOWN)
    return result_OK;

  if (button & wuss_BUTTON_MENU)
  {
    wuss_menu_tick_item(&task->base.menu, SATURN_MENU_PAUSE, !task->animate);

    return wuss_menu_open_at_pointer(task->base.delegate, &task->base.menu,
                                     &task->base.menu_handle);
  }

  /* the seed step is invertible, so ADJUST walks back through the
   * sketches SELECT has shown */
  if (button & wuss_BUTTON_SELECT)
  {
    task->seed += SATURN_SEED_STEP; /* fresh sketch */
    wuss_window_invalidate_visible(window);
  }
  else if (button & wuss_BUTTON_ADJUST)
  {
    task->seed -= SATURN_SEED_STEP; /* previous sketch */
    wuss_window_invalidate_visible(window);
  }

  return result_OK;
}

/* Left/Right step back/forward through the sketches, as ADJUST/SELECT
 * clicks do. Anything else is passed back unclaimed. */
static result_t saturn_key(saturn_task_t *task,
                           wuss_window_t *window,
                           int            code)
{
  switch (code)
  {
  case wuss_KEY_LEFT:  task->seed -= SATURN_SEED_STEP; break;
  case wuss_KEY_RIGHT: task->seed += SATURN_SEED_STEP; break;

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }

  wuss_window_invalidate_visible(window);

  return result_OK;
}

/* Resizes task->base.window (size + doc extent) if config.size has moved off
 * old_size, else just redraws. */
static result_t saturn_resize(saturn_task_t *task, int old_size)
{
  result_t rc;
  int      size_value;

  if (task->base.window == NULL)
    return result_OK; /* planet window closed while the dialogue lingers */

  size_value = task->config.size;
  if (size_value == old_size)
  {
    wuss_window_invalidate_visible(task->base.window); /* iterations changed */
    return result_OK;
  }

  rc = wuss_window_resize(task->base.window, SIZE2D(size_value, size_value));
  if (rc != result_OK)
    return rc;
  return wuss_window_set_doc(task->base.window, SIZE2D(size_value, size_value));
}

/* Every submenu leaf fires wuss_EVENT_PRE_SUBMENU_OPEN, not just the colour
 * pair's -- the rest open their own submenu unchanged. */
static result_t saturn_pre_submenu_open(saturn_task_t      *task,
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

/* Unless Menu > Pause is ticked: a fresh sketch every null event. The
 * Configuration window shares this (autoclose) delegate, so the task can
 * outlive the planet window; skip the dangling window in the meantime. */
static result_t saturn_idle(saturn_task_t *task)
{
  if (!task->animate || task->base.window == NULL)
    return result_OK;

  task->seed += SATURN_SEED_STEP;
  wuss_window_invalidate_visible(task->base.window);

  return result_OK;
}

static result_t saturn_menu_select(saturn_task_t      *task,
                                   const wuss_event_t *event)
{
  if (wuss_menu_select_is(event, &task->base.menu, SATURN_MENU_PAUSE))
  {
    task->animate = !task->animate;
    wuss_menu_tick_item_live(task->base.menu_handle, &task->base.menu,
                             SATURN_MENU_PAUSE, !task->animate);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &task->base.menu, SATURN_MENU_RANDOMISE))
  {
    task->seed = rng_xorshift32(&task->rng);
    task->bg   = colour_random(&task->rng);
    task->fg   = colour_random_contrast(&task->rng, task->bg);
    wuss_window_invalidate_visible(task->base.window);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &task->base.menu, SATURN_MENU_RESET))
  {
    int old_size;

    old_size = task->config.size;
    saturn_defaults(task);
    wuss_menu_tick_item_live(task->base.menu_handle, &task->base.menu,
                             SATURN_MENU_PAUSE, !task->animate);
    return saturn_resize(task, old_size);
  }

  if (wuss_colourlist_selected(&task->colours, event))
    wuss_window_invalidate_visible(task->base.window);

  return result_OK;
}

static result_t saturn_handle(wuss_window_t      *window,
                              const wuss_event_t *event,
                              void               *task_data)
{
  saturn_task_t *task;

  task = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return saturn_redraw(event, task);

  case wuss_EVENT_MOUSE:
    return saturn_mouse(task, event->data.mouse.action,
                        event->data.mouse.button, window);

  case wuss_EVENT_SCROLL:
    return saturn_scroll(task, event->data.scroll.delta);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(task->base.delegate, &task->base.menu, event, &rc))
      return rc;

    return saturn_key(task, window, event->data.key.code);
  }

  case wuss_EVENT_ICON:
  case wuss_EVENT_PRE_SHOW:
  case wuss_EVENT_CLOSE:
  case wuss_EVENT_QUIT:
    return confdlg_event(&task->conf, window, event);

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return saturn_pre_submenu_open(task, event);

  case wuss_EVENT_MENU_SELECT:
    return saturn_menu_select(task, event);

  case wuss_EVENT_IDLE:
    return saturn_idle(task);

  default:
    return result_OK;
  }
}

result_t saturn_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Saturn",
    .purpose       = "Elite loading-screen planet",
    .instance_size = sizeof(saturn_task_t),
    .init          = (taskapp_init_fn_t) saturn_init,
    .handle        = saturn_handle,
    .save_name     = "saturn.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
