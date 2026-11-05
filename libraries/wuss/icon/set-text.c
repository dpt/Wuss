/* wuss/icon/set-text.c -- replace a work-area icon's label */

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include <string.h>

#include "../core/types.h"
#include "../core/alloc.h"
#include "../core/windows.h"

result_t wuss_icon_set_text(wuss_window_t *window,
                            wuss_icon_t   *icon,
                            const char    *text)
{
  wuss_t *w;
  size_t  len;
  char   *dup;

  w = window->wuss;

  if (text == NULL)
    text = "";

  if (icon->spec.type == wuss_ICON_TYPE_WRITABLE)
  {
    wuss__writable_copy((char *) icon->spec.text, icon->spec.u.writable.size,
                        text);
    icon->text_scroll = 0;
    if (w->caret.icon == icon)
      wuss__writable_place_caret(window, icon, -1);
    wuss__icon_invalidate(window, icon);
    return result_OK;
  }

  if (strcmp(icon->spec.text, text) == 0)
    return result_OK;

  /* Live labels change on every slider event, so reuse the block when the text
   * fits. text may lie within the old text. ponytail: capacity is the old
   * text's length, so growing after a shrink allocates; store it if that
   * shows up. */
  len = strlen(text);
  if (len <= strlen(icon->spec.text))
  {
    memmove((char *) icon->spec.text, text, len + 1);
  }
  else
  {
    dup = alloc_strdup(&w->alloc, text);
    if (dup == NULL)
      return result_OOM;

    wuss__free(w, (char *) icon->spec.text);
    icon->spec.text = dup;
  }

  wuss__icon_invalidate(window, icon);

  return result_OK;
}
