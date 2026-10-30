/* wuss/menu/menu-spawn.c -- wuss pop-up menu level creation */

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "base/result.h"
#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "geom/box.h"
#include "geom/point.h"
#include "geom/size.h"

#include "wuss/icon.h"
#include "wuss/task.h"
#include "wuss/window.h"
#include "wuss/wuss.h"

#include "../core/types.h"
#include "../core/alloc.h"
#include "../core/geometry.h"

#include "impl.h"

#define WUSS_MENU_TITLE_PAD       2 /* margin either side of the titlebar caption slot, matching wuss__titlebar_draw */

/* ----------------------------------------------------------------------- */

result_t wuss__menu_spawn(wuss_t             *wuss,
                          wuss_task_t        *owner,
                          const wuss_menu_t  *menu,
                          point_t             at,
                          struct wuss__menu  *parent,
                          struct wuss__menu **out)
{
  result_t           rc;
  struct wuss__menu *node;
  wuss_task_t       *menu_task;
  wuss_icon_spec_t  *specs;
  wuss_icon_t      **made;
  int                fh;
  int                pitch;
  int                sep_h;
  int                y;
  int                widest;
  int                widest_shortcut;
  int                width, height;
  int                doc_h;
  int                max_h;
  int                i;
  int                ndashed;
  int                nspecs;
  int                s;
  int                outline_px;
  int                titlebar_height;
  point_t            carve;
  wuss_window_flags_t menu_flags;
  size2d_t           doc;
  size2d_t           min_doc;
  box_t              content;

  assert(wuss != NULL);
  assert(menu != NULL);
  assert(wuss->fonts.fonts[0] != NULL);

  if (menu->nitems <= 0)
    return result_WUSS_BAD_ICON;

  menu_task = wuss__menu_task(wuss);
  if (menu_task == NULL)
    return result_OOM;

  menu_flags = 0;

  outline_px      = wuss__outline_px_for(menu_flags);
  titlebar_height = wuss__titlebar_height_for(wuss, menu_flags);

  bmfont_get_info(wuss->fonts.fonts[0], NULL, &fh, NULL, NULL);
  pitch = fh + 2 * WUSS_MENU_ROW_PAD;
  sep_h = 2 * WUSS_MENU_ROW_PAD;

  ndashed = 0;
  for (i = 0; i < menu->nitems; i++)
    if (menu->items[i].flags & wuss_MENU_ITEM_DASHED)
      ndashed++;
  nspecs = menu->nitems + ndashed;

  widest = 0;
  for (i = 0; i < menu->nitems; i++)
  {
    bmfont_width_t w;

    w = wuss__text_width(wuss->fonts.fonts[0], menu->items[i].text);

    /* a swatch row's chip leads its label in the text column */
    if (menu->items[i].flags & wuss_MENU_ITEM_SWATCH)
      w += (bmfont_width_t) wuss__menu_entry_swatch_width(fh);

    widest = MAX(widest, (int) w);
  }

  /* shortcuts are drawn in the bold weight (plus symbol glyphs), so measure
   * them the same way */
  widest_shortcut = 0;
  for (i = 0; i < menu->nitems; i++)
  {
    const char *shortcut;
    int         w;

    shortcut = menu->items[i].shortcut;
    if (shortcut == NULL || shortcut[0] == '\0')
      continue;
    w               = (int) wuss__shortcut_measure(wuss, shortcut);
    widest_shortcut = MAX(widest_shortcut, w);
  }

  /* the row draws its text as if a space padded it either side (see
   * wuss__icon_draw_menu_entry), so the text column must be wide enough
   * for two of those; a shortcut column sits a further two spaces clear of
   * the widest label */
  {
    bmfont_width_t space_w;

    space_w = wuss__fontset_space_width(&wuss->fonts, wuss->fonts.fonts[0]);
    widest += 2 * (int) space_w;
    if (widest_shortcut > 0)
      widest += 2 * (int) space_w + widest_shortcut;
  }

  width  = wuss__menu_entry_gutter_left(fh) + widest +
           wuss__menu_entry_gutter_right(fh);

  /* widen for the titlebar caption too, so a title longer than every item
   * label (e.g. a one-item menu) isn't clipped; titles draw in the bold
   * weight (font slot 1), falling back to the system font, same as
   * wuss__titlebar_draw; buffered by a space either side so the caption
   * doesn't butt up against the window edges */
  if (menu->title != NULL && menu->title[0] != '\0')
  {
    bmfont_t *titlefont;
    int       need;

    titlefont = wuss__bold_font(wuss);
    need      = (int) wuss__text_width(titlefont, menu->title) +
                2 * WUSS_MENU_TITLE_PAD;
    need     += 2 * (int) wuss__fontset_space_width(&wuss->fonts, titlefont);
    width     = MAX(width, need);
  }

  /* every item is a full row now; a dashed item also gets a sep_h rule above.
   * doc_h is the whole menu; `height` is what the window actually shows. When
   * the menu is taller than the space the screen leaves for it, cap `height`
   * and give the window a real vertical scrollbar -- the icon draw and hit
   * test already honour window->scroll, so the rows just move under it. */
  doc_h = menu->nitems * pitch + ndashed * sep_h;

  max_h = wuss->scr->size.h - 2 * outline_px - titlebar_height;

  if (doc_h > max_h)
  {
    height      = max_h;
    menu_flags |= wuss_WINDOW_VSCROLL;
  }
  else
  {
    height = doc_h;
  }

  wuss__furniture_carve_for(menu_flags, wuss__button_size_for(wuss, menu_flags),
                            &carve);

  /* an icon bar menu is centred on the pointer, not hung from it */
  if (parent == NULL && wuss->menu->bottom_gap > 0)
    at.x -= width / 2;

  /* Nudge onto the screen where it can be, but keep the top-left on screen.
   * `at` is the content top-left; the window's visible box also spans the
   * outline on all four sides, the titlebar above and (for a scrolling menu)
   * the scrollbar carve on the right, so clamp against that whole extent --
   * otherwise wuss_window_create's own on-screen size clamp trims the content
   * and the last row (e.g. a trailing "Quit") is cropped. */
  if (at.x + width + outline_px + carve.x > wuss->scr->size.w)
    at.x = wuss->scr->size.w - width - outline_px - carve.x;
  if (at.y + height + outline_px > wuss->scr->size.h)
    at.y = wuss->scr->size.h - height - outline_px;
  if (at.x - outline_px < 0)
    at.x = outline_px;
  if (parent == NULL && wuss->menu->bottom_gap > 0)
    at.y = wuss->scr->size.h - wuss->menu->bottom_gap - height - outline_px;
  if (at.y - outline_px - titlebar_height < 0)
    at.y = outline_px + titlebar_height;

  node = wuss__malloc(wuss, sizeof(*node));
  if (node == NULL)
    return result_OOM;

  node->icons = wuss__malloc(wuss, (size_t) menu->nitems * sizeof(*node->icons));
  specs       = wuss__malloc(wuss, (size_t) nspecs * sizeof(*specs));
  made        = wuss__malloc(wuss, (size_t) nspecs * sizeof(*made));
  if (node->icons == NULL || specs == NULL || made == NULL)
  {
    wuss__free(wuss, made);
    wuss__free(wuss, specs);
    wuss__free(wuss, node->icons);
    wuss__free(wuss, node);
    return result_OOM;
  }
  memset(node->icons, 0, (size_t) menu->nitems * sizeof(*node->icons));

  node->flags      = 0;
  node->wuss       = wuss;
  node->owner      = owner;
  node->window     = NULL;
  node->menu       = menu;
  node->parent     = parent;
  node->child      = NULL;
  node->open_index = -1;
  memset(&node->flash, 0, sizeof(node->flash));

  /* One MENU_ENTRY icon per item, in item order, preceded by an inert
   * wuss_ICON_TYPE_RULE icon for each dashed item. specs[] therefore holds
   * nspecs entries; s walks it while i walks the items. */
  y = 0;
  s = 0;
  for (i = 0; i < menu->nitems; i++)
  {
    const wuss_menu_item_t *item;
    wuss_icon_flags_t       flags;

    item  = &menu->items[i];
    flags = wuss_ICON_FLAGS_NONE;

    if (item->flags & wuss_MENU_ITEM_DASHED)
    {
      flags |= wuss_ICON_FLAGS_SEPARATOR;

      specs[s].type    = wuss_ICON_TYPE_RULE;
      specs[s].bbox.x0 = 0;
      specs[s].bbox.y0 = y;
      specs[s].bbox.x1 = width;
      specs[s].bbox.y1 = y + sep_h;
      specs[s].text    = "";
      specs[s].fg      = wuss_COLOUR_BLACK;
      specs[s].bg      = wuss_NO_BACKGROUND;
      specs[s].u.menu_entry.swatch   = wuss_NO_BACKGROUND;
      specs[s].u.menu_entry.shortcut = NULL;
      specs[s].flags   = wuss_ICON_FLAGS_NONE;
      s++;
      y += sep_h;
    }

    if (item->flags & wuss_MENU_ITEM_DISABLED)
      flags |= wuss_ICON_FLAGS_DISABLED;
    if (item->submenu != NULL || item->window != NULL)
      flags |= wuss_ICON_FLAGS_SUBMENU;
    if (item->flags & wuss_MENU_ITEM_SWATCH)
      flags |= wuss_ICON_FLAGS_SWATCH;

    specs[s].type    = wuss_ICON_TYPE_MENU_ENTRY;
    specs[s].bbox.x0 = 0;
    specs[s].bbox.y0 = y;
    specs[s].bbox.x1 = width;
    specs[s].bbox.y1 = y + pitch;
    specs[s].text    = item->text ? item->text : "";
    specs[s].fg      = wuss_COLOUR_BLACK;
    specs[s].bg      = wuss_NO_BACKGROUND;
    specs[s].u.menu_entry.swatch =
      (item->flags & wuss_MENU_ITEM_SWATCH) ? item->swatch : wuss_NO_BACKGROUND;
    specs[s].u.menu_entry.shortcut = item->shortcut;
    specs[s].flags   = flags;
    s++;

    y += pitch;
  }

  /* doc spans the whole menu so the window can scroll it; min_doc is what the
   * window shows, so its own resize floor never exceeds what fits. */
  doc     = SIZE2D(width, doc_h);
  min_doc = SIZE2D(width, height);

  /* `at` is the content top-left, already clamped on screen above. Create the
   * window there directly -- creating it elsewhere and wuss_window_move-ing it
   * afterwards would blit its not-yet-rendered pixels and leave the titlebar
   * unpainted. */
  content.x0 = at.x;
  content.y0 = at.y;
  content.x1 = at.x + width;
  content.y1 = at.y + height;

  rc = wuss_window_create(menu_task, &content,
                          menu->title ? menu->title : "",
                          menu_flags | wuss_WINDOW_STACK_TOP,
                          wuss_BACKDROP_COLOUR(wuss_COLOUR_MENU),
                          doc, min_doc, &node->window);
  if (rc != result_OK)
  {
    wuss__free(wuss, made);
    wuss__free(wuss, specs);
    wuss__free(wuss, node->icons);
    wuss__free(wuss, node);
    return rc;
  }

  rc = wuss_icon_create_array(node->window, specs, nspecs, made);
  wuss__free(wuss, specs);
  if (rc != result_OK)
  {
    wuss__free(wuss, made);
    wuss_window_close(node->window);
    wuss__free(wuss, node->icons);
    wuss__free(wuss, node);
    return rc;
  }

  /* keep only the entry handles, item-indexed; the rule icons stay owned by
   * the window and need no further handling. Walk made[] with the same
   * rule-then-entry interleave as the specs loop. */
  s = 0;
  for (i = 0; i < menu->nitems; i++)
  {
    if (menu->items[i].flags & wuss_MENU_ITEM_DASHED)
      s++;
    node->icons[i] = made[s];
    s++;

    if (menu->items[i].flags & wuss_MENU_ITEM_TICKED)
      wuss_icon_set_selected(node->window, node->icons[i], 1);
  }
  wuss__free(wuss, made);

  *out = node;
  return result_OK;
}
