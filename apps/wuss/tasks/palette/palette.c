/* wuss/tasks/palette/palette.c -- desktop palette swatch grid task */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "framebuf/palettes.h"
#include "framebuf/pixelfmt.h"
#include "geom/box.h"
#include "io/namelist.h"
#include "io/path.h"
#include "wuss/gadget/swatches.h"
#include "wuss/menu.h"

#include "palette.h"
#include "../tasks.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define PALETTE_HEX_EXT     ".hex"
#define PALETTE_NCOLOURS    wuss_SYSTEM_PALETTE_LENGTH /* one colour_t[] row
                                * per *.hex file; the system palette
                                * wuss_create was given is fixed at this
                                * length */

/* indices of the top-level menu's fixed rows */
#define PALETTE_MENU_LOAD_INDEX    0
#define PALETTE_MENU_INVERT_INDEX  1
#define PALETTE_MENU_GREY_INDEX    2
#define PALETTE_MENU_EDIT_INDEX    3

/* indices of the Screen window's top-level menu */
#define PALETTE_SCREEN_MENU_LOAD_INDEX 0
#define PALETTE_SCREEN_MENU_EDIT_INDEX 1

/* ----------------------------------------------------------------------- */
/* Size in bytes of a *.hex file, or -1 if it can't be opened. Each line is
 * "rrggbb\n", so a file of n colours is 7n bytes. */

static long palette_hex_size(const char *resources, const char *name)
{
  const char *path;
  FILE       *fp;
  long        size;

  path = pathf("%s/resources/wuss-tasks/palettes/%s.hex", resources, name);

  fp = fopen(path, "r");
  if (fp == NULL)
    return -1;

  size = fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
  fclose(fp);
  return size;
}

/* Load a *.hex file of 1..max colours, setting *n to the count. *out may be
 * partly written on failure. */

result_t palette_load_hex_n(const char *resources,
                            const char *name,
                            colour_t   *out,
                            int         max,
                            int        *n)
{
  result_t rc;

  rc = palettes_load_hex(pathf("%s/resources/wuss-tasks/palettes/%s.hex",
                               resources, name),
                         out, max, n);
  return rc == result_BAD_ARG ? result_WUSS_BAD_PALETTE : rc;
}

/* As palette_load_hex_n, but exactly PALETTE_NCOLOURS lines, so a
 * wrongly-sized file can never desync from the fixed-length system palette
 * wuss was created with. */

result_t palette_load_hex(const char *resources,
                          const char *name,
                          colour_t   *out)
{
  result_t rc;
  colour_t tmp[PALETTE_NCOLOURS];
  int      n;

  rc = palette_load_hex_n(resources, name, tmp, PALETTE_NCOLOURS, &n);
  if (rc != result_OK)
    return rc;

  if (n != PALETTE_NCOLOURS)
    return result_WUSS_BAD_PALETTE;

  memcpy(out, tmp, sizeof(tmp));
  return result_OK;
}

/* ----------------------------------------------------------------------- */

/* where the palette windows open: the free region of the safe area nearest
 * its bottom right, else anchored there */
static const wuss_place_t palette_place =
{
  wuss_PLACE_SPARE,
  wuss_ALIGN_RIGHT | wuss_ALIGN_BOTTOM, NULL, { 0, 0 }, { 0, 0, 0, 0 }
};

/* the open desktop and Screen palette tasks, if any: each window is a
 * singleton, raised rather than opened twice. Cleared by palette_destroy. */
static struct
{
  palette_task_t *desktop;
  palette_task_t *screen;
}
g;

static wuss_window_fn_t palette_handle;

static result_t palette_create(wuss_t *wuss, void **out);
static result_t palette_screen_create(wuss_t *wuss, void **out);

static const taskapp_desc_t palette_desc =
{
  .name             = "Palette",
  .purpose          = "Desktop palette swatches",
  .create           = palette_create,
  .singleton        = true,
  .adjust           = palette_screen_create,
  .create_label     = "Desktop palette",
  .adjust_label     = "System palette",
  .iconbar_priority = 0x40000000, /* inside Display */
  .no_quit          = true,
  .instance_size    = sizeof(palette_task_t),
  .handle           = palette_handle,
  .all_windows      = true /* a Screen task's window is not the base window */
};

/* Make a task block and open one window on it: the desktop palette, or with
 * screen the physical screen's ("Screen") */
static result_t palette_open(wuss_t *wuss, bool screen, void **out)
{
  result_t        rc;
  palette_task_t *existing;
  void           *instance;
  palette_task_t *task;
  const char     *resources;
  const char     *dir;
  const colour_t *current;
  int             ncurrent;
  bool            matched;
  int             kept;
  int             i;

  existing = screen ? g.screen : g.desktop;
  if (existing != NULL)
  {
    wuss_window_t *open_window;

    open_window = screen ? existing->window2 : existing->base.window;
    if (open_window != NULL)
      wuss_window_restack(open_window, wuss_ZORDER_FRONT);
    if (out)
      *out = existing;
    return result_OK;
  }

  rc = taskapp_instance_create(wuss, &palette_desc, &instance);
  if (rc != result_OK)
    return rc;
  task = instance;

  task->cell = -1;

  resources = wuss_get_resources(wuss);
  dir       = pathf("%s/resources/wuss-tasks/palettes", resources);
  namelist_scan(dir, PALETTE_HEX_EXT, task->names[0],
                sizeof(task->names[0]), PALETTE_MAX_FILES, 1 /* sorted */,
                &task->nnames);

  /* list only the *.hex files the desktop can take, i.e. those of exactly
   * PALETTE_NCOLOURS entries, and tick whichever matches wuss's current
   * system palette, so the picker starts in sync with what wuss_create
   * actually loaded, without the caller having to tell us its leafname
   * separately */
  current = wuss_get_palette(wuss, &ncurrent);
  matched = false;
  kept    = 0;
  for (i = 0; !screen && i < task->nnames; i++)
  {
    colour_t candidate[PALETTE_NCOLOURS];

    if (palette_hex_size(resources, task->names[i]) != PALETTE_NCOLOURS * 7)
      continue;

    if (kept != i)
      strcpy(task->names[kept], task->names[i]);
    if (!matched && ncurrent == PALETTE_NCOLOURS &&
        palette_load_hex(resources, task->names[kept],
                         candidate) == result_OK &&
        memcmp(candidate, current, sizeof(candidate)) == 0)
    {
      task->selected = kept;
      matched        = true;
    }
    kept++;
  }
  if (!screen) /* the Screen menu filters per open instead */
    task->nnames = kept;

  /* built once; ticks are refreshed from task->selected/invert on each open */
  for (i = 0; i < task->nnames; i++)
  {
    WUSS_MENU_ITEM(task->load_items, i, task->names[i],
                  wuss_MENU_ITEM_NONE);
  }
  WUSS_MENU_TITLE(task->load_menu, "Load", task->load_items, task->nnames);

  WUSS_MENU_ITEM_MENU(task->menu_items, PALETTE_MENU_LOAD_INDEX, "Load",
                      wuss_MENU_ITEM_NONE, &task->load_menu);
  WUSS_MENU_ITEM(task->menu_items, PALETTE_MENU_INVERT_INDEX,
                "Invert", wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->menu_items, PALETTE_MENU_GREY_INDEX,
                "Greyscale", wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->menu_items, PALETTE_MENU_EDIT_INDEX,
                "Edit colour...", wuss_MENU_ITEM_DASHED);
  WUSS_MENU_TITLE(task->base.menu, "Palette", task->menu_items,
                 NELEMS(task->menu_items));

  /* backdrop for any rounding gap around the grid */
  rc = wuss_window_create_at(task->base.delegate,
                             &palette_place,
                             SIZE2D(100, 100),
                             screen ? "Screen" : "Palette",
                             wuss_WINDOW_DEFAULT | wuss_WINDOW_NO_RESIZE_BLIT, /* swatch grid is laid out across the whole window, so a resize must redraw all of it, not just the newly (un)covered edge */
                             wuss_BACKDROP_COLOUR(palette_PICO8_BLACK),
                             SIZE2D(100, 100),
                             SIZE2D(0, 0),
                             screen ? &task->window2 : &task->base.window);
  if (rc != result_OK)
  {
    wuss_task_destroy(task->base.delegate); /* its QUIT frees the block */
    return rc;
  }

  if (screen)
    g.screen = task;
  else
    g.desktop = task;

  if (out)
    *out = task;

  return result_OK;
}

static result_t palette_create(wuss_t *wuss, void **out)
{
  return palette_open(wuss, false, out);
}

static result_t palette_screen_create(wuss_t *wuss, void **out)
{
  return palette_open(wuss, true, out);
}

result_t palette_app_create(wuss_t *wuss)
{
  return taskapp_install(wuss, &palette_desc);
}

/* shared by both windows: a roughly-square grid of npalette swatches
 * filling a content area of "bounds" */
static void palette_grid(wuss_swatches_t *swatches,
                         const box_t     *bounds,
                         const colour_t  *palette,
                         int              npalette,
                         int              selected)
{
  swatches->bbox     = (box_t) BOX_POS_SIZE(0, 0, bounds->x1 - bounds->x0,
                                            bounds->y1 - bounds->y0);
  swatches->columns  = 0;
  swatches->gap      = 0;
  swatches->outline  = 0;
  swatches->colours  = palette;
  swatches->ncolours = npalette;
  swatches->selected = selected;
}

static void palette_draw_grid(const wuss_event_t *event,
                              const colour_t     *palette,
                              int                 npalette,
                              int                 selected)
{
  wuss_swatches_t swatches;

  palette_grid(&swatches, event->data.redraw.bounds, palette, npalette,
               selected);
  wuss_swatches_draw(&swatches, event);
}

static result_t palette_redraw(const wuss_event_t *event, void *task_data)
{
  palette_task_t *pc;
  const colour_t *palette;
  int             npalette;

  pc      = task_data;
  palette = wuss_get_palette(pc->base.wuss, &npalette);

  palette_draw_grid(event, palette, npalette, pc->cell);

  return result_OK;
}

/* The palette this task's window shows: the desktop palette, or the screen's
 * own (NULL with *n 0 on an unpaletted screen) */
static const colour_t *palette_colours(const palette_task_t *pc, int *n)
{
  if (pc->base.window!= NULL)
    return wuss_get_palette(pc->base.wuss, n);

  *n = (g_tasks.bm->palette != NULL) ?
       pixelfmt_paletted_nentries(g_tasks.bm->format) : 0;
  return g_tasks.bm->palette;
}

/* wuss_colourpicker_changed_fn_t: install this window's palette with the
 * edited entry swapped in */
static void palette_picker_changed(colour_t colour, void *opaque)
{
  palette_task_t *pc;
  const colour_t *current;
  int             ncurrent;
  colour_t        edited[256];

  pc = opaque;

  if (pc->base.window== NULL && pc->window2 == NULL)
    return; /* closing: the QUIT that frees the picker is on its way */

  current = palette_colours(pc, &ncurrent);
  if (pc->editing >= ncurrent || ncurrent > (int) NELEMS(edited))
    return; /* the palette shrank under the open picker */

  memcpy(edited, current, ncurrent * sizeof(*edited));
  edited[pc->editing] = colour;
  if (pc->base.window!= NULL)
    wuss_set_palette(pc->base.wuss, edited, ncurrent);
  else
    tasks_set_screen_palette(edited, ncurrent);
}

/* Open this window's picker on the selected swatch. Nothing happens with
 * none selected. */
static result_t palette_edit(palette_task_t *pc)
{
  result_t        rc;
  const colour_t *palette;
  int             npalette;

  palette = palette_colours(pc, &npalette);
  if (pc->cell < 0 || pc->cell >= npalette)
    return result_OK;

  if (pc->picker == NULL)
  {
    rc = wuss_colourpicker_create(&pc->picker, pc->base.wuss,
                                  pc->base.window!= NULL ? "Desktop colour" :
                                  "Screen colour",
                                  palette_picker_changed, pc);
    if (rc != result_OK)
      return rc;
  }

  pc->editing = pc->cell;

  return wuss_colourpicker_open(pc->picker, palette[pc->cell]);
}

/* Select on a swatch selects it, and a double-click edits it */
static result_t palette_swatch_click(palette_task_t     *pc,
                                     const wuss_event_t *event)
{
  wuss_window_t  *window;
  box_t           content;
  const colour_t *palette;
  int             npalette;
  wuss_swatches_t swatches;
  int             i;

  window = pc->base.window!= NULL ? pc->base.window: pc->window2;
  wuss_window_get_content_bounds(window, &content);
  palette = palette_colours(pc, &npalette);
  palette_grid(&swatches, &content, palette, npalette, pc->cell);

  i = wuss_swatches_hit_test(&swatches, event->data.mouse.point);
  if (i < 0)
    return result_OK;

  if (i != pc->cell)
  {
    pc->cell = i;
    wuss_window_invalidate_visible(window);
  }

  if (!(event->data.mouse.button & wuss_BUTTON_DOUBLE))
    return result_OK;

  return palette_edit(pc);
}

/* "Screen" window: the physical screen bitmap's own palette, whatever size
 * its pixel format needs (2/4/16/256 for 1/2/4/8bpp); 32bpp has none, so
 * just label it */
static result_t palette_redraw_screen(palette_task_t     *pc,
                                      const wuss_event_t *event)
{
  screen_t    *scr;
  const box_t *bounds;
  int          sx, sy, npalette;

  scr    = event->data.redraw.scr;
  bounds = event->data.redraw.bounds;
  sx     = event->data.redraw.scroll.x;
  sy     = event->data.redraw.scroll.y;

  if (scr->palette == NULL)
  {
    const char *label;

    bmfont_t          *font   = wuss_get_font(pc->base.wuss);
    colour_t           ink    = colour_rgb(0xFF, 0xFF, 0xFF);
    colour_t           bg     = colour_rgba(0, 0, 0, 0); /* transparent */

    if (scr->format == pixelfmt_rgb565)
      label = "16bpp (none)";
    else if (scr->format == pixelfmt_rgbx5551)
      label = "15bpp (none)";
    else
      label = "32bpp (none)";

    screen_fill_rect(scr, bounds->x0 - sx, bounds->y0 - sy,
                     box_size(bounds),
                     colour_rgb(0x00, 0x00, 0x00));
    if (font != NULL)
    {
      int     ascent;
      point_t pos;

      bmfont_get_info(font, NULL, NULL, &ascent, NULL);
      pos = POINT(bounds->x0 - sx + 2, bounds->y0 - sy + 2 + ascent);
      wuss_text_draw(pc->base.wuss, 0, scr, label, (int) strlen(label), ink, bg,
                     &pos, NULL);
    }
    return result_OK;
  }

  npalette = 1 << (1 << pixelfmt_log2bpp(scr->format));

  palette_draw_grid(event, scr->palette, npalette, pc->cell);

  return result_OK;
}

/* Set the picker menu's ticks from task->selected/invert, then open it. The
 * menu itself is built once by palette_create and lives in *pc, so it
 * outlives the open chain as wuss_menu_open requires. The invert row's
 * permanent DASHED flag is set at create time; wuss_menu_open_ticked only
 * touches the TICKED bit. Edit colour... is greyed with no swatch
 * selected. */
static result_t palette_menu_open(palette_task_t *pc)
{
  unsigned int ticks;

  wuss_menu_tick_exclusive(&pc->load_menu, pc->selected);
  pc->menu_items[PALETTE_MENU_EDIT_INDEX].flags =
    wuss_MENU_ITEM_DASHED | (pc->cell < 0 ? wuss_MENU_ITEM_DISABLED : 0);

  ticks = 0;
  if (pc->invert)
    ticks |= 1u << PALETTE_MENU_INVERT_INDEX;
  if (pc->grey)
    ticks |= 1u << PALETTE_MENU_GREY_INDEX;

  return wuss_menu_open_ticked(pc->base.delegate, &pc->base.menu, ticks,
                               wuss_get_pointer(pc->base.wuss), &pc->base.menu_handle);
}

/* Rebuild the Screen window's Load submenu from the *.hex files of exactly
 * the entry count the screen's format needs, ticking whichever matches the
 * screen's current palette, then open the Screen menu: Load, greyed when no
 * file fits, and Edit colour..., greyed with no swatch selected. Nothing
 * opens on an unpaletted screen. */
static result_t palette_screen_menu_open(palette_task_t *pc)
{
  const char *resources;
  int         nentries;
  colour_t    candidate[256];
  int         n;
  int         i;

  resources = wuss_get_resources(pc->base.wuss);
  nentries  = pixelfmt_paletted_nentries(g_tasks.bm->format);
  if (nentries == 0)
    return result_OK;

  pc->nscreen  = 0;
  pc->selected = -1;
  for (i = 0; i < pc->nnames; i++)
  {
    if (palette_hex_size(resources, pc->names[i]) != nentries * 7)
      continue;

    if (pc->selected < 0 && g_tasks.bm->palette != NULL &&
        palette_load_hex_n(resources, pc->names[i], candidate, nentries,
                           &n) == result_OK &&
        memcmp(candidate, g_tasks.bm->palette,
               nentries * sizeof(*candidate)) == 0)
      pc->selected = pc->nscreen;

    WUSS_MENU_ITEM(pc->load_items, pc->nscreen, pc->names[i],
                  wuss_MENU_ITEM_NONE);
    pc->screen_map[pc->nscreen++] = i;
  }
  WUSS_MENU_TITLE(pc->load_menu, "Load", pc->load_items, pc->nscreen);
  wuss_menu_tick_exclusive(&pc->load_menu, pc->selected);

  WUSS_MENU_ITEM_MENU(pc->menu_items, PALETTE_SCREEN_MENU_LOAD_INDEX, "Load",
                      pc->nscreen == 0 ? wuss_MENU_ITEM_DISABLED :
                      wuss_MENU_ITEM_NONE, &pc->load_menu);
  WUSS_MENU_ITEM(pc->menu_items, PALETTE_SCREEN_MENU_EDIT_INDEX,
                 "Edit colour...",
                 pc->cell < 0 ? wuss_MENU_ITEM_DISABLED :
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(pc->base.menu, "Screen", pc->menu_items, 2);

  return wuss_menu_open_at_pointer(pc->base.delegate, &pc->base.menu,
                                   &pc->base.menu_handle);
}

static result_t palette_click(palette_task_t *pc, const wuss_event_t *event)
{
  if (event->data.mouse.action != wuss_MOUSE_DOWN)
    return result_OK;

  if (event->data.mouse.button & wuss_BUTTON_SELECT)
    return palette_swatch_click(pc, event);

  if (!(event->data.mouse.button & wuss_BUTTON_MENU))
    return result_OK;

  if (pc->window2 != NULL)
    return palette_screen_menu_open(pc);

  return palette_menu_open(pc);
}

/* A Screen menu Load pick loads that file and makes it the screen's
 * palette; Edit colour... opens the picker */
static result_t palette_screen_menu_select(palette_task_t     *pc,
                                           const wuss_event_t *event)
{
  int      index;
  colour_t loaded[256];
  int      n;

  index = event->data.menu_select.index;
  if (event->data.menu_select.menu == &pc->base.menu)
    return index == PALETTE_SCREEN_MENU_EDIT_INDEX ? palette_edit(pc) :
           result_OK;
  if (index < 0 || index >= pc->nscreen)
    return result_OK;

  if (palette_load_hex_n(wuss_get_resources(pc->base.wuss),
                         pc->names[pc->screen_map[index]], loaded,
                         NELEMS(loaded), &n) != result_OK ||
      n != pixelfmt_paletted_nentries(g_tasks.bm->format))
    return result_OK; /* the depth changed under the open menu */

  if (event->data.menu_select.button & wuss_BUTTON_ADJUST)
    wuss_menu_tick_exclusive_live(pc->base.menu_handle, &pc->load_menu, index);
  pc->selected = index;
  wuss_menu_tick_exclusive(&pc->load_menu, index);

  return tasks_set_screen_palette(loaded, n);
}

/* A pick loads that *.hex file (or re-applies the current one, for a bare
 * Invert or Greyscale toggle) and, on success, installs it as the live
 * system palette via wuss_set_palette -- every task, including whichever one
 * owns the framebuffer bitmap and any physical palette, sees the change via
 * the resulting wuss_EVENT_PALETTE and reads the new array back with
 * wuss_get_palette. A load failure is silently ignored: the picker just
 * stays on the previous selection. Ticks are also updated in place via
 * wuss_menu_tick_item_live, so an ADJUST pick (which keeps the chain open)
 * shows the new tick without the menu being rebuilt or moved. */
static result_t palette_menu_select(palette_task_t     *pc,
                                    const wuss_event_t *event)
{
  result_t     rc;
  int          index;
  int          old;
  colour_t     loaded[PALETTE_NCOLOURS];
  int          i;
  unsigned int r, g, b, y;

  index = event->data.menu_select.index;

  if (event->data.menu_select.menu == &pc->base.menu &&
      index == PALETTE_MENU_EDIT_INDEX)
    return palette_edit(pc);

  if (event->data.menu_select.menu == &pc->base.menu &&
      index == PALETTE_MENU_INVERT_INDEX)
  {
    pc->invert = !pc->invert;
    if (event->data.menu_select.button & wuss_BUTTON_ADJUST)
      wuss_menu_tick_item_live(pc->base.menu_handle, &pc->base.menu, index, pc->invert);
  }
  else if (event->data.menu_select.menu == &pc->base.menu &&
           index == PALETTE_MENU_GREY_INDEX)
  {
    pc->grey = !pc->grey;
    if (event->data.menu_select.button & wuss_BUTTON_ADJUST)
      wuss_menu_tick_item_live(pc->base.menu_handle, &pc->base.menu, index, pc->grey);
  }
  else if (event->data.menu_select.menu == &pc->load_menu &&
          index >= 0 && index < pc->nnames)
  {
    old          = pc->selected;
    pc->selected = index;
    pc->invert   = false;
    pc->grey     = false;
    /* the data tick too: a submenu reopened within the chain is rebuilt
     * from the items */
    wuss_menu_tick_exclusive(&pc->load_menu, index);
    if (event->data.menu_select.button & wuss_BUTTON_ADJUST)
    {
      wuss_menu_tick_item_live(pc->base.menu_handle, &pc->load_menu, old, 0);
      wuss_menu_tick_item_live(pc->base.menu_handle, &pc->load_menu, index, 1);
      wuss_menu_tick_item_live(pc->base.menu_handle, &pc->base.menu,
                               PALETTE_MENU_INVERT_INDEX, 0);
      wuss_menu_tick_item_live(pc->base.menu_handle, &pc->base.menu,
                               PALETTE_MENU_GREY_INDEX, 0);
    }
  }
  else
  {
    return result_OK;
  }

  if (pc->nnames == 0)
    return result_OK;

  rc = palette_load_hex(wuss_get_resources(pc->base.wuss), pc->names[pc->selected],
                        loaded);
  if (rc != result_OK)
    return result_OK;

  if (pc->invert)
    for (i = 0; i < PALETTE_NCOLOURS; i++)
      loaded[i].primary ^= 0x00FFFFFFu;

  /* Rec. 601 luma, applied after any inversion */
  if (pc->grey)
    for (i = 0; i < PALETTE_NCOLOURS; i++)
    {
      colour_get_rgb(&loaded[i], &r, &g, &b);
      y         = (r * 299 + g * 587 + b * 114) / 1000;
      loaded[i] = colour_rgba(y, y, y, colour_get_alpha(&loaded[i]));
    }

  wuss_set_palette(pc->base.wuss, loaded, PALETTE_NCOLOURS);

  return result_OK;
}

static result_t palette_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data)
{
  palette_task_t *pc;

  pc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    if (window == pc->window2)
      return palette_redraw_screen(pc, event);
    return palette_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    return palette_click(task_data, event);

  case wuss_EVENT_MENU_SELECT:
    if (pc->window2 != NULL)
      return palette_screen_menu_select(task_data, event);
    return palette_menu_select(task_data, event);

  case wuss_EVENT_CLOSE:
    if (window == pc->window2)
      pc->window2 = NULL;
    return result_OK;

  case wuss_EVENT_QUIT:
    if (pc == g.desktop)
      g.desktop = NULL;
    if (pc == g.screen)
      g.screen = NULL;
    wuss_colourpicker_destroy(pc->picker);
    return result_OK;

  case wuss_EVENT_KEY:
    return result_WUSS_KEY_UNCLAIMED;

  default:
    return result_OK;
  }
}

#endif /* WUSS_APP */
