/* wuss/set-palette.c -- swap the system palette mid-session */

#include <assert.h>
#include <string.h>

#include "geom/box.h"

#include "impl.h"

result_t wuss__broadcast_palette(wuss_t *wuss)
{
  result_t          rc;
  wuss_event_t      event;
  wuss__task_walk_t walk;
  wuss_task_t      *task;

  rc = result_OK;
  event.kind = wuss_EVENT_PALETTE;
  wuss__task_walk_begin(wuss, &walk);
  while ((task = wuss__task_walk_next(&walk)) != NULL)
  {
    result_t crc;

    crc = wuss__deliver(task, NULL, &event);
    if (crc != result_OK && rc == result_OK)
      rc = crc;
  }
  wuss__task_walk_end(wuss, &walk);

  return rc;
}

result_t wuss_set_palette(wuss_t         *wuss,
                          const colour_t *palette,
                          int             npalette)
{
  result_t rc;

  assert(wuss    != NULL);
  assert(palette != NULL);

  /* Same length only: keeps every stored furniture/bevel/backdrop colour
   * index in range, and matches the fixed-size framebuffer palette on a
   * paletted screen. */
  if (npalette != wuss->npalette)
    return result_BAD_ARG;

  memcpy(wuss->palette, palette, npalette * sizeof(*wuss->palette));

  wuss__rebuild_palettecache(wuss);

  rc = wuss__broadcast_palette(wuss);

  /* after the broadcast: whichever task owns the screen rebuilds its
   * physical palette in its handler, and the fills must match that, not
   * the previous one */
  wuss__rebuild_fills(wuss);

  wuss__invalidate_screen(wuss);

  return rc;
}
