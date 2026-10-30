/* wuss/tasks/painting/painting.c -- basic painting task */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "utils/rng.h"

#include "painting.h"
#include "painting-bars.h"
#include "painting-new.h"
#include "painting-tools.h"
#include "../common/snapshot.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define PAINTING_CANVAS_W 640
#define PAINTING_CANVAS_H 480
#define PAINTING_VIEW_W   400 /* initial visible size of the canvas window */
#define PAINTING_VIEW_H   300

#define PAINTING_ANTS_IDLES 6 /* idles between steps of the marching ants */

/* point the option bar swatches at the desktop palette, which wuss owns and
 * wuss_set_palette may move */
static void painting_swatches_track(painting_task_t *pt)
{
  pt->swatches.colours = wuss_get_palette(pt->wuss, &pt->swatches.ncolours);
}

/* lay out the stage around the canvas, at its smallest, into boxes, noting
 * where the canvas lands. The stage lays out with y up, so flip it. Returns
 * the stage's size, the window's document */
static size2d_t painting_stage(painting_task_t *pt,
                               stagebox_t      *boxes,
                               size_t          *nboxes)
{
  const bitmap_t *bm;
  size2d_t        sz;

  bm = paintingdoc_bitmap(pt->doc);
  stage_generate(&pt->stagecfg, 0, 0, bm->size.w, bm->size.h,
                 &sz.w, &sz.h, boxes, nboxes);
  pt->page = POINT(boxes[0].box.x0, sz.h - boxes[0].box.y1); /* content */

  return sz;
}

/* as painting_stage, for its size alone */
static size2d_t painting_stage_size(painting_task_t *pt)
{
  stagebox_t boxes[stage_MAX_BOXES];
  size_t     nboxes;

  return painting_stage(pt, boxes, &nboxes);
}

/* ----------------------------------------------------------------------- */

/* title the canvas window "LEAF: Painting", starred while edited */
static void painting_title(painting_task_t *pt)
{
  if (pt->window == NULL)
    return;

  task_set_doc_title(pt->window,
                     pt->path[0] ? path_leaf(pt->path) : "<Untitled>",
                     "Painting", paintingdoc_is_edited(pt->doc));
}

/* the document is now the file at path */
static void painting_set_path(painting_task_t *pt, const char *path)
{
  snprintf(pt->path, sizeof(pt->path), "%s", path);
  wuss_saveas_set_leafname(pt->saveas, path_leaf(pt->path));
}

/* wuss_saveas_save_fn_t: opaque is the painting_task_t */
static result_t painting_saveas_save(const char *path, void *opaque)
{
  result_t         rc;
  painting_task_t *pt;

  pt = opaque;

  rc = bitmap_save_png(paintingdoc_bitmap(pt->doc), path);
  if (rc != result_OK)
    return rc;

  painting_set_path(pt, path);
  paintingdoc_set_saved(pt->doc);
  painting_title(pt);

  return result_OK;
}

/* load the PNG at path as the canvas, shrinking the window to the image's
 * stage if that is smaller than the usual view. A failure keeps the current
 * canvas */
static result_t painting_load(painting_task_t *pt, const char *path)
{
  result_t rc;
  size2d_t size;

  pt->held = 0; /* abandon any stroke in progress */

  rc = paintingdoc_load_png(pt->doc, path);
  if (rc != result_OK)
    return rc;

  painting_set_path(pt, path);
  painting_title(pt);

  size = painting_stage_size(pt);

  rc = wuss_window_resize(pt->window,
                          SIZE2D(MIN(size.w, PAINTING_VIEW_W),
                                 MIN(size.h, PAINTING_VIEW_H)));
  if (rc != result_OK)
    return rc;

  return wuss_window_set_doc(pt->window, size);
}

result_t painting_create(wuss_t *wuss, painting_task_t **out)
{
  paintingnew_params_t params;

  params.size = SIZE2D(PAINTING_CANVAS_W, PAINTING_CANVAS_H);
  params.bg   = colour_rgb(0xFF, 0xFF, 0xFF);
  params.fg   = colour_rgb(0x00, 0x00, 0x00);

  return painting_create_with(wuss, &params, out);
}

result_t painting_create_with(wuss_t                     *wuss,
                              const paintingnew_params_t *params,
                              painting_task_t           **out)
{
  result_t         rc;
  painting_task_t *task;
  wuss_task_t     *delegate;
  wuss_task_desc_t delegate_desc;
  wuss_place_t     place;
  size2d_t         stage;
  int              i;

  task = calloc(1, sizeof(*task));
  if (task == NULL)
    return result_OOM;

  task->wuss = wuss;
  task->fg   = params->fg;
  task->bg   = params->bg;
  task->tool = painting_TOOL_BRUSH;
  rng_seed(&task->rng, 1);
  task->size      = 3;
  task->density   = 20; /* about the old fixed dusting */
  task->rate      = 100;
  task->edge      = 100;
  task->wander    = 100;
  task->tolerance = 16;
  task->dithering = screen_DITHER_NEAREST; /* painted colours stay exact */

  task->swatches.bbox    = (box_t) BOX_POS_SIZE(OB_SWATCH_X, OB_SWATCH_Y,
                                                OB_SWATCH_ROW, OB_SWATCH);
  task->swatches.columns = OB_SWATCHES;
  task->swatches.gap     = OB_SWATCH_GAP;
  task->swatches.outline = 1;
  painting_swatches_track(task);

  rc = paintingdoc_create_filled(params->size, params->bg, &task->doc);
  if (rc != result_OK)
  {
    free(task);
    return rc;
  }

  stageconfig_init(&task->stagecfg);
  task->stagecfg.pasteboard_min = 128;
  task->stagecfg.margin = 1;

  stage = painting_stage_size(task);

  /* painting_redraw draws the stage and the canvas on it over the whole
   * content area */
  delegate_desc.handle    = painting_handle;
  delegate_desc.task_data = task;
  delegate_desc.name      = "painting";
  rc = wuss_task_create(wuss, &delegate_desc, &delegate);
  if (rc != result_OK)
  {
    paintingdoc_destroy(task->doc);
    free(task); /* nothing registered yet; the spawner will not free it */
    return rc;
  }
  task->delegate = delegate;

  /* room for the toolbox on the left and the option bar below, which share
   * the canvas's outline */
  place          = task_place;
  place.extra.x0 = TB_W + 1;
  place.extra.y1 = OB_H + 1;
  rc = wuss_window_create_at(delegate,
                             &place,
                             SIZE2D(MIN(stage.w, PAINTING_VIEW_W),
                                    MIN(stage.h, PAINTING_VIEW_H)),
                             "Painting",
                             wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                             wuss_NO_BACKDROP,
                             stage,
                             SIZE2D(0, 0),
                             &task->window);
  if (rc != result_OK)
  {
    wuss_task_destroy(delegate); /* unregister; its QUIT frees the task block */
    return rc;
  }

  /* toolbars on the canvas's left and bottom edges: they move, stack, hide
   * and close with the canvas, and pass the input focus to it. The toolbox
   * is nothing but icons */
  place = task_place_after(task->window);
  rc = wuss_window_create_at(delegate,
                             &place,
                             SIZE2D(TB_W, TB_H),
                             NULL,
                             wuss_WINDOW_NO_TITLEBAR | wuss_WINDOW_NO_REDRAW,
                             wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                             SIZE2D(TB_W, TB_H),
                             SIZE2D(0, 0),
                             &task->toolbox);
  /* toolbars align against the content without its panes, so drop the
   * toolbox to share its top outline with the size bar's bottom edge */
  if (rc == result_OK)
    rc = wuss_window_attach(task->toolbox, task->window, wuss_EDGE_LEFT,
                            wuss_ALIGN_TOP, SB_H + 1);
  if (rc == result_OK)
    rc = wuss_window_create_at(delegate,
                               &place,
                               SIZE2D(OB_W, OB_H),
                               NULL,
                               wuss_WINDOW_NO_TITLEBAR,
                               wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                               SIZE2D(OB_W, OB_H),
                               SIZE2D(0, 0),
                               &task->optionbar);
  if (rc == result_OK)
    rc = wuss_window_attach(task->optionbar, task->window, wuss_EDGE_BOTTOM,
                            wuss_ALIGN_LEFT, 0);
  /* the size bar is a pane across the top of the canvas, which is then
   * resized so that the view keeps its size below it */
  if (rc == result_OK)
    rc = wuss_window_create_at(delegate,
                               &place,
                               SIZE2D(SB_W, SB_H),
                               NULL,
                               wuss_WINDOW_NO_TITLEBAR |
                               wuss_WINDOW_NO_OUTLINE  |
                               wuss_WINDOW_NO_REDRAW,
                               wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                               SIZE2D(SB_W, SB_H),
                               SIZE2D(0, 0),
                               &task->sizebar);
  if (rc == result_OK)
    rc = wuss_window_attach_pane(task->sizebar, task->window, wuss_EDGE_TOP);
  if (rc == result_OK)
    rc = wuss_window_resize(task->window,
                            SIZE2D(MIN(stage.w, PAINTING_VIEW_W),
                                   MIN(stage.h, PAINTING_VIEW_H)));
  if (rc != result_OK)
  {
    wuss_task_destroy(delegate); /* closes the canvas, QUIT frees the block */
    return rc;
  }

  rc = painting_toolbar_icons(task);
  if (rc != result_OK)
  {
    wuss_task_destroy(delegate);
    return rc;
  }

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, PAINTING_MENU_UNDO, "Undo",
                          wuss_MENU_ITEM_NONE, "F8");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, PAINTING_MENU_SAVE, "Save",
                          wuss_MENU_ITEM_NONE, "F3"); /* window set below */

  WUSS_MENU_ITEM_SHORTCUT(task->sel_items, PAINTING_SEL_ALL, "Select all",
                          wuss_MENU_ITEM_NONE, "^A");
  WUSS_MENU_ITEM_SHORTCUT(task->sel_items, PAINTING_SEL_NONE,
                          "Clear selection", wuss_MENU_ITEM_NONE, "^Z");
  WUSS_MENU_ITEM_SHORTCUT(task->sel_items, PAINTING_SEL_INVERT, "Invert",
                          wuss_MENU_ITEM_NONE, "^I");
  WUSS_MENU_TITLE(task->sel_menu, "Selection", task->sel_items,
                  NELEMS(task->sel_items));
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_SELECTION, "Selection",
                      wuss_MENU_ITEM_NONE, &task->sel_menu);

  for (i = 0; i < paintingdoc_NOZZLE__COUNT; i++)
    WUSS_MENU_ITEM(task->nozzle_items, i, painting_nozzle_names[i],
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->nozzle_menu, "Nozzle", task->nozzle_items,
                  NELEMS(task->nozzle_items));
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_NOZZLE, "Nozzle",
                      wuss_MENU_ITEM_DASHED, &task->nozzle_menu);
  /* the shortcut, or a click on the row, steps to the next nozzle */
  task->menu_items[PAINTING_MENU_NOZZLE].shortcut = "N";

  dithermenu_init(&task->dither, task->dithering);
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_DITHERING, "Dithering",
                      wuss_MENU_ITEM_NONE, &task->dither.menu);

  WUSS_MENU_TITLE(task->menu, "Painting", task->menu_items,
                  NELEMS(task->menu_items));

  rc = snapshot_saveas_create(&task->saveas, wuss, "painting.png",
                              painting_saveas_save, task);
  if (rc != result_OK)
  {
    wuss_task_destroy(delegate); /* closes both windows, QUIT frees the block */
    return rc;
  }
  /* hover opens the Save As dialogue as a submenu; F3 shows it standalone */
  task->menu_items[PAINTING_MENU_SAVE].window = wuss_saveas_window(task->saveas);
  painting_title(task); /* "<Untitled>: Painting" */

  /* both windows up: from here, closing the last one reaps the task and its
   * wuss_EVENT_QUIT frees task_data */
  wuss_task_set_autoclose(delegate, 1);

  if (out)
    *out = task;

  return result_OK;
}

/* the New dialogue: made when first wanted, as a child of the app's task so
 * Quit takes it too, which also clears this */
static struct
{
  paintingnew_t *painting_new;
}
g;

static result_t painting_new_dialogue(wuss_t *wuss);

static const taskapp_desc_t painting_desc =
{
  "Painting",
  "Basic painting",
  (taskapp_create_fn_t) painting_create,
  (taskapp_data_load_fn_t) painting_load,
  false,
  painting_new_dialogue
};

/* taskapp_spawn_fn_t: param is the dialogue's paintingnew_params_t */
static result_t painting_spawn_new(wuss_t *wuss, void *param, void **out)
{
  (void) out;

  return painting_create_with(wuss, param, NULL);
}

/* paintingnew_accept_fn_t: opaque is the wuss_t */
static result_t painting_accept_new(const paintingnew_params_t *params,
                                    void                       *opaque)
{
  return taskapp_spawn(opaque, &painting_desc, painting_spawn_new,
                       (void *) params, NULL);
}

static result_t painting_new_dialogue(wuss_t *wuss)
{
  result_t rc;

  if (g.painting_new == NULL)
  {
    rc = paintingnew_create(&g.painting_new, wuss, painting_accept_new, wuss);
    if (rc != result_OK)
      return rc;
  }

  return paintingnew_open(g.painting_new);
}

result_t painting_app_create(wuss_t *wuss)
{
  return taskapp_install(wuss, &painting_desc);
}

void painting_destroy(painting_task_t *task)
{
  wuss_saveas_destroy(task->saveas);
  free(task->lasso);
  paintingdoc_destroy(task->doc);
  free(task);
}

/* ----------------------------------------------------------------------- */

/* the stage around the canvas: everything but its content box */
static void painting_redraw_stage(painting_task_t *pt,
                                  screen_t        *scr,
                                  int              ox,
                                  int              oy)
{
  stagebox_t boxes[stage_MAX_BOXES];
  size_t     nboxes;
  size2d_t   sz;
  size_t     i;
  box_t      b;
  colour_t   colour;

  sz = painting_stage(pt, boxes, &nboxes);

  for (i = 0; i < nboxes; i++)
  {
    switch (boxes[i].kind)
    {
    case stageboxkind_CONTENT:    continue;
    case stageboxkind_STROKE:     colour = colour_rgb(0, 0, 0);       break;
    case stageboxkind_SHADOW:     colour = colour_rgb(96, 96, 96);    break;
    case stageboxkind_MARGIN:     colour = colour_rgb(255, 255, 255); break;
    default:                      colour = colour_rgb(192, 192, 192); break;
    }

    b = (box_t) { ox + boxes[i].box.x0, oy + sz.h - boxes[i].box.y1,
                  ox + boxes[i].box.x1, oy + sz.h - boxes[i].box.y0 };
    screen_fill_rects(scr, &b, 1, colour);
  }
}

static result_t painting_redraw(painting_task_t    *pt,
                                const wuss_event_t *event)
{
  result_t        rc;
  const box_t    *bounds;
  screen_t       *scr;
  int             i, ox, oy;
  const bitmap_t *bm;
  box_t           canvas;
  point_t         a, b;

  scr    = event->data.redraw.scr;
  bounds = event->data.redraw.bounds;
  ox     = bounds->x0 - event->data.redraw.scroll.x;
  oy     = bounds->y0 - event->data.redraw.scroll.y;

  painting_redraw_stage(pt, scr, ox, oy);

  /* from here on, draw relative to the canvas */
  ox    += pt->page.x;
  oy    += pt->page.y;
  bm     = paintingdoc_bitmap(pt->doc);
  canvas = (box_t) BOX_POS_SIZE(ox, oy, bm->size.w, bm->size.h);

  /* the checks show through wherever the canvas is transparent */
  task_draw_checks(scr, &canvas, POINT(ox, oy));
  rc = screen_copy_bitmap_dithered(scr, ox, oy, bm, pt->dithering);

  if (pt->held && painting_is_rubber_band(pt))
  {
    painting_band_ends(pt, &a, &b);
    a = POINT(a.x + ox, a.y + oy);
    b = POINT(b.x + ox, b.y + oy);

    if (pt->tool == painting_TOOL_LINE)
      paintingdoc_draw_stroke(scr, a, b, pt->size, painting_stroke_colour(pt));
    else if (pt->tool == painting_TOOL_GRADIENT)
      paintingdoc_draw_stroke(scr, a, b, 1, colour_rgb(0x00, 0x00, 0x00));
    else if (pt->tool == painting_TOOL_SELECT)
      paintingdoc_draw_rect(scr, a, b, 1, 0, colour_rgb(0x00, 0x00, 0x00));
    else if (pt->tool == painting_TOOL_ELLIPSE ||
             pt->tool == painting_TOOL_ELLIPSE_FILL)
      paintingdoc_draw_ellipse(scr, a, b, pt->size,
                               pt->tool == painting_TOOL_ELLIPSE_FILL,
                               painting_stroke_colour(pt));
    else
      paintingdoc_draw_rect(scr, a, b, pt->size, pt->tool == painting_TOOL_RECT_FILL,
                         painting_stroke_colour(pt));
  }

  /* ponytail: a path is previewed as thin, open lines, as the lasso is;
   * draw it at its width, closed, if that misleads */
  if (pt->held && painting_collects_points(pt))
  {
    for (i = 1; i < pt->nlasso; i++)
      screen_draw_line(scr, pt->lasso[i - 1].x + ox, pt->lasso[i - 1].y + oy,
                       pt->lasso[i].x + ox, pt->lasso[i].y + oy,
                       painting_is_path(pt) ? painting_stroke_colour(pt) :
                                              colour_rgb(0x00, 0x00, 0x00));
  }

  painting_draw_ants(pt, scr, ox, oy);

  return rc;
}

/* a MOVE always reports the Select button, hovering or not, so a stroke in
 * progress is tracked in pt->held instead */
static result_t painting_mouse(painting_task_t    *pt,
                               const wuss_event_t *event)
{
  box_t    dirty = BOX_INIT;
  box_t    old;
  point_t  p;
  colour_t colour;

  p = POINT(event->data.mouse.point.x - pt->page.x,
            event->data.mouse.point.y - pt->page.y); /* canvas pixels */

  switch (event->data.mouse.action)
  {
  case wuss_MOUSE_DOWN:
    /* a second button pressed mid-stroke is ignored, so it can't restart
     * the stroke and strand what is drawn so far */
    if (pt->held ||
        !(event->data.mouse.button & (wuss_BUTTON_SELECT | wuss_BUTTON_ADJUST)))
      return result_OK;

    pt->held   = (event->data.mouse.button & wuss_BUTTON_SELECT) ?
                 wuss_BUTTON_SELECT : wuss_BUTTON_ADJUST;
    pt->anchor = p;
    pt->last   = p;
    pt->mods   = event->data.mouse.modifiers;

    switch (pt->tool)
    {
    case painting_TOOL_PICK:
      pt->held = 0; /* a click, not a stroke */
      if (!paintingdoc_pick(pt->doc, p, &colour))
        return result_OK;

      if (event->data.mouse.button & wuss_BUTTON_SELECT)
        pt->fg = colour;
      else
        pt->bg = colour;
      if (pt->optionbar != NULL)
        wuss_window_invalidate_visible(pt->optionbar);
      return result_OK;

    case painting_TOOL_WAND:
      pt->held = 0; /* a click, not a stroke */
      old      = paintingdoc_selection_bounds(pt->doc);
      (void) paintingdoc_select_similar(pt->doc, p, pt->tolerance,
                                        painting_selop(pt));
      painting_invalidate_box(pt, &old);
      old = paintingdoc_selection_bounds(pt->doc);
      painting_invalidate_box(pt, &old);
      return result_OK;

    case painting_TOOL_MOVE:
      /* a drag anywhere moves the selection, as long as there is one */
      if (paintingdoc_lift(pt->doc) != result_OK)
        pt->held = 0;
      return result_OK;

    case painting_TOOL_FILL:
      colour   = painting_stroke_colour(pt);
      pt->held = 0; /* a click, not a stroke */
      dirty    = paintingdoc_fill(pt->doc, p, colour);
      break;

    case painting_TOOL_LASSO:
    case painting_TOOL_PATH:
    case painting_TOOL_PATH_FILL:
      pt->nlasso = 0;
      painting_lasso_add(pt, p);
      dirty = painting_span_box(p, p);
      break;

    case painting_TOOL_SPRAY:
      if (paintingdoc_checkpoint(pt->doc) != result_OK)
      {
        pt->held = 0;
        return result_OK;
      }
      pt->spray_tick = 100 - pt->rate; /* the press always sprays */
      dirty          = painting_spray(pt, p);
      break;

    case painting_TOOL_BRUSH:
    case painting_TOOL_PAINTBRUSH:
    case painting_TOOL_WOBBLE:
    case painting_TOOL_ERASER:
      if (paintingdoc_checkpoint(pt->doc) != result_OK)
      {
        pt->held = 0; /* no undo step, so no stroke */
        return result_OK;
      }
      pt->wobble.seed = rng_xorshift32(&pt->rng); /* a fresh wobble */
      pt->wobble.step = 0;
      dirty = painting_stroke_segment(pt, p, p);
      break;

    default:
      dirty = painting_band_box(pt);
      break;
    }
    break;

  case wuss_MOUSE_MOVE:
    if (!pt->held)
      return result_OK;

    if (painting_collects_points(pt))
    {
      painting_lasso_add(pt, p);
      dirty    = painting_span_box(pt->last, p);
      pt->last = p;
    }
    else if (painting_is_rubber_band(pt))
    {
      old = painting_band_box(pt);
      painting_invalidate_box(pt, &old);
      pt->last = p;
      pt->mods = event->data.mouse.modifiers;
      dirty    = painting_band_box(pt);
    }
    else if (pt->tool == painting_TOOL_SPRAY)
    {
      dirty    = painting_spray(pt, p);
      pt->last = p;
    }
    else if (pt->tool == painting_TOOL_MOVE)
    {
      dirty = painting_place(pt, p);
    }
    else
    {
      dirty    = painting_stroke_segment(pt, pt->last, p);
      pt->last = p;
    }
    break;

  case wuss_MOUSE_UP:
    /* only releasing the button that began the stroke ends it */
    if (!(event->data.mouse.button & pt->held))
      return result_OK;

    if (painting_is_selector(pt))
    {
      painting_select_commit(pt, p);
    }
    else if (pt->tool == painting_TOOL_MOVE)
    {
      dirty = painting_place(pt, p);
      paintingdoc_drop(pt->doc);
    }
    else if (painting_is_path(pt))
    {
      painting_lasso_add(pt, p);

      /* the preview is no longer drawn over the canvas */
      old = painting_points_box(pt);
      painting_invalidate_box(pt, &old);

      dirty = paintingdoc_path(pt->doc, pt->lasso, pt->nlasso, pt->size,
                               pt->tool == painting_TOOL_PATH_FILL,
                               painting_stroke_colour(pt));
      pt->nlasso = 0;
    }
    else if (painting_is_rubber_band(pt))
    {
      /* the band is no longer drawn over the canvas */
      old = painting_band_box(pt);
      painting_invalidate_box(pt, &old);

      pt->last = p;
      pt->mods = event->data.mouse.modifiers;
      dirty    = painting_band_commit(pt);
    }

    pt->held = 0;
    break;

  default:
    return result_OK;
  }

  painting_invalidate_box(pt, &dirty);

  painting_title(pt);

  return result_OK;
}

/* shade Undo when there is nothing to undo and Clear selection when nothing is
 * selected, also on the menu if it is open (an ADJUST pick keeps it open) */
static void painting_menu_shade(painting_task_t *pt)
{
  int no_undo, no_sel;

  no_undo = !paintingdoc_can_undo(pt->doc);
  no_sel  = !paintingdoc_has_selection(pt->doc);

  pt->menu_items[PAINTING_MENU_UNDO].flags = no_undo ?
                                          wuss_MENU_ITEM_DISABLED :
                                          wuss_MENU_ITEM_NONE;
  pt->sel_items[PAINTING_SEL_NONE].flags   = no_sel ?
                                          wuss_MENU_ITEM_DISABLED :
                                          wuss_MENU_ITEM_NONE;

  wuss_menu_disable_item_live(pt->menu_handle, &pt->menu, PAINTING_MENU_UNDO,
                              no_undo);
  wuss_menu_disable_item_live(pt->menu_handle, &pt->sel_menu, PAINTING_SEL_NONE,
                              no_sel);
}

static result_t painting_undo(painting_task_t *pt)
{
  box_t dirty;

  pt->held = 0; /* abandon any stroke in progress */

  dirty = paintingdoc_undo(pt->doc);
  painting_invalidate_box(pt, &dirty);

  painting_menu_shade(pt);
  painting_title(pt);

  return result_OK;
}

/* B, H, E, L, R, X, C, D, N, M, F, G, P, S, O, W and I pick the matching
 * tool, [ and ] step the size down and up, Escape drops the selection.
 * Anything else is passed back unclaimed */
static result_t painting_key(painting_task_t *pt, int code)
{
  painting_tool_t tool;
  box_t           box;

  switch (code)
  {
  case 'b': case 'B': tool = painting_TOOL_BRUSH;        break;
  case 'h': case 'H': tool = painting_TOOL_PAINTBRUSH;   break;
  case 'k': case 'K': tool = painting_TOOL_WOBBLE;       break;
  case 'e': case 'E': tool = painting_TOOL_ERASER;       break;
  case 'l': case 'L': tool = painting_TOOL_LINE;         break;
  case 'r': case 'R': tool = painting_TOOL_RECT;         break;
  case 'x': case 'X': tool = painting_TOOL_RECT_FILL;    break;
  case 'c': case 'C': tool = painting_TOOL_ELLIPSE;      break;
  case 'd': case 'D': tool = painting_TOOL_ELLIPSE_FILL; break;
  case 'n': case 'N': tool = painting_TOOL_PATH;         break;
  case 'm': case 'M': tool = painting_TOOL_PATH_FILL;    break;
  case 'f': case 'F': tool = painting_TOOL_FILL;         break;
  case 'g': case 'G': tool = painting_TOOL_GRADIENT;     break;
  case 'p': case 'P': tool = painting_TOOL_SPRAY;        break;
  case 's': case 'S': tool = painting_TOOL_SELECT;       break;
  case 'o': case 'O': tool = painting_TOOL_LASSO;        break;
  case 'w': case 'W': tool = painting_TOOL_WAND;         break;
  case 'i': case 'I': tool = painting_TOOL_PICK;         break;
  case 'v': case 'V': tool = painting_TOOL_MOVE;         break;
  case '[':           painting_step_size(pt, -1);        return result_OK;
  case ']':           painting_step_size(pt, +1);        return result_OK;
  case wuss_KEY_ESCAPE:
    box = paintingdoc_selection_bounds(pt->doc);
    paintingdoc_select_none(pt->doc);
    painting_invalidate_box(pt, &box);
    return result_OK;
  default:            return result_WUSS_KEY_UNCLAIMED;
  }

  return painting_set_tool(pt, tool);
}

/* apply a Selection menu row, redrawing where the selection was and is */
static result_t painting_selection_op(painting_task_t *pt, int op)
{
  result_t rc;
  box_t    old, now;

  old = paintingdoc_selection_bounds(pt->doc);
  rc  = result_OK;

  switch (op)
  {
  case PAINTING_SEL_ALL:    rc = paintingdoc_select_all(pt->doc);    break;
  case PAINTING_SEL_NONE:   paintingdoc_select_none(pt->doc);        break;
  case PAINTING_SEL_INVERT: rc = paintingdoc_select_invert(pt->doc); break;
  default:               break;
  }

  now = paintingdoc_selection_bounds(pt->doc);
  painting_invalidate_box(pt, &old);
  painting_invalidate_box(pt, &now);
  painting_menu_shade(pt);

  return rc;
}

/* a MENU click on the canvas opens the menu, with Undo greyed out when
 * there is nothing to undo and Clear selection when there is nothing
 * selected */
static result_t painting_menu_open(painting_task_t *pt)
{
  painting_menu_shade(pt);

  wuss_menu_tick_exclusive(&pt->nozzle_menu, pt->nozzle);

  return wuss_menu_open_at_pointer(pt->delegate, &pt->menu, &pt->menu_handle);
}

result_t painting_handle(wuss_window_t      *window,
                         const wuss_event_t *event,
                         void               *task_data)
{
  painting_task_t *pt;

  pt = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    if (window == pt->optionbar)
      return painting_optionbar_redraw(pt, event);
    return painting_redraw(pt, event);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action == wuss_MOUSE_DOWN &&
        (event->data.mouse.button & wuss_BUTTON_MENU))
      return window == pt->window ? painting_menu_open(pt) : result_OK;

    if (window == pt->optionbar)
      return painting_optionbar_mouse(pt, event);
    if (window == pt->toolbox || window == pt->sizebar)
      return result_OK; /* between the buttons */
    return painting_mouse(pt, event);

  case wuss_EVENT_ICON:
    return painting_toolbar_icon(pt, event);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    if (!task_key_is_plain(pt->delegate, &pt->menu, event, &rc))
      return rc;

    return painting_key(pt, event->data.key.code);
  }

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu == &pt->sel_menu)
      return painting_selection_op(pt, event->data.menu_select.index);
    if (event->data.menu_select.menu == &pt->nozzle_menu)
    {
      painting_set_nozzle(pt, event->data.menu_select.index);
      return result_OK;
    }
    if (event->data.menu_select.menu == &pt->dither.menu)
    {
      pt->dithering = dithermenu_select(&pt->dither, pt->menu_handle,
                                        event->data.menu_select.index);
      wuss_window_invalidate_visible(pt->window);
      return result_OK;
    }
    if (event->data.menu_select.index == PAINTING_MENU_UNDO)
      return painting_undo(pt);
    if (event->data.menu_select.index == PAINTING_MENU_NOZZLE)
    {
      painting_set_nozzle(pt, (pt->nozzle + 1) % paintingdoc_NOZZLE__COUNT);
      return result_OK;
    }
    /* clicking the row, rather than hovering, shows Save As standalone */
    if (event->data.menu_select.index == PAINTING_MENU_SAVE)
      return wuss_saveas_open(pt->saveas);
    return result_OK;

  case wuss_EVENT_MESSAGE:
    if (pt->window == NULL || window != pt->window)
      return result_OK;
    return taskapp_window_data_load(pt->wuss, pt->delegate, pt,
                                    (taskapp_data_load_fn_t) painting_load,
                                    event->data.message);

  case wuss_EVENT_IDLE:
    if (pt->window == NULL)
      return result_OK;

    /* the spray keeps building while the button is held still */
    if (pt->held && pt->tool == painting_TOOL_SPRAY)
    {
      box_t dirty;

      dirty = painting_spray(pt, pt->last);
      painting_invalidate_box(pt, &dirty);
      painting_title(pt);
    }

    if (!paintingdoc_has_selection(pt->doc))
      return result_OK;

    if (++pt->ants_tick >= PAINTING_ANTS_IDLES)
    {
      box_t bounds;

      pt->ants_tick  = 0;
      pt->ants_phase = (pt->ants_phase + 1) & 7;

      bounds = paintingdoc_selection_bounds(pt->doc);
      painting_invalidate_box(pt, &bounds);
    }
    return result_OK;

  case wuss_EVENT_MENU_CLOSED:
    pt->menu_handle = NULL;
    return result_OK;

  case wuss_EVENT_PALETTE:
    /* wuss_set_palette repaints the whole screen itself */
    painting_swatches_track(pt);
    return result_OK;

  case wuss_EVENT_CLOSE_ADJUST:
    if (window != pt->window)
      return result_OK;
    return task_close_adjust(pt->wuss, pt->delegate, window, pt->path,
                             event);

  case wuss_EVENT_CLOSE:
    /* the toolbars and pane belong to the canvas, so they close with it */
    pt->window    = NULL;
    pt->toolbox   = NULL;
    pt->optionbar = NULL;
    pt->sizebar   = NULL;
    return result_OK;

  case wuss_EVENT_QUIT:
    /* the task autocloses once its windows go, so free the block here */
    painting_destroy(pt);
    return result_OK;

  default:
    return result_OK;
  }
}

#endif /* WUSS_APP */
