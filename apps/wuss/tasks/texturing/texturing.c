/* wuss/tasks/texturing/texturing.c -- tileable procedural texture task */

#ifdef WUSS_APP

#include <limits.h>

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
#include "../common/taskapp.h"
#include "../common/common.h"

/* Texture edge in pixels, as a power of two so that every lattice step is a
 * shift and a mask. */
#define TEXTURING_LOG2_SIZE 8
#define TEXTURING_SIZE      (1 << TEXTURING_LOG2_SIZE)

/* The window shows the texture repeated this many times each way, so that
 * any seam would show. */
#define TEXTURING_REPEAT 2

/* Lattice cells across the tile in the coarsest noise octave (log2). */
#define TEXTURING_LOG2_BASE 2

/* Feature points across the tile in Cells (log2). */
#define TEXTURING_LOG2_CELLS 3

/* Fills the content area with a procedural texture that tiles seamlessly:
 * every noise lattice wraps at the tile edge. The texture is generated once
 * per change as 8-bit values into a pixelfmt_p8 bitmap; the colour scheme
 * is only its palette, so a scheme change redraws without regenerating.
 *
 * SELECT reseeds; ADJUST steps the colour scheme. Menu > Kind and Menu >
 * Colours pick directly; Menu > Randomise picks a fresh seed, kind and
 * scheme; Menu > Save writes a single tile as a PNG. Once a click has given
 * the window the input focus, Space reseeds, Left/Right step the colour
 * scheme and K steps the kind. */

enum
{
  TEXTURING_KIND_CLOUDS,
  TEXTURING_KIND_MARBLE,
  TEXTURING_KIND_WOOD,
  TEXTURING_KIND_CELLS,
  TEXTURING_NKINDS
};

static const char *texturing_kind_names[TEXTURING_NKINDS] =
{
  "Clouds", "Marble", "Wood", "Cells"
};

/* Colour schemes: three stops, at values 0, 128 and 255, given as 0xRRGGBB. */
static const struct
{
  const char  *name;
  unsigned int stops[3];
}
texturing_schemes[] =
{
  { "Sky",   { 0x1E4FA0, 0x7FB0E8, 0xFFFFFF } },
  { "Stone", { 0x202020, 0x808078, 0xE8E8E0 } },
  { "Wood",  { 0x3A1E0C, 0x8A5528, 0xD8A868 } },
  { "Lava",  { 0x000000, 0xC02000, 0xFFE040 } },
  { "Moss",  { 0x102008, 0x4A7A20, 0xC8E088 } },
  { "Ice",   { 0x002838, 0x40A0C0, 0xF0FFFF } }
};

#define TEXTURING_NSCHEMES ((int) NELEMS(texturing_schemes))

enum
{
  TEXTURING_MENU_SAVE = 0,
  TEXTURING_MENU_KIND,
  TEXTURING_MENU_COLOURS,
  TEXTURING_MENU_RANDOMISE
};

typedef struct texturing_task
{
  taskapp_instance_t base;
  rng_t              seed;
  int                kind;   /* TEXTURING_KIND_* */
  int                scheme; /* texturing_schemes[] row */
  colour_t           palette[256];
  bitmap_t           bm;     /* wraps pixels[] and palette[] */
  unsigned char      pixels[TEXTURING_SIZE * TEXTURING_SIZE];
  wuss_menu_item_t   menu_items[4];
  wuss_menu_item_t   kind_items[TEXTURING_NKINDS];
  wuss_menu_t        kind_menu;
  wuss_menu_item_t   scheme_items[NELEMS(texturing_schemes)];
  wuss_menu_t        scheme_menu;
}
texturing_task_t;

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

/* Fractal sum of noise octaves, 0..~254: each octave doubles the lattice
 * and halves the weight, down to one-pixel cells. */
static int texturing_fbm(uint32_t seed, int x, int y)
{
  int sum, log2n, weight;

  sum    = 0;
  weight = 128;
  for (log2n = TEXTURING_LOG2_BASE; log2n <= TEXTURING_LOG2_SIZE; log2n++)
  {
    sum    += texturing_noise(seed + (uint32_t) log2n, x, y, log2n) * weight;
    weight >>= 1;
  }

  return sum >> 8;
}

/* Fold a value into a 0..255 triangle wave of period 512. */
static int texturing_fold(int v)
{
  v &= 511;
  return v < 256 ? v : 511 - v;
}

/* Squared distance from (x,y) to the nearest jittered feature point, one
 * per cell of a wrapping grid, scaled to 0..255. */
static int texturing_cells(uint32_t seed, int x, int y)
{
  int log2c, cx, cy, best, i, j;

  log2c = TEXTURING_LOG2_SIZE - TEXTURING_LOG2_CELLS;
  cx    = x >> log2c;
  cy    = y >> log2c;
  best  = INT_MAX;

  for (j = -1; j <= 1; j++)
    for (i = -1; i <= 1; i++)
    {
      uint32_t h;
      int      px, py, dx, dy;

      /* point position in unwrapped pixels, hashed from the wrapped cell */
      h  = rng_hash32(seed, (((cy + j) & ((1 << TEXTURING_LOG2_CELLS) - 1)) <<
                             TEXTURING_LOG2_CELLS) |
                            ((cx + i) & ((1 << TEXTURING_LOG2_CELLS) - 1)));
      px = (cx + i) * (1 << log2c) + (int) (h & ((1u << log2c) - 1));
      py = (cy + j) * (1 << log2c) + (int) ((h >> 16) & ((1u << log2c) - 1));
      dx = px - x;
      dy = py - y;
      best = MIN(best, dx * dx + dy * dy);
    }

  /* ponytail: fixed scale for 32-pixel cells; nearest points rarely sit
   * more than 32 pixels away, so the clamp seldom bites */
  return MIN(best >> 2, 255);
}

static void texturing_generate(texturing_task_t *task)
{
  unsigned char *p;
  int            x, y, v;

  p = task->pixels;
  for (y = 0; y < TEXTURING_SIZE; y++)
    for (x = 0; x < TEXTURING_SIZE; x++)
    {
      switch (task->kind)
      {
      default:
      case TEXTURING_KIND_CLOUDS:
        v = texturing_fbm(task->seed, x, y);
        break;

      case TEXTURING_KIND_MARBLE:
        /* four veins across the tile, bent by turbulence */
        v = texturing_fold(x * 4 * 512 / TEXTURING_SIZE +
                           texturing_fbm(task->seed, x, y) * 4);
        break;

      case TEXTURING_KIND_WOOD:
        /* noise sliced into bands, roughened by a fine grain */
        v = texturing_fold(texturing_fbm(task->seed, x, y) * 16 +
                           texturing_noise(task->seed ^ 0x55u, x, y, 6) / 4);
        break;

      case TEXTURING_KIND_CELLS:
        v = texturing_cells(task->seed, x, y);
        break;
      }

      *p++ = (unsigned char) v;
    }
}

/* Build the 256-entry palette from the current scheme's three stops. */
static void texturing_build_palette(texturing_task_t *task)
{
  const unsigned int *stops;
  colour_t            c[3];
  int                 i;

  stops = texturing_schemes[task->scheme].stops;
  for (i = 0; i < 3; i++)
    c[i] = colour_rgb((stops[i] >> 16) & 0xFF,
                      (stops[i] >>  8) & 0xFF,
                       stops[i]        & 0xFF);

  for (i = 0; i < 256; i++)
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

static result_t texturing_set_kind(texturing_task_t *task, int kind)
{
  task->kind = kind;
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

/* Randomise: fresh seed, kind and scheme. Shared by the menu row and its
 * Ctrl-R shortcut. */
static result_t texturing_randomise(texturing_task_t *task)
{
  rng_t s;

  rng_lcg32(&task->seed);
  rng_seed(&s, task->seed);
  task->kind   = rng_range(&s, TEXTURING_NKINDS);
  task->scheme = rng_range(&s, TEXTURING_NSCHEMES);
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
  texturing_task_t *task;

  NOT_USED(window);

  task = task_data;

  switch (event->kind)
  {
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
  {
    result_t rc;

    if (!task_key_is_plain(task->base.delegate, &task->base.menu, event, &rc))
      return rc;

    return texturing_key(task, event->data.key.code);
  }

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
  result_t rc;
  int      i;

  task->seed   = 0x9E3779B9u;
  task->kind   = TEXTURING_KIND_CLOUDS;
  task->scheme = 0;

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

  for (i = 0; i < TEXTURING_NSCHEMES; i++)
    WUSS_MENU_ITEM(task->scheme_items, i, texturing_schemes[i].name,
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->scheme_menu, "Colours", task->scheme_items,
                  TEXTURING_NSCHEMES);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->scheme_menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, TEXTURING_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_DASHED, "^R");

  WUSS_MENU_TITLE(task->base.menu, "Texturing", task->menu_items,
                  NELEMS(task->menu_items));

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
