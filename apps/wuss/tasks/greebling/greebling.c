/* wuss/tasks/greebling/greebling.c -- random-scatter greebling pattern task */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/colour.h"
#include "geom/box.h"
#include "utils/rng.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"
#include "greebling-tiles.h"

/* Largest grid the generator will fill; Menu > Size picks the live grid
 * from a table of presets within these caps. */
#define GREEBLING_MAX_COLS 48
#define GREEBLING_MAX_ROWS 48

/* Most base palettes the Palette submenu can list; checked against
 * GREEBLING_NPALETTE at compile time. */
#define GREEBLING_MAX_PALETTES 16

/* Number of presets in the Size submenu. */
#define GREEBLING_NSIZES 4

/* Fills the content area with a Xenon-2-style circuit-greeble texture built
 * as a tile-placement problem. greebling-tiles.h bakes 205 hand-drawn 8x8
 * PICO-8 stamps plus, from a second sheet where the artist laid them out in
 * their intended shapes, a set of prefab blocks and a shortlist of stamps
 * that read well standing alone. The generator scatters the prefab blocks
 * without overlap, then fills every remaining cell from the standalone
 * shortlist. Each stamp is blitted 1:1; its four slots index the current
 * greebling_palettes[] row.
 *
 * Select reseeds the pattern; Adjust steps the base palette, which Menu >
 * Palette also picks directly; Menu > Random palettes toggles per-prefab
 * random palettes; Menu > Size sets the grid (and window) to a preset size;
 * Menu > Save opens a Save As dialogue (drag its icon onto a Filer
 * window, or Save with a full path already typed) that writes the window's
 * content out. Once a click has given the window the input focus, Space
 * reseeds, Left/Right step the base palette and R toggles random
 * palettes. */
typedef struct greebling_task
{
  taskapp_instance_t base;
  rng_t              seed;   /* current pattern seed; advanced on click */
  int                cols, rows; /* live grid extent, <= the MAX_* caps */
  unsigned char      palette;  /* greebling_palettes[] row used */
  int                size;     /* greebling_sizes[] preset for the grid */
  int                random_prefab_palettes; /* nonzero: each scattered prefab
                                              * gets its own random palette */
  /* grid[row][col], one stamp index (0..GREEBLING_NTILES-1) per cell;
   * regenerated whenever the seed changes */
  unsigned char      grid[GREEBLING_MAX_ROWS][GREEBLING_MAX_COLS];
  /* cellpal[row][col], the greebling_palettes[] row to draw each cell with;
   * kept in step with grid[][] */
  unsigned char      cellpal[GREEBLING_MAX_ROWS][GREEBLING_MAX_COLS];
  wuss_menu_item_t   menu_items[5]; /* per-instance: a shared static would
                                     * leak one instance's .window pointer
                                     * into another's menu */
  char               palette_names[GREEBLING_MAX_PALETTES][4]; /* "1".. */
  wuss_menu_item_t   palette_items[GREEBLING_MAX_PALETTES];
  wuss_menu_t        palette_menu;
  wuss_menu_item_t   size_items[GREEBLING_NSIZES];
  wuss_menu_t        size_menu;
}
greebling_task_t;

/* ----------------------------------------------------------------------- */

/* MENU click over the content pops this. "Random palettes" is an independent
 * toggle for per-prefab random palettes. "Info" is a wuss_menu_item_t.window
 * leaf retargeted at the shared proginfo singleton just before the menu
 * opens. The item table and wuss_menu_t live per-instance in greebling_task_t,
 * not as a file-scope static, so that each window's Info row and tick state
 * are its own rather than every instance sharing (and overwriting) one
 * global. */
enum
{
  GREEBLING_MENU_SAVE = 0,
  GREEBLING_MENU_SIZE,
  GREEBLING_MENU_PALETTE,
  GREEBLING_MENU_RANDPAL,
  GREEBLING_MENU_RANDOMISE
};

/* the task block sizes its Palette submenu by GREEBLING_MAX_PALETTES, since
 * greebling-tiles.h (and so GREEBLING_NPALETTE) is private to this file */
typedef char greebling_palette_cap_check[GREEBLING_NPALETTE <= GREEBLING_MAX_PALETTES
                                       ? 1 : -1];

/* Size submenu presets, in tiles; each fits the GREEBLING_MAX_* caps. The
 * last is the full grid and the one a new window opens at. */
static const struct
{
  const char *name;
  int         cols, rows;
}
greebling_sizes[GREEBLING_NSIZES] =
{
  { "16 x 16", 16, 16 },
  { "32 x 24", 32, 24 },
  { "40 x 30", 40, 30 },
  { "48 x 48", 48, 48 }
};

/* ----------------------------------------------------------------------- */

/* cell value for "nothing placed here yet"; stamp indices are 0..204 so 0xFF
 * is free. greebling_redraw skips these. */
#define GREEBLING_EMPTY 0xFF

/* try to drop prefab p with its top-left at (row,col): succeeds only if every
 * non-hole cell of the prefab lands on an in-bounds, still-empty grid cell, so
 * blocks never overlap. pal is the greebling_palettes[] row to tag every cell
 * this block fills with. returns 1 on placement. */
static int greebling_try_prefab(greebling_task_t *task,
                                int               p,
                                int               row,
                                int               col,
                                unsigned char     pal)
{
  const unsigned char *cells;
  int                  w, h, x, y;

  w     = greebling_prefab[p].w;
  h     = greebling_prefab[p].h;
  cells = greebling_prefab_cells + greebling_prefab[p].off;

  if (row < 0 || col < 0 ||
      row + h > task->rows || col + w > task->cols)
    return 0;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      if (cells[y * w + x] != GREEBLING_PREFAB_HOLE &&
          task->grid[row + y][col + x] != GREEBLING_EMPTY)
        return 0;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      if (cells[y * w + x] != GREEBLING_PREFAB_HOLE)
      {
        task->grid[row + y][col + x]    = cells[y * w + x];
        task->cellpal[row + y][col + x] = pal;
      }

  return 1;
}

/* Fill the grid by scattering the artist's prefab blocks (greebling-tiles.h)
 * without overlap, then dropping a loose stamp into every cell no block
 * claimed. The blocks carry the intended circuit shapes; the loose stamps come
 * from greebling_filler[] -- the stamps the artist drew standing alone, i.e. the
 * shortlist that reads well without neighbours. GREEBLING_PREFAB_ATTEMPTS
 * placement tries give a dense but varied cover; more attempts just repaint
 * cells already taken.
 *
 * Blocks are tried largest-footprint first: once small blocks and filler have
 * fragmented the grid a big prefab's bounding box rarely lands wholly empty,
 * so a random prefab order leaves the large shapes almost never placed. The
 * attempt budget is split evenly across prefabs; each prefab gets its slice of
 * random (row,col) tries in descending-area order. */
#define GREEBLING_PREFAB_ATTEMPTS (GREEBLING_MAX_COLS * GREEBLING_MAX_ROWS)

static void greebling_generate(greebling_task_t *task)
{
  rng_t         s;
  int           i;
  unsigned char order[GREEBLING_NPREFAB]; /* prefab indices, largest area first */
  int           tries_each;
  int           r, c;

  rng_seed(&s, task->seed);

  /* insertion-sort a fresh index list by w*h descending; NPREFAB is small and
   * this runs once per regenerate */
  for (i = 0; i < GREEBLING_NPREFAB; i++)
  {
    int a, j;

    a = greebling_prefab[i].w * greebling_prefab[i].h;
    for (j = i; j > 0; j--)
    {
      int b = greebling_prefab[order[j - 1]].w * greebling_prefab[order[j - 1]].h;
      if (b >= a)
        break;
      order[j] = order[j - 1];
    }
    order[j] = (unsigned char) i;
  }

  tries_each = MAX(GREEBLING_NPREFAB > 0
                   ? GREEBLING_PREFAB_ATTEMPTS / GREEBLING_NPREFAB
                   : 0,
                   1);

  /* grid[][] is contiguous; the cols..MAX_COLS tail of each live row is never
   * read, so one clear over the live rows is enough. GREEBLING_EMPTY is a byte
   * value, so memset is exact. cellpal[][] defaults to task->palette; prefab
   * placements overwrite their own cells when random palettes are on. */
  memset(task->grid, GREEBLING_EMPTY,
         (size_t) task->rows * GREEBLING_MAX_COLS);
  memset(task->cellpal, task->palette,
         (size_t) task->rows * GREEBLING_MAX_COLS);

  for (i = 0; i < GREEBLING_NPREFAB; i++)
  {
    int p, t;

    p = order[i];

    for (t = 0; t < tries_each; t++)
    {
      int           row, col;
      unsigned char pal;

      row = rng_range(&s, task->rows);
      col = rng_range(&s, task->cols);

      pal = task->palette;
      if (task->random_prefab_palettes)
        pal = (unsigned char) rng_range(&s, GREEBLING_NPALETTE);

      greebling_try_prefab(task, p, row, col, pal);
    }
  }

  for (r = 0; r < task->rows; r++)
    for (c = 0; c < task->cols; c++)
      if (task->grid[r][c] == GREEBLING_EMPTY)
        task->grid[r][c] =
          greebling_filler[rng_range(&s, GREEBLING_NFILLER)];
}

/* ----------------------------------------------------------------------- */

/* Blit one 8x8 stamp 1:1 at (ox,oy). palette is the four colours for this
 * pattern's greebling_palettes[] row, indexed by the stamp's 2-bit slots.
 * Each row is walked as same-slot runs and drawn with screen_fill_hline
 * (which clips and writes contiguous words) rather than a pixel at a time:
 * a stamp row is 1-4 runs, not 8 clipped stores. */
static void greebling_stamp(screen_t       *scr,
                            const colour_t *palette,
                            int             tile_index,
                            int             ox,
                            int             oy)
{
  const unsigned short *rows;
  int                   x, y;

  rows = greebling_tiles[tile_index];

  for (y = 0; y < GREEBLING_TILE_PX; y++)
  {
    unsigned short bits = rows[y];
    int            run_start = 0;
    int            run_slot = bits & 3;

    for (x = 1; x <= GREEBLING_TILE_PX; x++)
    {
      int slot = (x < GREEBLING_TILE_PX) ? ((bits >> (2 * x)) & 3) : -1;

      if (slot != run_slot)
      {
        screen_fill_hline(scr, ox + run_start, oy + y,
                          x - run_start, palette[run_slot]);
        run_start = x;
        run_slot  = slot;
      }
    }
  }
}

static result_t greebling_redraw(const wuss_event_t *event,
                                 greebling_task_t   *task)
{
  screen_t    *scr;
  const box_t *content, *bounds;
  colour_t     palette[GREEBLING_NPALETTE][4];
  int          p, r, c, sx, sy, ox, oy;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;
  sx      = event->data.redraw.scroll.x;
  sy      = event->data.redraw.scroll.y;

  /* expand every palette row into colour_t once; cells index by cellpal[][] */
  for (p = 0; p < GREEBLING_NPALETTE; p++)
    for (r = 0; r < 4; r++)
    {
      unsigned int v = greebling_palettes[p][r];
      palette[p][r] = colour_rgba((int) ( v        & 0xFF),
                                  (int) ((v >>  8) & 0xFF),
                                  (int) ((v >> 16) & 0xFF),
                                  (int) ((v >> 24) & 0xFF));
    }

  for (r = 0; r < task->rows; r++)
  {
    oy = bounds->y0 - sy + r * GREEBLING_TILE_PX;
    if (oy + GREEBLING_TILE_PX <= content->y0 || oy >= content->y1)
      continue;

    for (c = 0; c < task->cols; c++)
    {
      ox = bounds->x0 - sx + c * GREEBLING_TILE_PX;
      if (ox + GREEBLING_TILE_PX <= content->x0 || ox >= content->x1)
        continue;

      greebling_stamp(scr, palette[task->cellpal[r][c]],
                    task->grid[r][c], ox, oy);
    }
  }

  return result_OK;
}

/* Select: advance to a fresh pattern (new seed, regenerate the grid). */
static result_t greebling_select(greebling_task_t *task,
                                 wuss_window_t    *window)
{
  /* LCG-hop to the next start seed; rng_range's xorshift then walks that.
   * Remap the one zero state so the xorshift never lands on its fixed
   * point. */
  rng_lcg32(&task->seed);
  if (task->seed == 0)
    task->seed = 1;

  greebling_generate(task);
  wuss_window_invalidate_extent(window);

  return result_OK;
}

/* Flip per-prefab random palettes and regenerate from the same seed so it is a
 * straight A/B of the pattern. The shared menu struct's tick is not touched
 * here: a fresh open re-syncs it from task state, and an ADJUST pick reticks
 * the open chain in place (see greebling_menu_select). Shared by the Adjust
 * click and the menu row. */
static result_t greebling_toggle_randpal(greebling_task_t *task,
                                         wuss_window_t    *window)
{
  task->random_prefab_palettes = !task->random_prefab_palettes;
  greebling_generate(task);
  wuss_window_invalidate_extent(window);

  return result_OK;
}

/* Switch the base palette on the current pattern. Regenerate so filler
 * cells (and prefab cells, when random palettes are off) pick up the new row;
 * the seed is unchanged so the layout is identical. Shared by the Adjust
 * click and the Palette submenu. */
static result_t greebling_set_palette(greebling_task_t *task, int index)
{
  task->palette = (unsigned char) index;
  greebling_generate(task);
  wuss_window_invalidate_extent(task->base.window);

  return result_OK;
}

/* Switch the grid to a size preset: regenerate from the same seed, then fit
 * the document extent and the window to it. */
static result_t greebling_set_size(greebling_task_t *task, int index)
{
  result_t rc;
  size2d_t px;

  task->size = index;
  task->cols = greebling_sizes[index].cols;
  task->rows = greebling_sizes[index].rows;
  greebling_generate(task);

  px = SIZE2D(task->cols * GREEBLING_TILE_PX, task->rows * GREEBLING_TILE_PX);

  rc = wuss_window_set_doc(task->base.window, px);
  if (rc != result_OK)
    return rc;

  return wuss_window_resize(task->base.window, px);
}

/* Randomise: fresh seed and a random base palette, then regenerate. Shared by
 * the menu row and its Ctrl-R shortcut. */
static result_t greebling_randomise(greebling_task_t *task)
{
  rng_t s;

  rng_lcg32(&task->seed);
  if (task->seed == 0)
    task->seed = 1;

  rng_seed(&s, task->seed);
  task->palette = (unsigned char) rng_range(&s, GREEBLING_NPALETTE);

  greebling_generate(task);
  wuss_window_invalidate_extent(task->base.window);

  return result_OK;
}

/* Adjust: step to the next base palette. */
static result_t greebling_adjust(greebling_task_t *task)
{
  return greebling_set_palette(task, (task->palette + 1) % GREEBLING_NPALETTE);
}

/* Space reseeds, as Select does; Left/Right step the base palette back and
 * forward. Anything else is passed back unclaimed. */
static result_t greebling_key(greebling_task_t *task,
                              wuss_window_t    *window,
                              int               code)
{
  switch (code)
  {
  case ' ':
    return greebling_select(task, window);

  case wuss_KEY_LEFT:
    return greebling_set_palette(task, (task->palette + GREEBLING_NPALETTE - 1) %
                                     GREEBLING_NPALETTE);

  case wuss_KEY_RIGHT:
    return greebling_adjust(task);

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }
}

/* Menu pick: the Random palettes row toggles per-prefab random palettes; a
 * Palette submenu row picks the base palette. An ADJUST pick keeps the chain
 * open without rebuilding it, so the tick set at open is now
 * stale on screen -- retick the still-open level in place. A SELECT pick has
 * already closed and freed the chain by the time this arrives, so the handle
 * is stale; drop it. */
static result_t greebling_menu_select(greebling_task_t   *task,
                                      const wuss_event_t *event)
{
  result_t rc;

  if (event->data.menu_select.menu == &task->palette_menu)
  {
    rc = greebling_set_palette(task, event->data.menu_select.index);
    wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->palette_menu,
                                  task->palette);
  }
  else if (event->data.menu_select.menu == &task->size_menu)
  {
    rc = greebling_set_size(task, event->data.menu_select.index);
    wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->size_menu,
                                  task->size);
  }
  else if (wuss_menu_select_is(event, &task->base.menu, GREEBLING_MENU_RANDPAL))
  {
    rc = greebling_toggle_randpal(task, task->base.window);
    wuss_menu_tick_item_live(task->base.menu_handle, &task->base.menu,
                             GREEBLING_MENU_RANDPAL,
                             task->random_prefab_palettes);
  }
  else if (wuss_menu_select_is(event, &task->base.menu, GREEBLING_MENU_RANDOMISE))
  {
    rc = greebling_randomise(task);
    wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->palette_menu,
                                  task->palette);
  }
  else
  {
    return result_OK;
  }

  return rc;
}

static result_t greebling_handle(wuss_window_t      *window,
                                 const wuss_event_t *event,
                                 void               *task_data)
{
  greebling_task_t *task;

  task = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return greebling_redraw(event, task);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      unsigned int ticks;

      wuss_menu_tick_exclusive(&task->palette_menu, task->palette);
      wuss_menu_tick_exclusive(&task->size_menu, task->size);

      ticks = task->random_prefab_palettes ? 1u << GREEBLING_MENU_RANDPAL : 0;
      return wuss_menu_open_ticked(task->base.delegate, &task->base.menu, ticks,
                                   wuss_get_pointer(task->base.wuss),
                                   &task->base.menu_handle);
    }
    if (event->data.mouse.button & wuss_BUTTON_SELECT)
      return greebling_select(task, window);
    if (event->data.mouse.button & wuss_BUTTON_ADJUST)
      return greebling_adjust(task);
    return result_OK;

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(task->base.delegate, &task->base.menu, event, &rc))
      return rc;

    return greebling_key(task, window, event->data.key.code);
  }

  case wuss_EVENT_MENU_SELECT:
    return greebling_menu_select(task, event);

  default:
    return result_OK;
  }
}

static result_t greebling_init(greebling_task_t *task)
{
  result_t rc;
  size2d_t grid_px;
  int      i;

  /* open at the last (full grid) size preset */
  task->size = GREEBLING_NSIZES - 1;
  task->cols = greebling_sizes[task->size].cols;
  task->rows = greebling_sizes[task->size].rows;
  grid_px    = SIZE2D(task->cols * GREEBLING_TILE_PX,
                      task->rows * GREEBLING_TILE_PX);

  task->seed = 0x9E3779B9u; /* any nonzero start */

  /* greebling_redraw paints every pixel itself */
  rc = task_window_create(task->base.delegate, grid_px, "Greebling", &task->base.window);
  if (rc != result_OK)
    return rc;

  /* fill the whole document, not just the visible content box: placement
   * can shrink the window below grid_px and the rest scrolls into view */
  greebling_generate(task);

  taskapp_save_row(&task->base, task->menu_items, GREEBLING_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  for (i = 0; i < GREEBLING_NSIZES; i++)
    WUSS_MENU_ITEM(task->size_items, i, greebling_sizes[i].name,
                   wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->size_menu, "Size", task->size_items, GREEBLING_NSIZES);

  WUSS_MENU_ITEM_MENU(task->menu_items, GREEBLING_MENU_SIZE, "Size",
                      wuss_MENU_ITEM_DASHED, &task->size_menu);

  /* ponytail: the palettes are unnamed, so the rows are numbered */
  for (i = 0; i < GREEBLING_NPALETTE; i++)
  {
    snprintf(task->palette_names[i], sizeof(task->palette_names[i]), "%d",
             i + 1);
    WUSS_MENU_ITEM(task->palette_items, i, task->palette_names[i],
                   wuss_MENU_ITEM_NONE);
  }

  WUSS_MENU_TITLE(task->palette_menu, "Palette", task->palette_items,
                 GREEBLING_NPALETTE);

  WUSS_MENU_ITEM_MENU(task->menu_items, GREEBLING_MENU_PALETTE, "Palette",
                      wuss_MENU_ITEM_NONE, &task->palette_menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, GREEBLING_MENU_RANDPAL,
                          "Random palettes", wuss_MENU_ITEM_NONE, "R");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, GREEBLING_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_DASHED, "^R");

  WUSS_MENU_TITLE(task->base.menu, "Greebling", task->menu_items,
                 NELEMS(task->menu_items));

  return result_OK;
}

result_t greebling_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Greebling",
    .purpose       = "Scattered greebling pattern",
    .instance_size = sizeof(greebling_task_t),
    .init          = (taskapp_init_fn_t) greebling_init,
    .handle        = greebling_handle,
    .save_name     = "greebling.png"
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
