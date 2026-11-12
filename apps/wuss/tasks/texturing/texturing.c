/* wuss/tasks/texturing/texturing.c -- tileable procedural texture task */

#ifdef WUSS_APP

#include <limits.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/colour.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "utils/rng.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/common.h"
#include "../common/confdlg.h"
#include "../common/taskapp.h"

/* Texture edge in pixels, as a power of two so that every lattice step is a
 * shift and a mask. */
#define TEXTURING_LOG2_SIZE 8
#define TEXTURING_SIZE      (1 << TEXTURING_LOG2_SIZE)

/* The window shows the texture repeated this many times each way, so that
 * any seam would show. */
#define TEXTURING_REPEAT 2

/* Lattice cells across the tile in the coarsest octave of the noise behind
 * Stone and Weave's broad patches (log2). */
#define TEXTURING_LOG2_BASE 2

/* Most parameters any kind has. */
#define TEXTURING_NPARAMS 3

/* Fills the content area with a procedural texture that tiles seamlessly:
 * every noise lattice wraps at the tile edge. The texture is generated once
 * per change as 8-bit values into a pixelfmt_p8 bitmap; the colour scheme
 * is only its palette, so a scheme change redraws without regenerating.
 *
 * SELECT reseeds; ADJUST steps the colour scheme. Menu > Kind and Menu >
 * Colours pick directly; Menu > Configuration shows the current kind's
 * parameters as sliders and Menu > Grain the grain's; Menu > Randomise
 * picks a fresh seed, kind and scheme; Menu > Save writes a single tile as
 * a PNG. Once a click has given the window the input focus, Space reseeds,
 * Left/Right step the colour scheme, K steps the kind and G steps the grain
 * through None, Light and Heavy presets. */

enum
{
  TEXTURING_KIND_CLOUDS,
  TEXTURING_KIND_MARBLE,
  TEXTURING_KIND_WOOD,
  TEXTURING_KIND_CELLS,
  TEXTURING_KIND_STONE,
  TEXTURING_KIND_WEAVE,
  TEXTURING_NKINDS
};

static const char *texturing_kind_names[TEXTURING_NKINDS] =
{
  "Clouds", "Marble", "Wood", "Cells", "Stone", "Weave"
};

/* Colour schemes, as 0xRRGGBB. A smooth scheme ramps through its three
 * stops at values 0, 128 and 255. A stepped scheme maps each fifth of the
 * value range to one of its five stops exactly, giving the few close tones
 * of the RISC OS desktop textures; grain then speckles between them. */
static const struct
{
  const char  *name;
  int          stepped;
  unsigned int stops[5];
}
texturing_schemes[] =
{
  { "Sky",     0, { 0x1E4FA0, 0x7FB0E8, 0xFFFFFF } },
  { "Slate",   0, { 0x202020, 0x808078, 0xE8E8E0 } },
  { "Wood",    0, { 0x3A1E0C, 0x8A5528, 0xD8A868 } },
  { "Lava",    0, { 0x000000, 0xC02000, 0xFFE040 } },
  { "Moss",    0, { 0x102008, 0x4A7A20, 0xC8E088 } },
  { "Ice",     0, { 0x002838, 0x40A0C0, 0xF0FFFF } },
  /* the 16-colour desktop's greys */
  { "Desktop", 1, { 0x333333, 0x555555, 0x777777, 0x999999, 0xBBBBBB } },
  /* 4-bit greys, darker */
  { "Grey 16", 1, { 0x222222, 0x444444, 0x666666, 0x888888, 0xAAAAAA } },
  { "Linen",   1, { 0x6E6450, 0x8A806A, 0xA69C84, 0xC2B89E, 0xDDD4BA } },
  { "Denim",   1, { 0x1E2C44, 0x2E4060, 0x40567C, 0x566E96, 0x7088AE } }
};

#define TEXTURING_NSCHEMES ((int) NELEMS(texturing_schemes))

/* Per-pixel grain: the noise amplitude it adds to a value, as its own
 * one-slider dialogue edits it. G steps through these presets. */
#define TEXTURING_GRAIN_MAX 128

static const int texturing_grain_presets[] = { 0, 48, 96 };

static const confdlg_row_t texturing_grain_row =
{
  "Grain", 0, TEXTURING_GRAIN_MAX, 0, 0
};

static const int texturing_grain_default = 48;

/* Each kind's parameters are ints in a row of params[][], edited by its own
 * Configuration dialogue. A "Scale" is the log2 of the lattice cells across
 * the tile, so higher is finer; every lattice is a power of two and every
 * band count a whole number, so the texture always tiles. Percentages are
 * turned into 8.8 multipliers once per generate, not divided per pixel. */
#define TEXTURING_PARAM(i) ((i) * sizeof(int))

static const confdlg_row_t texturing_rows[TEXTURING_NKINDS][TEXTURING_NPARAMS] =
{
  { /* Clouds */
    { "Scale",      1,   5, 0, TEXTURING_PARAM(0) },
    { "Contrast",  25, 400, 0, TEXTURING_PARAM(1) },
    { "Detail",     1,   7, 0, TEXTURING_PARAM(2) }
  },
  { /* Marble */
    { "Scale",      1,   5, 0, TEXTURING_PARAM(0) },
    { "Veins",      1,  16, 0, TEXTURING_PARAM(1) },
    { "Turbulence", 0,  16, 0, TEXTURING_PARAM(2) }
  },
  { /* Wood */
    { "Scale",      1,   5, 0, TEXTURING_PARAM(0) },
    { "Rings",      2,  32, 0, TEXTURING_PARAM(1) },
    { "Roughness",  0,  16, 0, TEXTURING_PARAM(2) }
  },
  { /* Cells */
    { "Scale",      1,   5, 0, TEXTURING_PARAM(0) },
    { "Contrast",  25, 400, 0, TEXTURING_PARAM(1) }
  },
  { /* Stone */
    { "Scale",      3,   6, 0, TEXTURING_PARAM(0) },
    { "Relief",     0, 400, 0, TEXTURING_PARAM(1) },
    { "Patches",    0, 100, 0, TEXTURING_PARAM(2) }
  },
  { /* Weave: Thread is the log2 of the thread width in pixels */
    { "Thread",     2,   5, 0, TEXTURING_PARAM(0) },
    { "Relief",     0, 400, 0, TEXTURING_PARAM(1) },
    { "Wobble",     0,  16, 0, TEXTURING_PARAM(2) }
  }
};

static const int texturing_defaults[TEXTURING_NKINDS][TEXTURING_NPARAMS] =
{
  { 2, 100,  7 }, /* Clouds */
  { 2,   4,  4 }, /* Marble */
  { 2,  16,  4 }, /* Wood */
  { 3, 100,  0 }, /* Cells */
  { 5, 100, 25 }, /* Stone */
  { 3, 100,  4 }  /* Weave */
};

typedef struct texturing_task texturing_task_t;

static result_t texturing_applied(texturing_task_t *task, int old);

#define TEXTURING_CONF(kind, nrows) \
  { texturing_rows[kind], nrows, texturing_defaults[kind], \
    sizeof(texturing_defaults[kind]), \
    (confdlg_applied_fn_t) texturing_applied }

/* A dialogue per kind, then the grain's. */
#define TEXTURING_CONF_GRAIN TEXTURING_NKINDS
#define TEXTURING_NCONFS     (TEXTURING_NKINDS + 1)

static const confdlg_desc_t texturing_confs[TEXTURING_NCONFS] =
{
  TEXTURING_CONF(TEXTURING_KIND_CLOUDS, 3),
  TEXTURING_CONF(TEXTURING_KIND_MARBLE, 3),
  TEXTURING_CONF(TEXTURING_KIND_WOOD,   3),
  TEXTURING_CONF(TEXTURING_KIND_CELLS,  2),
  TEXTURING_CONF(TEXTURING_KIND_STONE,  3),
  TEXTURING_CONF(TEXTURING_KIND_WEAVE,  3),
  { &texturing_grain_row, 1, &texturing_grain_default,
    sizeof(texturing_grain_default),
    (confdlg_applied_fn_t) texturing_applied }
};

enum
{
  TEXTURING_MENU_SAVE = 0,
  TEXTURING_MENU_KIND,
  TEXTURING_MENU_CONFIGURATION,
  TEXTURING_MENU_COLOURS,
  TEXTURING_MENU_GRAIN,
  TEXTURING_MENU_RANDOMISE
};

struct texturing_task
{
  taskapp_instance_t base;
  rng_t              seed;
  int                kind;   /* TEXTURING_KIND_* */
  int                scheme; /* texturing_schemes[] row */
  int                grain;  /* amplitude, 0..TEXTURING_GRAIN_MAX */
  int                params[TEXTURING_NKINDS][TEXTURING_NPARAMS];
  confdlg_t          confs[TEXTURING_NCONFS]; /* see texturing_confs */
  colour_t           palette[256];
  bitmap_t           bm;     /* wraps pixels[] and palette[] */
  unsigned char      pixels[TEXTURING_SIZE * TEXTURING_SIZE];
  wuss_menu_item_t   menu_items[6];
  wuss_menu_item_t   kind_items[TEXTURING_NKINDS];
  wuss_menu_t        kind_menu;
  wuss_menu_item_t   scheme_items[NELEMS(texturing_schemes)];
  wuss_menu_t        scheme_menu;
};

/* ----------------------------------------------------------------------- */

/* Lattice value 0..255 at cell (cx,cy) of an n x n lattice that wraps. */
static int texturing_lattice(uint32_t seed, int cx, int cy, int log2n)
{
  int mask;

  mask = (1 << log2n) - 1;

  return (int) (rng_hash32(seed, ((cy & mask) << log2n) | (cx & mask)) & 255);
}

/* Smoothstep over a 0..256 fraction. */
static int texturing_ease(int f)
{
  return f * f * (768 - 2 * f) >> 16;
}

/* Bilinear smooth value noise, 0..255, on a 2^log2n lattice across the
 * tile. */
static int texturing_noise(uint32_t seed, int x, int y, int log2n)
{
  int log2c, cx, cy, fx, fy, a, b, c, d, top, bot;

  log2c = TEXTURING_LOG2_SIZE - log2n; /* log2 of the cell size in pixels */
  cx    = x >> log2c;
  cy    = y >> log2c;
  fx    = texturing_ease(((x & ((1 << log2c) - 1)) << 8) >> log2c);
  fy    = texturing_ease(((y & ((1 << log2c) - 1)) << 8) >> log2c);

  a = texturing_lattice(seed, cx,     cy,     log2n);
  b = texturing_lattice(seed, cx + 1, cy,     log2n);
  c = texturing_lattice(seed, cx,     cy + 1, log2n);
  d = texturing_lattice(seed, cx + 1, cy + 1, log2n);

  top = a + ((b - a) * fx >> 8);
  bot = c + ((d - c) * fx >> 8);

  return top + ((bot - top) * fy >> 8);
}

/* Fractal sum of up to octaves noise octaves, 0..255, starting from a
 * 2^log2base lattice: each octave doubles the lattice and halves the
 * weight, stopping at one-pixel cells. The last octave takes the weight
 * left over, so the weights always sum to 256. */
static int texturing_fbm(uint32_t seed,
                         int      x,
                         int      y,
                         int      log2base,
                         int      octaves)
{
  int sum, log2n, last, weight;

  sum    = 0;
  last   = MIN(log2base + octaves - 1, TEXTURING_LOG2_SIZE);
  weight = 128;
  for (log2n = log2base; log2n <= last; log2n++)
  {
    sum    += texturing_noise(seed + (uint32_t) log2n, x, y, log2n) *
              (log2n == last ? weight * 2 : weight);
    weight >>= 1;
  }

  return sum >> 8;
}

/* The broad light and dark patches under Stone and Weave, -128..127. */
static int texturing_patches(uint32_t seed, int x, int y)
{
  return texturing_fbm(seed ^ 0x5A5Au, x, y, TEXTURING_LOG2_BASE,
                       TEXTURING_LOG2_SIZE) - 128;
}

/* Scale v about 128 by an 8.8 multiplier. */
static int texturing_contrast(int v, int mul)
{
  return 128 + ((v - 128) * mul >> 8);
}

/* Fold a value into a 0..255 triangle wave of period 512. */
static int texturing_fold(int v)
{
  v &= 511;
  return v < 256 ? v : 511 - v;
}

/* Squared distance from (x,y) to the nearest jittered feature point, one
 * per cell of a wrapping 2^log2n grid, scaled to 0..255. */
static int texturing_cells(uint32_t seed, int x, int y, int log2n)
{
  int log2c, mask, shift, cx, cy, best, i, j;

  log2c = TEXTURING_LOG2_SIZE - log2n; /* log2 of the cell size in pixels */
  mask  = (1 << log2n) - 1;
  shift = 2 * log2c - 8; /* takes a cell size squared to 256 */
  cx    = x >> log2c;
  cy    = y >> log2c;
  best  = INT_MAX;

  for (j = -1; j <= 1; j++)
    for (i = -1; i <= 1; i++)
    {
      uint32_t h;
      int      px, py, dx, dy;

      /* point position in unwrapped pixels, hashed from the wrapped cell */
      h  = rng_hash32(seed, (((cy + j) & mask) << log2n) | ((cx + i) & mask));
      px = (cx + i) * (1 << log2c) + (int) (h & ((1u << log2c) - 1));
      py = (cy + j) * (1 << log2c) + (int) ((h >> 16) & ((1u << log2c) - 1));
      dx = px - x;
      dy = py - y;
      best = MIN(best, dx * dx + dy * dy);
    }

  /* ponytail: nearest points rarely sit more than a cell away, so the
   * clamp seldom bites */
  best = shift >= 0 ? best >> shift : best << -shift;
  return MIN(best, 255);
}

/* Rough stone, lit from the top left: an embossed noise height field on a
 * 2^log2n lattice and the two finer ones, the slope taken across the
 * diagonal neighbours (wrapped so it still tiles) and scaled by an 8.8
 * relief multiplier, over broad light and dark patches scaled by another.
 * Centred on 128. */
static int texturing_rough(uint32_t seed, int x, int y, int log2n)
{
  return (texturing_noise(seed,     x, y, log2n) * 2 +
          texturing_noise(seed + 1, x, y, log2n + 1) +
          texturing_noise(seed + 2, x, y, log2n + 2)) >> 2;
}

static int texturing_stone(uint32_t seed,
                           int      x,
                           int      y,
                           int      log2n,
                           int      relief,
                           int      patches)
{
  int m, slope;

  m     = TEXTURING_SIZE - 1;
  slope = texturing_rough(seed, (x - 1) & m, (y - 1) & m, log2n) -
          texturing_rough(seed, (x + 1) & m, (y + 1) & m, log2n);

  return 128 + (slope * relief >> 8) +
         (texturing_patches(seed, x, y) * patches >> 8);
}

/* Height of a plain weave: 2^log2t-pixel threads alternate over and under
 * in a checkerboard. Each visible thread is highest along its middle and
 * drops toward its edges and toward the ends where it dives under its
 * neighbour. The coordinates wobble with noise, up to wobble pixels, so
 * the threads wander like yarn; the wobble repeats with the tile, so the
 * weave still tiles. */
static int texturing_weave_height(uint32_t seed,
                                  int      x,
                                  int      y,
                                  int      log2t,
                                  int      wobble)
{
  int t, over, across, along, a, b;

  x += (texturing_noise(seed + 7, x, y, 4) - 128) * wobble >> 7;
  y += (texturing_noise(seed + 8, x, y, 4) - 128) * wobble >> 7;
  x &= TEXTURING_SIZE - 1;
  y &= TEXTURING_SIZE - 1;

  t      = 1 << log2t;
  over   = ((x >> log2t) + (y >> log2t)) & 1; /* nonzero: weft on top */
  across = over ? (y & (t - 1)) : (x & (t - 1));
  along  = over ? (x & (t - 1)) : (y & (t - 1));
  a      = across < t / 2 ? across : t - 1 - across; /* 0 at an edge */
  b      = along  < t / 2 ? along  : t - 1 - along;  /* 0 where it dips */

  return a * 24 + b * 12 + (texturing_rough(seed, x, y, 5) >> 2);
}

/* Woven fabric, embossed from the top left as Stone is. Centred on 128. */
static int texturing_weave(uint32_t seed,
                           int      x,
                           int      y,
                           int      log2t,
                           int      relief,
                           int      wobble)
{
  int m, slope;

  m     = TEXTURING_SIZE - 1;
  slope = texturing_weave_height(seed, (x - 1) & m, (y - 1) & m, log2t,
                                 wobble) -
          texturing_weave_height(seed, (x + 1) & m, (y + 1) & m, log2t,
                                 wobble);

  return 128 + (slope * relief >> 8) + texturing_patches(seed, x, y) / 4;
}

static void texturing_generate(texturing_task_t *task)
{
  unsigned char *p;
  const int     *param;
  int            mul, mul2;
  int            amplitude;
  int            x, y, v;

  p         = task->pixels;
  param     = task->params[task->kind];
  mul       = param[1] * 256 / 100; /* rows 1 and 2 as 8.8 multipliers */
  mul2      = param[2] * 256 / 100;
  amplitude = task->grain;
  for (y = 0; y < TEXTURING_SIZE; y++)
    for (x = 0; x < TEXTURING_SIZE; x++)
    {
      switch (task->kind)
      {
      default:
      case TEXTURING_KIND_CLOUDS:
        v = texturing_contrast(texturing_fbm(task->seed, x, y, param[0],
                                             param[2]), mul);
        break;

      case TEXTURING_KIND_MARBLE:
        /* veins across the tile, bent by turbulence */
        v = texturing_fold(x * param[1] * 512 / TEXTURING_SIZE +
                           texturing_fbm(task->seed, x, y, param[0],
                                         TEXTURING_LOG2_SIZE) * param[2]);
        break;

      case TEXTURING_KIND_WOOD:
        /* noise sliced into rings, roughened by a fine grain */
        v = texturing_fold(texturing_fbm(task->seed, x, y, param[0],
                                         TEXTURING_LOG2_SIZE) * param[1] +
                           (texturing_noise(task->seed ^ 0x55u, x, y, 6) *
                            param[2] >> 4));
        break;

      case TEXTURING_KIND_CELLS:
        v = texturing_contrast(texturing_cells(task->seed, x, y, param[0]),
                               mul);
        break;

      case TEXTURING_KIND_STONE:
        v = texturing_stone(task->seed, x, y, param[0], mul, mul2);
        break;

      case TEXTURING_KIND_WEAVE:
        v = texturing_weave(task->seed, x, y, param[0], mul, param[2]);
        break;
      }

      /* grain: per-pixel noise, keyed by position so it tiles too */
      if (amplitude)
        v += ((int) (rng_hash32(task->seed ^ 0xC3A5u,
                                (y << TEXTURING_LOG2_SIZE) | x) & 255) - 128) *
             amplitude >> 7;

      *p++ = (unsigned char) CLAMP(v, 0, 255);
    }
}

/* Build the 256-entry palette from the current scheme's stops. */
static void texturing_build_palette(texturing_task_t *task)
{
  const unsigned int *stops;
  int                 nstops;
  colour_t            c[5];
  int                 i;

  stops  = texturing_schemes[task->scheme].stops;
  nstops = texturing_schemes[task->scheme].stepped ? 5 : 3;
  for (i = 0; i < nstops; i++)
    c[i] = colour_rgb((stops[i] >> 16) & 0xFF,
                      (stops[i] >>  8) & 0xFF,
                       stops[i]        & 0xFF);

  for (i = 0; i < 256; i++)
    if (texturing_schemes[task->scheme].stepped)
      task->palette[i] = c[i * 5 >> 8];
    else
      task->palette[i] = i < 128 ? colour_mix(c[0], c[1], (unsigned) i, 128)
                                 : colour_mix(c[1], c[2], (unsigned) i - 128, 127);
}

/* ----------------------------------------------------------------------- */

static result_t texturing_redraw(const wuss_event_t *event,
                                 texturing_task_t   *task)
{
  const box_t *bounds;

  bounds = event->data.redraw.bounds;

  /* dithered, as the desktop is often 16 colours */
  return screen_copy_bitmap_tiled_dithered(event->data.redraw.scr,
                                           event->data.redraw.content,
                                           &task->bm,
                                           POINT(bounds->x0 - event->data.redraw.scroll.x,
                                                 bounds->y0 - event->data.redraw.scroll.y),
                                           screen_DITHER_BLUE_NOISE);
}

static result_t texturing_reseed(texturing_task_t *task)
{
  rng_lcg32(&task->seed);
  texturing_generate(task);
  wuss_window_invalidate_extent(task->base.window);

  return result_OK;
}

/* Point Menu > Configuration at the current kind's dialogue. */
static void texturing_retarget(texturing_task_t *task)
{
  task->menu_items[TEXTURING_MENU_CONFIGURATION].window =
    wuss_dialogue_window(task->confs[task->kind].dialogue);
}

/* confdlg_applied_fn_t: a dialogue's Apply or Default changed params[] */
static result_t texturing_applied(texturing_task_t *task, int old)
{
  NOT_USED(old);

  if (task->base.window == NULL)
    return result_OK; /* window closed while the dialogue lingers */

  texturing_generate(task);
  wuss_window_invalidate_extent(task->base.window);

  return result_OK;
}

static result_t texturing_set_kind(texturing_task_t *task, int kind)
{
  task->kind = kind;
  texturing_retarget(task);
  texturing_generate(task);
  wuss_window_invalidate_extent(task->base.window);

  return result_OK;
}

static result_t texturing_set_scheme(texturing_task_t *task, int scheme)
{
  task->scheme = scheme;
  texturing_build_palette(task);
  wuss_window_invalidate_extent(task->base.window);

  return result_OK;
}

/* G: step to the next grain preset above the slider's value, wrapping. */
static result_t texturing_next_grain(texturing_task_t *task)
{
  int i;

  for (i = 0; i < (int) NELEMS(texturing_grain_presets); i++)
    if (texturing_grain_presets[i] > task->grain)
      break;
  task->grain = i < (int) NELEMS(texturing_grain_presets) ?
                texturing_grain_presets[i] : texturing_grain_presets[0];

  return texturing_applied(task, 0);
}

/* Randomise: fresh seed, kind and scheme. Shared by the menu row and its
 * Ctrl-R shortcut. */
static result_t texturing_randomise(texturing_task_t *task)
{
  rng_t s;

  rng_lcg32(&task->seed);
  rng_seed(&s, task->seed);
  task->kind   = rng_range(&s, TEXTURING_NKINDS);
  task->scheme = rng_range(&s, TEXTURING_NSCHEMES);
  texturing_retarget(task);
  texturing_build_palette(task);
  texturing_generate(task);
  wuss_window_invalidate_extent(task->base.window);

  return result_OK;
}

static result_t texturing_key(texturing_task_t *task, int code)
{
  switch (code)
  {
  case ' ':
    return texturing_reseed(task);

  case 'k':
  case 'K':
    return texturing_set_kind(task, (task->kind + 1) % TEXTURING_NKINDS);

  case 'g':
  case 'G':
    return texturing_next_grain(task);

  case wuss_KEY_LEFT:
    return texturing_set_scheme(task, (task->scheme + TEXTURING_NSCHEMES - 1) %
                                      TEXTURING_NSCHEMES);

  case wuss_KEY_RIGHT:
    return texturing_set_scheme(task, (task->scheme + 1) % TEXTURING_NSCHEMES);

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }
}

static result_t texturing_menu_select(texturing_task_t   *task,
                                      const wuss_event_t *event)
{
  result_t rc;

  if (event->data.menu_select.menu == &task->kind_menu)
  {
    rc = texturing_set_kind(task, event->data.menu_select.index);
    wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->kind_menu,
                                  task->kind);
  }
  else if (event->data.menu_select.menu == &task->scheme_menu)
  {
    rc = texturing_set_scheme(task, event->data.menu_select.index);
    wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->scheme_menu,
                                  task->scheme);
  }
  else if (wuss_menu_select_is(event, &task->base.menu,
                               TEXTURING_MENU_RANDOMISE))
  {
    rc = texturing_randomise(task);
    wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->kind_menu,
                                  task->kind);
    wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->scheme_menu,
                                  task->scheme);
  }
  else
  {
    return result_OK;
  }

  return rc;
}

static result_t texturing_handle(wuss_window_t      *window,
                                 const wuss_event_t *event,
                                 void               *task_data)
{
  result_t          rc;
  texturing_task_t *task;
  int               k;

  task = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_ICON:
  case wuss_EVENT_CLOSE:
  case wuss_EVENT_QUIT:
  case wuss_EVENT_RELAYOUT:
    /* each dialogue ignores what is not its own window's */
    for (k = 0; k < TEXTURING_NCONFS; k++)
    {
      rc = confdlg_event(&task->confs[k], window, event);
      if (rc != result_OK)
        return rc;
    }
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    /* only one dialogue may answer, as answering opens the menu row */
    for (k = 0; k < TEXTURING_NCONFS; k++)
      if (window == wuss_dialogue_window(task->confs[k].dialogue))
        break;
    return confdlg_event(&task->confs[k < TEXTURING_NCONFS ? k : task->kind],
                         window, event);

  case wuss_EVENT_REDRAW:
    return texturing_redraw(event, task);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      wuss_menu_tick_exclusive(&task->kind_menu, task->kind);
      wuss_menu_tick_exclusive(&task->scheme_menu, task->scheme);
      return wuss_menu_open_ticked(task->base.delegate, &task->base.menu, 0,
                                   wuss_get_pointer(task->base.wuss),
                                   &task->base.menu_handle);
    }
    if (event->data.mouse.button & wuss_BUTTON_SELECT)
      return texturing_reseed(task);
    if (event->data.mouse.button & wuss_BUTTON_ADJUST)
      return texturing_set_scheme(task, (task->scheme + 1) % TEXTURING_NSCHEMES);
    return result_OK;

  case wuss_EVENT_KEY:
    if (!task_key_is_plain(task->base.delegate, &task->base.menu, event, &rc))
      return rc;

    return texturing_key(task, event->data.key.code);

  case wuss_EVENT_MENU_SELECT:
    return texturing_menu_select(task, event);

  default:
    return result_OK;
  }
}

/* wuss_saveas_save_fn_t: opaque is the texturing_task_t; writes one tile */
static result_t texturing_saveas_save(const char *path, void *opaque)
{
  texturing_task_t *task;

  task = opaque;

  return bitmap_save_png(&task->bm, path);
}

static result_t texturing_init(texturing_task_t *task)
{
  result_t       rc;
  int            i;
  wuss_window_t *grain_window;

  task->seed   = 0x9E3779B9u;
  task->kind   = TEXTURING_KIND_CLOUDS;
  task->scheme = 0;
  task->grain  = texturing_grain_default;
  memcpy(task->params, texturing_defaults, sizeof(task->params));

  rc = bitmap_init(&task->bm, SIZE2D(TEXTURING_SIZE, TEXTURING_SIZE),
                   pixelfmt_p8, TEXTURING_SIZE, NULL, task->pixels);
  if (rc != result_OK)
    return rc;

  /* borrow the palette: bitmap_init would allocate a copy that nothing
   * frees, as the instance block is freed whole */
  task->bm.palette = task->palette;

  texturing_build_palette(task);
  texturing_generate(task);

  rc = task_window_create(task->base.delegate,
                          SIZE2D(TEXTURING_SIZE * TEXTURING_REPEAT,
                                 TEXTURING_SIZE * TEXTURING_REPEAT),
                          "Texturing", &task->base.window);
  if (rc != result_OK)
    return rc;

  taskapp_save_row(&task->base, task->menu_items, TEXTURING_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  for (i = 0; i < TEXTURING_NKINDS; i++)
    WUSS_MENU_ITEM(task->kind_items, i, texturing_kind_names[i],
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->kind_menu, "Kind", task->kind_items, TEXTURING_NKINDS);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_KIND, "Kind",
                      wuss_MENU_ITEM_DASHED, &task->kind_menu);

  WUSS_MENU_ITEM(task->menu_items, TEXTURING_MENU_CONFIGURATION,
                 "Configuration",
                 wuss_MENU_ITEM_BORROWED_SUBMENU | wuss_MENU_ITEM_PRE_OPEN);

  for (i = 0; i < TEXTURING_NSCHEMES; i++)
    WUSS_MENU_ITEM(task->scheme_items, i, texturing_schemes[i].name,
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->scheme_menu, "Colours", task->scheme_items,
                  TEXTURING_NSCHEMES);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->scheme_menu);

  WUSS_MENU_ITEM(task->menu_items, TEXTURING_MENU_GRAIN, "Grain",
                 wuss_MENU_ITEM_BORROWED_SUBMENU | wuss_MENU_ITEM_PRE_OPEN);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, TEXTURING_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_DASHED, "^R");

  WUSS_MENU_TITLE(task->base.menu, "Texturing", task->menu_items,
                  NELEMS(task->menu_items));

  for (i = 0; i < TEXTURING_NKINDS; i++)
  {
    rc = confdlg_create(&task->confs[i], &task->base, &texturing_confs[i],
                        task->params[i]);
    if (rc != result_OK)
      return rc;
  }

  rc = confdlg_create(&task->confs[TEXTURING_CONF_GRAIN], &task->base,
                      &texturing_confs[TEXTURING_CONF_GRAIN], &task->grain);
  if (rc != result_OK)
    return rc;

  grain_window = wuss_dialogue_window(task->confs[TEXTURING_CONF_GRAIN].dialogue);
  wuss_window_set_title(grain_window, "Grain");
  task->menu_items[TEXTURING_MENU_GRAIN].window = grain_window;

  texturing_retarget(task);

  return result_OK;
}

result_t texturing_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Texturing",
    .purpose       = "Tileable procedural textures",
    .instance_size = sizeof(texturing_task_t),
    .init          = (taskapp_init_fn_t) texturing_init,
    .handle        = texturing_handle,
    .save_name     = "texture.png",
    .save          = texturing_saveas_save
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
