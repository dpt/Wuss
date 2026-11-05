/* wuss/tasks/viewing/viewing.c -- static bitmap viewing task */

#ifdef WUSS_APP

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/palettes.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/stage.h"
#include "io/dirlist.h"
#include "io/path.h"
#include "wuss/component/colourmenu.h"
#include "wuss/menu.h"
#include "wuss/menu-desc.h"
#include "wuss/message.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/dithermenu.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* window's task: a loaded PNG, alpha-tested against the window
 * background (this test screen is paletted, so fully-transparent source
 * pixels are skipped and everything else is drawn at full strength). The
 * window starts empty; a wuss_MESSAGE_DATA_LOAD sent to it, whose payload is
 * the path of a PNG, shows that image; a file that will not load is not
 * acknowledged. Menu's "Tile" repeats the image edge to edge over a
 * screen-sized window instead; Select drag then moves the tile origin. */
typedef struct viewing_task
{
  taskapp_instance_t base;
  char           path[DPTLIB_MAXPATH]; /* file shown; "" until one is
                                       * loaded */
  bitmap_t       bitmap;    /* owned: base freed by the caller when done */
  int            alpha;     /* nonzero: bitmap has transparent pixels, so
                             * draw checks behind it */
  wuss_menu_t   *menu;      /* owned: MENU-button demo tree, built from a
                              * descriptor string; rebuilt on each open,
                              * freed at QUIT */
  dithermenu_t   dither;     /* "Display" submenu, borrowed by menu */
  screen_dither_t dithering; /* method for paletted screens */
  colour_t       colour_pasteboard; /* stage: outside the page */
  colour_t       colour_paper;      /* page; the band behind the image when
                                     * the stage is off */
  colour_t       colour_shadow;
  colour_t       colour_border;
  wuss_colourlist_t colours; /* "Colours" submenu over the four above */
  int            stage;     /* nonzero: draw a stage (page, border, shadow)
                             * behind the image instead of a plain band */
  stageconfig_t  stagecfg;
  int            tile;      /* nonzero: tile the image over the window, in
                             * place of the stage or band */
  point_t        origin;    /* window-local position of one tile's top-left */
  point_t        grab;      /* pointer position at the last drag event */
  int            dragging;  /* Select held: pointer moves drag the origin */
}
viewing_task_t;

#define VIEWING_BORDERSZ 8 /* solid band drawn around the image */
#define VIEWING_MARGINSZ VIEWING_BORDERSZ
#define VIEWING_EMPTYSZ  64 /* image area before the first DataLoad */
#define VIEWING_SCROLL_STEP 20 /* pixels per arrow key, as a scroll arrow */

/* rows of the main menu (its caption is not a row) */
enum
{
  VIEWING_MENU_DISPLAY,
  VIEWING_MENU_COLOURS,
  VIEWING_MENU_STAGE,
  VIEWING_MENU_TILE,
  VIEWING_MENU_RESET_ORIGIN
};

/* The window size that shows the image: tiled, the screen; with the stage,
 * the smallest one that fits it; without, the image in its band. */
static size2d_t viewing_size(const viewing_task_t *ic)
{
  stagebox_t boxes[stage_MAX_BOXES];
  size_t     nboxes;
  size2d_t   sz;

  if (ic->tile)
  {
    wuss_window_get_max_content(ic->base.window, &sz);
    return sz;
  }

  if (ic->stage)
  {
    stage_generate(&ic->stagecfg, 0, 0,
                   ic->bitmap.size.w, ic->bitmap.size.h,
                   &sz.w, &sz.h, boxes, &nboxes);
    return sz;
  }

  sz.w = ic->bitmap.size.w + VIEWING_MARGINSZ * 2;
  sz.h = ic->bitmap.size.h + VIEWING_MARGINSZ * 2;

  return sz;
}

/* Fit the window and its document to the image, opening the window, placed
 * for that size, on the first image */
static result_t viewing_fit(viewing_task_t *ic)
{
  return task_window_fit(ic->base.delegate, &ic->base.window, "Viewing",
                         wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                         wuss_NO_BACKDROP,
                         viewing_size(ic), SIZE2D(32, 32));
}

static result_t viewing_init(viewing_task_t *task)
{
  static const char *const colour_names[4] =
  {
    "Pasteboard", "Paper", "Shadow", "Border"
  };

  colour_t *colour_fields[4];

  /* No autoclose: QUIT is delivered at wuss_destroy instead. Closing the
   * main window early leaks this block until then -- fine for a demo. The
   * window opens with the first image, sized and placed for it */
  wuss_task_set_autoclose(task->base.delegate, 0);

  task->bitmap.size = SIZE2D(VIEWING_EMPTYSZ, VIEWING_EMPTYSZ); /* no pixels */
  task->dithering   = screen_DITHER_BLUE_NOISE;
  task->stage       = 1;
  stageconfig_init(&task->stagecfg);

  task->colour_pasteboard = colour_rgb(192, 192, 192); /* light grey */
  task->colour_paper      = colour_rgb(255, 255, 255);
  task->colour_shadow     = colour_rgb(96, 96, 96); /* dark grey */
  task->colour_border     = colour_rgb(0, 0, 0);
  colour_fields[0] = &task->colour_pasteboard;
  colour_fields[1] = &task->colour_paper;
  colour_fields[2] = &task->colour_shadow;
  colour_fields[3] = &task->colour_border;
  wuss_colourlist_init(&task->colours, task->base.wuss, 4, colour_fields,
                       colour_names);

  dithermenu_init(&task->dither, task->dithering);

  return result_OK;
}

/* Draw the image at (x, y), over checks if it has transparent pixels */
static void viewing_draw_image(viewing_task_t *ic,
                               screen_t       *scr,
                               int             x,
                               int             y)
{
  box_t box;

  if (ic->bitmap.base == NULL)
    return;

  if (ic->alpha)
  {
    box = (box_t) BOX_POS_SIZE(x, y, ic->bitmap.size.w, ic->bitmap.size.h);
    task_draw_checks(scr, &box, POINT(x, y));
  }

  screen_copy_bitmap_dithered(scr, x, y, &ic->bitmap, ic->dithering);
}

/* Draw the stage behind the image, centred in the window. The stage lays out
 * with y up, so flip it. */
static void viewing_redraw_stage(viewing_task_t *ic,
                                 screen_t       *scr,
                                 const box_t    *bounds,
                                 int             ox,
                                 int             oy)
{
  stagebox_t boxes[stage_MAX_BOXES];
  size_t     nboxes;
  size_t     i;
  int        w, h;
  box_t      b;
  colour_t   colour;

  stage_generate(&ic->stagecfg,
                 bounds->x1 - bounds->x0, bounds->y1 - bounds->y0,
                 ic->bitmap.size.w, ic->bitmap.size.h,
                 &w, &h, boxes, &nboxes);

  for (i = 0; i < nboxes; i++)
  {
    b = (box_t) { ox + boxes[i].box.x0, oy + h - boxes[i].box.y1,
                  ox + boxes[i].box.x1, oy + h - boxes[i].box.y0 };

    switch (boxes[i].kind)
    {
    case stageboxkind_CONTENT:
      viewing_draw_image(ic, scr, b.x0, b.y0);
      continue;

    case stageboxkind_STROKE:
      colour = ic->colour_border;
      break;

    case stageboxkind_SHADOW:
      colour = ic->colour_shadow;
      break;

    case stageboxkind_MARGIN:
      colour = ic->colour_paper;
      break;

    default:
      colour = ic->colour_pasteboard;
      break;
    }

    screen_fill_rects(scr, &b, 1, colour);
  }
}

/* Repeat the image over the dirty box, one tile at (ox, oy), one copy at a
 * time so tiles are dithered like the single image.
 * ponytail: one call per tile, so a tiny image is slow to fill a large window;
 * add a dithered tiled copy to DPTLib if that matters. */
static void viewing_redraw_tiles(viewing_task_t *ic,
                                 screen_t       *scr,
                                 const box_t    *dirty,
                                 int             ox,
                                 int             oy)
{
  int w, h;
  int x0, y0;
  int x, y;

  w = ic->bitmap.size.w;
  h = ic->bitmap.size.h;

  x0 = dirty->x0 - ((dirty->x0 - ox) % w + w) % w;
  y0 = dirty->y0 - ((dirty->y0 - oy) % h + h) % h;

  for (y = y0; y < dirty->y1; y += h)
    for (x = x0; x < dirty->x1; x += w)
      viewing_draw_image(ic, scr, x, y);
}

static result_t viewing_redraw(const wuss_event_t *event, void *task_data)
{
  viewing_task_t *ic;
  screen_t       *scr;
  const box_t    *bounds;
  int             sx, sy;
  int             bx, by;
  int             bx0, bx1, by1;
  box_t           band[4];

  ic = task_data;

  scr    = event->data.redraw.scr;
  bounds = event->data.redraw.bounds;
  sx     = event->data.redraw.scroll.x;
  sy     = event->data.redraw.scroll.y;

  if (ic->tile)
  {
    viewing_redraw_tiles(ic, scr, event->data.redraw.content,
                         bounds->x0 - sx + ic->origin.x,
                         bounds->y0 - sy + ic->origin.y);
    return result_OK;
  }

  if (ic->stage)
  {
    viewing_redraw_stage(ic, scr, bounds, bounds->x0 - sx, bounds->y0 - sy);
    return result_OK;
  }

  bx     = bounds->x0 - sx + VIEWING_MARGINSZ;
  by     = bounds->y0 - sy + VIEWING_MARGINSZ;
  bx0    = bx - VIEWING_BORDERSZ;
  bx1    = bx + ic->bitmap.size.w + VIEWING_BORDERSZ;
  by1    = by + ic->bitmap.size.h;

  /* solid 8px band around the image */
  band[0] = (box_t) { bx0, by - VIEWING_BORDERSZ, bx1, by };    /* top */
  band[1] = (box_t) { bx0, by1, bx1, by1 + VIEWING_BORDERSZ };  /* bottom */
  band[2] = (box_t) { bx0, by, bx, by1 };                     /* left */
  band[3] = (box_t) { bx + ic->bitmap.size.w, by, bx1, by1 }; /* right */
  screen_fill_rects(scr, band, 4, ic->colour_paper);

  viewing_draw_image(ic, scr, bx, by);

  return result_OK;
}

/* Load the PNG at path and show it, resizing the window to fit. On failure
 * the current image stays and the error is returned. */
static result_t viewing_show(viewing_task_t *ic, const char *path)
{
  result_t rc;
  bitmap_t next;

  rc = bitmap_load_png_deep(&next, path, pixelfmt_rgbx8888);
  if (rc != result_OK)
  {
    logf_warning("viewing: cannot load \"%s\" (rc=0x%X)", path, rc);
    return rc;
  }

  free(ic->bitmap.base);
  ic->bitmap = next;
  ic->alpha  = bitmap_has_transparency(&ic->bitmap);

  rc = viewing_fit(ic);
  if (rc != result_OK)
    return rc;

  snprintf(ic->path, sizeof(ic->path), "%s", path);
  task_set_doc_title(ic->base.window, path_leaf(path), "Viewing", 0);
  return result_OK;
}

/* Show the PNG 'step' places (+1 or -1) from the current one in its
 * directory, in leafname order and wrapping at either end. Files that fail
 * to load are skipped. */
static result_t viewing_page(viewing_task_t *ic, int step)
{
  char                   dir[DPTLIB_MAXPATH];
  char                   next[DPTLIB_MAXPATH];
  dirlist_t             *list;
  const char            *leaf;
  size_t                 dirlen;
  int                    n;
  int                    cur;
  int                    i;
  int                    idx;
  const dirlist_entry_t *entry;

  if (ic->path[0] == '\0' || !path_parent(ic->path, dir, sizeof(dir)))
    return result_OK;
  if (dirlist_scan(dir, &list) != result_OK)
    return result_OK;

  leaf   = path_leaf(ic->path);
  dirlen = (size_t) (leaf - ic->path); /* keeps the separator */
  n      = dirlist_count(list);

  for (cur = 0; cur < n; cur++)
    if (strcmp(dirlist_get(list, cur)->leaf, leaf) == 0)
      break;
  if (cur == n)
    cur = step > 0 ? -1 : n; /* current file gone: start from an end */

  for (i = 1; i <= n; i++)
  {
    idx = ((cur + step * i) % n + n) % n;
    if (idx == cur)
      break; /* back round to the current file */
    entry = dirlist_get(list, idx);
    if (entry->is_dir || entry->type.riscos != filetype_RISCOS_PNG)
      continue;
    if (dirlen + strlen(entry->leaf) >= sizeof(next))
      continue;
    memcpy(next, ic->path, dirlen);
    strcpy(next + dirlen, entry->leaf);
    if (viewing_show(ic, next) == result_OK)
      break;
  }

  dirlist_destroy(list);
  return result_OK;
}

/* Arrows, Page Up/Down and Home/End scroll the image; Space shows the next
 * image in the directory and Shift-Space the previous one */
static result_t viewing_key(viewing_task_t *ic, const wuss_event_t *event)
{
  box_t content;
  int   page;

  if (event->data.key.modifiers & (wuss_KEY_MOD_CTRL | wuss_KEY_MOD_ALT))
    return result_WUSS_KEY_UNCLAIMED;

  wuss_window_get_content_bounds(ic->base.window, &content);
  page = MAX(content.y1 - content.y0 - VIEWING_SCROLL_STEP, 1);

  switch (event->data.key.code)
  {
  case ' ':
    return viewing_page(ic,
             (event->data.key.modifiers & wuss_KEY_MOD_SHIFT) ? -1 : 1);

  case wuss_KEY_UP:
    wuss_window_scroll_by(ic->base.window, POINT(0, -VIEWING_SCROLL_STEP));
    break;

  case wuss_KEY_DOWN:
    wuss_window_scroll_by(ic->base.window, POINT(0, VIEWING_SCROLL_STEP));
    break;

  case wuss_KEY_LEFT:
    wuss_window_scroll_by(ic->base.window, POINT(-VIEWING_SCROLL_STEP, 0));
    break;

  case wuss_KEY_RIGHT:
    wuss_window_scroll_by(ic->base.window, POINT(VIEWING_SCROLL_STEP, 0));
    break;

  case wuss_KEY_PAGE_UP:
    wuss_window_scroll_by(ic->base.window, POINT(0, -page));
    break;

  case wuss_KEY_PAGE_DOWN:
    wuss_window_scroll_by(ic->base.window, POINT(0, page));
    break;

  case wuss_KEY_HOME:
    wuss_window_scroll_by(ic->base.window, POINT(-INT_MAX / 2, -INT_MAX / 2));
    break;

  case wuss_KEY_END:
    wuss_window_scroll_by(ic->base.window, POINT(0, INT_MAX / 2));
    break;

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }

  return result_OK;
}

/* A DataLoad on the viewing window: show the file it names. */
static result_t viewing_message(viewing_task_t     *ic,
                                wuss_window_t      *window,
                                const wuss_event_t *event)
{
  if (ic->base.window == NULL || window != ic->base.window)
    return result_OK;

  return taskapp_window_data_load(ic->base.wuss, ic->base.delegate, ic,
                                  (taskapp_data_load_fn_t) viewing_show,
                                  event->data.message);
}

/* Menu shape built from a descriptor string. The tree must outlive the open
 * chain, so it is kept on the task and rebuilt (previous one freed) on each
 * open. Freed for good in the QUIT handler. */
static result_t viewing_open_menu(viewing_task_t *ic)
{

  result_t     rc;
  wuss_menu_t *m;
  int          i;

  rc = wuss_menu_create_from_desc(&m,
         "Viewing, Display, Colours, !~Stage, !Tile, ~Reset origin",
         ic->stage, ic->tile, ic->tile, !ic->tile);
  if (rc != result_OK)
    return rc;

  /* '>' would deep-copy a submenu, so its picks would not match ours: point
   * "Display" and "Colours" at per-instance submenus built at create time,
   * by hand.
   * Marked BORROWED_SUBMENU so wuss_menu_destroy leaves them alone -- they
   * belong to the task, not to this per-open tree. */
  wuss_menu_tick_exclusive(&ic->dither.menu, (int) ic->dithering);
  for (i = 0; i < m->nitems; i++)
  {
    wuss_menu_item_t *it = (wuss_menu_item_t *) &m->items[i];

    if (it->text == NULL)
      continue;
    if (strcmp(it->text, "Display") == 0)
      it->submenu = &ic->dither.display;
    else if (strcmp(it->text, "Colours") == 0)
      it->submenu = &ic->colours.menu;
    else
      continue;

    it->flags |= wuss_MENU_ITEM_BORROWED_SUBMENU;
  }

  wuss_menu_destroy(ic->menu);
  ic->menu = m;

  return wuss_menu_open_at_pointer(ic->base.delegate, ic->menu,
                                   &ic->base.menu_handle);
}

/* Menu opens the menu; while tiled, Select drag moves the tile origin */
static result_t viewing_mouse(viewing_task_t *ic, const wuss_event_t *event)
{
  point_t point;

  point = event->data.mouse.point;

  if (event->data.mouse.action == wuss_MOUSE_UP)
  {
    ic->dragging = 0;
  }
  else if (event->data.mouse.action == wuss_MOUSE_DOWN)
  {
    if (event->data.mouse.button & wuss_BUTTON_MENU)
      return viewing_open_menu(ic);

    if ((event->data.mouse.button & wuss_BUTTON_SELECT) && ic->tile)
    {
      ic->dragging = 1;
      ic->grab     = point;
    }
  }
  else if (ic->dragging) /* wuss_MOUSE_MOVE */
  {
    ic->origin.x += point.x - ic->grab.x;
    ic->origin.y += point.y - ic->grab.y;
    ic->grab      = point;
    wuss_window_invalidate_visible(ic->base.window);
  }

  return result_OK;
}

/* Turn tiling on or off, shading the rows that apply only to the other mode,
 * and refit the window: to the screen, or back to the image */
static result_t viewing_toggle_tile(viewing_task_t    *ic,
                                    const wuss_menu_t *menu)
{
  result_t rc;
  box_t    visible;
  box_t    content;

  ic->tile     = !ic->tile;
  ic->dragging = 0;

  wuss_menu_tick_item_live(ic->base.menu_handle, menu, VIEWING_MENU_TILE,
                           ic->tile);
  wuss_menu_disable_item_live(ic->base.menu_handle, menu, VIEWING_MENU_STAGE,
                              ic->tile);
  wuss_menu_disable_item_live(ic->base.menu_handle, menu,
                              VIEWING_MENU_RESET_ORIGIN, !ic->tile);

  /* a resize stops at the screen's edge, so start from its top left */
  if (ic->tile)
  {
    wuss_window_get_visible_bounds(ic->base.window, &visible);
    wuss_window_get_content_bounds(ic->base.window, &content);
    wuss_window_move(ic->base.window, POINT(content.x0 - visible.x0,
                                            content.y0 - visible.y0));
  }

  rc = viewing_fit(ic);
  if (rc != result_OK || ic->tile)
    return rc;

  return wuss_window_place(ic->base.window, &task_place);
}

static result_t viewing_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data)
{
  viewing_task_t    *ic;
  const wuss_menu_t *menu;
  int                index;

  ic = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return viewing_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    return viewing_mouse(ic, event);

  case wuss_EVENT_MENU_SELECT:
    if (wuss_colourlist_selected(&ic->colours, event))
    {
      wuss_window_invalidate_visible(ic->base.window);
      return result_OK;
    }

    menu  = event->data.menu_select.menu;
    index = event->data.menu_select.index;
    if (menu == ic->menu && index == VIEWING_MENU_STAGE)
    {
      ic->stage = !ic->stage;
      wuss_menu_tick_item_live(ic->base.menu_handle, menu, index, ic->stage);
      return viewing_fit(ic);
    }
    if (menu == ic->menu && index == VIEWING_MENU_TILE)
      return viewing_toggle_tile(ic, menu);
    if (menu == ic->menu && index == VIEWING_MENU_RESET_ORIGIN)
    {
      ic->origin = POINT(0, 0);
      wuss_window_invalidate_visible(ic->base.window);
      return result_OK;
    }
    if (menu == &ic->dither.menu)
    {
      ic->dithering = dithermenu_select(&ic->dither, ic->base.menu_handle, index);
      wuss_window_invalidate_visible(ic->base.window);
    }
    return result_OK;

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    {
      result_t rc;

      if (wuss_colourlist_pre_open(&ic->colours, ic->base.wuss, event, &rc))
        return rc;

      return result_OK;
    }

  case wuss_EVENT_MESSAGE:
    return viewing_message(ic, window, event);

  case wuss_EVENT_CLOSE_ADJUST:
    if (window != ic->base.window)
      return result_OK;
    return task_close_adjust(ic->base.wuss, ic->base.delegate, window, ic->path,
                             event);

  case wuss_EVENT_QUIT:
    wuss_menu_destroy(ic->menu);
    free(ic->bitmap.base);
    return result_OK;

  case wuss_EVENT_KEY:
    return viewing_key(ic, event);

  default:
    return result_OK;
  }
}

result_t viewing_app_create(wuss_t *wuss)
{
  static const unsigned short opens[] = { filetype_RISCOS_PNG, 0 };
  static const taskapp_desc_t desc =
  {
    .name          = "Viewing",
    .purpose       = "View PNG images",
    .data_load     = (taskapp_data_load_fn_t) viewing_show,
    .drop_only     = true,
    .opens         = opens,
    .instance_size = sizeof(viewing_task_t),
    .init          = (taskapp_init_fn_t) viewing_init,
    .handle        = viewing_handle
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
