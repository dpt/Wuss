/* wuss/icon/iconpool.c -- wuss-wide icon set loaded from a directory of PNGs */

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/result.h"
#include "datastruct/atom.h"
#include "framebuf/bitmap.h"
#include "framebuf/pixelfmt.h"
#include "io/dirscan.h"
#include "io/path.h"

/* pathf returns a single static buffer, so the per-entry join in the
 * callback would clobber a caller's dir pointer if we held it directly --
 * keep our own copy. Bounded by that buffer's own DPTLIB_MAXPATH. */
#define ICONS_DIR_MAX 256

/* Longest leafname loaded, terminator included; longer ones are skipped. */
#define ICONS_LEAF_MAX 128

/* Sheet cell sizes are at most this many digits. */
#define ICONS_DIM_DIGITS 4

typedef char icons_leaf_t[ICONS_LEAF_MAX];

#include "../core/types.h"
#include "../core/alloc.h"
#include "../core/windows.h"

#ifdef WUSS_ICONS

/* State while a set is being built: the dirscan collects the leaves, then
 * they are loaded in strcmp order. The arrays grow via array_grow_alloc. */
typedef struct icons_load
{
  wuss_t                     *wuss;
  char                        dir[ICONS_DIR_MAX]; /* own copy; see above */
  icons_leaf_t               *leaves;   /* owned */
  int                         nleaves;
  int                         leafcap;
  struct atom_set            *names;    /* owned until handed to the pool */
  struct wuss_iconpool_entry *entries;  /* owned */
  int                         nentries;
  int                         entcap;
  bitmap_t                   *bitmaps;  /* owned; every entry's frames */
  int                         n;
  int                         cap;
  result_t                    rc;       /* first failure; stops the walk */
}
icons_load_t;

/* The RLE blit (screen_copy_bitmap_rle) decodes straight into the screen's
 * byte layout -- no per-pixel format convert -- so a compressed icon must
 * already be in screen order. bitmap_load_png yields rgb(x|a)8888; the screen
 * is usually bgr(x|a)8888. Swap R and B in place, keeping the alpha/pad byte
 * (rgbx<->bgrx, rgba<->bgra). Bitmap must be uncompressed and one of those
 * four formats. */
static result_t icons_swap_rb(bitmap_t *bm)
{
  pixelfmt_t to;

  assert(!pixelfmt_is_rle(bm->format));

  switch (bm->format)
  {
  case pixelfmt_rgbx8888: to = pixelfmt_bgrx8888; break;
  case pixelfmt_rgba8888: to = pixelfmt_bgra8888; break;
  case pixelfmt_bgrx8888: to = pixelfmt_rgbx8888; break;
  case pixelfmt_bgra8888: to = pixelfmt_rgba8888; break;
  default: return result_NOT_SUPPORTED;
  }

  return bitmap_convert_in_place(bm, to);
}

/* True when "a" and "b" are 32bpp RGB(A/X) formats of opposite R/B order. */
static int icons_rb_swapped(pixelfmt_t a, pixelfmt_t b)
{
  return (a == pixelfmt_rgbx8888 && b == pixelfmt_bgrx8888) ||
         (a == pixelfmt_bgrx8888 && b == pixelfmt_rgbx8888) ||
         (a == pixelfmt_rgba8888 && b == pixelfmt_bgra8888) ||
         (a == pixelfmt_bgra8888 && b == pixelfmt_rgba8888) ||
         (a == pixelfmt_rgbx8888 && b == pixelfmt_bgra8888) ||
         (a == pixelfmt_rgba8888 && b == pixelfmt_bgrx8888) ||
         (a == pixelfmt_bgrx8888 && b == pixelfmt_rgba8888) ||
         (a == pixelfmt_bgra8888 && b == pixelfmt_rgbx8888);
}

/* The channel order a 32bpp icon must hold to blit onto screen format scr:
 * the screen's own on a 32bpp screen, else rgba -- the paletted RLE blits
 * decode into an rgba8888 scratch row. */
static pixelfmt_t icons_order_for(pixelfmt_t scr)
{
  return (pixelfmt_log2bpp(scr) == 5) ? scr : pixelfmt_rgba8888;
}

/* Free what bitmap_load_png gave a pool bitmap. */
static void icons_free_bitmap(bitmap_t *bm)
{
  free(bm->base);
  free(bm->palette);
}

/* dirscan callback: note each ".png" leaf for loading once all are known. */
static result_t icons_collect(const char *leaf, void *opaque)
{
  icons_load_t *st = opaque;
  char          name[ICONS_LEAF_MAX];

  if (!path_leaf_strip_ext(leaf, ".png", name, sizeof(name)) ||
      strlen(leaf) >= ICONS_LEAF_MAX)
    return result_OK; /* not a ".png", or name too long -- skip */

  if (array_grow_alloc(&st->wuss->alloc, (void **) &st->leaves,
                       sizeof(*st->leaves), st->nleaves, &st->leafcap, 1,
                       8) != 0)
  {
    st->rc = result_OOM;
    return result_STOP_WALK;
  }

  strcpy(st->leaves[st->nleaves++], leaf);
  return result_OK;
}

static int icons_leaf_compare(const void *a, const void *b)
{
  return strcmp(a, b);
}

/* Parse a cell dimension at *p: decimal, no leading zero, at most
 * ICONS_DIM_DIGITS digits. Advances *p past it. Returns 0 if there is none. */
static int icons_parse_dim(const char **p)
{
  char *end;
  long  v;

  /* strtol alone would take a sign, spaces and leading zeros */
  if (!isdigit((unsigned char) **p) || **p == '0')
    return 0;

  v = strtol(*p, &end, 10);
  if (end - *p > ICONS_DIM_DIGITS)
    return 0;

  *p = end;
  return (int) v;
}

/* If name ends in "_<w>x<h>" it names a sprite sheet: cut the suffix off
 * name, set cell and return non-zero. */
static int icons_parse_sheet(char *name, size2d_t *cell)
{
  char       *us;
  const char *p;

  *cell = SIZE2D(0, 0); /* set on every path, for compilers that cannot see
                         * the result */

  us = strrchr(name, '_');
  if (us == NULL || us == name)
    return 0;

  p       = us + 1;
  cell->w = icons_parse_dim(&p);
  if (cell->w == 0 || *p++ != 'x')
    return 0;
  cell->h = icons_parse_dim(&p);
  if (cell->h == 0 || *p != '\0')
    return 0;

  *us = '\0';
  return 1;
}

/* Compress bm and append it to the frames. Takes ownership of bm's pixels,
 * freeing them on failure. */
static result_t icons_push_frame(icons_load_t *st, bitmap_t *bm)
{
  result_t rc;

  /* paletted formats can't be RLE-compressed: keep those uncompressed */
  rc = bitmap_compress(bm);
  if (rc != result_OK && rc != result_NOT_SUPPORTED)
  {
    icons_free_bitmap(bm);
    return rc;
  }

  if (array_grow_alloc(&st->wuss->alloc, (void **) &st->bitmaps,
                       sizeof(*st->bitmaps), st->n, &st->cap, 1, 8) != 0)
  {
    icons_free_bitmap(bm);
    return result_OOM;
  }

  st->bitmaps[st->n++] = *bm;
  return result_OK;
}

/* Split sheet into cell-sized frames, row-major, appending them for entry e
 * and setting its grid. Partial edge cells are dropped. A sheet that yields
 * no frames is skipped, leaving e's grid empty. */
static result_t icons_split(icons_load_t               *st,
                            const char                 *leaf,
                            const bitmap_t             *sheet,
                            size2d_t                    cell,
                            struct wuss_iconpool_entry *e)
{
  result_t rc;
  box_t    box;
  bitmap_t frame;
  int      r, c;

  e->cols = sheet->size.w / cell.w;
  e->rows = sheet->size.h / cell.h;
  if (e->cols == 0 || e->rows == 0)
  {
    logf_warning("iconpool: %s is smaller than one cell; skipped", leaf);
    e->cols = e->rows = 0;
    return result_OK;
  }
  if (sheet->size.w % cell.w != 0 || sheet->size.h % cell.h != 0)
    logf_warning("iconpool: %s is not a whole number of cells; partial "
                 "cells dropped", leaf);

  for (r = 0; r < e->rows; r++)
    for (c = 0; c < e->cols; c++)
    {
      box.x0 = c * cell.w;
      box.y0 = r * cell.h;
      box.x1 = box.x0 + cell.w;
      box.y1 = box.y0 + cell.h;

      /* ponytail: a paletted frame gets its own copy of the sheet's palette;
       * share one if many paletted sheets make that matter */
      rc = bitmap_extract(sheet, &box, &frame);
      if (rc == result_NOT_SUPPORTED) /* sub-byte cells off a byte */
      {
        logf_warning("iconpool: %s has cells off byte boundaries; skipped",
                     leaf);
        while (st->n > e->first)
          icons_free_bitmap(&st->bitmaps[--st->n]);
        e->cols = e->rows = 0;
        return result_OK;
      }
      if (rc == result_OK)
        rc = icons_push_frame(st, &frame);
      if (rc != result_OK)
        return rc;
    }

  return result_OK;
}

/* Load one collected leaf as a new entry. */
static result_t icons_load_leaf(icons_load_t *st, const char *leaf)
{
  result_t rc;
  char     name[ICONS_LEAF_MAX];
  size2d_t cell;
  int      sheet;
  bitmap_t bm;
  struct wuss_iconpool_entry e;
  atom_t                     atom;

  (void) path_leaf_strip_ext(leaf, ".png", name, sizeof(name)); /* checked */
  sheet = icons_parse_sheet(name, &cell);

  /* leaves arrive in strcmp order, so of two sharing a name the first wins */
  if (atom_for_block(st->names, (const unsigned char *) name,
                     strlen(name) + 1) != atom_NOT_FOUND)
  {
    logf_warning("iconpool: %s repeats the name \"%s\"; skipped", leaf, name);
    return result_OK;
  }

  rc = bitmap_load_png(&bm, pathf("%s/%s", st->dir, leaf));
  if (rc != result_OK)
    return rc;

  /* bring the pixels into screen byte order before compressing -- see
   * icons_swap_rb. Only the R/B-swapped 32bpp case is handled; a matching
   * format needs nothing, anything else is left for the blit to reject. */
  if (pixelfmt_log2bpp(bm.format) == 5 &&
      icons_rb_swapped(bm.format, icons_order_for(st->wuss->scr->format)))
    (void) icons_swap_rb(&bm); /* icons_rb_swapped admits only its formats */

  /* the RLE codec only skips wholly-zero pixels and the 32bpp RLE blit draws
   * the rest as is, so a colour key kept under alpha 0 would be drawn */
  bitmap_clear_transparent(&bm);

  e.first = st->n;
  if (sheet)
  {
    rc = icons_split(st, leaf, &bm, cell, &e);
    icons_free_bitmap(&bm);
  }
  else
  {
    e.cols = e.rows = 1;
    rc     = icons_push_frame(st, &bm);
  }
  if (rc != result_OK)
    return rc;
  if (e.cols == 0)
    return result_OK; /* skipped */

  rc = atom_new(st->names, (const unsigned char *) name, strlen(name) + 1,
                &atom);
  if (rc != result_OK)
    return rc;
  e.atom = (int) atom;

  if (array_grow_alloc(&st->wuss->alloc, (void **) &st->entries,
                       sizeof(*st->entries), st->nentries, &st->entcap, 1,
                       8) != 0)
    return result_OOM;

  st->entries[st->nentries++] = e;
  return result_OK;
}

void wuss__iconpool_clear(wuss_t *wuss, struct wuss_iconpool *pool)
{
  int i;

  assert(wuss != NULL);
  assert(pool != NULL);

  for (i = 0; i < pool->nbitmaps; i++)
    icons_free_bitmap(&pool->bitmaps[i]);

  wuss__free(wuss, pool->bitmaps);
  wuss__free(wuss, pool->entries);
  pool->bitmaps  = NULL;
  pool->nbitmaps = 0;
  pool->entries  = NULL;
  pool->nentries = 0;

  atom_destroy(pool->names);
  pool->names = NULL;
}

static result_t icons_match_pool(pixelfmt_t            order,
                                 struct wuss_iconpool *pool)
{
  result_t  rc;
  bitmap_t *bm;
  int       i;

  for (i = 0; i < pool->nbitmaps; i++)
  {
    bm = &pool->bitmaps[i];
    if (pixelfmt_log2bpp(pixelfmt_base(bm->format)) != 5 ||
        !icons_rb_swapped(pixelfmt_base(bm->format), order))
      continue;

    /* in place: icons hold pointers to these bitmap_t structs, so only
     * their pixel stores may move */
    rc = bitmap_decompress(bm);
    if (rc != result_OK)
      return rc;

    rc = icons_swap_rb(bm);
    if (rc != result_OK)
      return rc;

    rc = bitmap_compress(bm);
    if (rc != result_OK)
      return rc;
  }

  return result_OK;
}

result_t wuss__iconpool_match_screen(wuss_t *wuss)
{
  result_t   rc;
  pixelfmt_t order;
  list_t    *e;

  assert(wuss != NULL);

  order = icons_order_for(wuss->scr->format);

  rc = icons_match_pool(order, &wuss->icon);
  if (rc != result_OK)
    return rc;

  for (e = wuss->tasks.next; e != NULL; e = e->next)
  {
    rc = icons_match_pool(order, &wuss__task_from_link(e)->iconpool);
    if (rc != result_OK)
      return rc;
  }

  return result_OK;
}

/* Order the entries by name (strcmp): a sheet's suffix is cut from its name,
 * so leaf order is not quite name order. Insertion sort: a handful of icons.
 * The frames stay put. */
static void icons_sort(struct atom_set            *names,
                       struct wuss_iconpool_entry *entries,
                       int                         n)
{
  struct wuss_iconpool_entry e;
  const char *name;
  int         i, j;

  for (i = 1; i < n; i++)
  {
    e    = entries[i];
    name = (const char *) atom_get(names, (atom_t) e.atom, NULL);

    for (j = i; j > 0; j--)
    {
      const char *prev;

      prev = (const char *) atom_get(names, (atom_t) entries[j - 1].atom,
                                     NULL);
      if (strcmp(prev, name) <= 0)
        break;

      entries[j] = entries[j - 1];
    }

    entries[j] = e;
  }
}

/* Build "pool" from every "*.png" in dir, replacing any previous contents. */
static result_t icons_load(wuss_t               *wuss,
                           struct wuss_iconpool *pool,
                           const char           *dir)
{
  result_t     rc;
  icons_load_t st;
  int          i;

  if (strlen(dir) >= sizeof(st.dir))
    return result_BAD_ARG;

  wuss__iconpool_clear(wuss, pool);

  st.wuss     = wuss;
  st.leaves   = NULL;
  st.nleaves  = 0;
  st.leafcap  = 0;
  st.names    = atom_create();
  st.entries  = NULL;
  st.nentries = 0;
  st.entcap   = 0;
  st.bitmaps  = NULL;
  st.n        = 0;
  st.cap      = 0;
  st.rc       = result_OK;
  strcpy(st.dir, dir);

  if (st.names == NULL)
    return result_OOM;

  rc = dirscan_walk(dir, icons_collect, &st);

  /* A missing directory just yields no entries. */
  if (rc == result_FILE_NOT_FOUND)
    rc = result_OK;

  if (rc == result_OK && st.rc != result_OK)
    rc = st.rc;

  /* in strcmp order, so which of two leaves sharing a name wins does not
   * depend on the platform's directory scan order */
  if (rc == result_OK)
  {
    qsort(st.leaves, (size_t) st.nleaves, sizeof(*st.leaves),
          icons_leaf_compare);
    for (i = 0; i < st.nleaves && rc == result_OK; i++)
      rc = icons_load_leaf(&st, st.leaves[i]);
  }

  wuss__free(wuss, st.leaves);

  if (rc != result_OK)
  {
    for (i = 0; i < st.n; i++)
      icons_free_bitmap(&st.bitmaps[i]);
    wuss__free(wuss, st.bitmaps);
    wuss__free(wuss, st.entries);
    atom_destroy(st.names);
    return rc;
  }

  icons_sort(st.names, st.entries, st.nentries);

  pool->names    = st.names;
  pool->entries  = st.entries;
  pool->nentries = st.nentries;
  pool->bitmaps  = st.bitmaps;
  pool->nbitmaps = st.n;
  return result_OK;
}

result_t wuss_iconpool_load(wuss_t *wuss, const char *dir)
{
  assert(wuss != NULL);

  if (dir == NULL)
    return result_NULL_ARG;

  return icons_load(wuss, &wuss->icon, dir);
}

/* Copy "<resources>/resources/<sub>" into dir (size ICONS_DIR_MAX): pathf
 * hands back one shared static buffer, and the per-file joins would clobber
 * it mid-load. */
static void icons_resource_dir(char       *dir,
                               const char *resources,
                               const char *sub)
{
  strncpy(dir, pathf("%s/resources/%s", resources, sub), ICONS_DIR_MAX - 1);
  dir[ICONS_DIR_MAX - 1] = '\0';
}

result_t wuss_iconpool_load_resource(wuss_t *wuss, const char *resources)
{
  char dir[ICONS_DIR_MAX];

  assert(wuss != NULL);

  if (resources == NULL)
    return result_NULL_ARG;

  icons_resource_dir(dir, resources, "wuss/iconpool");

  return wuss_iconpool_load(wuss, dir);
}

/* Index of "name" in pool, or -1. */
static int icons_find(const struct wuss_iconpool *pool, const char *name)
{
  atom_t atom;
  int    i;

  if (name == NULL || pool->names == NULL)
    return -1;

  atom = atom_for_block(pool->names, (const unsigned char *) name,
                        strlen(name) + 1);
  if (atom == atom_NOT_FOUND)
    return -1;

  /* atom values are not array indices -- find which entry holds it. A handful
   * of icons, so a linear scan is cheaper than a second index structure. */
  for (i = 0; i < pool->nentries; i++)
    if (pool->entries[i].atom == (int) atom)
      return i;

  return -1;
}

/* Frame "frame" of entry "index" in pool, or NULL if either is out of range.
 */
static const bitmap_t *icons_frame(const struct wuss_iconpool *pool,
                                   int                         index,
                                   int                         frame)
{
  const struct wuss_iconpool_entry *e;

  if (index < 0 || index >= pool->nentries)
    return NULL;

  e = &pool->entries[index];
  if (frame < 0 || frame >= e->cols * e->rows)
    return NULL;

  return &pool->bitmaps[e->first + frame];
}

int wuss_iconpool_count(const wuss_t *wuss)
{
  assert(wuss != NULL);
  return wuss->icon.nentries;
}

int wuss_iconpool_lookup(const wuss_t *wuss, const char *name)
{
  assert(wuss != NULL);

  return icons_find(&wuss->icon, name);
}

const bitmap_t *wuss_iconpool_bitmap(const wuss_t *wuss, int index)
{
  assert(wuss != NULL);

  return icons_frame(&wuss->icon, index, 0);
}

const bitmap_t *wuss_iconpool_frame(const wuss_t *wuss, int index, int frame)
{
  assert(wuss != NULL);

  return icons_frame(&wuss->icon, index, frame);
}

const char *wuss_iconpool_name(const wuss_t *wuss, int index)
{
  assert(wuss != NULL);

  if (index < 0 || index >= wuss->icon.nentries)
    return NULL;

  return (const char *) atom_get(wuss->icon.names,
                                 (atom_t) wuss->icon.entries[index].atom,
                                 NULL);
}

/* ----------------------------------------------------------------------- */

result_t wuss_task_iconpool_load(wuss_task_t *task, const char *dir)
{
  assert(task != NULL);

  if (dir == NULL)
    return result_NULL_ARG;

  return icons_load(task->wuss, &task->iconpool, dir);
}

result_t wuss_task_iconpool_load_resource(wuss_task_t *task,
                                          const char  *resources)
{
  char  sub[ICONS_DIR_MAX];
  char *p;
  char  dir[ICONS_DIR_MAX];

  assert(task != NULL);

  if (resources == NULL)
    return result_NULL_ARG;

  if (task->name == NULL)
    return result_BAD_ARG;

  if ((size_t) snprintf(sub, sizeof(sub), "wuss-tasks/%s/iconpool",
                        task->name) >= sizeof(sub))
    return result_BAD_ARG;

  /* task names are for display; their directories are lower case */
  for (p = sub; *p != '\0'; p++)
    *p = (char) tolower((unsigned char) *p);

  icons_resource_dir(dir, resources, sub);

  return wuss_task_iconpool_load(task, dir);
}

const bitmap_t *wuss__iconpool_find(const wuss_t      *wuss,
                                    const wuss_task_t *task,
                                    const char        *name,
                                    int                frame)
{
  int idx;

  assert(wuss != NULL);

  if (task != NULL)
  {
    idx = icons_find(&task->iconpool, name);
    if (idx >= 0)
      return icons_frame(&task->iconpool, idx, frame);
  }

  return icons_frame(&wuss->icon, icons_find(&wuss->icon, name), frame);
}

const bitmap_t *wuss_task_iconpool_find(const wuss_task_t *task,
                                        const char        *name)
{
  assert(task != NULL);

  return wuss__iconpool_find(task->wuss, task, name, 0);
}

const bitmap_t *wuss_task_iconpool_frame(const wuss_task_t *task,
                                         const char        *name,
                                         int                frame)
{
  assert(task != NULL);

  return wuss__iconpool_find(task->wuss, task, name, frame);
}

#endif /* WUSS_ICONS */
