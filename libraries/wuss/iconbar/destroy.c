/* wuss/iconbar/destroy.c -- remove an icon from the icon bar */

#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "../core/types.h"
#include "../core/alloc.h"
#include "../core/dispatch.h"
#include "../core/invalidate.h"

#ifdef WUSS_ICONBAR

void wuss_iconbar_icon_destroy(wuss_t *wuss, wuss_iconbar_icon_t *icon)
{
  int   i;
  box_t bar;

  if (icon == NULL)
    return;

  for (i = 0; i < wuss->iconbar.n; i++)
  {
    if (wuss->iconbar.icons[i] == icon)
    {
      /* Shift the rest down: array order is each side's slot order from
       * its edge, so every later icon of the same side moves one slot
       * outward. */
      wuss->iconbar.n--;
      memmove(&wuss->iconbar.icons[i], &wuss->iconbar.icons[i + 1],
              (size_t) (wuss->iconbar.n - i) *
              sizeof(wuss->iconbar.icons[0]));
      break;
    }
  }

  wuss__message_purge_icon(wuss, icon);

  wuss__iconbar_box(wuss, &bar);
  wuss__invalidate_clipped(wuss->iconbar.window, &bar);

  wuss__free(wuss, (char *) icon->spec.text);
  wuss__free(wuss, icon);
}

#endif /* WUSS_ICONBAR */
