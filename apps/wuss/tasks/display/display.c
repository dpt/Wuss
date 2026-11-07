/* wuss/tasks/display/display.c -- desktop display mode picker task */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "geom/box.h"
#include "geom/size.h"
#include "geom/stack.h"
#include "wuss/icon-spec.h"
#include "wuss/icon.h"

#include "../tasks.h" /* app_set_mode, app_get_depth */

#include "display.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define DISPLAY_G_LABEL      1 /* size group, see stack_item_t::group */

enum
{
  DISPLAY_ST_ROOT,
  DISPLAY_ST_COLOURS_ROW,
  DISPLAY_ST_COLOURS_LABEL,
  DISPLAY_ST_COLOURS,
  DISPLAY_ST_RESOLUTION_ROW,
  DISPLAY_ST_RESOLUTION_LABEL,
  DISPLAY_ST_RESOLUTION,
  DISPLAY_ST_ZOOM_ROW,
  DISPLAY_ST_ZOOM_LABEL,
  DISPLAY_ST_ZOOM,
  DISPLAY_ST_CRT_ROW,
  DISPLAY_ST_CRT_LABEL,
  DISPLAY_ST_CRT,
  DISPLAY_ST_POINTER_ROW,
  DISPLAY_ST_POINTER_LABEL,
  DISPLAY_ST_POINTER,
  DISPLAY_ST_BUTTONS,
  DISPLAY_ST_BUTTONS_LABEL,
  DISPLAY_ST_CANCEL,
  DISPLAY_ST_CHANGE,
  DISPLAY_ST__LIMIT
};

/* sizes are zero here: display_layout sets them from the font */
static const stack_item_t g_display_stack[DISPLAY_ST__LIMIT] =
{
  [DISPLAY_ST_ROOT] = { .kind = stack_KIND_VBOX, .parent = -1,
                        .gap = wuss_STD_GAP, .pad = wuss_STD_INSETS },

  [DISPLAY_ST_COLOURS_ROW]      = STACK_HBOX(DISPLAY_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_START),
  [DISPLAY_ST_COLOURS_LABEL]    = STACK_LEAF_GROUP(DISPLAY_ST_COLOURS_ROW, 0, 0, stack_ALIGN_CENTRE, DISPLAY_G_LABEL),
  [DISPLAY_ST_COLOURS]          = STACK_LEAF_EX(DISPLAY_ST_COLOURS_ROW, 0, 0, stack_ALIGN_CENTRE, 1, 0, 0),

  [DISPLAY_ST_RESOLUTION_ROW]   = STACK_HBOX(DISPLAY_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_START),
  [DISPLAY_ST_RESOLUTION_LABEL] = STACK_LEAF_GROUP(DISPLAY_ST_RESOLUTION_ROW, 0, 0, stack_ALIGN_CENTRE, DISPLAY_G_LABEL),
  [DISPLAY_ST_RESOLUTION]       = STACK_LEAF_EX(DISPLAY_ST_RESOLUTION_ROW, 0, 0, stack_ALIGN_CENTRE, 1, 0, 0),

  [DISPLAY_ST_ZOOM_ROW]         = STACK_HBOX(DISPLAY_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_START),
  [DISPLAY_ST_ZOOM_LABEL]       = STACK_LEAF_GROUP(DISPLAY_ST_ZOOM_ROW, 0, 0, stack_ALIGN_CENTRE, DISPLAY_G_LABEL),
  [DISPLAY_ST_ZOOM]             = STACK_LEAF_EX(DISPLAY_ST_ZOOM_ROW, 0, 0, stack_ALIGN_CENTRE, 1, 0, 0),

  [DISPLAY_ST_CRT_ROW]          = STACK_HBOX(DISPLAY_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_START),
  [DISPLAY_ST_CRT_LABEL]        = STACK_SPACER_GROUP(DISPLAY_ST_CRT_ROW, 0, DISPLAY_G_LABEL), /* empty first column */
  [DISPLAY_ST_CRT]              = STACK_LEAF_EX(DISPLAY_ST_CRT_ROW, 0, 0, stack_ALIGN_CENTRE, 1, 0, 0),

  [DISPLAY_ST_POINTER_ROW]      = STACK_HBOX(DISPLAY_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_START),
  [DISPLAY_ST_POINTER_LABEL]    = STACK_SPACER_GROUP(DISPLAY_ST_POINTER_ROW, 0, DISPLAY_G_LABEL),
  [DISPLAY_ST_POINTER]          = STACK_LEAF_EX(DISPLAY_ST_POINTER_ROW, 0, 0, stack_ALIGN_CENTRE, 1, 0, 0),

  [DISPLAY_ST_BUTTONS]          = STACK_HBOX(DISPLAY_ST_ROOT, 0, wuss_STD_GAP, stack_ALIGN_START),
  [DISPLAY_ST_BUTTONS_LABEL]    = STACK_SPACER_GROUP(DISPLAY_ST_BUTTONS, 0, DISPLAY_G_LABEL),
  [DISPLAY_ST_CANCEL]           = STACK_LEAF_EX(DISPLAY_ST_BUTTONS, 0, 0, stack_ALIGN_CENTRE, 1, 0, 0),
  [DISPLAY_ST_CHANGE]           = STACK_LEAF_EX(DISPLAY_ST_BUTTONS, 0, 0, stack_ALIGN_CENTRE, 1, 0, 0),
};

enum
{
  DISPLAY_ICON_COLOURS_LABEL,
  DISPLAY_ICON_RESOLUTION_LABEL,
  DISPLAY_ICON_ZOOM_LABEL,
  DISPLAY_ICON_CRT,
  DISPLAY_ICON_POINTER,
  DISPLAY_ICON_CANCEL,
  DISPLAY_ICON_CHANGE,
  DISPLAY_NICONS
};

/* fixed set of depths the Colours picker offers, in bits per pixel;
 * app_set_mode rejects (via wuss_frontend_resize) anything a backend can't
 * honour, e.g. a screen mode the RISC OS machine lacks */
static const int g_display_depths[] =
{
  1, 2, 4, 8, app_DEPTH_GREY256, 15, 16, 32
};

/* wuss_stringset_create borrows its strings, so these are static, paired
 * 1:1 with g_display_depths */
static const char *const g_display_depth_labels[NELEMS(g_display_depths)] =
{
  "2 colours",
  "4 colours",
  "16 colours",
  "256 colours",
  "256 greys",
  "32K colours",
  "64K colours",
  "16M colours"
};

/* fixed set of resolutions the Resolution picker offers; rejected likewise */
static const size2d_t g_display_resolutions[] =
{
  { 480,  360  },
  { 640,  480  },
  { 800,  600  },
  { 1024, 768  },
  { 1152, 864  },
  { 1280, 720  },
  { 1280, 800  },
  { 1366, 768  },
  { 1920, 1080 }
};

/* label of the entry past the list, which keeps the size in force: that
 * size when it is unlisted (full screen follows the display), else
 * "Current"; rewritten by display_resolution_index */
static char g_display_current_label[24] = "Current";

/* borrowed likewise, paired 1:1 with g_display_resolutions, plus the
 * current-size entry */
static const char *const g_display_resolution_labels[NELEMS(g_display_resolutions) + 1] =
{
  "480 x 360",
  "640 x 480",
  "800 x 600",
  "1024 x 768",
  "1152 x 864",
  "1280 x 720",
  "1280 x 800",
  "1366 x 768",
  "1920 x 1080",
  g_display_current_label
};

/* fixed set of window zooms the Zoom picker offers, 1x..8x (device pixels,
 * so 2x on a Retina display looks like 1x elsewhere): the entry index
 * plus one is the zoom */
static const char *const g_display_zoom_labels[] =
{
  "1x",
  "2x",
  "3x",
  "4x",
  "5x",
  "6x",
  "7x",
  "8x"
};

/* where the window opens: bottom right of the safe area, above the icon
 * bar */
static const wuss_place_t g_display_place =
{
  0, wuss_ALIGN_RIGHT | wuss_ALIGN_BOTTOM, NULL, { 0, 0 },
  { 0, 0, 0, 0 }
};

/* the one open instance, or NULL: display_create fronts it rather than
 * opening another */
static display_task_t *g_display;

/* ----------------------------------------------------------------------- */

/* the entry matching the current depth. ponytail: an unlisted mode shows
 * entry 0 */
static int display_depth_index(void)
{
  int depth;
  int i;

  depth = app_get_depth();
  for (i = 0; i < (int) NELEMS(g_display_depths); i++)
    if (g_display_depths[i] == depth)
      return i;

  return 0;
}

/* the entry matching the current screen size, or the current-size entry
 * (relabelled with it) when unlisted */
static int display_resolution_index(const wuss_t *wuss)
{
  size2d_t size;
  int      i;

  size = wuss_get_screen_size(wuss);
  for (i = 0; i < (int) NELEMS(g_display_resolutions); i++)
    if (g_display_resolutions[i].w == size.w &&
        g_display_resolutions[i].h == size.h)
      break;

  if (i < (int) NELEMS(g_display_resolutions))
    strcpy(g_display_current_label, "Current");
  else
    snprintf(g_display_current_label, sizeof(g_display_current_label),
             "%d x %d", size.w, size.h);

  return i;
}

/* show the settings actually in force, e.g. after app_set_mode refused a
 * pick, or to revert the fields */
static void display_sync(display_task_t *dc)
{
  wuss_icon_set_selected(dc->base.window, dc->crt, g_tasks.crt);
  wuss_icon_set_selected(dc->base.window, dc->pointer, g_tasks.pointer);
  (void) wuss_stringset_set_index(dc->zoom,
                                  wuss_frontend_get_zoom(g_tasks.frontend) - 1);
  (void) wuss_stringset_set_index(dc->colours, display_depth_index());
  (void) wuss_stringset_set_index(dc->resolution,
                                  display_resolution_index(dc->base.wuss));
}

/* apply every pick; on refusal show the settings still in force */
static result_t display_change(display_task_t *dc)
{
  result_t rc;
  int      zoom;
  int      res;
  int      depth;
  size2d_t size;

  zoom = wuss_stringset_get_index(dc->zoom) + 1;
  wuss_frontend_zoom(g_tasks.frontend,
                     zoom - wuss_frontend_get_zoom(g_tasks.frontend));

  if (!!wuss_icon_get_selected(dc->crt) != g_tasks.crt)
  {
    tasks_set_crt(!g_tasks.crt);
    /* the new surface starts empty: present the whole frame */
    g_tasks.debug_redraw_all = true;
  }

  if (!!wuss_icon_get_selected(dc->pointer) != g_tasks.pointer)
    tasks_set_pointer(!g_tasks.pointer);

  res   = wuss_stringset_get_index(dc->resolution);
  depth = wuss_stringset_get_index(dc->colours);
  size  = (res < (int) NELEMS(g_display_resolutions))
        ? g_display_resolutions[res] : wuss_get_screen_size(dc->base.wuss);
  rc    = app_set_mode(size, g_display_depths[depth]);
  if (rc != result_OK)
    display_sync(dc);

  return rc;
}

/* ----------------------------------------------------------------------- */

/* widest of n strings, in pixels */
static int display_widest(const wuss_t      *wuss,
                          const char *const *strings,
                          int                n)
{
  int w;
  int i;

  w = 0;
  for (i = 0; i < n; i++)
    w = MAX(w, task_text_width(wuss, strings[i]));

  return w;
}

/* solve the window's layout for the current font */
static result_t display_layout(const wuss_t *wuss,
                               size2d_t     *size,
                               box_t         boxes[])
{
  result_t     rc;
  stack_item_t items[DISPLAY_ST__LIMIT];
  int          row_h;
  int          button_h;
  int          set_w;
  int          i;
  box_t        root;

  memcpy(items, g_display_stack, sizeof(items));

  /* rows fit a field or an option glyph, whichever is taller */
  row_h    = MAX(wuss_std_height(wuss, wuss_STD_FIELD), wuss_STD_GLYPH_SIZE);
  button_h = wuss_std_height(wuss, wuss_STD_SECONDARY_BUTTON);
  for (i = DISPLAY_ST_COLOURS_ROW; i < DISPLAY_ST__LIMIT; i++)
  {
    if (items[i].kind == stack_KIND_HBOX)
      items[i].axis_size = (i == DISPLAY_ST_BUTTONS) ? button_h : row_h;
    else if (items[i].kind == stack_KIND_LEAF)
      items[i].cross_size = (i >= DISPLAY_ST_CANCEL) ? button_h : row_h;
  }

  /* a set fits its widest entry beside a square arrow */
  set_w = MAX(display_widest(wuss, g_display_depth_labels,
                             NELEMS(g_display_depth_labels)),
              display_widest(wuss, g_display_resolution_labels,
                             NELEMS(g_display_resolution_labels)));
  set_w = MAX(set_w, display_widest(wuss, g_display_zoom_labels,
                                    NELEMS(g_display_zoom_labels)));
  set_w = task_button_width(wuss, "", 0) + set_w + wuss_STD_GAP + row_h;
  items[DISPLAY_ST_COLOURS].min    = set_w;
  items[DISPLAY_ST_RESOLUTION].min = set_w;
  items[DISPLAY_ST_ZOOM].min       = set_w;

  /* each label's natural width; the size group widens the other to match */
  items[DISPLAY_ST_COLOURS_LABEL].axis_size =
    task_text_width(wuss, "Colours");
  items[DISPLAY_ST_RESOLUTION_LABEL].axis_size =
    task_text_width(wuss, "Resolution");
  items[DISPLAY_ST_ZOOM_LABEL].axis_size =
    task_text_width(wuss, "Zoom");
  items[DISPLAY_ST_CRT].min = wuss_STD_GLYPH_SIZE + wuss_STD_GAP +
                              task_text_width(wuss, "CRT effect");
  items[DISPLAY_ST_POINTER].min = wuss_STD_GLYPH_SIZE + wuss_STD_GAP +
    task_text_width(wuss, "Software pointer");
  items[DISPLAY_ST_CANCEL].min =
    task_button_width(wuss, "Cancel", wuss_STD_SECONDARY_BUTTON_BORDER);
  items[DISPLAY_ST_CHANGE].min =
    task_button_width(wuss, "Change", wuss_STD_SECONDARY_BUTTON_BORDER);

  rc = stack_smallest(items, NELEMS(items), size);
  if (rc != result_OK)
    return rc;

  root = (box_t) BOX_POS_SIZE(0, 0, size->w, size->h);
  return stack_solve(items, NELEMS(items), &root, boxes);
}

/* re-solve for a new font and move everything to match */
static result_t display_relayout(display_task_t *dc)
{
  result_t       rc;
  size2d_t       size;
  box_t          boxes[DISPLAY_ST__LIMIT];
  wuss_window_t *window;

  rc = display_layout(dc->base.wuss, &size, boxes);
  if (rc != result_OK)
    return rc;

  window = dc->base.window;

  rc = task_fit_window(window, size);
  if (rc != result_OK)
    return rc;

  wuss_icon_set_bbox(window, dc->labels[0], &boxes[DISPLAY_ST_COLOURS_LABEL]);
  wuss_icon_set_bbox(window, dc->labels[1],
                     &boxes[DISPLAY_ST_RESOLUTION_LABEL]);
  wuss_icon_set_bbox(window, dc->labels[2], &boxes[DISPLAY_ST_ZOOM_LABEL]);
  wuss_icon_set_bbox(window, dc->crt, &boxes[DISPLAY_ST_CRT]);
  wuss_icon_set_bbox(window, dc->pointer, &boxes[DISPLAY_ST_POINTER]);
  wuss_icon_set_bbox(window, dc->cancel, &boxes[DISPLAY_ST_CANCEL]);
  wuss_icon_set_bbox(window, dc->change, &boxes[DISPLAY_ST_CHANGE]);
  wuss_stringset_set_bbox(dc->colours, boxes[DISPLAY_ST_COLOURS]);
  wuss_stringset_set_bbox(dc->resolution, boxes[DISPLAY_ST_RESOLUTION]);
  wuss_stringset_set_bbox(dc->zoom, boxes[DISPLAY_ST_ZOOM]);

  return result_OK;
}

static result_t display_init(display_task_t *task)
{
  result_t         rc;
  size2d_t         size;
  box_t            boxes[DISPLAY_ST__LIMIT];
  wuss_icon_spec_t specs[DISPLAY_NICONS];
  wuss_icon_t     *made[DISPLAY_NICONS];

  /* label the unlisted-size entry before measuring the entries */
  (void) display_resolution_index(task->base.wuss);

  rc = display_layout(task->base.wuss, &size, boxes);
  if (rc != result_OK)
    return rc;

  rc = wuss_window_create_at(task->base.delegate,
                             &g_display_place,
                             size,
                             "Display",
                             wuss_WINDOW_CLOSE | wuss_WINDOW_BACK,
                             wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                             size,
                             SIZE2D(0, 0),
                             &task->base.window);
  if (rc != result_OK)
    return rc;

  wuss_icon_spec_label(&specs[DISPLAY_ICON_COLOURS_LABEL],
                       boxes[DISPLAY_ST_COLOURS_LABEL],
                       "Colours", wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  wuss_icon_spec_label(&specs[DISPLAY_ICON_RESOLUTION_LABEL],
                       boxes[DISPLAY_ST_RESOLUTION_LABEL],
                       "Resolution", wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  wuss_icon_spec_label(&specs[DISPLAY_ICON_ZOOM_LABEL],
                       boxes[DISPLAY_ST_ZOOM_LABEL],
                       "Zoom", wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  wuss_icon_spec_option(&specs[DISPLAY_ICON_CRT],
                        boxes[DISPLAY_ST_CRT], "CRT effect");
  wuss_icon_spec_option(&specs[DISPLAY_ICON_POINTER],
                        boxes[DISPLAY_ST_POINTER], "Software pointer");
  wuss_icon_spec_action(&specs[DISPLAY_ICON_CANCEL],
                        boxes[DISPLAY_ST_CANCEL], "Cancel", 0);
  wuss_icon_spec_action(&specs[DISPLAY_ICON_CHANGE],
                        boxes[DISPLAY_ST_CHANGE], "Change", 0);

  rc = wuss_icon_create_array(task->base.window, specs, NELEMS(specs), made);
  if (rc != result_OK)
    return rc;

  task->crt       = made[DISPLAY_ICON_CRT];
  task->pointer   = made[DISPLAY_ICON_POINTER];
  task->cancel    = made[DISPLAY_ICON_CANCEL];
  task->change    = made[DISPLAY_ICON_CHANGE];
  task->labels[0] = made[DISPLAY_ICON_COLOURS_LABEL];
  task->labels[1] = made[DISPLAY_ICON_RESOLUTION_LABEL];
  task->labels[2] = made[DISPLAY_ICON_ZOOM_LABEL];

  rc = wuss_stringset_create(&task->zoom, task->base.window,
                             boxes[DISPLAY_ST_ZOOM], "Zoom", -1,
                             g_display_zoom_labels,
                             NELEMS(g_display_zoom_labels), NULL, NULL);
  if (rc != result_OK)
    return rc;

  /* mode picks only update the fields; Change applies them */
  rc = wuss_stringset_create(&task->colours, task->base.window,
                             boxes[DISPLAY_ST_COLOURS], "Colours", -1,
                             g_display_depth_labels,
                             NELEMS(g_display_depth_labels), NULL, NULL);
  if (rc != result_OK)
    return rc;

  rc = wuss_stringset_create(&task->resolution, task->base.window,
                             boxes[DISPLAY_ST_RESOLUTION], "Resolution", -1,
                             g_display_resolution_labels,
                             NELEMS(g_display_resolution_labels), NULL, NULL);
  if (rc != result_OK)
    return rc;

  display_sync(task);

  g_display = task;

  return result_OK;
}

/* offer an event to both string sets; 1 if one consumed it, its outcome in
 * *rc */
static int display_offer(display_task_t     *dc,
                         const wuss_event_t *event,
                         result_t           *rc)
{
  return wuss_stringset_handle_event(dc->zoom, event, rc) ||
         wuss_stringset_handle_event(dc->colours, event, rc) ||
         wuss_stringset_handle_event(dc->resolution, event, rc);
}

static result_t display_handle(wuss_window_t      *window,
                               const wuss_event_t *event,
                               void               *task_data)
{
  result_t        rc;
  display_task_t *dc;

  NOT_USED(window);

  dc = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_ICON:
    if (display_offer(dc, event, &rc))
      return rc;

    if (event->data.icon.action != wuss_MOUSE_UP)
      return result_OK;

    if (event->data.icon.icon == dc->change)
    {
      rc = display_change(dc);
      if (rc != result_OK || (event->data.icon.button & wuss_BUTTON_ADJUST))
        return rc;

      /* SELECT closes, as on RISC OS; with autoclose this frees dc, so it
       * must not be touched afterwards */
      return wuss_window_try_close(dc->base.window);
    }
    if (event->data.icon.icon == dc->cancel)
    {
      if (!(event->data.icon.button & wuss_BUTTON_ADJUST))
        return wuss_window_try_close(dc->base.window); /* frees dc likewise */

      display_sync(dc); /* revert the fields to the settings in force */
    }
    return result_OK;

  case wuss_EVENT_MENU_SELECT:
    if (display_offer(dc, event, &rc))
      return rc;

    return result_OK;

  case wuss_EVENT_MENU_CLOSED:
    /* both sets must see it, and neither consumes it */
    (void) wuss_stringset_handle_event(dc->zoom, event, &rc);
  (void) wuss_stringset_handle_event(dc->colours, event, &rc);
    (void) wuss_stringset_handle_event(dc->resolution, event, &rc);
    return result_OK;

  case wuss_EVENT_QUIT:
    wuss_stringset_destroy(dc->zoom);
    wuss_stringset_destroy(dc->colours);
    wuss_stringset_destroy(dc->resolution);
    if (dc == g_display)
      g_display = NULL;
    return result_OK;

  case wuss_EVENT_KEY:
    return result_WUSS_KEY_UNCLAIMED;

  case wuss_EVENT_RELAYOUT:
    return display_relayout(dc);

  default:
    return result_OK;
  }
}

/* ----------------------------------------------------------------------- */

static result_t display_create(wuss_t *wuss, void **out);

static const taskapp_desc_t display_desc =
{
  .name             = "Display",
  .purpose          = "Set colours and resolution",
  .create           = display_create,
  .singleton        = true,
  .iconbar_priority = 0x50000000, /* beside the Task Manager */
  .no_quit          = true,
  .instance_size    = sizeof(display_task_t),
  .init             = (taskapp_init_fn_t) display_init,
  .handle           = display_handle
};

static result_t display_create(wuss_t *wuss, void **out)
{
  /* one closed but not yet quit no longer counts */
  if (g_display != NULL && g_display->base.window != NULL)
  {
    (void) wuss_task_front(wuss, g_display->base.delegate);
    if (out)
      *out = g_display;
    return result_OK;
  }

  return taskapp_instance_create(wuss, &display_desc, out);
}

result_t display_app_create(wuss_t *wuss)
{
  return taskapp_install(wuss, &display_desc);
}

static result_t display__spawn(wuss_t *wuss, void *param, void **out)
{
  NOT_USED(param);

  return display_create(wuss, out);
}

result_t display_open(wuss_t *wuss)
{
  void *task;

  return taskapp_spawn(wuss, &display_desc, display__spawn, NULL, &task);
}

#endif /* WUSS_APP */
