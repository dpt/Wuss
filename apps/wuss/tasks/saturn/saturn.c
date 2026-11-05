/* wuss/tasks/saturn/saturn.c -- Elite loading-screen planet, recreated */

#ifdef WUSS_APP

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/colour.h"
#include "framebuf/palettes.h"
#include "geom/box.h"
#include "geom/size.h"
#include "geom/stack.h"
#include "utils/maths.h"
#include "utils/rng.h"
/* ponytail: no #ifdef WUSS_COMPONENTS guard -- it is PUBLIC on WussLib and ON
 * by default, so the wuss app always has the colourmenu component. */
#include "wuss/component/colourmenu.h"
#include "wuss/component/dialogue.h"
#include "wuss/icon-spec.h"
#include "wuss/icon.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* window task: recreates the ringed planet from the loading screen of
 * Elite (Ian Bell and David Braben, Acornsoft, 1984). A cryptic BBC BASIC
 * one-liner stipples it out of three rejection-sampled point clouds - a
 * ring of dots, a sheared streak across it and a filled disc for the
 * planet body. Select re-seeds the RNG for a fresh sketch and Adjust
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

/* the size dialogue's slider rows, in creation/layout order */
enum
{
  SATURN_SIZEDLG_ROW_SIZE = 0,
  SATURN_SIZEDLG_ROW_STARS,
  SATURN_SIZEDLG_ROW_RING,
  SATURN_SIZEDLG_ROW_BODY,

  SATURN_SIZEDLG_NROWS
};

/* the size dialogue: built once, hidden; a menu leaf (see
 * saturn_task_t.menu_items[SATURN_MENU_SIZE].window) */
typedef struct saturn_conf
{
  wuss_dialogue_t   *dialogue;
  wuss_slider_row_t  rows[SATURN_SIZEDLG_NROWS]; /* size, stars, ring, body */
  wuss_icon_t       *deflt;
  wuss_icon_t       *cancel;
  wuss_icon_t       *apply;
}
saturn_conf_t;

/* zoom range/step for the wheel: 1.0 fits the window, larger zooms in about
 * the centre */
#define SATURN_ZOOM_MIN  0.3
#define SATURN_ZOOM_MAX  4.0
#define SATURN_ZOOM_STEP 0.1

typedef struct saturn_task
{
  taskapp_instance_t base;
  colour_t           bg, fg;
  unsigned long      seed;     /* RNG state; Select/Adjust step it */
  saturn_config_t    config;
  saturn_conf_t      conf;
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
 * Select click can re-seed for a fresh sketch. Every draw in a redraw shares
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

/* the size dialogue's icons, in creation order (see saturn_conf_dialogue_create):
 * one label/slider/value triple per SATURN_SIZEDLG_ROW_*, then the buttons */
enum {
  SATURN_SIZE_ICON_LABEL1 = 0,
  SATURN_SIZE_ICON_SLIDER1,
  SATURN_SIZE_ICON_VALUE1,

  SATURN_SIZE_ICON_LABEL2,
  SATURN_SIZE_ICON_SLIDER2,
  SATURN_SIZE_ICON_VALUE2,

  SATURN_SIZE_ICON_LABEL3,
  SATURN_SIZE_ICON_SLIDER3,
  SATURN_SIZE_ICON_VALUE3,

  SATURN_SIZE_ICON_LABEL4,
  SATURN_SIZE_ICON_SLIDER4,
  SATURN_SIZE_ICON_VALUE4,

  SATURN_SIZE_ICON_DEFAULT,
  SATURN_SIZE_ICON_CANCEL,
  SATURN_SIZE_ICON_APPLY,

  SATURN_SIZE_NICONS
};

/* per-row static description, indexed by SATURN_SIZEDLG_ROW_*: label text,
 * slider [min,max] and the byte offset of the saturn_config_t field the row
 * reads/writes (via *(int *) ((char *) &task->config + offset)), shared by
 * saturn_conf_dialogue_create (to fill specs) and
 * saturn_sizedlg_pre_show/saturn_sizedlg_apply (to sync task->config) */
typedef struct saturn_sizedlg_rowdesc
{
  const char *label;
  int         min, max, step;
  size_t      config_offset;
}
saturn_sizedlg_rowdesc_t;

static const saturn_sizedlg_rowdesc_t g_saturn_sizedlg_rows[SATURN_SIZEDLG_NROWS] =
{
  [SATURN_SIZEDLG_ROW_SIZE]  = { "Size",  SATURN_SIZE_MIN, SATURN_SIZE_MAX, SATURN_SIZE_STEP, offsetof(saturn_config_t, size) },
  [SATURN_SIZEDLG_ROW_STARS] = { "Stars", 1, 9999, 0, offsetof(saturn_config_t, stars_iters) },
  [SATURN_SIZEDLG_ROW_RING]  = { "Ring",  1, 9999, 0, offsetof(saturn_config_t, ring_iters) },
  [SATURN_SIZEDLG_ROW_BODY]  = { "Body",  1, 9999, 0, offsetof(saturn_config_t, body_iters) },
};

/* task->config field a row reads/writes, per g_saturn_sizedlg_rows */
static int *saturn_sizedlg_field(saturn_task_t *task, int row)
{
  return (int *) ((char *) &task->config + g_saturn_sizedlg_rows[row].config_offset);
}

/* The item table and wuss_menu_t values live per-instance in saturn_task_t,
 * not as file-scope statics, so that each window's Configuration row points
 * at its own .window pointer rather than every instance sharing (and
 * overwriting) one global .window pointer. */

static result_t saturn_conf_dialogue_create(saturn_task_t *task);
static result_t saturn_conf_fillout(void *opaque);
static result_t saturn_conf_cancel(void *opaque, wuss_button_t button);
static result_t saturn_conf_apply_action(void *opaque, wuss_button_t button);
static result_t saturn_conf_default_action(void         *opaque,
                                           wuss_button_t button);

/* the starting sketch, as saturn_init and Reset restore it */
static void saturn_defaults(saturn_task_t *task)
{
  static const saturn_config_t default_config = SATURN_CONFIG_DEFAULT;

  task->bg      = colour_rgb(0x00, 0x00, 0x00);
  task->fg      = colour_rgb(0xFF, 0xFF, 0xFF);
  task->seed    = 1;
  task->config  = default_config;
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

  rc = saturn_conf_dialogue_create(task);
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

  /* the seed step is invertible, so Adjust walks back through the
   * sketches Select has shown */
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

/* Left/Right step back/forward through the sketches, as Adjust/Select
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

/* stack items for the size dialogue's layout: a VBOX of label / slider /
 * value-echo rows above a button row. Labels share size group ST_G_LABEL and
 * values ST_G_VALUE, so each column is as wide as its widest member (their
 * axis_size is patched from the font in saturn_conf_dialogue_create). The
 * button row starts with a spacer in the label group, so the buttons sit
 * under the sliders and values and share that width between them. */
enum
{
  ST_ROOT,

  ST_ROW1,
  ST_LABL1,
  ST_SLDR1,
  ST_VAL1,

  ST_ROW2,
  ST_LABL2,
  ST_SLDR2,
  ST_VAL2,

  ST_ROW3,
  ST_LABL3,
  ST_SLDR3,
  ST_VAL3,

  ST_ROW4,
  ST_LABL4,
  ST_SLDR4,
  ST_VAL4,

  ST_BTNS,
  ST_BSPC,
  ST_BBOX,
  ST_DFLT,
  ST_CNCL,
  ST_APLY,

  SIZE_STACK__LIMIT
};

/* Leaf main-axis sizes, named so saturn_conf_dialogue_create's hand-computed
 * minimum window size can share them with the table below instead of
 * repeating the numbers as bare literals. */
#define ST_G_LABEL          1 /* size groups, see stack_item_t::group */
#define ST_G_VALUE          2
#define ST_SLIDER_MIN_W     (64)

static const stack_item_t g_saturn_conf_stack[SIZE_STACK__LIMIT] =
{
  [ST_ROOT]  = { .kind = stack_KIND_VBOX, .parent = -1,
                .gap = wuss_STD_GAP, .pad = wuss_STD_INSETS },

  [ST_ROW1]  = STACK_HBOX(ST_ROOT, wuss_STD_SLIDER_HEIGHT, wuss_STD_GAP, stack_ALIGN_START),
  [ST_LABL1] = STACK_LEAF_GROUP(ST_ROW1, 0, 16, stack_ALIGN_CENTRE, ST_G_LABEL),
  [ST_SLDR1] = STACK_LEAF_EX(ST_ROW1, 0, wuss_STD_SLIDER_HEIGHT, stack_ALIGN_CENTRE, 1, ST_SLIDER_MIN_W, 0),
  [ST_VAL1]  = STACK_LEAF_GROUP(ST_ROW1, 0, 16, stack_ALIGN_CENTRE, ST_G_VALUE),

  [ST_ROW2]  = STACK_HBOX(ST_ROOT, wuss_STD_SLIDER_HEIGHT, wuss_STD_GAP, stack_ALIGN_START),
  [ST_LABL2] = STACK_LEAF_GROUP(ST_ROW2, 0, 16, stack_ALIGN_CENTRE, ST_G_LABEL),
  [ST_SLDR2] = STACK_LEAF_EX(ST_ROW2, 0, wuss_STD_SLIDER_HEIGHT, stack_ALIGN_CENTRE, 1, ST_SLIDER_MIN_W, 0),
  [ST_VAL2]  = STACK_LEAF_GROUP(ST_ROW2, 0, 16, stack_ALIGN_CENTRE, ST_G_VALUE),

  [ST_ROW3]  = STACK_HBOX(ST_ROOT, wuss_STD_SLIDER_HEIGHT, wuss_STD_GAP, stack_ALIGN_START),
  [ST_LABL3] = STACK_LEAF_GROUP(ST_ROW3, 0, 16, stack_ALIGN_CENTRE, ST_G_LABEL),
  [ST_SLDR3] = STACK_LEAF_EX(ST_ROW3, 0, wuss_STD_SLIDER_HEIGHT, stack_ALIGN_CENTRE, 1, ST_SLIDER_MIN_W, 0),
  [ST_VAL3]  = STACK_LEAF_GROUP(ST_ROW3, 0, 16, stack_ALIGN_CENTRE, ST_G_VALUE),

  [ST_ROW4]  = STACK_HBOX(ST_ROOT, wuss_STD_SLIDER_HEIGHT, wuss_STD_GAP, stack_ALIGN_START),
  [ST_LABL4] = STACK_LEAF_GROUP(ST_ROW4, 0, 16, stack_ALIGN_CENTRE, ST_G_LABEL),
  [ST_SLDR4] = STACK_LEAF_EX(ST_ROW4, 0, wuss_STD_SLIDER_HEIGHT, stack_ALIGN_CENTRE, 1, ST_SLIDER_MIN_W, 0),
  [ST_VAL4]  = STACK_LEAF_GROUP(ST_ROW4, 0, 16, stack_ALIGN_CENTRE, ST_G_VALUE),

  [ST_BTNS]  = STACK_HBOX(ST_ROOT, wuss_STD_SECONDARY_BUTTON_HEIGHT, wuss_STD_GAP, stack_ALIGN_START),
  [ST_BSPC]  = STACK_SPACER_GROUP(ST_BTNS, 0, ST_G_LABEL),
  [ST_BBOX]  = { .kind = stack_KIND_HBOX, .parent = ST_BTNS,
                .gap = wuss_STD_GAP, .flex = 1 },
  [ST_DFLT]  = STACK_LEAF_EX(ST_BBOX, 0, wuss_STD_SECONDARY_BUTTON_HEIGHT, stack_ALIGN_CENTRE, 1, 0, 0),
  [ST_CNCL]  = STACK_LEAF_EX(ST_BBOX, 0, wuss_STD_SECONDARY_BUTTON_HEIGHT, stack_ALIGN_CENTRE, 1, 0, 0),
  [ST_APLY]  = STACK_LEAF_EX(ST_BBOX, 0, wuss_STD_SECONDARY_BUTTON_HEIGHT, stack_ALIGN_CENTRE, 1, 0, 0),
};

/* Build the size dialogue once: a label, a slider snapped to
 * SATURN_SIZE_STEP, a label echoing the slider's current value, and
 * Cancel/Apply buttons, positioned by stack_solve. Created hidden -- wuss
 * shows and hides it itself, as a menu leaf's borrowed window (see
 * task->menu_items[SATURN_MENU_SIZE] and wuss_menu_item_t::window), so
 * it must outlive the open menu chain and is never closed here, only
 * hidden. */
static result_t saturn_conf_dialogue_create(saturn_task_t *task)
{
  static const int label_box[SATURN_SIZEDLG_NROWS]   = { ST_LABL1, ST_LABL2, ST_LABL3, ST_LABL4 };
  static const int slider_box[SATURN_SIZEDLG_NROWS]  = { ST_SLDR1, ST_SLDR2, ST_SLDR3, ST_SLDR4 };
  static const int value_box[SATURN_SIZEDLG_NROWS]   = { ST_VAL1, ST_VAL2, ST_VAL3, ST_VAL4 };
  static const int label_icon[SATURN_SIZEDLG_NROWS]  = { SATURN_SIZE_ICON_LABEL1, SATURN_SIZE_ICON_LABEL2, SATURN_SIZE_ICON_LABEL3, SATURN_SIZE_ICON_LABEL4 };
  static const int slider_icon[SATURN_SIZEDLG_NROWS] = { SATURN_SIZE_ICON_SLIDER1, SATURN_SIZE_ICON_SLIDER2, SATURN_SIZE_ICON_SLIDER3, SATURN_SIZE_ICON_SLIDER4 };
  static const int value_icon[SATURN_SIZEDLG_NROWS]  = { SATURN_SIZE_ICON_VALUE1, SATURN_SIZE_ICON_VALUE2, SATURN_SIZE_ICON_VALUE3, SATURN_SIZE_ICON_VALUE4 };

  result_t         rc;
  wuss_icon_spec_t specs[SATURN_SIZE_NICONS];
  wuss_icon_t     *made[SATURN_SIZE_NICONS];
  stack_item_t     items[SIZE_STACK__LIMIT];
  box_t            boxes[SIZE_STACK__LIMIT];
  box_t            root;
  char             value_bufs[SATURN_SIZEDLG_NROWS][WUSS_SLIDER_ROW_BUF];
  int              value, row;
  size2d_t         min_sz;

  /* each label/value's natural width; the size groups widen the rest to match */
  memcpy(items, g_saturn_conf_stack, sizeof(items));
  for (row = 0; row < SATURN_SIZEDLG_NROWS; row++)
  {
    const saturn_sizedlg_rowdesc_t *desc = &g_saturn_sizedlg_rows[row];
    char                            widest[WUSS_SLIDER_ROW_BUF];

    items[label_box[row]].axis_size = task_text_width(task->base.wuss, desc->label);
    snprintf(widest, sizeof(widest), "%d", desc->max);
    items[value_box[row]].axis_size = task_text_width(task->base.wuss, widest);
  }

  /* the buttons share the widest label's width */
  items[ST_DFLT].min = task_button_width(task->base.wuss, "Default",
                                         wuss_STD_SECONDARY_BUTTON_BORDER);
  items[ST_CNCL].min = items[ST_DFLT].min;
  items[ST_APLY].min = task_button_width(task->base.wuss, "Default",
                                         wuss_STD_SECONDARY_BUTTON_BORDER);

  /* smallest window the layout can be solved into without any flex item
   * (the sliders) growing past its minimum */
  rc = stack_smallest(items, NELEMS(items), &min_sz);
  if (rc != result_OK)
    return rc;

  rc = wuss_dialogue_create(&task->conf.dialogue, task->base.delegate, min_sz,
                            "Configuration", saturn_conf_fillout, task);
  if (rc != result_OK)
    return rc;

  wuss_dialogue_set_focusable(task->conf.dialogue, 0); /* no writables */

  root = (box_t) BOX_POS_SIZE(0, 0, min_sz.w, min_sz.h);
  rc = stack_solve(items, NELEMS(items), &root, boxes);
  if (rc != result_OK)
    goto exit;

  for (row = 0; row < SATURN_SIZEDLG_NROWS; row++)
  {
    const saturn_sizedlg_rowdesc_t *desc = &g_saturn_sizedlg_rows[row];

    value = *saturn_sizedlg_field(task, row);

    wuss_icon_spec_label(&specs[label_icon[row]], boxes[label_box[row]],
                         desc->label, wuss_ICON_FLAGS_JUSTIFY_RIGHT);
    wuss_icon_spec_slider_row(&specs[slider_icon[row]], &specs[value_icon[row]],
                              boxes[slider_box[row]], boxes[value_box[row]],
                              wuss_SLIDER_HORIZONTAL,
                              desc->min, desc->max, value, NULL, desc->step,
                              value_bufs[row], sizeof(value_bufs[row]));
  }

  wuss_icon_spec_action(&specs[SATURN_SIZE_ICON_DEFAULT], boxes[ST_DFLT], "Default", 0);
  wuss_icon_spec_action(&specs[SATURN_SIZE_ICON_CANCEL], boxes[ST_CNCL], "Cancel", 0);
  wuss_icon_spec_action(&specs[SATURN_SIZE_ICON_APPLY], boxes[ST_APLY], "Apply", 0);

  rc = wuss_icon_create_array(wuss_dialogue_window(task->conf.dialogue),
                              specs, SATURN_SIZE_NICONS, made);
  if (rc != result_OK)
    goto exit;

  for (row = 0; row < SATURN_SIZEDLG_NROWS; row++)
    wuss_slider_row_bind(&task->conf.rows[row], made[slider_icon[row]],
                         made[value_icon[row]], NULL,
                         g_saturn_sizedlg_rows[row].min,
                         g_saturn_sizedlg_rows[row].max,
                         g_saturn_sizedlg_rows[row].step);
  task->conf.deflt  = made[SATURN_SIZE_ICON_DEFAULT];
  task->conf.cancel = made[SATURN_SIZE_ICON_CANCEL];
  task->conf.apply  = made[SATURN_SIZE_ICON_APPLY];

  {
    wuss_dialogue_action_t actions[3];

    actions[0].icon = task->conf.deflt;
    actions[0].fn   = saturn_conf_default_action;
    actions[1].icon = task->conf.cancel;
    actions[1].fn   = saturn_conf_cancel;
    actions[2].icon = task->conf.apply;
    actions[2].fn   = saturn_conf_apply_action;

    rc = wuss_dialogue_set_actions(task->conf.dialogue, actions,
                                   NELEMS(actions));
    if (rc != result_OK)
      goto exit;
  }

  return result_OK;


exit:
  wuss_dialogue_destroy(task->conf.dialogue); /* not yet a menu leaf: safe to close */
  task->conf.dialogue = NULL;
  return rc;
}

/* Dialogue fillout callback: resync the slider and its echo label to
 * task->config.size, in case Apply (or a config passed to saturn_init)
 * changed it since the dialogue was last shown. Called by
 * wuss_dialogue_handle_pre_show on every reveal, and directly by
 * saturn_conf_cancel to reset the dialogue on an Adjust-Cancel click. */
static result_t saturn_conf_fillout(void *opaque)
{
  result_t       rc;
  saturn_task_t *task;
  int            row, value;

  task = opaque;

  rc = result_OK;
  for (row = 0; row < SATURN_SIZEDLG_NROWS; row++)
  {
    value = *saturn_sizedlg_field(task, row);
    rc = wuss_slider_row_set(wuss_dialogue_window(task->conf.dialogue),
                             &task->conf.rows[row], value);
  }

  return rc;
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

/* Applies the slider's current value to task->base.window (resize + doc extent),
 * shared by a Select and an Adjust click on Apply. */
static result_t saturn_conf_apply(saturn_task_t *task)
{
  int row, old_size;

  old_size = task->config.size;
  for (row = 0; row < SATURN_SIZEDLG_NROWS; row++)
    *saturn_sizedlg_field(task, row) =
      wuss_icon_get_value(task->conf.rows[row].slider);

  return saturn_resize(task, old_size);
}

/* Dialogue action callback for Cancel, split by button per the RISC OS
 * "Adjust doesn't dismiss" convention: Select dismisses the menu chain;
 * Adjust resets the dialogue to task->config.size instead. Dismissing goes
 * through wuss_menu_close rather than touching the (borrowed, reused) window
 * directly -- that is what hides it, same as a click outside the chain
 * would. */
static result_t saturn_conf_cancel(void *opaque, wuss_button_t button)
{
  saturn_task_t *task;

  task = opaque;

  if (button & wuss_BUTTON_SELECT)
  {
    wuss_menu_close(task->base.menu_handle);
    task->base.menu_handle = NULL;
    return result_OK;
  }
  if (button & wuss_BUTTON_ADJUST)
    return saturn_conf_fillout(task);
  return result_OK;
}

/* Dialogue action callback for Apply: Select applies and dismisses; Adjust
 * applies but leaves the dialogue open. */
static result_t saturn_conf_apply_action(void *opaque, wuss_button_t button)
{
  saturn_task_t *task;

  task = opaque;

  if (button & wuss_BUTTON_SELECT)
  {
    wuss_menu_close(task->base.menu_handle);
    task->base.menu_handle = NULL;
    return saturn_conf_apply(task);
  }
  if (button & wuss_BUTTON_ADJUST)
    return saturn_conf_apply(task);
  return result_OK;
}

/* Dialogue action callback for Default: resets task->config to
 * SATURN_CONFIG_DEFAULT, refills the sliders/echo labels to match, and
 * applies it (window resize + doc extent) -- Select then dismisses the
 * menu chain same as Apply's Select; Adjust leaves the dialogue open,
 * showing the reset values. */
static result_t saturn_conf_default_action(void         *opaque,
                                           wuss_button_t button)
{
  static const saturn_config_t default_config = SATURN_CONFIG_DEFAULT;

  result_t       rc;
  saturn_task_t *task;

  task = opaque;

  task->config = default_config;
  rc = saturn_conf_fillout(task);
  if (rc != result_OK)
    return rc;

  if (button & wuss_BUTTON_SELECT)
  {
    wuss_menu_close(task->base.menu_handle);
    task->base.menu_handle = NULL;
    return saturn_conf_apply(task);
  }
  if (button & wuss_BUTTON_ADJUST)
    return saturn_conf_apply(task);
  return result_OK;
}

/* wuss_EVENT_ICON on the size dialogue: slider drag updates the echo label
 * (snapped to SATURN_SIZE_STEP) live on DOWN/MOVE, handled here directly;
 * a Cancel/Apply click (UP only -- a press that drags off the button before
 * release is not taken as a click) is dispatched through the dialogue's
 * action table. */
static result_t saturn_conf_dialogue_icon(saturn_task_t      *task,
                                          const wuss_event_t *event)
{
  result_t rc;
  int      row;

  for (row = 0; row < SATURN_SIZEDLG_NROWS; row++)
    if (wuss_slider_row_event(wuss_dialogue_window(task->conf.dialogue),
                              &task->conf.rows[row], event, NULL))
      return result_OK;

  if (wuss_dialogue_handle_icon(task->conf.dialogue, event, &rc))
    return rc;

  return result_OK;
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

/* A pick from the shared colour submenu, applied to whichever field it was
 * last retargeted at (see wuss_colourlist_pre_open). "Configuration" is a wuss_menu_item_t::window
 * leaf, not a leaf pick, so it never reaches here -- see
 * saturn_conf_dialogue_icon and saturn_sizedlg_pre_show. */
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
    if (window == wuss_dialogue_window(task->conf.dialogue))
      return saturn_conf_dialogue_icon(task, event);
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
  {
    result_t rc;

    if (window == wuss_dialogue_window(task->conf.dialogue))
    {
      rc = wuss_dialogue_handle_pre_show(task->conf.dialogue);
      if (rc != result_OK)
        return rc;
    }

    if (event->data.pre_show.handle == NULL)
      return result_OK; /* plain window reveal, not a flagged menu leaf:
                         * already proceeding by default */

    return wuss_menu_open_window_now(event->data.pre_show.handle,
                                     event->data.pre_show.index);
  }

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return saturn_pre_submenu_open(task, event);

  case wuss_EVENT_MENU_SELECT:
    return saturn_menu_select(task, event);

  case wuss_EVENT_IDLE:
    return saturn_idle(task);

  case wuss_EVENT_CLOSE:
    wuss_dialogue_handle_close(task->conf.dialogue, window);
    return result_OK;

  case wuss_EVENT_QUIT:
    wuss_dialogue_destroy(task->conf.dialogue);
    return result_OK;

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
