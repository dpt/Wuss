/* wuss/hourglass.c -- wuss - busy pointer signalling for long operations */

#include <assert.h>

#include "impl.h"

void wuss_set_hourglass_handler(wuss_t              *wuss,
                                wuss_hourglass_fn_t *fn,
                                void                *opaque)
{
  assert(wuss != NULL);

  wuss->hourglass.fn     = fn;
  wuss->hourglass.opaque = opaque;
}

void wuss_hourglass_on(wuss_t *wuss)
{
  assert(wuss != NULL);

  if (wuss->hourglass.depth++ == 0 && wuss->hourglass.fn != NULL)
    wuss->hourglass.fn(1, wuss->hourglass.opaque);
}

void wuss_hourglass_off(wuss_t *wuss)
{
  assert(wuss != NULL);

  if (wuss->hourglass.depth == 0)
    return;

  if (--wuss->hourglass.depth == 0 && wuss->hourglass.fn != NULL)
    wuss->hourglass.fn(0, wuss->hourglass.opaque);
}
