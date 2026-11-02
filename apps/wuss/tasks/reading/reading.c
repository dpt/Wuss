/* wuss/tasks/reading/reading.c -- Reading task for Markdown and text files */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bmfontfamily.h"
#include "framebuf/palettes.h"
#include "geom/box.h"
#include "geom/point.h"
#include "io/path.h"
#include "wuss/menu.h"
#include "wuss/message.h"
#include "wuss/component/error.h"

#include "reading.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* "Colours" leads to a submenu with one row per task->fg_index/bg_index;
 * both rows share the colourmenu singleton, retargeted per hover by
 * reading_pre_submenu_open (wuss_EVENT_PRE_SUBMENU_OPEN) so one instance
 * serves either row -- see saturn.c's SATURN_COLOURS_MENU_* for the same
 * pattern. */
enum { READING_COLOURS_MENU_FOREGROUND = 0, READING_COLOURS_MENU_BACKGROUND };

/* one entry per row of the "Spacing" submenu */
typedef struct reading_spacing_preset
{
  const char *name;
  int         letter_spacing;
  int         word_spacing;
}
reading_spacing_preset_t;

static const reading_spacing_preset_t reading_spacing_presets[] =
{
  { "Normal",      0, 0 },
  { "Letter",      1, 0 },
  { "Word",        0, 1 },
  { "Letter+Word", 1, 1 }
};

#define READING_DEFAULT_SPACING 0 /* "Normal" */

#define READING_INSET 4
#define LEADING       2

/* index into task->top_items[] of the "Font" leaf */
#define READING_MENU_FONT 0

/* "Alignment" submenu rows, in mdtext_align_t order */
enum { READING_ALIGN_LEFT = 0, READING_ALIGN_CENTRE, READING_ALIGN_RIGHT };

/* ----------------------------------------------------------------------- */

/* the shared fontmenu singleton, retargeted at task->wuss's bmfonts dir --
 * cheap to call repeatedly since wuss_fontmenu_menu only rebuilds when the
 * dir or wuss_t actually changes */
static const wuss_menu_t *reading_fontmenu(reading_task_t *task)
{
  const char *resources;
  const char *bmfonts_dir;

  resources   = wuss_get_resources(task->wuss);
  bmfonts_dir = pathf("%s/resources/bmfonts", resources);
  return wuss_fontmenu_menu(bmfonts_dir, "Font", task->wuss,
                            wuss_FONTMENU_FAMILIES);
}

/* release the fonts this task acquired; the system font (current == -1) is
 * wuss's, not ours */
static void reading_release_fonts(reading_task_t *task)
{
  if (task->current < 0)
    return;

  bmfontcache_release(wuss_get_font_cache(task->wuss), task->md.font);
  if (task->md.bold != NULL)
    bmfontcache_release(wuss_get_font_cache(task->wuss), task->md.bold);
}

/* make family name (font menu row idx) the paragraph's: its Regular face
 * (or its first face if it has none) and its Bold face, if any. On failure
 * the current fonts stay. */
static result_t reading_load_family(reading_task_t *task,
                                    int             idx,
                                    const char     *name)
{
  result_t            rc;
  const char         *resources;
  bmfontfamily_t     *family;
  const bmfontface_t *face;
  bmfontcache_t      *cache;
  bmfont_t           *font;
  bmfont_t           *bold;

  resources = wuss_get_resources(task->wuss);
  rc = bmfontfamily_scan(pathf("%s/resources/bmfonts/%s", resources, name),
                         &family);
  if (rc != result_OK)
    return rc;

  face = bmfontfamily_find(family, bmfontfamily_WEIGHT_REGULAR,
                           bmfontfamily_SLANT_UPRIGHT);
  if (face == NULL)
    face = bmfontfamily_face(family, 0); /* a scanned family has a face */

  cache = wuss_get_font_cache(task->wuss);
  rc = bmfontcache_acquire(cache, face->path, &font);
  if (rc != result_OK)
  {
    bmfontfamily_destroy(family);
    return rc;
  }

  bold = NULL;
  face = bmfontfamily_find(family, bmfontfamily_WEIGHT_BOLD,
                           bmfontfamily_SLANT_UPRIGHT);
  if (face != NULL)
    rc = bmfontcache_acquire(cache, face->path, &bold);
  bmfontfamily_destroy(family);
  if (rc != result_OK)
  {
    bmfontcache_release(cache, font);
    return rc;
  }

  reading_release_fonts(task);
  task->md.font = font;
  task->md.bold = bold;
  task->current = idx;
  return result_OK;
}

/* switch the paragraph to the family at font menu row idx (label name) */
static result_t reading_set_family(reading_task_t *task,
                                   int             idx,
                                   const char     *name)
{
  if (idx == task->current)
    return result_OK;

  if (reading_load_family(task, idx, name) != result_OK)
    return result_OK; /* leave the current font in place */

  wuss_window_invalidate_visible(task->window);
  return result_OK;
}

/* read the whole of path into a freshly malloc'd NUL-terminated buffer,
 * less any trailing newlines; caller frees *out_text */
static result_t reading__load_file(const char *path,
                                   char      **out_text,
                                   int        *out_len)
{
  result_t rc;
  FILE    *f;
  long     len;
  char    *text;

  *out_text = NULL;
  *out_len  = 0;

  f = fopen(path, "rb");
  if (f == NULL)
    return result_FILE_NOT_FOUND;

  rc = result_FILE_NOT_FOUND;
  if (fseek(f, 0, SEEK_END) != 0)
    goto exit;

  len = ftell(f);
  if (len < 0 || fseek(f, 0, SEEK_SET) != 0)
    goto exit;

  rc   = result_OOM;
  text = malloc((size_t) len + 1);
  if (text == NULL)
    goto exit;

  if (fread(text, 1, (size_t) len, f) != (size_t) len)
  {
    free(text);
    rc = result_FILE_NOT_FOUND;
    goto exit;
  }

  /* a NUL byte means binary data, not text */
  if (memchr(text, '\0', (size_t) len) != NULL)
  {
    free(text);
    rc = result_BAD_ARG;
    goto exit;
  }

  while (len > 0 && text[len - 1] == '\n')
    len--;
  text[len] = '\0';

  *out_text = text;
  *out_len  = (int) len;
  rc        = result_OK;

exit:
  fclose(f);
  return rc;
}

/* load the file at path into task->md, parsed as Markdown if markdown is
 * set; on failure the previous text stays shown */
static result_t reading__load_text(reading_task_t *task,
                                   const char     *path,
                                   bool            markdown)
{
  result_t rc;
  char    *raw;
  int      rawlen;

  rc = reading__load_file(path, &raw, &rawlen);
  if (rc != result_OK)
    return rc;

  rc = mdtext_set(&task->md, raw, rawlen, markdown);
  free(raw);
  return rc;
}

/* show the file at path (e.g. dropped from the Filer): parsed as Markdown if
 * named ".md", else shown as plain text. Binary files are refused. */
static result_t reading_show(reading_task_t *task, const char *path)
{
  result_t    rc;
  const char *leaf;
  char        name[64];
  char        message[128];
  char        glyph[TASKAPP_GLYPH_NAME_MAX];
  bool        markdown;

  leaf = path_leaf(path);
  /* ponytail: on RISC OS every leaf matches ".md", so any file is parsed as
   * Markdown -- check the filetype if that matters */
  markdown = path_leaf_strip_ext(leaf, ".md", name, sizeof(name));

  rc = reading__load_text(task, path, markdown);
  if (rc != result_OK)
  {
    logf_warning("reading: cannot load \"%s\" (rc=0x%X)", path, rc);

    if (rc == result_BAD_ARG)
      snprintf(message, sizeof(message), "%s is not a text file", leaf);
    else
      snprintf(message, sizeof(message), "Cannot read %s", leaf);
    taskapp_glyph_name("Reading", glyph);
    (void) wuss_error_report(task->wuss, glyph, "Reading", message,
                             wuss_ERROR_BUTTONS_OK, NULL, NULL);

    return rc;
  }

  snprintf(task->path, sizeof(task->path), "%s", path);
  task_set_doc_title(task->window, leaf, "Reading", 0);
  wuss_window_invalidate_visible(task->window);
  return result_OK;
}

/* a file dropped on the app icon opened a fresh instance: if the file will
 * not load, close that instance rather than leave an empty window */
static result_t reading_drop(reading_task_t *task, const char *path)
{
  result_t rc;

  rc = reading_show(task, path);
  if (rc != result_OK)
    wuss_task_destroy(task->delegate); /* its QUIT frees task */

  return rc;
}

/* switch the paragraph's foreground to system palette index "picked" */
static result_t reading_set_fg(reading_task_t *task, wuss_colour_t picked)
{
  task->fg_index = picked;
  wuss_window_invalidate_visible(task->window);
  return result_OK;
}

/* switch the paragraph's background to system palette index "picked", or to
 * none (glyphs blend straight onto whatever is behind the window's content)
 * for wuss_NO_BACKGROUND, the colourmenu's "None" row */
static result_t reading_set_bg(reading_task_t *task, wuss_colour_t picked)
{
  task->bg_transparent = (picked == wuss_NO_BACKGROUND);
  if (!task->bg_transparent)
    task->bg_index = picked;
  wuss_window_invalidate_visible(task->window);
  return result_OK;
}

/* switch letter/word spacing to reading_spacing_presets[idx] */
static result_t reading_set_spacing(reading_task_t *task, int idx)
{
  if (idx < 0 || idx >= NELEMS(reading_spacing_presets) || idx == task->spacing_idx)
    return result_OK;

  task->spacing_idx               = idx;
  task->md.spacing.letter_spacing = reading_spacing_presets[idx].letter_spacing;
  task->md.spacing.word_spacing   = reading_spacing_presets[idx].word_spacing;

  wuss_menu_tick_exclusive(&task->spacing_menu, idx);
  wuss_menu_tick_exclusive_live(task->menu_handle, &task->spacing_menu, idx);
  wuss_window_invalidate_visible(task->window);
  return result_OK;
}

/* align each line to the left, centre or right of the wrap width */
static result_t reading_set_align(reading_task_t *task, int idx)
{
  if (idx < 0 || idx >= NELEMS(task->align_items) ||
      idx == (int) task->md.align)
    return result_OK;

  task->md.align = (mdtext_align_t) idx;

  wuss_menu_tick_exclusive(&task->align_menu, idx);
  wuss_menu_tick_exclusive_live(task->menu_handle, &task->align_menu, idx);
  wuss_window_invalidate_visible(task->window);
  return result_OK;
}

static result_t reading_open_menu(reading_task_t *task)
{
  return wuss_menu_open_at_pointer(task->delegate, &task->top_menu,
                                   &task->menu_handle);
}

/* Every submenu leaf fires wuss_EVENT_PRE_SUBMENU_OPEN, not just the
 * Foreground/Background rows -- "Font"/"Spacing" are each one too
 * (their submenu never changes, so they just open unchanged). Only the
 * Foreground/Background level needs to retarget the shared colourmenu
 * singleton before opening it -- see saturn.c's saturn_pre_submenu_open for
 * the same pattern. */
static result_t reading_pre_submenu_open(reading_task_t     *task,
                                         const wuss_event_t *event)
{
  wuss_menu_handle_t handle;
  int                index;

  handle = event->data.pre_submenu_open.handle;
  index  = event->data.pre_submenu_open.index;

  if (wuss_menu_handle_menu(handle) != &task->colours_menu)
  {
    if (index == READING_MENU_FONT)
    {
      const wuss_menu_t *fontmenu;

      /* the fontmenu singleton may have been rebuilt (at a new address) by
       * another task since top_items[READING_MENU_FONT].submenu was cached in
       * reading_create -- re-fetch instead of handing the spawner a stale
       * pointer, and before ticking, as a rebuild would drop the tick */
      fontmenu = reading_fontmenu(task);
      wuss_fontmenu_set_ticked(task->current);
      return wuss_menu_open_submenu_now(handle, index, fontmenu);
    }

    return wuss_menu_open_submenu_now(handle, index,
                                      task->top_items[index].submenu);
  }

  if (index == READING_COLOURS_MENU_FOREGROUND)
  {
    task->colourmenu_target = &task->fg_index;
    wuss_colourmenu_set_none(0);
    wuss_colourmenu_set_title("Foreground");
  }
  else
  {
    task->colourmenu_target = &task->bg_index;
    wuss_colourmenu_set_none(1);
    wuss_colourmenu_set_title("Background");
  }
  wuss_colourmenu_set_ticked(*task->colourmenu_target);

  return wuss_menu_open_submenu_now(handle, index,
                                    wuss_colourmenu_menu(task->wuss));
}

/* ----------------------------------------------------------------------- */

result_t reading_create(wuss_t *wuss, reading_task_t **out)
{
  result_t           rc;
  reading_task_t    *task;
  wuss_task_t       *delegate;
  wuss_task_desc_t   delegate_desc;
  const wuss_menu_t *menu;
  const char        *sysname;
  char               family[128];
  const char        *style;
  int                i;
  size2d_t           sz;

  task = calloc(1, sizeof(*task));
  if (task == NULL)
    return result_OOM;

  task->wuss        = wuss;
  task->md.font     = wuss_get_font(wuss);
  task->md.leading  = LEADING;
  task->current     = -1; /* until the system font's row is found below */
  task->spacing_idx = READING_DEFAULT_SPACING;
  task->md.spacing.letter_spacing = reading_spacing_presets[READING_DEFAULT_SPACING].letter_spacing;
  task->md.spacing.word_spacing   = reading_spacing_presets[READING_DEFAULT_SPACING].word_spacing;
  task->fg_index    = wuss_nearest_colour(wuss, 0x00, 0x00, 0x00);
  task->bg_index    = wuss_nearest_colour(wuss, 0xFF, 0xFF, 0xFF);
  task->bg_transparent = true; /* "None": blend onto the window backdrop */
  task->colourmenu_target = NULL;

  /* the shared picker: every font family under resources/bmfonts, sorted,
   * less any SYSTEM-class font (e.g. the one wuss draws menu ticks/arrows
   * from) */
  menu = reading_fontmenu(task);
  if (menu == NULL)
  {
    free(task); /* nothing registered yet; the spawner will not free it */
    return result_OOM;
  }

  /* start in the system font's family ("Family Style"), if the picker lists
   * it; else stay on the system font itself with no row ticked */
  sysname = wuss_get_font_name_n(wuss, 0);
  if (sysname != NULL &&
      bmfontfamily_split_label(sysname, family, sizeof(family), &style) ==
      result_OK)
  {
    for (i = 0; i < menu->nitems; i++)
      if (strcmp(menu->items[i].text, family) == 0)
        (void) reading_load_family(task, i, menu->items[i].text);
  }

  if (wuss_colourmenu_menu(wuss) == NULL)
  {
    reading_release_fonts(task);
    free(task);
    return result_OOM;
  }

  task->spacing_items[0].text    = "Normal";
  task->spacing_items[0].flags   = wuss_MENU_ITEM_TICKED;
  task->spacing_items[0].submenu = NULL;
  task->spacing_items[0].window  = NULL;
  task->spacing_items[1].text    = "Letter";
  task->spacing_items[1].flags   = wuss_MENU_ITEM_NONE;
  task->spacing_items[1].submenu = NULL;
  task->spacing_items[1].window  = NULL;
  task->spacing_items[2].text    = "Word";
  task->spacing_items[2].flags   = wuss_MENU_ITEM_NONE;
  task->spacing_items[2].submenu = NULL;
  task->spacing_items[2].window  = NULL;
  task->spacing_items[3].text    = "Letter+Word";
  task->spacing_items[3].flags   = wuss_MENU_ITEM_NONE;
  task->spacing_items[3].submenu = NULL;
  task->spacing_items[3].window  = NULL;

  WUSS_MENU_TITLE(task->spacing_menu, "Spacing", task->spacing_items,
                 NELEMS(task->spacing_items));

  WUSS_MENU_ITEM(task->align_items, READING_ALIGN_LEFT, "Left",
                 wuss_MENU_ITEM_TICKED);
  WUSS_MENU_ITEM(task->align_items, READING_ALIGN_CENTRE, "Centre",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->align_items, READING_ALIGN_RIGHT, "Right",
                 wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->align_menu, "Alignment", task->align_items,
                 NELEMS(task->align_items));

  /* Both rows' .submenu just need to be non-NULL to draw an arrow and
   * become hoverable; which menu they name doesn't matter since
   * reading_pre_submenu_open always supplies the menu to open (see saturn.c's
   * SATURN_COLOURS_MENU_* for the same pattern). */
  WUSS_MENU_ITEM_MENU(task->colours_items, READING_COLOURS_MENU_FOREGROUND,
                      "Foreground", wuss_MENU_ITEM_PRE_OPEN,
                      wuss_colourmenu_menu(wuss));
  WUSS_MENU_ITEM_MENU(task->colours_items, READING_COLOURS_MENU_BACKGROUND,
                      "Background", wuss_MENU_ITEM_PRE_OPEN,
                      wuss_colourmenu_menu(wuss));

  WUSS_MENU_TITLE(task->colours_menu, "Colours", task->colours_items,
                 NELEMS(task->colours_items));

  /* top-level menu: "Font" borrows the fontmenu's own live wuss_menu_t (so
   * ticks and wuss_fontmenu_selected keep working), "Spacing"/"Alignment"/
   * "Colours" are the per-instance menus built above */
  task->top_items[0].text    = "Font";
  task->top_items[0].flags   = wuss_MENU_ITEM_PRE_OPEN;
  task->top_items[0].submenu = menu;
  task->top_items[0].window  = NULL;
  task->top_items[1].text    = "Spacing";
  task->top_items[1].flags   = wuss_MENU_ITEM_NONE;
  task->top_items[1].submenu = &task->spacing_menu;
  task->top_items[1].window  = NULL;
  task->top_items[2].text    = "Alignment";
  task->top_items[2].flags   = wuss_MENU_ITEM_NONE;
  task->top_items[2].submenu = &task->align_menu;
  task->top_items[2].window  = NULL;
  task->top_items[3].text    = "Colours";
  task->top_items[3].flags   = wuss_MENU_ITEM_NONE;
  task->top_items[3].submenu = &task->colours_menu;
  task->top_items[3].window  = NULL;

  WUSS_MENU_TITLE(task->top_menu, "Reading", task->top_items,
                 NELEMS(task->top_items));

  delegate_desc.handle    = reading_handle;
  delegate_desc.task_data = task;
  delegate_desc.name      = "reading";
  rc = wuss_task_create(wuss, &delegate_desc, &delegate);
  if (rc != result_OK)
  {
    reading_release_fonts(task);
    mdtext_free(&task->md);
    free(task); /* nothing registered yet; the spawner will not free it */
    return rc;
  }
  wuss_task_set_autoclose(delegate, 1);
  task->delegate = delegate;

  sz                = SIZE2D(220, 220);
  rc = wuss_window_create_at(delegate,
                             &task_place,
                             sz,
                             "Reading",
                             (wuss_WINDOW_DEFAULT & ~wuss_WINDOW_HSCROLL) | wuss_WINDOW_NO_RESIZE_BLIT, /* paragraph reflows across the whole window, so a resize must redraw all of it, not just the newly (un)covered edge */
                             wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                             SIZE2D(sz.w, 220 * 4),
                             SIZE2D(0, 0),
                             &task->window);
  if (rc != result_OK)
  {
    wuss_task_destroy(delegate); /* unregister; its QUIT frees the task block */
    return rc;
  }

  if (out)
    *out = task;

  return result_OK;
}

static const unsigned short reading_opens[] =
{
  filetype_RISCOS_TEXT, filetype_RISCOS_MARKDOWN, 0
};

static const taskapp_desc_t reading_desc =
{
  .name      = "Reading",
  .purpose   = "Read Markdown and text files",
  .create    = (taskapp_create_fn_t) reading_create,
  .data_load = (taskapp_data_load_fn_t) reading_drop,
  .drop_only = true,
  .opens     = reading_opens
};

result_t reading_app_create(wuss_t *wuss)
{
  return taskapp_install(wuss, &reading_desc);
}

static result_t reading__spawn_show(wuss_t *wuss, void *param, void **out)
{
  result_t        rc;
  reading_task_t *task;

  rc = reading_create(wuss, &task);
  if (rc != result_OK)
    return rc;

  *out = task;
  return reading_drop(task, (const char *) param);
}

result_t reading_open_file(wuss_t *wuss, const char *path)
{
  void *task;
  char  copy[512];

  /* reading_create calls pathf, so path may be pathf's shared buffer */
  snprintf(copy, sizeof(copy), "%s", path);

  return taskapp_spawn(wuss, &reading_desc, reading__spawn_show, copy,
                       &task);
}

void reading_destroy(reading_task_t *task)
{
  reading_release_fonts(task);
  mdtext_free(&task->md);
  free(task);
}

static result_t reading_redraw(const wuss_event_t *event, void *task_data)
{
  result_t        rc;
  reading_task_t *tcx;
  const box_t    *bounds;
  const colour_t *palette;
  colour_t        fg, bg;
  point_t         origin;

  tcx = task_data;
  if (tcx->md.text == NULL)
    return result_OK; /* nothing dropped yet; the backdrop shows */

  bounds = event->data.redraw.bounds;

  palette = wuss_get_palette(tcx->wuss, NULL);
  fg      = palette[tcx->fg_index];
  bg      = tcx->bg_transparent ? colour_rgba(0, 0, 0, 0) : palette[tcx->bg_index];

  rc = mdtext_layout(&tcx->md, bounds->x1 - bounds->x0 - 2 * READING_INSET);
  if (rc != result_OK)
    return rc;

  origin.x = bounds->x0 - event->data.redraw.scroll.x + READING_INSET;
  origin.y = bounds->y0 - event->data.redraw.scroll.y + READING_INSET;
  mdtext_draw(&tcx->md, event->data.redraw.scr, origin, fg, bg);

  return result_OK;
}

/* Size the scrollable document to the laid-out text. Done from idle rather
 * than from redraw: set_doc repaints the scrollbar and invalidates the
 * content, which a redraw in progress would lose. Only done when the line
 * count changes. */
static result_t reading_idle(void *task_data)
{
  result_t        rc;
  reading_task_t *tcx;
  box_t           bounds;
  size2d_t        doc;

  tcx = task_data;

  /* the proginfo dialogue is a second window on this same (autoclose)
   * delegate, so the main window may already be gone */
  if (tcx->window == NULL || tcx->md.text == NULL)
    return result_OK;

  wuss_window_get_content_bounds(tcx->window, &bounds);
  rc = mdtext_layout(&tcx->md, bounds.x1 - bounds.x0 - 2 * READING_INSET);
  if (rc != result_OK)
    return rc;

  /* a window cannot be dragged wider than its document, so make the
   * document as wide as the screen allows; the text reflows to fit */
  wuss_window_get_max_content(tcx->window, &doc);
  doc.h = mdtext_nlines(&tcx->md) * mdtext_line_height(&tcx->md) +
          2 * READING_INSET;

  if (doc.h == tcx->doc_h)
    return result_OK;
  tcx->doc_h = doc.h;

  return wuss_window_set_doc(tcx->window, doc);
}

/* A DataLoad on the reading window: show the file it names. */
static result_t reading_message(reading_task_t     *tcx,
                                wuss_window_t      *window,
                                const wuss_event_t *event)
{
  if (tcx->window == NULL || window != tcx->window)
    return result_OK;

  return taskapp_window_data_load(tcx->wuss, tcx->delegate, tcx,
                                  (taskapp_data_load_fn_t) reading_show,
                                  event->data.message);
}

result_t reading_handle(wuss_window_t      *window,
                        const wuss_event_t *event,
                        void               *task_data)
{
  reading_task_t *tcx;

  tcx = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return reading_redraw(event, task_data);

  case wuss_EVENT_MOUSE:
    if (window != tcx->window)
      return result_OK; /* the proginfo dialogue has no click behaviour of
                         * its own */
    if (event->data.mouse.action == wuss_MOUSE_DOWN &&
        (event->data.mouse.button & wuss_BUTTON_MENU))
      return reading_open_menu(tcx);
    return result_OK;

  case wuss_EVENT_MENU_SELECT:
    {
      result_t      rc;
      const char   *name;
      wuss_colour_t picked;
      int           mine;

      name = wuss_fontmenu_selected(event);
      if (name != NULL)
      {
        rc = reading_set_family(tcx, event->data.menu_select.index, name);

        /* ADJUST keeps the chain open, so retick the still-open font level
         * in place (a no-op once a SELECT pick has closed it). Use the
         * event's own menu, not a fresh reading_fontmenu(): that can rebuild
         * the singleton under the open chain. */
        wuss_menu_tick_exclusive_live(tcx->menu_handle,
                                      event->data.menu_select.menu,
                                      tcx->current);
        return rc;
      }
      if (event->data.menu_select.menu == &tcx->spacing_menu)
        return reading_set_spacing(tcx, event->data.menu_select.index);
      if (event->data.menu_select.menu == &tcx->align_menu)
        return reading_set_align(tcx, event->data.menu_select.index);
      if (tcx->colourmenu_target == NULL)
        return result_OK;
      picked = wuss_colourmenu_selected(event, &mine);
      if (!mine)
        return result_OK;
      if (tcx->colourmenu_target == &tcx->fg_index)
        return reading_set_fg(tcx, picked);
      return reading_set_bg(tcx, picked);
    }
    return result_OK;

  case wuss_EVENT_MESSAGE:
    return reading_message(tcx, window, event);

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return reading_pre_submenu_open(tcx, event);

  case wuss_EVENT_MENU_CLOSED:
    tcx->menu_handle = NULL;
    return result_OK;

  case wuss_EVENT_CLOSE_ADJUST:
    if (window != tcx->window)
      return result_OK;
    return task_close_adjust(tcx->wuss, tcx->delegate, window, tcx->path,
                             event);

  case wuss_EVENT_CLOSE:
    if (window == tcx->window)
      tcx->window = NULL;
    return result_OK;

  case wuss_EVENT_QUIT:
    reading_destroy(tcx);
    return result_OK;

  case wuss_EVENT_IDLE:
    return reading_idle(task_data);

  case wuss_EVENT_KEY:
    return result_WUSS_KEY_UNCLAIMED;

  default:
    return result_OK;
  }
}

#endif /* WUSS_APP */
