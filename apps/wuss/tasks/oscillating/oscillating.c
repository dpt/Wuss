/* wuss/tasks/oscillating/oscillating.c -- Lissajous figure task */

#ifdef WUSS_APP

#include <math.h>
#include <stdlib.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/colour.h"
#include "framebuf/palettes.h"
#include "geom/box.h"
#include "utils/easing.h"
#include "utils/rng.h"
#include "wuss/component/colourmenu.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define OSCILLATING_POINTS 512 /* samples plotted along the curve */
#define OSCILLATING_NFREQS 12  /* frequency pairs offered */
#define OSCILLATING_NDECAYS 4   /* trail lengths offered; Off is the first */
#define OSCILLATING_MAX_TRAIL 16 /* most previous frames drawn dimmed */
#define OSCILLATING_NEASINGS 11 /* easing_NKINDS; None is the first */

/* window task: a Lissajous figure x=sin(a*t+phase), y=sin(b*t) plotted as a
 * ring of dots. The phase drifts each idle tick so the figure slowly morphs.
 * Select cycles the frequency pair (a,b); Adjust reverses the drift. The
 * menu's Ratio row picks the frequency pair directly; its Easing row picks
 * the curve for the random backwards runs (None by default); its Decay row
 * picks how many previous frames trail behind, dimmed (Off by default); its
 * Colours
 * row has Foreground and Background rows that pick fg and bg from the shared
 * colourmenu; Randomise (^R) picks every setting afresh and Reset (R)
 * restores the starting ones; Save
 * opens a Save As dialogue (drag its icon onto a Filer window, or Save with
 * a full path already typed) that writes the window's content out. Once a
 * click has given the window the input focus, Space pauses/resumes and
 * Left/Right step the frequency pair back/forward. */
typedef struct oscillating_task
{
  taskapp_instance_t  base;
  wuss_menu_item_t    menu_items[9]; /* per-instance: a shared static would
                                       * leak one instance's .window pointer
                                       * into another's menu */
  int                 paused; /* Menu > Pause; idle does nothing while set */
  int                 reversals; /* Menu > Reversals; random backwards runs */
  wuss_menu_item_t    ratio_items[OSCILLATING_NFREQS]; /* one row per pair */
  wuss_menu_t         ratio_menu;
  wuss_menu_item_t    easing_items[OSCILLATING_NEASINGS]; /* one per curve */
  wuss_menu_t         easing_menu;
  wuss_colourlist_t   colours; /* Foreground, Background */
  wuss_menu_item_t    decay_items[OSCILLATING_NDECAYS]; /* one per length */
  wuss_menu_t         decay_menu;
  int                 decay_index; /* index into oscillating_decays */
  double              trail[OSCILLATING_MAX_TRAIL]; /* ring of past phases */
  int                 trail_head; /* newest entry in trail */
  int                 trail_count; /* valid entries in trail */
  int                 easing_index; /* easing_kind_t; easing_LINEAR = None */
  colour_t       bg, fg;
  int            a, b;    /* frequency ratio */
  double         phase;
  double         drift;
  double         reversing; /* fraction of the backwards run still to do */
  double         reverse_from; /* phase at the start of the backwards run */
  rng_t          rng;     /* Randomise */
  int            freq_index; /* index into oscillating_freqs */
}
oscillating_task_t;

/* MENU click pops this menu; the item table and wuss_menu_t live
 * per-instance in oscillating_task_t, not as a file-scope static, so that each
 * window's Info row can hold its own .window pointer to the shared proginfo
 * singleton, retargeted just before wuss_menu_open */
enum
{
  OSCILLATING_MENU_PAUSE = 0,
  OSCILLATING_MENU_SAVE,
  OSCILLATING_MENU_RATIO,
  OSCILLATING_MENU_REVERSALS,
  OSCILLATING_MENU_EASING,
  OSCILLATING_MENU_DECAY,
  OSCILLATING_MENU_COLOURS,
  OSCILLATING_MENU_RANDOMISE,
  OSCILLATING_MENU_RESET
};

/* frequency pairs cycled by a Select click and listed in Menu > Ratio */
static const struct
{
  int         a, b;
  const char *name;
}
oscillating_freqs[OSCILLATING_NFREQS] =
{
  { 3, 2, "3:2" }, { 5, 4, "5:4" }, { 3, 4, "3:4" },
  { 5, 6, "5:6" }, { 1, 2, "1:2" }, { 7, 4, "7:4" },
  { 1, 1, "1:1" }, { 1, 3, "1:3" }, { 2, 3, "2:3" },
  { 3, 5, "3:5" }, { 4, 5, "4:5" }, { 7, 6, "7:6" }
};

/* easing curves listed in Menu > Easing, indexed by easing_kind_t */
static const char *const oscillating_easings[OSCILLATING_NEASINGS] =
{
  "None", "Quad", "Cubic", "Quart", "Quint", "Sine",
  "Expo", "Circ", "Back", "Elastic", "Bounce"
};

/* trail lengths listed in Menu > Decay */
static const struct
{
  int         frames;
  const char *name;
}
oscillating_decays[OSCILLATING_NDECAYS] =
{
  { 0, "Off" }, { 4, "Short" }, { 8, "Medium" }, { 16, "Long" }
};

/* the starting figure, as oscillating_init and Reset restore it */
static void oscillating_defaults(oscillating_task_t *task)
{
  task->bg           = colour_rgb(0x00, 0x00, 0x00);
  task->fg           = colour_rgb(0x00, 0xFF, 0x00);
  task->freq_index   = 0;
  task->a            = oscillating_freqs[0].a;
  task->b            = oscillating_freqs[0].b;
  task->phase        = 0.0;
  task->drift        = 0.01;
  task->reversals    = 1;
  task->reversing    = 0.0;
  task->easing_index = easing_LINEAR; /* row 0, "None" */
  task->decay_index  = 0;             /* row 0, "Off" */
  task->trail_count  = 0;
}

/* pick the ratio, easing, decay, drift direction and colours afresh */
static void oscillating_randomise(oscillating_task_t *task)
{
  task->freq_index   = rng_range(&task->rng, OSCILLATING_NFREQS);
  task->a            = oscillating_freqs[task->freq_index].a;
  task->b            = oscillating_freqs[task->freq_index].b;
  task->easing_index = rng_range(&task->rng, OSCILLATING_NEASINGS);
  task->decay_index  = rng_range(&task->rng, OSCILLATING_NDECAYS);
  task->drift        = rng_range(&task->rng, 2) ? task->drift : -task->drift;
  task->reversing    = 0.0;
  task->trail_count  = 0;
  task->bg           = colour_random(&task->rng);
  task->fg           = colour_random_contrast(&task->rng, task->bg);
}

static result_t oscillating_init(oscillating_task_t *task)
{
  wuss_t *wuss;
  int     i;

  wuss = task->base.wuss;

  rng_seed(&task->rng, (uint32_t) rand());
  oscillating_defaults(task);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, OSCILLATING_MENU_PAUSE, "Pause",
                          wuss_MENU_ITEM_NONE, "SPACE");

  taskapp_save_row(&task->base, task->menu_items, OSCILLATING_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  for (i = 0; i < OSCILLATING_NFREQS; i++)
    WUSS_MENU_ITEM(task->ratio_items, i, oscillating_freqs[i].name,
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->ratio_menu, "Ratio", task->ratio_items,
                 NELEMS(task->ratio_items));

  WUSS_MENU_ITEM_MENU(task->menu_items, OSCILLATING_MENU_RATIO, "Ratio",
                      wuss_MENU_ITEM_DASHED, &task->ratio_menu);

  WUSS_MENU_ITEM(task->menu_items, OSCILLATING_MENU_REVERSALS, "Reversals",
                 wuss_MENU_ITEM_NONE);

  for (i = 0; i < OSCILLATING_NDECAYS; i++)
    WUSS_MENU_ITEM(task->decay_items, i, oscillating_decays[i].name,
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->decay_menu, "Decay", task->decay_items,
                 NELEMS(task->decay_items));

  WUSS_MENU_ITEM_MENU(task->menu_items, OSCILLATING_MENU_DECAY, "Decay",
                      wuss_MENU_ITEM_NONE, &task->decay_menu);

  for (i = 0; i < OSCILLATING_NEASINGS; i++)
    WUSS_MENU_ITEM(task->easing_items, i, oscillating_easings[i],
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->easing_menu, "Easing", task->easing_items,
                 NELEMS(task->easing_items));

  WUSS_MENU_ITEM_MENU(task->menu_items, OSCILLATING_MENU_EASING, "Easing",
                      wuss_MENU_ITEM_NONE, &task->easing_menu);

  wuss_colourlist_init_pair(&task->colours, wuss, &task->fg, &task->bg);

  WUSS_MENU_ITEM_MENU(task->menu_items, OSCILLATING_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->colours.menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, OSCILLATING_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_DASHED, "^R");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, OSCILLATING_MENU_RESET, "Reset",
                          wuss_MENU_ITEM_NONE, "R");

  WUSS_MENU_TITLE(task->base.menu, "Oscillating", task->menu_items,
                 NELEMS(task->menu_items));

  return result_OK;
}

static result_t oscillating_redraw(const wuss_event_t *event,
                                   void               *task_data)
{
  oscillating_task_t *lc;
  screen_t           *scr;
  const box_t        *content, *bounds;
  int                 sx, sy;
  int                 width, height, cx, cy, rx, ry;
  int                 i, age, ntrail;
  double              phase;
  pixelfmt_any_t      fgpix;

  lc = task_data;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;
  sx      = event->data.redraw.scroll.x;
  sy      = event->data.redraw.scroll.y;

  screen_fill_rect(scr, content->x0, content->y0, box_size(content), lc->bg);

  width  = bounds->x1 - bounds->x0;
  height = bounds->y1 - bounds->y0;
  cx     = bounds->x0 - sx + width  / 2;
  cy     = bounds->y0 - sy + height / 2;
  rx     = width  / 2 - 10;
  ry     = height / 2 - 10;

  ntrail = MIN(oscillating_decays[lc->decay_index].frames, lc->trail_count);

  /* oldest first so the newest frame is drawn on top; each age fades fg
   * towards bg. Resolve the colour once per frame, not once per point */
  for (age = ntrail; age >= 0; age--)
  {
    if (age == 0)
      phase = lc->phase;
    else
      phase = lc->trail[(lc->trail_head - (age - 1) + OSCILLATING_MAX_TRAIL) %
                        OSCILLATING_MAX_TRAIL];

    fgpix = screen_colour_to_pixel(scr,
                                   colour_mix(lc->fg, lc->bg, age, ntrail + 1));

    for (i = 0; i < OSCILLATING_POINTS; i++)
    {
      double t;
      int    px, py;

      t  = (double) i / OSCILLATING_POINTS * 2.0 * M_PI;
      px = cx + (int) (rx * sin(lc->a * t + phase));
      py = cy + (int) (ry * sin(lc->b * t));

      screen_set_pixel_value(scr, px, py, fgpix);
    }
  }

  return result_OK;
}

/* switch to the given entry of oscillating_freqs */
static void oscillating_set_freq(oscillating_task_t *lc, int index)
{
  lc->freq_index = index;
  lc->a          = oscillating_freqs[index].a;
  lc->b          = oscillating_freqs[index].b;
  wuss_window_invalidate_visible(lc->base.window); /* whole figure changes */
}

static result_t oscillating_mouse(wuss_mouse_action_t action,
                                  wuss_button_t       button,
                                  void               *task_data)
{
  oscillating_task_t *lc;

  lc = task_data;

  if (action != wuss_MOUSE_DOWN)
    return result_OK;

  if (button & wuss_BUTTON_MENU)
  {
    wuss_menu_tick_item(&lc->base.menu, OSCILLATING_MENU_PAUSE, lc->paused);
    wuss_menu_tick_item(&lc->base.menu, OSCILLATING_MENU_REVERSALS,
                        lc->reversals);
    wuss_menu_tick_exclusive(&lc->ratio_menu, lc->freq_index);
    wuss_menu_tick_exclusive(&lc->easing_menu, lc->easing_index);
    wuss_menu_tick_exclusive(&lc->decay_menu, lc->decay_index);

    return wuss_menu_open_at_pointer(lc->base.delegate, &lc->base.menu,
                                     &lc->base.menu_handle);
  }

  if (button & wuss_BUTTON_SELECT)
  {
    oscillating_set_freq(lc, (lc->freq_index + 1) % OSCILLATING_NFREQS);
  }
  else if (button & wuss_BUTTON_ADJUST)
  {
    lc->drift = -lc->drift;
  }

  return result_OK;
}

/* Left/Right step to the previous/next frequency pair. Anything else is
 * passed back unclaimed. */
static result_t oscillating_key(oscillating_task_t *lc, int code)
{
  switch (code)
  {
  case wuss_KEY_LEFT:
    oscillating_set_freq(lc, (lc->freq_index + OSCILLATING_NFREQS - 1) %
                           OSCILLATING_NFREQS);
    return result_OK;

  case wuss_KEY_RIGHT:
    oscillating_set_freq(lc, (lc->freq_index + 1) % OSCILLATING_NFREQS);
    return result_OK;

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }
}

static result_t oscillating_idle(void *task_data)
{
  oscillating_task_t *lc;

  lc = task_data;

  /* the proginfo dialogue is a second window on this same (autoclose)
   * delegate, so closing the main window alone never empties task->windows
   * and the task lingers until the dialogue closes too -- guard against the
   * dangling window in the meantime */
  if (lc->base.window == NULL)
    return result_OK;

  if (lc->paused)
    return result_OK;

  /* remember the frame about to be replaced for the decay trail */
  if (oscillating_decays[lc->decay_index].frames > 0)
  {
    lc->trail_head = (lc->trail_head + 1) % OSCILLATING_MAX_TRAIL;
    lc->trail[lc->trail_head] = lc->phase;
    lc->trail_count = MIN(lc->trail_count + 1, OSCILLATING_MAX_TRAIL);
  }

  /* 2% of ticks start a 90 degree backwards run, then the drift resumes */
  if (lc->reversals && lc->reversing <= 0.0 &&
      rng_range(&lc->rng, 100) < 2)
  {
    lc->reversing    = 1.0;
    lc->reverse_from = lc->phase;
  }

  if (lc->reversing > 0.0)
  {
    /* same duration as a linear run at the drift rate; the chosen curve
     * shapes it (None is linear) */
    lc->reversing = MAX(lc->reversing - fabs(lc->drift) / (M_PI / 2.0), 0.0);
    lc->phase     = lc->reverse_from -
                    M_PI / 2.0 * easing((easing_kind_t) lc->easing_index,
                                        easing_IN_OUT, 1.0 - lc->reversing);
  }
  else
  {
    lc->phase += lc->drift;
  }

  if (lc->phase > 2.0 * M_PI)
    lc->phase -= 2.0 * M_PI;
  else if (lc->phase < 0.0)
    lc->phase += 2.0 * M_PI;

  wuss_window_invalidate_visible(lc->base.window);

  return result_OK;
}

/* Only the Colours rows are PRE_OPEN, so the event is the pair's. */
static result_t oscillating_pre_submenu_open(oscillating_task_t *lc,
                                             const wuss_event_t *event)
{
  result_t rc;

  if (wuss_colourlist_pre_open(&lc->colours, lc->base.wuss, event, &rc))
    return rc;

  return result_OK;
}

static result_t oscillating_menu_select(oscillating_task_t *lc,
                                        const wuss_event_t *event)
{
  if (event->data.menu_select.menu == &lc->base.menu &&
      (event->data.menu_select.index == OSCILLATING_MENU_RANDOMISE ||
       event->data.menu_select.index == OSCILLATING_MENU_RESET))
  {
    if (event->data.menu_select.index == OSCILLATING_MENU_RANDOMISE)
      oscillating_randomise(lc);
    else
      oscillating_defaults(lc);

    wuss_menu_tick_exclusive_live(lc->base.menu_handle, &lc->ratio_menu,
                                  lc->freq_index);
    wuss_menu_tick_exclusive_live(lc->base.menu_handle, &lc->easing_menu,
                                  lc->easing_index);
    wuss_menu_tick_exclusive_live(lc->base.menu_handle, &lc->decay_menu,
                                  lc->decay_index);
    wuss_menu_tick_item_live(lc->base.menu_handle, &lc->base.menu,
                             OSCILLATING_MENU_REVERSALS, lc->reversals);
    wuss_window_invalidate_visible(lc->base.window);
    return result_OK;
  }

  if (event->data.menu_select.menu == &lc->ratio_menu)
  {
    oscillating_set_freq(lc, event->data.menu_select.index);
    wuss_menu_tick_exclusive_live(lc->base.menu_handle, &lc->ratio_menu,
                                  lc->freq_index);
    return result_OK;
  }

  if (event->data.menu_select.menu == &lc->decay_menu)
  {
    lc->decay_index = event->data.menu_select.index;
    wuss_menu_tick_exclusive_live(lc->base.menu_handle, &lc->decay_menu,
                                  lc->decay_index);
    return result_OK;
  }

  if (event->data.menu_select.menu == &lc->easing_menu)
  {
    lc->easing_index = event->data.menu_select.index;
    wuss_menu_tick_exclusive_live(lc->base.menu_handle, &lc->easing_menu,
                                  lc->easing_index);
    return result_OK;
  }

  if (wuss_colourlist_selected(&lc->colours, event))
    wuss_window_invalidate_visible(lc->base.window);

  return result_OK;
}

/* The "Pause" row: stop or restart the idle animation. An ADJUST pick keeps
 * the menu open, so retick the live row; a SELECT pick has already closed
 * it. */
static result_t oscillating_toggle_pause(oscillating_task_t *lc,
                                         const wuss_event_t *event)
{
  lc->paused = !lc->paused;

  wuss_menu_tick_item_live(lc->base.menu_handle, &lc->base.menu, OSCILLATING_MENU_PAUSE,
                           lc->paused);

  return result_OK;
}

static result_t oscillating_handle(wuss_window_t      *window,
                                   const wuss_event_t *event,
                                   void               *task_data)
{
  oscillating_task_t *lc;

  NOT_USED(window);

  lc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return oscillating_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    return oscillating_mouse(event->data.mouse.action,
                             event->data.mouse.button, task_data);

  case wuss_EVENT_IDLE:
    return oscillating_idle(task_data);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(lc->base.delegate, &lc->base.menu, event, &rc))
      return rc;

    return oscillating_key(lc, event->data.key.code);
  }

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return oscillating_pre_submenu_open(lc, event);

  case wuss_EVENT_MENU_SELECT:
    if (wuss_menu_select_is(event, &lc->base.menu, OSCILLATING_MENU_PAUSE))
      return oscillating_toggle_pause(lc, event);
    if (wuss_menu_select_is(event, &lc->base.menu, OSCILLATING_MENU_REVERSALS))
    {
      lc->reversals = !lc->reversals;
      wuss_menu_tick_item_live(lc->base.menu_handle, &lc->base.menu,
                               OSCILLATING_MENU_REVERSALS, lc->reversals);
      return result_OK;
    }
    return oscillating_menu_select(lc, event);

  default:
    return result_OK;
  }
}

result_t oscillating_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Oscillating",
    .purpose       = "Drifting Lissajous figure",
    .instance_size = sizeof(oscillating_task_t),
    .window_size   = { 220, 220 },
    .init          = (taskapp_init_fn_t) oscillating_init,
    .handle        = oscillating_handle,
    .save_name     = "oscillating.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
