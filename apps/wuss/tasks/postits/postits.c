/* wuss/tasks/postits/postits.c -- Post-it notes */

#ifdef WUSS_APP

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

#include "postits.h"
#include "../common/taskapp.h"

#define POSTIT_INSET     4
#define POSTIT_TITLE_MAX 40

/* a note's file is <Notes><sep><title><ext>: RISC OS has '.' for a
 * separator and no extensions */
#ifdef __riscos
#define POSTIT_SEP "."
#define POSTIT_EXT ""
#else
#define POSTIT_SEP "/"
#define POSTIT_EXT ".md"
#endif

enum
{
  POSTIT_MENU_NEW,
  POSTIT_MENU_COLOUR,
  POSTIT_MENU_EDIT,
  POSTIT_MENU_PIN,
  POSTIT_MENU_DELETE,
  POSTIT_MENU__LIMIT
};

typedef struct postit
{
  wuss_t            *wuss;
  wuss_task_t       *delegate;
  wuss_window_t     *window;
  mdtext_t           md;
  char              *src;       /* owned Markdown source, terminated */
  int                len;
  int                allocated;
  bool               editing;   /* showing the raw source, taking keys */
  bool               dirty;     /* edited since it was last saved */
  wuss_colour_t      colour;
  wuss_colour_t      fg;
  char               path[DPTLIB_MAXPATH]; /* "" until pinned or loaded */
  wuss_menu_item_t   items[POSTIT_MENU__LIMIT];
  wuss_menu_t        menu;
  wuss_menu_handle_t menu_handle;
}
postit_t;

static const char postits_welcome[] =
  "# Welcome\n"
  "\n"
  "This is a **Post-it** note. MENU on it to make a new note, change its "
  "colour or edit it.\n"
  "\n"
  "*Pin to backdrop* saves the note in your Notes directory and pins it to "
  "the backdrop. Double-click the pin to open the note again.";

static char      g_notes[DPTLIB_MAXPATH]; /* notes directory; "" if none */
static postit_t *g_doomed; /* the note whose Delete awaits confirmation */

static result_t postit_handle(wuss_window_t      *window,
                              const wuss_event_t *event,
                              void               *task_data);

/* ----------------------------------------------------------------------- */

static void postit_report(postit_t *note, const char *message)
{
  char glyph[TASKAPP_GLYPH_NAME_MAX];

  taskapp_glyph_name("Post-Its", glyph);
  (void) wuss_error_report(note->wuss, glyph, "Post-Its", message,
                           wuss_ERROR_BUTTONS_OK, NULL, NULL);
}

/* room for extra more bytes, plus the caret and the terminator */
static result_t postit_reserve(postit_t *note, int extra)
{
  if (array_grow((void **) &note->src, 1, note->len, &note->allocated,
                 extra + 2, 64))
    return result_OOM;

  return result_OK;
}

static result_t postit_set_source(postit_t *note, const char *src, int len)
{
  result_t rc;

  note->len = 0;
  rc        = postit_reserve(note, len);
  if (rc != result_OK)
    return rc;

  memcpy(note->src, src, (size_t) len);
  note->len           = len;
  note->src[note->len] = '\0';

  return result_OK;
}

/* the first line, without its leading '#'s and spaces */
static void postit_title(const postit_t *note, char *title, size_t cap)
{
  const char *s;
  int         n;

  s = note->src;
  while (*s == '#' || *s == ' ')
    s++;
  n = MIN((int) strcspn(s, "\n"), POSTIT_TITLE_MAX);

  if (n == 0)
    snprintf(title, cap, "Note");
  else
    snprintf(title, cap, "%.*s", n, s);
}

/* re-set the shown text and title after a change: editing shows the raw
 * source with a caret at its end, viewing renders the Markdown */
static result_t postit_refresh(postit_t *note)
{
  result_t rc;
  char     title[POSTIT_TITLE_MAX + 1];

  note->src[note->len] = '_'; /* postit_reserve keeps room for it */
  rc = mdtext_set(&note->md, note->src, note->len + (note->editing ? 1 : 0),
                  !note->editing);
  note->src[note->len] = '\0';
  if (rc != result_OK)
    return rc;

  postit_title(note, title, sizeof(title));
  wuss_window_set_title(note->window, title);
  wuss_window_invalidate_visible(note->window);

  return result_OK;
}

/* ----------------------------------------------------------------------- */

static result_t postit_save(postit_t *note)
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
static result_t postit_choose_path(postit_t *note)
{
  char title[POSTIT_TITLE_MAX + 1];
  char name[POSTIT_TITLE_MAX + 1];
  int  i, j;
  struct stat st;

  if (note->path[0] != '\0')
    return result_OK;
  if (g_notes[0] == '\0')
    return result_NOT_FOUND;

  /* a portable leaf: letters, digits and '-', with '_' for spaces */
  postit_title(note, title, sizeof(title));
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
      snprintf(note->path, sizeof(note->path), "%s" POSTIT_SEP "%s"
               POSTIT_EXT, g_notes, name);
    else
      snprintf(note->path, sizeof(note->path), "%s" POSTIT_SEP "%s-%d"
               POSTIT_EXT, g_notes, name, i);
    if (stat(note->path, &st) != 0)
      return result_OK;
  }

  note->path[0] = '\0';
  return result_NOT_FOUND;
}

/* save the note to its file and ask the Pinboard to pin it; a bounce says
 * there is no Pinboard */
static result_t postit_pin(postit_t *note)
{
  char message[DPTLIB_MAXPATH + 32];

  if (postit_choose_path(note) != result_OK)
  {
    postit_report(note, "There is no Notes directory");
    return result_OK;
  }

  if (postit_save(note) != result_OK)
  {
    snprintf(message, sizeof(message), "Cannot save %s",
             path_leaf(note->path));
    postit_report(note, message);
    return result_OK;
  }

  return wuss_send_recorded(note->wuss, note->delegate, NULL,
                            wuss_MESSAGE_PINBOARD_PIN, note->path,
                            strlen(note->path) + 1, 0, NULL);
}

static void postit_delete_chosen(wuss_error_choice_t choice, void *opaque)
{
  postit_t *note;

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
static void postit_delete(postit_t *note)
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
  (void) wuss_error_report(note->wuss, NULL, "Post-Its", message,
                           wuss_ERROR_BUTTONS_OK_CANCEL, postit_delete_chosen,
                           NULL);
}

static result_t postit_set_editing(postit_t *note, bool editing)
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
           postit_save(note) != result_OK)
  {
    snprintf(message, sizeof(message), "Cannot save %s",
             path_leaf(note->path));
    postit_report(note, message);
  }

  return postit_refresh(note);
}

/* ----------------------------------------------------------------------- */

/* ponytail: typing goes at the end only; add a movable caret if notes grow
 * past a few lines */
static result_t postit_key(postit_t *note, const wuss_event_t *event)
{
  result_t rc;
  int      code;
  char     utf8[4];
  int      n;

  code = event->data.key.code;
  if (!note->editing ||
      (event->data.key.modifiers & (wuss_KEY_MOD_CTRL | wuss_KEY_MOD_ALT)))
    return result_WUSS_KEY_UNCLAIMED;

  switch (code)
  {
  case wuss_KEY_ESCAPE:
    return postit_set_editing(note, false);

  case wuss_KEY_BACKSPACE:
    if (note->len == 0)
      return result_OK;
    note->len            = utf8_prev(note->src, note->len);
    note->src[note->len] = '\0';
    note->dirty          = true;
    return postit_refresh(note);

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

  rc = postit_reserve(note, n);
  if (rc != result_OK)
    return rc;

  memcpy(note->src + note->len, utf8, (size_t) n);
  note->len            += n;
  note->src[note->len]  = '\0';
  note->dirty           = true;

  return postit_refresh(note);
}

static void postit_redraw(postit_t *note, const wuss_event_t *event)
{
  const box_t    *bounds;
  const colour_t *palette;
  point_t         origin;

  bounds = event->data.redraw.bounds;
  if (mdtext_layout(&note->md, bounds->x1 - bounds->x0 - 2 * POSTIT_INSET) !=
      result_OK)
    return;

  palette  = wuss_get_palette(note->wuss, NULL);
  origin.x = bounds->x0 - event->data.redraw.scroll.x + POSTIT_INSET;
  origin.y = bounds->y0 - event->data.redraw.scroll.y + POSTIT_INSET;
  mdtext_draw(&note->md, event->data.redraw.scr, origin, palette[note->fg],
              colour_rgba(0, 0, 0, 0));
}

/* ----------------------------------------------------------------------- */

static result_t postit_create(wuss_t *wuss, postit_t **out)
{
  result_t         rc;
  postit_t        *note;
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

  rc = postit_set_source(note, "", 0);
  if (rc != result_OK)
  {
    free(note);
    return rc;
  }

  WUSS_MENU_ITEM(note->items, POSTIT_MENU_NEW, "New note",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM_MENU(note->items, POSTIT_MENU_COLOUR, "Colour",
                      wuss_MENU_ITEM_PRE_OPEN |
                      wuss_MENU_ITEM_BORROWED_SUBMENU,
                      wuss_colourmenu_menu(wuss));
  WUSS_MENU_ITEM(note->items, POSTIT_MENU_EDIT, "Edit", wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(note->items, POSTIT_MENU_PIN, "Pin to backdrop",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM(note->items, POSTIT_MENU_DELETE, "Delete",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(note->menu, "Note", note->items, NELEMS(note->items));

  desc.handle    = postit_handle;
  desc.task_data = note;
  desc.name      = "postit";
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

  return postit_refresh(note);
}

/* open the note file at path; edits are saved back to it */
static result_t postit_load(postit_t *note, const char *path)
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
    postit_report(note, message);
    return result_FOPEN_FAILED;
  }

  note->len = 0;
  do
  {
    rc = postit_reserve(note, 256);
    if (rc != result_OK)
      break;
    n          = fread(note->src + note->len, 1, 256, f);
    note->len += (int) n;
  }
  while (n == 256);
  fclose(f);
  note->src[note->len] = '\0';
  if (rc != result_OK)
    return rc;

  snprintf(note->path, sizeof(note->path), "%s", path);
  note->editing = false;
  note->dirty   = false;

  return postit_refresh(note);
}

static void postit_destroy(postit_t *note)
{
  if (g_doomed == note)
    g_doomed = NULL;

  /* don't lose an edit to a note that has a file */
  if (note->dirty && note->path[0] != '\0')
    (void) postit_save(note);

  mdtext_free(&note->md);
  free(note->src);
  free(note);
}

/* a DataOpen claims only the notes in the notes directory, so other ".md"
 * files go to Reading */
static bool postits_claims(const char *path)
{
  size_t n;

  n = strlen(g_notes);

  return n > 0 && strncmp(path, g_notes, n) == 0 && path[n] == POSTIT_SEP[0];
}

static const unsigned short postits_opens[] =
{
  filetype_RISCOS_MARKDOWN, 0
};

static const taskapp_desc_t postits_desc =
{
  .name      = "Post-Its",
  .purpose   = "Sticky notes for the desktop",
  .create    = (taskapp_create_fn_t) postit_create,
  .data_load = (taskapp_data_load_fn_t) postit_load,
  .opens     = postits_opens,
  .claims    = postits_claims
};

/* make a note showing text, or a blank one being edited for NULL */
static result_t postits_spawn(wuss_t *wuss, void *param, void **out)
{
  result_t  rc;
  postit_t *note;

  rc = postit_create(wuss, &note);
  if (rc != result_OK)
    return rc;

  *out = note;
  if (param == NULL)
    return result_OK;

  note->editing = false;
  rc = postit_set_source(note, param, (int) strlen(param));
  if (rc != result_OK)
    return rc;

  return postit_refresh(note);
}

/* ----------------------------------------------------------------------- */

static result_t postit_menu_select(postit_t *note, const wuss_event_t *event)
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
  case POSTIT_MENU_NEW:
    return taskapp_spawn(note->wuss, &postits_desc, postits_spawn, NULL,
                         &instance);

  case POSTIT_MENU_EDIT:
    return postit_set_editing(note, !note->editing);

  case POSTIT_MENU_PIN:
    return postit_pin(note);

  case POSTIT_MENU_DELETE:
    postit_delete(note);
    return result_OK;

  default:
    return result_OK;
  }
}

static result_t postit_handle(wuss_window_t      *window,
                              const wuss_event_t *event,
                              void               *task_data)
{
  postit_t *note;

  NOT_USED(window);

  note = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    postit_redraw(note, event);
    return result_OK;

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action == wuss_MOUSE_MOVE ||
        event->data.mouse.action == wuss_MOUSE_UP)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      if (note->editing)
        note->items[POSTIT_MENU_EDIT].flags |= wuss_MENU_ITEM_TICKED;
      else
        note->items[POSTIT_MENU_EDIT].flags &=
          (wuss_menu_item_flags_t) ~wuss_MENU_ITEM_TICKED;
      return wuss_menu_open_at_pointer(note->delegate, &note->menu,
                                       &note->menu_handle);
    }
    if ((event->data.mouse.button & wuss_BUTTON_DOUBLE) && !note->editing)
      return postit_set_editing(note, true);
    return result_OK;

  case wuss_EVENT_KEY:
    return postit_key(note, event);

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    return wuss_colourmenu_open_rgb(note->wuss, event, "Note colour",
                                    wuss_get_palette(note->wuss, NULL)
                                      [note->colour]);

  case wuss_EVENT_MENU_SELECT:
    return postit_menu_select(note, event);

  case wuss_EVENT_MENU_CLOSED:
    note->menu_handle = NULL;
    return result_OK;

  case wuss_EVENT_MESSAGE_BOUNCED:
    if (event->data.message->action == wuss_MESSAGE_PINBOARD_PIN)
      postit_report(note, "No pinboard is running");
    return result_OK;

  case wuss_EVENT_QUIT:
    postit_destroy(note);
    return result_OK;

  default:
    return result_OK;
  }
}

/* ----------------------------------------------------------------------- */

result_t postits_app_create(wuss_t *wuss)
{
  result_t rc;
  void    *note;

  /* ponytail: one call nests Notes with '/', which RISC OS takes for an
   * extension; make the two levels separately if notes are wanted there */
  if (!path_user_data("Wuss/Notes", g_notes, sizeof(g_notes)))
    g_notes[0] = '\0';

  rc = taskapp_install(wuss, &postits_desc);
  if (rc != result_OK)
    return rc;

#ifdef WUSS_ICONBAR
  /* without the icon bar, install has opened a note already */
  rc = taskapp_spawn(wuss, &postits_desc, postits_spawn,
                     (void *) postits_welcome, &note);
#else
  NOT_USED(note);
#endif

  return rc;
}

#endif /* WUSS_APP */
