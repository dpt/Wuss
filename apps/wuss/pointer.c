/* wuss/pointer.c -- software pointer for the Wuss demo */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "wuss/icon.h"
#include "wuss/wuss.h"

#include "frontend.h"
#include "tasks/tasks.h"

#include "pointer.h"

/* ----------------------------------------------------------------------- */

static struct
{
  const bitmap_t *image;    /* the arrow, from the icon pool; hotspot at
                             * its top-left; NULL if the pool lacks it */
  const bitmap_t *hourglass; /* shown instead while busy; hotspot at its
                              * centre; NULL if the pool lacks it */
  bool           busy;      /* see pointer_set_busy */
  bool           loaded;
  unsigned char *under;     /* framebuffer bytes beneath the drawn arrow */
  box_t          drawn;     /* where it was drawn, if is_drawn */
  bool           is_drawn;
  point_t        pos;       /* last reported mouse position */
  bool           in_window; /* false until a mouse event, or after a
                             * leave */
}
g_pointer;

/* ----------------------------------------------------------------------- */

void pointer_load(wuss_t *wuss)
{
  size2d_t most;

  g_pointer.image     = wuss_iconpool_bitmap(wuss,
                          wuss_iconpool_lookup(wuss, "pointer"));
  g_pointer.hourglass = wuss_iconpool_bitmap(wuss,
                          wuss_iconpool_lookup(wuss, "hourglass"));
  if (g_pointer.image == NULL)
  {
    logf_error("wuss: icon 'pointer' unavailable -- "
               "software pointer unavailable");
  }
  else
  {
    if (g_pointer.hourglass == NULL)
      logf_warning("wuss: icon 'hourglass' unavailable -- "
                   "software pointer stays an arrow while busy");

    /* under holds the bytes beneath either image at any depth: at most 4
     * per pixel, which also covers a sub-byte format's extra partial byte */
    most = g_pointer.image->size;
    if (g_pointer.hourglass != NULL)
    {
      most.w = MAX(most.w, g_pointer.hourglass->size.w);
      most.h = MAX(most.h, g_pointer.hourglass->size.h);
    }
    g_pointer.under  = malloc((size_t) most.w * 4 * most.h);
    g_pointer.loaded = (g_pointer.under != NULL);
  }
}

void pointer_free(void)
{
  free(g_pointer.under);
}

void pointer_moved(point_t pos)
{
  g_pointer.pos       = pos;
  g_pointer.in_window = true;
}

void pointer_left(void)
{
  g_pointer.in_window = false;
}

/* ----------------------------------------------------------------------- */

/* Copy the framebuffer bytes spanning box to (save) or from g_pointer.under.
 * Whole bytes, so a sub-byte format needs no masking: the stray pixels
 * either side go back exactly as they were saved. */
static void pointer_copy_under(bitmap_t *bm, const box_t *box, bool save)
{
  int            log2bpp;
  int            bx0, nbytes;
  unsigned char *under;
  int            y;
  unsigned char *row;

  log2bpp = pixelfmt_log2bpp(bm->format);
  bx0     = (box->x0 << log2bpp) >> 3;
  nbytes  = (((box->x1 << log2bpp) + 7) >> 3) - bx0;
  under   = g_pointer.under;
  for (y = box->y0; y < box->y1; y++)
  {
    row = (unsigned char *) bm->base + y * bm->rowbytes + bx0;
    if (save)
      memcpy(under, row, nbytes);
    else
      memcpy(row, under, nbytes);
    under += nbytes;
  }
}

bool pointer_undraw(bitmap_t *bm, box_t *box)
{
  if (!g_pointer.is_drawn)
    return false;

  pointer_copy_under(bm, &g_pointer.drawn, false);
  g_pointer.is_drawn = false;
  *box = g_pointer.drawn;
  return true;
}

bool pointer_draw(bitmap_t *bm, box_t *box)
{
  const bitmap_t *image;
  point_t         at;
  box_t           whole;
  box_t           screen;
  screen_t        scr;

  if (!g_tasks.pointer || !g_pointer.in_window)
    return false;

  image = g_pointer.image;
  at    = g_pointer.pos;
  if (g_pointer.busy && g_pointer.hourglass != NULL)
  {
    image = g_pointer.hourglass;
    at.x -= image->size.w / 2;
    at.y -= image->size.h / 2;
  }

  whole.x0  = at.x;
  whole.y0  = at.y;
  whole.x1  = at.x + image->size.w;
  whole.y1  = at.y + image->size.h;
  screen.x0 = 0;
  screen.y0 = 0;
  screen.x1 = bm->size.w;
  screen.y1 = bm->size.h;
  if (box_intersection(&whole, &screen, box))
    return false;

  pointer_copy_under(bm, box, true);

  /* a private screen_t: wuss's own may be left clipped to its last redraw */
  screen_for_bitmap(&scr, bm);
  (void) screen_copy_bitmap(&scr, at.x, at.y, image);

  g_pointer.drawn    = *box;
  g_pointer.is_drawn = true;
  return true;
}

/* The main loop is about to block (or has just unblocked), so the pointer
 * must change on screen now rather than at the end of the frame. The frame
 * undraws the pointer before wuss runs, which is when a task turns the
 * hourglass on, so this leaves it as undrawn as it found it: the frame's own
 * pointer_draw then saves the right pixels. */
void pointer_set_busy(bool busy)
{
  bool  was_drawn;
  box_t dirty;
  bool  have_dirty;
  box_t box;

  g_pointer.busy = busy;
  if (!g_tasks.pointer || !g_pointer.loaded || g_pointer.hourglass == NULL)
    return;

  /* where the screen last showed the pointer, drawn now or not */
  was_drawn  = pointer_undraw(g_tasks.bm, &dirty);
  have_dirty = was_drawn || !box_is_empty(&g_pointer.drawn);
  if (!was_drawn)
    dirty = g_pointer.drawn;

  if (pointer_draw(g_tasks.bm, &box))
  {
    if (have_dirty)
      box_union(&dirty, &box, &dirty);
    else
      dirty = box;
    have_dirty = true;
  }

  if (have_dirty)
    wuss_frontend_present(g_tasks.frontend, g_tasks.bm, &dirty);

  if (!was_drawn)
    (void) pointer_undraw(g_tasks.bm, &box);
}

bool app_set_pointer(bool on)
{
  return wuss_frontend_hide_pointer(g_tasks.frontend, on && g_pointer.loaded);
}

/* Only the software pointer can be held: the hardware one goes where the
 * mouse does. So the hidden hardware pointer is warped back to the edge with
 * it, leaving no dead zone to cross before a reversal moves the arrow --
 * except under a script, whose input is not the mouse's. */
point_t pointer_clamp(wuss_t *wuss, point_t p, bool scripted)
{
  box_t   bounds;
  point_t clamped;

  if (!g_tasks.pointer || !wuss_get_pointer_bounds(wuss, &bounds))
    return p;

  clamped.x = CLAMP(p.x, bounds.x0, bounds.x1 - 1);
  clamped.y = CLAMP(p.y, bounds.y0, bounds.y1 - 1);
  if (!scripted && (clamped.x != p.x || clamped.y != p.y))
    wuss_frontend_warp_pointer(g_tasks.frontend, clamped);
  return clamped;
}
