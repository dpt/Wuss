/* wuss/tasks/jottings/jottings.c -- Jottings sticky notes */

#ifdef WUSS_APP

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "io/filetype.h"
#include "io/path.h"
#include "text/mdtext.h"
#include "text/utf8.h"
#include "utils/array.h"
#include "wuss/component/colourmenu.h"
#include "wuss/component/error.h"
#include "wuss/menu.h"
#include "wuss/message.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"

/* Jottings: notes are Markdown, kept in the per-user "Notes" directory once
 * pinned to the backdrop. Start it before other apps that open ".md" files so
 * it claims the notes in its own directory first. See
 * docs/windowing/backdrop.md. */

#define JOTTING_INSET     4
#define JOTTING_TITLE_MAX 40
#define JOTTING_COLUMN    (-1)    /* a pixel x meaning the caret's own column */
#define JOTTING_LINE_END  0x10000 /* a pixel x past the end of any line */

/* a note's file is <Notes><sep><title><ext>: RISC OS has '.' for a
 * separator and no extensions */
#ifdef __riscos
#define JOTTING_SEP "."
#define JOTTING_EXT ""
#else
#define JOTTING_SEP "/"
#define JOTTING_EXT ".md"
#endif

enum
{
  JOTTING_MENU_NEW,
  JOTTING_MENU_COLOUR,
  JOTTING_MENU_EDIT,
  JOTTING_MENU_PIN,
  JOTTING_MENU_DELETE,
  JOTTING_MENU__LIMIT
};

typedef struct jotting
{
  wuss_t            *wuss;
  wuss_task_t       *delegate;
  wuss_window_t     *window;
  mdtext_t           md;
  char              *src;       /* owned Markdown source, terminated */
  int                len;
  int                allocated;
  int                caret;     /* byte index into src */
  int                goal_x;    /* column held by vertical moves, or -1 */
  bool               editing;   /* showing the raw source, taking keys */
  bool               dirty;     /* edited since it was last saved */
  wuss_colour_t      colour;
  wuss_colour_t      fg;
  char               path[DPTLIB_MAXPATH]; /* "" until pinned or loaded */
  wuss_menu_item_t   items[JOTTING_MENU__LIMIT];
  wuss_menu_t        menu;
  wuss_menu_handle_t menu_handle;
}
jotting_t;

static const char jottings_welcome[] =
  "# Welcome\n"
  "\n"
  "This is a **Jottings** note. MENU on it to make a new note, change its "
  "colour or edit it.\n"
  "\n"
  "*Pin to backdrop* saves the note in your Notes directory and pins it to "
  "the backdrop. Double-click the pin to open the note again.";

static char       g_notes[DPTLIB_MAXPATH]; /* notes directory; "" if none */
static jotting_t *g_doomed; /* the note whose Delete awaits confirmation */

static result_t jotting_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data);

/* ----------------------------------------------------------------------- */

static void jotting_report(jotting_t *note, const char *message)
{
  char glyph[TASKAPP_GLYPH_NAME_MAX];

  taskapp_glyph_name("Jottings", glyph);
  (void) wuss_error_report(note->wuss, glyph, "Jottings", message,
                           wuss_ERROR_BUTTONS_OK, NULL, NULL);
}

/* room for extra more bytes, plus the terminator */
static result_t jotting_reserve(jotting_t *note, int extra)
{
  if (array_grow((void **) &note->src, 1, note->len, &note->allocated,
                 extra + 1, 64))
    return result_OOM;

  return result_OK;
}

static result_t jotting_set_source(jotting_t *note, const char *src, int len)
{
  result_t rc;

  note->len = 0;
  rc        = jotting_reserve(note, len);
  if (rc != result_OK)
    return rc;

  memcpy(note->src, src, (size_t) len);
  note->len            = len;
  note->src[note->len] = '\0';
  note->caret          = len;
  note->goal_x         = -1;

  return result_OK;
}

/* the first line, without its leading '#'s and spaces */
static void jotting_title(const jotting_t *note, char *title, size_t cap)
{
  const char *s;
  int         n;

  s = note->src;
  while (*s == '#' || *s == ' ')
    s++;
  n = MIN((int) strcspn(s, "\n"), JOTTING_TITLE_MAX);

  if (n == 0)
    snprintf(title, cap, "Note");
  else
    snprintf(title, cap, "%.*s", n, s);
}

/* re-set the shown text and title after a change: editing shows the raw
 * source (jotting_redraw adds the caret), viewing renders the Markdown */
static result_t jotting_refresh(jotting_t *note)
{
  result_t rc;
  char     title[JOTTING_TITLE_MAX + 1];

  rc = mdtext_set(&note->md, note->src, note->len, !note->editing);
  if (rc != result_OK)
    return rc;

  jotting_title(note, title, sizeof(title));
  wuss_window_set_title(note->window, title);
  wuss_window_invalidate_visible(note->window);

  return result_OK;
}

/* ----------------------------------------------------------------------- */

static result_t jotting_save(jotting_t *note)
{
  FILE *f;
  int   ok;

  f = fopen(note->path, "wb");
  if (f == NULL)
    return result_FOPEN_FAILED;

  ok = fwrite(note->src, 1, (size_t) note->len, f) == (size_t) note->len;
  ok = fclose(f) == 0 && ok;
  if (!ok)
    return result_FOPEN_FAILED;

  note->dirty = false;
  return result_OK;
}

/* a new file in the notes directory named after the title: "-2", "-3" and
 * so on are added if the name is taken. A note keeps its file once it has
 * one. */
static result_t jotting_choose_path(jotting_t *note)
{
  char title[JOTTING_TITLE_MAX + 1];
  char name[JOTTING_TITLE_MAX + 1];
  int  i, j;

  if (note->path[0] != '\0')
    return result_OK;
  if (g_notes[0] == '\0')
    return result_NOT_FOUND;

  /* a portable leaf: letters, digits and '-', with '_' for spaces */
  jotting_title(note, title, sizeof(title));
  j = 0;
  for (i = 0; title[i] != '\0'; i++)
  {
    if (isalnum((unsigned char) title[i]) || title[i] == '-')
      name[j++] = title[i];
    else if (title[i] == ' ' || title[i] == '_')
      name[j++] = '_';
  }
  name[j] = '\0';
  if (j == 0)
    snprintf(name, sizeof(name), "Note");

  for (i = 1; i < 1000; i++)
  {
    if (i == 1)
      snprintf(note->path, sizeof(note->path), "%s" JOTTING_SEP "%s"
               JOTTING_EXT, g_notes, name);
    else
      snprintf(note->path, sizeof(note->path), "%s" JOTTING_SEP "%s-%d"
               JOTTING_EXT, g_notes, name, i);
    if (!path_exists(note->path))
      return result_OK;
  }

  note->path[0] = '\0';
  return result_NOT_FOUND;
}

/* save the note to its file and ask the Pinboard to pin it; a bounce says
 * there is no Pinboard */
static result_t jotting_pin(jotting_t *note)
{
  char message[DPTLIB_MAXPATH + 32];

  if (jotting_choose_path(note) != result_OK)
  {
    jotting_report(note, "There is no Notes directory");
    return result_OK;
  }

  if (jotting_save(note) != result_OK)
  {
    snprintf(message, sizeof(message), "Cannot save %s",
             path_leaf(note->path));
    jotting_report(note, message);
    return result_OK;
  }

  return wuss_send_recorded(note->wuss, note->delegate, NULL,
                            wuss_MESSAGE_PINBOARD_PIN, note->path,
                            strlen(note->path) + 1, 0, NULL);
}

static void jotting_delete_chosen(wuss_error_choice_t choice, void *opaque)
{
  jotting_t *note;

  NOT_USED(opaque);

  note     = g_doomed;
  g_doomed = NULL;
  if (note == NULL || choice != wuss_ERROR_OK)
    return;

  (void) remove(note->path);
  note->dirty = false;
  wuss_window_close(note->window); /* its QUIT frees the note */
}

/* close the note, and once confirmed delete its file if it has one; a pin
 * of it is left to the Pinboard */
static void jotting_delete(jotting_t *note)
{
  char message[DPTLIB_MAXPATH + 32];

  if (note->path[0] == '\0')
  {
    wuss_window_close(note->window); /* its QUIT frees the note */
    return;
  }

  if (g_doomed != NULL)
    return; /* one confirmation at a time */

  g_doomed = note;
  snprintf(message, sizeof(message), "Delete the note %s?",
           path_leaf(note->path));
  (void) wuss_error_report(note->wuss, NULL, "Jottings", message,
                           wuss_ERROR_BUTTONS_OK_CANCEL, jotting_delete_chosen,
                           NULL);
}

static result_t jotting_set_editing(jotting_t *note, bool editing)
{
  char message[DPTLIB_MAXPATH + 32];

  if (note->editing == editing)
    return result_OK;

  note->editing = editing;
  if (editing)
  {
    (void) wuss_set_focus(note->wuss, note->window);
  }
  else if (note->dirty && note->path[0] != '\0' &&
           jotting_save(note) != result_OK)
  {
    snprintf(message, sizeof(message), "Cannot save %s",
             path_leaf(note->path));
    jotting_report(note, message);
  }

  return jotting_refresh(note);
}

/* ----------------------------------------------------------------------- */

/* key and click events can outrun the redraw that would lay the text out */
static bool jotting_layout(jotting_t *note)
{
  box_t bounds;

  wuss_window_get_content_bounds(note->window, &bounds);

  return mdtext_layout(&note->md, bounds.x1 - bounds.x0 - 2 * JOTTING_INSET) ==
         result_OK;
}

/* every caret change ends a run of vertical moves; those restore goal_x */
static void jotting_caret_set(jotting_t *note, int index)
{
  note->caret  = index;
  note->goal_x = -1;
  wuss_window_invalidate_visible(note->window);
}

/* move the caret to the pixel x, or for JOTTING_COLUMN its own column, on the
 * line 'lines' from its own */
static void jotting_caret_to(jotting_t *note, int x, int lines)
{
  point_t pos;
  int     goal;
  int     index;

  if (!jotting_layout(note) ||
      !mdtext_caret_pos(&note->md, note->caret, &pos))
    return;

  /* a caret draws a pixel left of its boundary, and that pixel can fall in
   * a narrow glyph: take the boundary, so Down then Up comes back */
  goal = -1;
  if (x == JOTTING_COLUMN)
  {
    goal = note->goal_x >= 0 ? note->goal_x : pos.x + 1;
    x    = goal;
  }

  pos.x  = x;
  pos.y += lines * mdtext_line_height(&note->md);
  if (!mdtext_find_caret(&note->md, pos, &index))
    return;

  jotting_caret_set(note, index);
  note->goal_x = goal;
}

/* lines in a window's height, for Page Up and Page Down */
static int jotting_page_lines(jotting_t *note)
{
  box_t bounds;

  wuss_window_get_content_bounds(note->window, &bounds);

  return MAX((bounds.y1 - bounds.y0) / mdtext_line_height(&note->md), 1);
}

/* the bounds of the source line, not wrapped line, holding the caret */
static int jotting_line_start(const jotting_t *note)
{
  int i;

  i = note->caret;
  while (i > 0 && note->src[i - 1] != '\n')
    i--;

  return i;
}

static int jotting_line_end(const jotting_t *note)
{
  return note->caret + (int) strcspn(note->src + note->caret, "\n");
}

/* replace the bytes lo..hi of the source with n bytes of 'ins', leaving the
 * caret after them */
static result_t jotting_splice(jotting_t  *note,
                               int         lo,
                               int         hi,
                               const char *ins,
                               int         n)
{
  result_t rc;

  if (lo == hi && n == 0)
    return result_OK;

  rc = jotting_reserve(note, n);
  if (rc != result_OK)
    return rc;

  memmove(note->src + lo + n, note->src + hi, (size_t) (note->len - hi + 1));
  memcpy(note->src + lo, ins, (size_t) n);
  note->len   += n - (hi - lo);
  note->dirty  = true;
  jotting_caret_set(note, lo + n);

  return jotting_refresh(note);
}

/* remove the source line holding the caret, with one of its newlines */
static result_t jotting_delete_line(jotting_t *note)
{
  int lo;
  int hi;

  lo = jotting_line_start(note);
  hi = jotting_line_end(note);
  if (note->src[hi] == '\n')
    hi++;
  else if (lo > 0)
    lo--;

  return jotting_splice(note, lo, hi, "", 0);
}

/* Modifiers follow writable icons: Shift moves by word (so does Alt, for
 * macOS's Option) and Ctrl to the end of the line (Cmd on macOS). */
static result_t jotting_key(jotting_t *note, const wuss_event_t *event)
{
  int           code;
  int           word;
  int           ctrl;
  unsigned long codepoint;
  int           index;
  char          utf8[4];
  int           n;

  code = event->data.key.code;
  if (!note->editing)
    return result_WUSS_KEY_UNCLAIMED;

  word  = event->data.key.modifiers & (wuss_KEY_MOD_SHIFT | wuss_KEY_MOD_ALT);
  ctrl  = event->data.key.modifiers & wuss_KEY_MOD_CTRL;
  index = note->caret;

  switch (code)
  {
  case wuss_KEY_LEFT:
    if (ctrl)
      jotting_caret_to(note, 0, 0);
    else if (word)
      jotting_caret_set(note, utf8_word_left(note->src, index));
    else if (index > 0)
      jotting_caret_set(note, utf8_prev(note->src, index));
    return result_OK;

  case wuss_KEY_RIGHT:
    if (ctrl)
      jotting_caret_to(note, JOTTING_LINE_END, 0);
    else if (word)
      jotting_caret_set(note, utf8_word_right(note->src, note->len, index));
    else if (index < note->len)
      jotting_caret_set(note, index + utf8_decode(note->src + index,
                                                  note->len - index,
                                                  &codepoint));
    return result_OK;

  case wuss_KEY_HOME:
    if (ctrl)
      jotting_caret_set(note, 0);
    else
      jotting_caret_to(note, 0, 0);
    return result_OK;

  case wuss_KEY_END:
    if (ctrl)
      jotting_caret_set(note, note->len);
    else
      jotting_caret_to(note, JOTTING_LINE_END, 0);
    return result_OK;

  case wuss_KEY_UP:
    if (ctrl)
      jotting_caret_set(note, 0);
    else
      jotting_caret_to(note, JOTTING_COLUMN, -1);
    return result_OK;

  case wuss_KEY_DOWN:
    if (ctrl)
      jotting_caret_set(note, note->len);
    else
      jotting_caret_to(note, JOTTING_COLUMN, 1);
    return result_OK;

  case wuss_KEY_PAGE_UP:
    jotting_caret_to(note, JOTTING_COLUMN, -jotting_page_lines(note));
    return result_OK;

  case wuss_KEY_PAGE_DOWN:
    jotting_caret_to(note, JOTTING_COLUMN, jotting_page_lines(note));
    return result_OK;

  case wuss_KEY_BACKSPACE:
    if (ctrl)
      n = jotting_line_start(note);
    else if (word)
      n = utf8_word_left(note->src, index);
    else
      n = index > 0 ? utf8_prev(note->src, index) : 0;
    return jotting_splice(note, n, index, "", 0);

  case wuss_KEY_DELETE:
    if (ctrl)
      n = jotting_line_end(note);
    else if (word)
      n = utf8_word_right(note->src, note->len, index);
    else if (index < note->len)
      n = index + utf8_decode(note->src + index, note->len - index, &codepoint);
    else
      n = index;
    return jotting_splice(note, index, n, "", 0);

  default:
    break;
  }

  if (ctrl)
  {
    switch (code)
    {
    case 'u':
    case 'U':
      return jotting_delete_line(note);

    case 'k':
    case 'K':
      return jotting_splice(note, index, jotting_line_end(note), "", 0);

    default:
      return result_WUSS_KEY_UNCLAIMED;
    }
  }

  if (event->data.key.modifiers & wuss_KEY_MOD_ALT)
    return result_WUSS_KEY_UNCLAIMED;

  switch (code)
  {
  case wuss_KEY_ESCAPE:
    return jotting_set_editing(note, false);

  case wuss_KEY_RETURN:
    utf8[0] = '\n';
    n       = 1;
    break;

  default:
    if (code < 32 || code == 127)
      return result_WUSS_KEY_UNCLAIMED;
    n = utf8_encode((unsigned long) code, utf8);
    if (n == 0)
      return result_WUSS_KEY_UNCLAIMED; /* a wuss_KEY_* constant */
    break;
  }

  return jotting_splice(note, index, index, utf8, n);
}

/* put the caret at a point in the content, as a mouse event gives it */
static void jotting_caret_at_point(jotting_t *note, point_t point)
{
  int index;

  if (jotting_layout(note) &&
      mdtext_find_caret(&note->md,
                        POINT(point.x - JOTTING_INSET, point.y - JOTTING_INSET),
                        &index))
    jotting_caret_set(note, index);
}

static void jotting_redraw(jotting_t *note, const wuss_event_t *event)
{
  const box_t    *bounds;
  const colour_t *palette;
  point_t         origin;
  point_t         caret;
  wuss_colour_t   ink;

  if (!jotting_layout(note))
    return;

  bounds   = event->data.redraw.bounds;
  palette  = wuss_get_palette(note->wuss, NULL);
  origin.x = bounds->x0 - event->data.redraw.scroll.x + JOTTING_INSET;
  origin.y = bounds->y0 - event->data.redraw.scroll.y + JOTTING_INSET;
  mdtext_draw(&note->md, event->data.redraw.scr, origin, palette[note->fg],
              colour_rgba(0, 0, 0, 0));

  /* red with the focus, grey without */
  if (note->editing && mdtext_caret_pos(&note->md, note->caret, &caret))
  {
    caret.x += origin.x;
    caret.y += origin.y;
    if (wuss_get_focus(note->wuss) == note->window)
      ink = wuss_nearest_colour(note->wuss, 0xFF, 0x00, 0x00);
    else
      ink = wuss_nearest_colour(note->wuss, 0x88, 0x88, 0x88);
    bmfont_draw_caret(note->md.font, event->data.redraw.scr, palette[ink],
                      &caret);
  }
}

/* ----------------------------------------------------------------------- */

static result_t jotting_create(wuss_t *wuss, jotting_t **out)
{
  result_t         rc;
  jotting_t       *note;
  wuss_task_desc_t desc;

  note = calloc(1, sizeof(*note));
  if (note == NULL)
    return result_OOM;

  note->wuss       = wuss;
  note->colour     = wuss_nearest_colour(wuss, 0xFF, 0xFF, 0x80);
  note->fg         = wuss_nearest_colour(wuss, 0x00, 0x00, 0x00);
  note->editing    = true; /* a new note is typed into at once */
  note->md.font    = wuss_get_font(wuss);
  note->md.leading = 2;

  rc = jotting_set_source(note, "", 0);
  if (rc != result_OK)
  {
    free(note);
    return rc;
  }

  WUSS_MENU_ITEM(note->items, JOTTING_MENU_NEW, "New note",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM_MENU(note->items, JOTTING_MENU_COLOUR, "Colour",
                      wuss_MENU_ITEM_PRE_OPEN |
                      wuss_MENU_ITEM_BORROWED_SUBMENU,
                      wuss_colourmenu_menu(wuss));
  WUSS_MENU_ITEM(note->items, JOTTING_MENU_EDIT, "Edit", wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(note->items, JOTTING_MENU_PIN, "Pin to backdrop",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM(note->items, JOTTING_MENU_DELETE, "Delete",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(note->menu, "Note", note->items, NELEMS(note->items));

  desc.handle    = jotting_handle;
  desc.task_data = note;
  desc.name      = "Jotting";
  rc = wuss_task_create(wuss, &desc, &note->delegate);
  if (rc != result_OK)
  {
    free(note->src);
    free(note);
    return rc;
  }
  wuss_task_set_autoclose(note->delegate, 1);

  rc = wuss_window_create_placed(note->delegate, SIZE2D(200, 160), "Note",
                                 wuss_WINDOW_CLOSE | wuss_WINDOW_BACK |
                                 wuss_WINDOW_RESIZE | wuss_WINDOW_FOCUSABLE |
                                 wuss_WINDOW_NO_RESIZE_BLIT,
                                 wuss_BACKDROP_COLOUR(note->colour),
                                 SIZE2D(640, 480), SIZE2D(0, 0),
                                 &note->window);
  if (rc != result_OK)
  {
    wuss_task_destroy(note->delegate); /* its QUIT frees the note */
    return rc;
  }

  if (out)
    *out = note;

  return jotting_refresh(note);
}

/* open the note file at path; edits are saved back to it */
static result_t jotting_load(jotting_t *note, const char *path)
{
  result_t rc;
  FILE    *f;
  size_t   n;
  char     message[DPTLIB_MAXPATH + 32];

  rc = result_OK;
  f  = fopen(path, "rb");
  if (f == NULL)
  {
    snprintf(message, sizeof(message), "Cannot read %s", path_leaf(path));
    jotting_report(note, message);
    return result_FOPEN_FAILED;
  }

  note->len = 0;
  do
  {
    rc = jotting_reserve(note, 256);
    if (rc != result_OK)
      break;
    n          = fread(note->src + note->len, 1, 256, f);
    note->len += (int) n;
  }
  while (n == 256);
  fclose(f);
  note->src[note->len] = '\0';
  note->caret          = note->len;
  if (rc != result_OK)
    return rc;

  snprintf(note->path, sizeof(note->path), "%s", path);
  note->editing = false;
  note->dirty   = false;

  return jotting_refresh(note);
}

static void jotting_destroy(jotting_t *note)
{
  if (g_doomed == note)
    g_doomed = NULL;

  /* don't lose an edit to a note that has a file */
  if (note->dirty && note->path[0] != '\0')
    (void) jotting_save(note);

  mdtext_free(&note->md);
  free(note->src);
  free(note);
}

/* a DataOpen claims only the notes in the notes directory, so other ".md"
 * files go to Reading */
static bool jottings_claims(const char *path)
{
  size_t n;

  n = strlen(g_notes);

  return n > 0 && strncmp(path, g_notes, n) == 0 && path[n] == JOTTING_SEP[0];
}

static const unsigned short jottings_opens[] =
{
  filetype_RISCOS_MARKDOWN, 0
};

static const taskapp_desc_t jottings_desc =
{
  .name      = "Jottings",
  .purpose   = "Sticky notes for the desktop",
  .create    = (taskapp_create_fn_t) jotting_create,
  .data_load = (taskapp_data_load_fn_t) jotting_load,
  .opens     = jottings_opens,
  .claims    = jottings_claims
};

/* make a note showing text, or a blank one being edited for NULL */
static result_t jottings_spawn(wuss_t *wuss, void *param, void **out)
{
  result_t   rc;
  jotting_t *note;

  rc = jotting_create(wuss, &note);
  if (rc != result_OK)
    return rc;

  *out = note;
  if (param == NULL)
    return result_OK;

  note->editing = false;
  rc = jotting_set_source(note, param, (int) strlen(param));
  if (rc != result_OK)
    return rc;

  return jotting_refresh(note);
}

/* ----------------------------------------------------------------------- */

static result_t jotting_menu_select(jotting_t          *note,
                                    const wuss_event_t *event)
{
  wuss_colour_t colour;
  int           ok;
  void         *instance;

  colour = wuss_colourmenu_selected(event, &ok);
  if (ok)
  {
    if (colour == wuss_NO_BACKGROUND)
      return result_OK;
    note->colour = colour;
    return wuss_window_set_background(note->window,
                                      wuss_BACKDROP_COLOUR(colour));
  }

  if (event->data.menu_select.menu != &note->menu)
    return result_OK;

  switch (event->data.menu_select.index)
  {
  case JOTTING_MENU_NEW:
    return taskapp_spawn(note->wuss, &jottings_desc, jottings_spawn, NULL,
                         &instance);

  case JOTTING_MENU_EDIT:
    return jotting_set_editing(note, !note->editing);

  case JOTTING_MENU_PIN:
    return jotting_pin(note);

  case JOTTING_MENU_DELETE:
    jotting_delete(note);
    return result_OK;

  default:
    return result_OK;
  }
}

static result_t jotting_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data)
{
  result_t   rc;
  jotting_t *note;

  NOT_USED(window);

  note = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    jotting_redraw(note, event);
    return result_OK;

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action == wuss_MOUSE_MOVE ||
        event->data.mouse.action == wuss_MOUSE_UP)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      if (note->editing)
        note->items[JOTTING_MENU_EDIT].flags |= wuss_MENU_ITEM_TICKED;
      else
        note->items[JOTTING_MENU_EDIT].flags &=
          (wuss_menu_item_flags_t) ~wuss_MENU_ITEM_TICKED;
      return wuss_menu_open_at_pointer(note->delegate, &note->menu,
                                       &note->menu_handle);
    }
    if ((event->data.mouse.button & wuss_BUTTON_DOUBLE) && !note->editing)
    {
      rc = jotting_set_editing(note, true);
      if (rc != result_OK)
        return rc;
    }
    /* entering the source from the rendered text, this is near, not exact:
     * the source shows the Markdown markers too */
    if (note->editing)
      jotting_caret_at_point(note, event->data.mouse.point);
    return result_OK;

  case wuss_EVENT_KEY:
    return jotting_key(note, event);

  case wuss_EVENT_GAIN_FOCUS:
  case wuss_EVENT_LOSE_FOCUS:
    if (note->editing)
      wuss_window_invalidate_visible(note->window); /* recolour the caret */
    return result_OK;

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return wuss_colourmenu_open_rgb(note->wuss, event, "Note colour",
                                    wuss_get_palette(note->wuss, NULL)
                                      [note->colour]);

  case wuss_EVENT_MENU_SELECT:
    return jotting_menu_select(note, event);

  case wuss_EVENT_MENU_CLOSED:
    note->menu_handle = NULL;
    return result_OK;

  case wuss_EVENT_RELAYOUT:
    /* follow the system font: the old one may have been released */
    note->md.font = wuss_get_font(note->wuss);
    wuss_window_invalidate_visible(note->window);
    return result_OK;

  case wuss_EVENT_MESSAGE_BOUNCED:
    if (event->data.message->action == wuss_MESSAGE_PINBOARD_PIN)
      jotting_report(note, "No pinboard is running");
    return result_OK;

  case wuss_EVENT_QUIT:
    jotting_destroy(note);
    return result_OK;

  default:
    return result_OK;
  }
}

/* ----------------------------------------------------------------------- */

result_t jottings_app_create(wuss_t *wuss)
{
  result_t rc;
  void    *note;

  /* ponytail: one call nests Notes with '/', which RISC OS takes for an
   * extension; make the two levels separately if notes are wanted there */
  if (!path_user_data("Wuss/Notes", g_notes, sizeof(g_notes)))
    g_notes[0] = '\0';

  rc = taskapp_install(wuss, &jottings_desc);
  if (rc != result_OK)
    return rc;

#ifdef WUSS_ICONBAR
  /* without the icon bar, install has opened a note already */
  rc = taskapp_spawn(wuss, &jottings_desc, jottings_spawn,
                     (void *) jottings_welcome, &note);
#else
  NOT_USED(note);
#endif

  return rc;
}

#endif /* WUSS_APP */
