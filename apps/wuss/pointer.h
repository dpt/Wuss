/* wuss/pointer.h -- software pointer for the Wuss demo */

#ifndef WUSS_POINTER_H
#define WUSS_POINTER_H

#ifdef WUSS_APP

#include <stdbool.h>

#include "framebuf/bitmap.h"
#include "geom/box.h"
#include "geom/point.h"
#include "wuss/wuss.h"

/* The optional software pointer (--pointer, Display > Software pointer): an
 * arrow drawn into the framebuffer itself, so it scales with the window zoom
 * and goes through the CRT shader like everything else. wuss never learns of
 * it: each frame puts back the pixels under it before wuss can draw (a window
 * drag's screen_copy_rect would otherwise smear it along), then saves them
 * afresh and redraws it once wuss is done. app_set_pointer (tasks.h) switches
 * it on and off. */

/* Fetch the arrow from wuss's icon pool and allocate its backing store. Logs
 * and leaves the pointer unavailable if the pool lacks it. */
void pointer_load(wuss_t *wuss);

/* Free what pointer_load allocated. */
void pointer_free(void);

/* Record the latest mouse position, or that the mouse left the window. */
void pointer_moved(point_t pos);
void pointer_left(void);

/* Put back the pixels under the pointer in bm, if drawn. Returns whether it
 * was, setting *box to where, for the caller to present. */
bool pointer_undraw(bitmap_t *bm, box_t *box);

/* Save what's under the pointer's position in bm and draw it there, if it's
 * on and over the window. Returns whether it drew, setting *box to where. */
bool pointer_draw(bitmap_t *bm, box_t *box);

/* Hold "p" inside the box wuss locks the pointer to during a drag, if any,
 * warping the hidden hardware pointer to match unless "scripted". */
point_t pointer_clamp(wuss_t *wuss, point_t p, bool scripted);

#endif /* WUSS_APP */

#endif /* WUSS_POINTER_H */
