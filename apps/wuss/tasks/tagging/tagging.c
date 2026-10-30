/* wuss/tasks/tagging/tagging.c -- tag cloud demo task */

#ifdef WUSS_APP

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "geom/box.h"
#include "geom/stack.h"
#include "utils/array.h"
#include "utils/rng.h"
#include "wuss/icon-spec.h"
#include "wuss/task.h"

#include "../common/common.h"

#include "tagging.h"
#include "../common/taskapp.h"

/* ----------------------------------------------------------------------- */

#define TAGGING_INITIAL_TAGS 100
#define TAGGING_FIELD_W      200

/* name dialogue layout items, in creation order. See
 * docs/windowing/wuss-layout.md */
enum
{
  TAGGING_ST_ROOT,
  TAGGING_ST_FIELD_ROW,
  TAGGING_ST_FIELD,
  TAGGING_ST_BUTTONS,
  TAGGING_ST_SPACER,
  TAGGING_ST_CANCEL,
  TAGGING_ST_OK,
  TAGGING_ST__LIMIT
};

static const stack_item_t g_tagging_stack[TAGGING_ST__LIMIT] =
{
  [TAGGING_ST_ROOT]      = { .kind = stack_KIND_VBOX, .parent = -1,
                             .gap = wuss_STD_GAP, .pad = wuss_STD_INSETS },

  [TAGGING_ST_FIELD_ROW] = STACK_HBOX(TAGGING_ST_ROOT, wuss_STD_GLYPH_SIZE, wuss_STD_GAP, stack_ALIGN_START),
  [TAGGING_ST_FIELD]     = STACK_LEAF(TAGGING_ST_FIELD_ROW, TAGGING_FIELD_W, wuss_STD_GLYPH_SIZE, stack_ALIGN_CENTRE),

  [TAGGING_ST_BUTTONS]   = STACK_HBOX(TAGGING_ST_ROOT, wuss_STD_PRIMARY_BUTTON_HEIGHT, wuss_STD_GAP, stack_ALIGN_END),
  [TAGGING_ST_SPACER]    = STACK_SPACER(TAGGING_ST_BUTTONS, 1), /* push the buttons right */
  [TAGGING_ST_CANCEL]    = STACK_LEAF(TAGGING_ST_BUTTONS, 0, wuss_STD_SECONDARY_BUTTON_HEIGHT, stack_ALIGN_CENTRE),
  [TAGGING_ST_OK]        = STACK_LEAF(TAGGING_ST_BUTTONS, 0, wuss_STD_PRIMARY_BUTTON_HEIGHT, stack_ALIGN_CENTRE),
};

/* ----------------------------------------------------------------------- */

/* A pronounceable name of one or two capitalised words, each of two
 * consonant-vowel syllables, with letters weighted as in English. */
static void tagging_random_name(rng_t *rng, char *buf)
{
  static const char cns[] = "bbccccdddddddffffggghhhhhhhhhhklllllllmmmmnnnn"
                            "nnnnnnnnppprrrrrrrrrrsssssssssssttttttttttttttt"
                            "tvvwwwyyy";
  static const char vwl[] = "aaaeeeeeiiiooou";

  char *p;
  char *start;
  int   nwords, w, s, n;

  p      = buf;
  nwords = 1 + rng_range(rng, 2);
  for (w = 0; w < nwords; w++)
  {
    if (w > 0)
      *p++ = ' ';
    start = p;
    for (s = 0; s < 2; s++)
    {
      *p++ = cns[rng_range(rng, (int) sizeof(cns) - 1)];
      for (n = 1 + rng_range(rng, 2); n > 0; n--)
        *p++ = vwl[rng_range(rng, (int) sizeof(vwl) - 1)];
    }
    *start = (char) toupper((unsigned char) *start);
  }
  *p = '\0';
}

/* Hand the gadget the current tags. */
static result_t tagging_push(tagging_task_t *tt)
{
  wuss_tagcloud_tag_t tags[TAGGING_MAX_TAGS];
  int                 i;

  for (i = 0; i < tt->ntags; i++)
  {
    tags[i].name        = tt->tags[i].name;
    tags[i].count       = tt->tags[i].count;
    tags[i].highlighted = tt->tags[i].highlighted;
  }

  return wuss_tagcloud_set_tags(tt->tagcloud, tags, tt->ntags);
}

/* wuss_tagcloud_toggle_fn_t: opaque is the tagging_task_t */
static void tagging_toggled(wuss_tagcloud_t *tagcloud,
                            int              index,
                            int              highlighted,
                            void            *opaque)
{
  tagging_task_t *tt;

  NOT_USED(tagcloud);

  tt = opaque;
  tt->tags[index].highlighted = highlighted;
  tt->current                 = index;
}

static result_t tagging_delete(tagging_task_t *tt, int index)
{
  if (index < 0 || index >= tt->ntags)
    return result_OK;

  array_delete_element(tt->tags, sizeof(tt->tags[0]), tt->ntags, index);
  tt->ntags--;
  tt->current = -1;

  return tagging_push(tt);
}

/* ----------------------------------------------------------------------- */

static void tagging_name_dismiss(tagging_task_t *tt)
{
  wuss_menu_close(tt->name_handle);
  tt->name_handle = NULL;
}

static result_t tagging_name_cancel(void *opaque, wuss_button_t button)
{
  tagging_task_t *tt;

  tt = opaque;

  /* ADJUST leaves the dialogue open, as on RISC OS */
  if (!(button & wuss_BUTTON_ADJUST))
    tagging_name_dismiss(tt);

  return result_OK;
}

static result_t tagging_name_ok(void *opaque, wuss_button_t button)
{
  tagging_task_t *tt;
  const char     *text;
  tagging_tag_t  *tag;

  tt   = opaque;
  text = wuss_icon_get_text(tt->name_icons[TAGGING_NAME_FIELD]);

  if (!(button & wuss_BUTTON_ADJUST))
    tagging_name_dismiss(tt);

  if (text == NULL || text[0] == '\0')
    return result_OK;

  if (tt->renaming >= 0 && tt->renaming < tt->ntags)
  {
    tag = &tt->tags[tt->renaming];
  }
  else
  {
    if (tt->ntags >= TAGGING_MAX_TAGS)
      return result_OK;
    tag              = &tt->tags[tt->ntags++];
    tag->count       = 1;
    tag->highlighted = 0;
  }
  snprintf(tag->name, sizeof(tag->name), "%s", text);

  return tagging_push(tt);
}

/* the name dialogue's own task: not autoclose, so it survives its window
 * being dismissed and shown again. Its QUIT frees the dialogue, and the
 * task block too if the window's QUIT has already run */
static result_t tagging_name_handle(wuss_window_t      *window,
                                    const wuss_event_t *event,
                                    void               *task_data)
{
  tagging_task_t *tt;
  result_t        rc;

  tt = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_ICON:
    rc = result_OK;
    (void) wuss_dialogue_handle_icon(tt->name_dialogue, event, &rc);
    return rc;

  case wuss_EVENT_KEY:
    rc = result_WUSS_KEY_UNCLAIMED;
    (void) wuss_dialogue_handle_key(tt->name_dialogue, event, &rc);
    return rc;

  case wuss_EVENT_CLOSE:
    wuss_dialogue_handle_close(tt->name_dialogue, window);
    return result_OK;

  case wuss_EVENT_MENU_CLOSED:
    tt->name_handle = NULL; /* wuss closed the chain under us */
    return result_OK;

  case wuss_EVENT_QUIT:
    wuss_dialogue_destroy(tt->name_dialogue);
    tt->name_dialogue = NULL;
    tt->name_task     = NULL;
    if (tt->delegate == NULL)
      free(tt); /* the window's QUIT came first and left the block to us */
    return result_OK;

  default:
    return result_OK;
  }
}

static result_t tagging_name_create(tagging_task_t *tt)
{
  result_t               rc;
  wuss_task_desc_t       desc;
  stack_item_t           items[TAGGING_ST__LIMIT];
  size2d_t               size;
  box_t                  root;
  box_t                  boxes[TAGGING_ST__LIMIT];
  wuss_icon_spec_t       specs[TAGGING_NAME__LIMIT];
  wuss_dialogue_action_t actions[2];

  desc.handle    = tagging_name_handle;
  desc.task_data = tt;
  desc.name      = "tagging-name";
  rc = wuss_task_create(tt->wuss, &desc, &tt->name_task);
  if (rc != result_OK)
    return rc;

  memcpy(items, g_tagging_stack, sizeof(items));
  items[TAGGING_ST_CANCEL].axis_size =
    task_button_width(tt->wuss, "Cancel", wuss_STD_SECONDARY_BUTTON_BORDER);
  items[TAGGING_ST_OK].axis_size =
    task_button_width(tt->wuss, "OK", wuss_STD_PRIMARY_BUTTON_BORDER);

  rc = stack_smallest(items, NELEMS(items), &size);
  if (rc != result_OK)
    return rc;
  root = (box_t) BOX_POS_SIZE(0, 0, size.w, size.h);
  rc = stack_solve(items, NELEMS(items), &root, boxes);
  if (rc != result_OK)
    return rc;

  rc = wuss_dialogue_create(&tt->name_dialogue, tt->name_task, size,
                            "Tag name", NULL, tt);
  if (rc != result_OK)
    return rc;

  wuss_icon_spec_writable(&specs[TAGGING_NAME_FIELD],
                          boxes[TAGGING_ST_FIELD], "", TAGGING_NAME_LEN,
                          wuss_ICON_FLAGS_JUSTIFY_LEFT);
  wuss_icon_spec_action(&specs[TAGGING_NAME_CANCEL],
                        boxes[TAGGING_ST_CANCEL], "Cancel", 0);
  wuss_icon_spec_action(&specs[TAGGING_NAME_OK], boxes[TAGGING_ST_OK], "OK",
                        1);
  rc = wuss_icon_create_array(wuss_dialogue_window(tt->name_dialogue), specs,
                              TAGGING_NAME__LIMIT, tt->name_icons);
  if (rc != result_OK)
    return rc;

  actions[0].icon = tt->name_icons[TAGGING_NAME_CANCEL];
  actions[0].fn   = tagging_name_cancel;
  actions[1].icon = tt->name_icons[TAGGING_NAME_OK];
  actions[1].fn   = tagging_name_ok;
  wuss_dialogue_set_cancel(tt->name_dialogue,
                           tt->name_icons[TAGGING_NAME_CANCEL]);

  return wuss_dialogue_set_actions(tt->name_dialogue, actions, 2);
}

/* Show the name dialogue at the pointer to rename tag "renaming", or to add
 * a new tag if it is -1. */
static result_t tagging_name_open(tagging_task_t *tt, int renaming)
{
  result_t       rc;
  wuss_window_t *win;

  win          = wuss_dialogue_window(tt->name_dialogue);
  tt->renaming = renaming;

  rc = wuss_icon_set_text(win, tt->name_icons[TAGGING_NAME_FIELD],
                          renaming >= 0 ? tt->tags[renaming].name : "");
  if (rc != result_OK)
    return rc;

  rc = wuss_menu_open_window(tt->name_task, win, wuss_get_pointer(tt->wuss),
                             &tt->name_handle);
  if (rc != result_OK)
    return rc;

  return wuss_icon_set_caret(win, tt->name_icons[TAGGING_NAME_FIELD], -1);
}

/* ----------------------------------------------------------------------- */

static unsigned int tagging_display_ticks(const tagging_task_t *tt)
{
  const wuss_tagcloud_options_t *o;
  unsigned int                   ticks;

  o     = &tt->options;
  ticks = 1u << (o->display == wuss_TAGCLOUD_DISPLAY_LIST
                 ? TAGGING_DISPLAY_LIST : TAGGING_DISPLAY_CLOUD);
  ticks |= 1u << (o->scaling ? TAGGING_DISPLAY_SCALING_ON
                             : TAGGING_DISPLAY_SCALING_OFF);
  ticks |= 1u << (o->sort == wuss_TAGCLOUD_SORT_NAME
                  ? TAGGING_DISPLAY_SORT_NAME : TAGGING_DISPLAY_SORT_COUNT);
  if (o->selected_first)
    ticks |= 1u << TAGGING_DISPLAY_SELECTED_FIRST;

  return ticks;
}

/* Set the Display ticks and the Tag row from the task, ahead of opening the
 * menu or dispatching a shortcut through it. */
static void tagging_menu_refresh(tagging_task_t *tt)
{
  wuss_menu_tick_set(&tt->display_menu, tagging_display_ticks(tt));

  if (tt->current >= 0)
  {
    snprintf(tt->tag_text, sizeof(tt->tag_text), "Tag '%s'",
             tt->tags[tt->current].name);
    tt->menu_items[TAGGING_MENU_TAG].flags &= ~wuss_MENU_ITEM_DISABLED;
  }
  else
  {
    snprintf(tt->tag_text, sizeof(tt->tag_text), "Tag ''");
    tt->menu_items[TAGGING_MENU_TAG].flags |= wuss_MENU_ITEM_DISABLED;
  }
}

static result_t tagging_menu_select(tagging_task_t     *tt,
                                    const wuss_event_t *event)
{
  const wuss_menu_t *menu;
  int                index;

  menu  = event->data.menu_select.menu;
  index = event->data.menu_select.index;

  if (menu == &tt->menu && index == TAGGING_MENU_NEW)
    return tagging_name_open(tt, -1);

  if (menu == &tt->tag_menu && index == TAGGING_TAG_RENAME)
    return tagging_name_open(tt, tt->current);

  if (menu == &tt->tag_menu && index == TAGGING_TAG_DELETE)
    return tagging_delete(tt, tt->current);

  if (menu != &tt->display_menu)
    return result_OK;

  switch (index)
  {
  case TAGGING_DISPLAY_LIST:
    tt->options.display = wuss_TAGCLOUD_DISPLAY_LIST;
    break;
  case TAGGING_DISPLAY_CLOUD:
    tt->options.display = wuss_TAGCLOUD_DISPLAY_CLOUD;
    break;
  case TAGGING_DISPLAY_SCALING_OFF:
    tt->options.scaling = 0;
    break;
  case TAGGING_DISPLAY_SCALING_ON:
    tt->options.scaling = 1;
    break;
  case TAGGING_DISPLAY_SORT_NAME:
    tt->options.sort = wuss_TAGCLOUD_SORT_NAME;
    break;
  case TAGGING_DISPLAY_SORT_COUNT:
    tt->options.sort = wuss_TAGCLOUD_SORT_COUNT;
    break;
  case TAGGING_DISPLAY_SELECTED_FIRST:
    tt->options.selected_first = !tt->options.selected_first;
    break;
  default:
    return result_OK;
  }

  wuss_tagcloud_set_options(tt->tagcloud, &tt->options);
  tagging_menu_refresh(tt);
  wuss_menu_tick_set_live(tt->menu_handle, &tt->display_menu,
                          tagging_display_ticks(tt));

  return result_OK;
}

/* ----------------------------------------------------------------------- */

result_t tagging_create(wuss_t *wuss, tagging_task_t **out)
{
  result_t         rc;
  tagging_task_t  *task;
  wuss_task_desc_t delegate_desc;
  rng_t            rng;
  int              i;

  task = calloc(1, sizeof(*task));
  if (task == NULL)
    return result_OOM;

  task->wuss                   = wuss;
  task->current                = -1;
  task->renaming               = -1;
  task->options.display        = wuss_TAGCLOUD_DISPLAY_CLOUD;
  task->options.sort           = wuss_TAGCLOUD_SORT_NAME;
  task->options.selected_first = 0;
  task->options.scaling        = 1;

  rng_seed(&rng, (uint32_t) rand());
  task->ntags = TAGGING_INITIAL_TAGS;
  for (i = 0; i < task->ntags; i++)
  {
    tagging_random_name(&rng, task->tags[i].name);
    task->tags[i].count = 1 + rng_range(&rng, task->ntags);
  }

  WUSS_MENU_ITEM_SHORTCUT(task->display_items, TAGGING_DISPLAY_LIST,
                          "List", wuss_MENU_ITEM_NONE, "F2");
  WUSS_MENU_ITEM_SHORTCUT(task->display_items, TAGGING_DISPLAY_CLOUD,
                          "Cloud", wuss_MENU_ITEM_NONE, "F3");
  WUSS_MENU_ITEM_SHORTCUT(task->display_items, TAGGING_DISPLAY_SCALING_OFF,
                          "Scaling off", wuss_MENU_ITEM_DASHED, "F4");
  WUSS_MENU_ITEM_SHORTCUT(task->display_items, TAGGING_DISPLAY_SCALING_ON,
                          "Scaling on", wuss_MENU_ITEM_NONE, "F5");
  WUSS_MENU_ITEM_SHORTCUT(task->display_items, TAGGING_DISPLAY_SORT_NAME,
                          "Sort by name", wuss_MENU_ITEM_DASHED, "F6");
  WUSS_MENU_ITEM_SHORTCUT(task->display_items, TAGGING_DISPLAY_SORT_COUNT,
                          "Sort by count", wuss_MENU_ITEM_NONE, "F7");
  WUSS_MENU_ITEM_SHORTCUT(task->display_items,
                          TAGGING_DISPLAY_SELECTED_FIRST, "Selected first",
                          wuss_MENU_ITEM_DASHED, "^F3");
  WUSS_MENU_TITLE(task->display_menu, "Display", task->display_items,
                  TAGGING_DISPLAY__LIMIT);

  WUSS_MENU_ITEM_SHORTCUT(task->tag_items, TAGGING_TAG_RENAME, "Rename...",
                          wuss_MENU_ITEM_NONE, "^R");
  WUSS_MENU_ITEM_SHORTCUT(task->tag_items, TAGGING_TAG_DELETE, "Delete",
                          wuss_MENU_ITEM_NONE, "^K");
  WUSS_MENU_TITLE(task->tag_menu, "Tag", task->tag_items, TAGGING_TAG__LIMIT);

  WUSS_MENU_ITEM_MENU(task->menu_items, TAGGING_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_NONE, &task->display_menu);
  WUSS_MENU_ITEM_MENU(task->menu_items, TAGGING_MENU_TAG, task->tag_text,
                      wuss_MENU_ITEM_NONE, &task->tag_menu);
  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, TAGGING_MENU_NEW, "New tag...",
                          wuss_MENU_ITEM_DASHED, "^N");
  WUSS_MENU_TITLE(task->menu, "Tagging", task->menu_items,
                  TAGGING_MENU__LIMIT);
  tagging_menu_refresh(task);

  delegate_desc.handle    = tagging_handle;
  delegate_desc.task_data = task;
  delegate_desc.name      = "tagging";
  rc = wuss_task_create(wuss, &delegate_desc, &task->delegate);
  if (rc != result_OK)
  {
    free(task); /* nothing registered yet; nobody else owns it */
    return rc;
  }

  /* closing the window reaps the task; its QUIT frees the block */
  wuss_task_set_autoclose(task->delegate, 1);

  rc = tagging_name_create(task);
  if (rc == result_OK)
    rc = wuss_window_create_at(task->delegate, &task_place,
                               SIZE2D(480, 320), "Tagging",
                               (wuss_WINDOW_DEFAULT & ~wuss_WINDOW_HSCROLL) |
                               wuss_WINDOW_FOCUSABLE,
                               wuss_NO_BACKDROP, /* the gadget paints it all */
                               SIZE2D(480, 320), SIZE2D(0, 0),
                               &task->window);
  if (rc == result_OK)
    rc = wuss_tagcloud_create(&task->tagcloud, task->window, tagging_toggled,
                              task);
  if (rc == result_OK)
    rc = tagging_push(task);
  if (rc != result_OK)
  {
    wuss_task_destroy(task->delegate); /* QUIT frees the block */
    return rc;
  }

  if (out)
    *out = task;

  return result_OK;
}

result_t tagging_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    "Tagging",
    "Tag cloud",
    (taskapp_create_fn_t) tagging_create
  };

  return taskapp_install(wuss, &desc);
}

void tagging_destroy(tagging_task_t *task)
{
  wuss_tagcloud_destroy(task->tagcloud);
  task->tagcloud = NULL;
  task->delegate = NULL;

  /* whichever QUIT comes last frees the block. During wuss_destroy the name
   * task is already being reaped, so its QUIT may come before or after */
  if (task->name_task != NULL)
    wuss_task_destroy(task->name_task); /* no-op while reaping */
  else
    free(task);
}

/* ----------------------------------------------------------------------- */

result_t tagging_handle(wuss_window_t      *window,
                        const wuss_event_t *event,
                        void               *task_data)
{
  tagging_task_t *tt;
  int             index;

  tt = task_data;

  if (window != NULL && window == tt->window && tt->tagcloud != NULL &&
      wuss_tagcloud_handle_event(tt->tagcloud, event))
    return result_OK;

  switch (event->kind)
  {
  case wuss_EVENT_MOUSE:
    if (window != tt->window || event->data.mouse.action != wuss_MOUSE_DOWN ||
        !(event->data.mouse.button & wuss_BUTTON_MENU))
      return result_OK;

    /* the Tag row acts on the tag under the pointer, shaded if none */
    index = wuss_tagcloud_hit_test(tt->tagcloud, event->data.mouse.point);
    tt->current = index;
    tagging_menu_refresh(tt);
    return wuss_menu_open_at_pointer(tt->delegate, &tt->menu,
                                     &tt->menu_handle);

  case wuss_EVENT_MENU_SELECT:
    return tagging_menu_select(tt, event);

  case wuss_EVENT_MENU_CLOSED:
    tt->menu_handle = NULL; /* wuss closed the chain under us */
    return result_OK;

  case wuss_EVENT_CLOSE:
    if (window == tt->window)
      tt->window = NULL;
    return result_OK;

  case wuss_EVENT_QUIT:
    tagging_destroy(tt);
    return result_OK;

  case wuss_EVENT_KEY:
    tagging_menu_refresh(tt);
    return wuss_menu_dispatch_shortcut(tt->delegate, &tt->menu, event);

  default:
    return result_OK;
  }
}

#endif /* WUSS_APP */
