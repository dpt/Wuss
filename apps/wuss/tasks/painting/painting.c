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
#include "../common/taskapp.h"
#include "../common/common.h"

#define PAINTING_CANVAS_W 640
#define PAINTING_CANVAS_H 480
#define PAINTING_VIEW_W   400 /* initial visible size of the canvas window */
#define PAINTING_VIEW_H   300

#define PAINTING_ANTS_IDLES 6 /* idles between steps of the marching ants */
#define PAINTING_GRID_ZOOM  6 /* least zoom showing the pixel grid */

/* lay out the stage around the canvas, at its smallest, into boxes, noting
 * where the canvas lands. The stage lays out with y up, so flip it. Returns
 * the stage's size, the window's document */
static size2d_t painting_stage(painting_task_t *pt,
                               stagebox_t      *boxes,
                               size_t          *nboxes)
{
  const bitmap_t *bm;
  int             n, w, h;
  size2d_t        sz;

  /* tiled, the content is the canvas with a copy on every side */
  bm = paintingdoc_bitmap(pt->doc);
  n  = (pt->symmetry & (1u << PAINTING_SYM_TILED)) ? 3 : 1;
  w  = bm->size.w * pt->zoom;
  h  = bm->size.h * pt->zoom;
  stage_generate(&pt->stagecfg, 0, 0, w * n, h * n,
                 &sz.w, &sz.h, boxes, nboxes);
  pt->page = POINT(boxes[0].box.x0 + w * (n / 2),
                   sz.h - boxes[0].box.y1 + h * (n / 2)); /* content */

  return sz;
}

/* as painting_stage, for its size alone */
static size2d_t painting_stage_size(painting_task_t *pt)
{
  stagebox_t boxes[stage_MAX_BOXES];
  size_t     nboxes;

  return painting_stage(pt, boxes, &nboxes);
}

/* size the canvas window's document to the stage, shrinking the window to
 * it if that is smaller than the usual view */
static result_t painting_fit(painting_task_t *pt)
{
  result_t rc;
  size2d_t size;

  size = painting_stage_size(pt);

  rc = wuss_window_resize(pt->base.window,
                          SIZE2D(MIN(size.w, PAINTING_VIEW_W),
                                 MIN(size.h, PAINTING_VIEW_H)));
  if (rc != result_OK)
    return rc;

  return wuss_window_set_doc(pt->base.window, size);
}

/* ----------------------------------------------------------------------- */

/* title the canvas window "LEAF: Painting", starred while edited */
static void painting_title(painting_task_t *pt)
{
  if (pt->base.window == NULL)
    return;

  task_set_doc_title(pt->base.window,
                     pt->path[0] ? path_leaf(pt->path) : "<Untitled>",
                     "Painting", paintingdoc_is_edited(pt->doc));
}

/* the document is now the file at path */
static void painting_set_path(painting_task_t *pt, const char *path)
{
  snprintf(pt->path, sizeof(pt->path), "%s", path);
  wuss_saveas_set_leafname(pt->base.saveas, path_leaf(pt->path));
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

/* load the PNG or JPEG at path as the canvas, shrinking the window to the
 * image's stage if that is smaller than the usual view. A file that is no
 * image is tried as a .hex palette. A failure keeps the current canvas and
 * palette */
static result_t painting_load(painting_task_t *pt, const char *path)
{
  result_t rc;

  pt->held = 0; /* abandon any stroke in progress */

  /* ponytail: sniffs by trying both loaders rather than by extension, since
   * RISC OS leaves carry none. Match on filetype if more formats arrive */
  rc = paintingdoc_load(pt->doc, path);
  if (rc != result_OK)
    return painting_palette_load(pt, path) == result_OK ? result_OK : rc;

  painting_set_path(pt, path);
  painting_title(pt);
  painting_layers_changed(pt);

  return painting_fit(pt);
}

static wuss_scaleview_choice_fn_t painting_scaleview_choice;
static paintingnew_accept_fn_t    painting_resize_accept;

/* the Transform submenu's rows, in paintingdoc_xform_t order */
static const char *const painting_xform_names[paintingdoc_XFORM__COUNT] =
{
  "Flip horizontally",
  "Flip vertically",
  "Rotate right",
  "Rotate left",
  "Rotate 180"
};

/* the pixel clipboard, shared by every canvas so pixels copy between them,
 * and the number of canvases, the last of which empties it */
static paintingdoc_clip_t painting_clip;
static int                painting_docs;

/* build the canvas and its windows; on failure the caller reaps the task */
static result_t painting_build(painting_task_t            *task,
                               const paintingnew_params_t *params)
{
  result_t        rc;
  wuss_task_t    *delegate;
  const colour_t *desktop;
  int             ndesktop;
  wuss_place_t    place;
  size2d_t        stage;
  int             i;
  filetype_t      text;

  delegate = task->base.delegate;

  task->fg   = params->fg;
  task->bg   = params->bg;
  task->tool = painting_TOOL_PENCIL;
  rng_seed(&task->rng, 1);
  task->size      = 3;
  task->density   = 20; /* about the old fixed dusting */
  task->rate      = 100;
  task->edge      = 100;
  task->wander    = 100;
  task->tolerance = 16;
  task->perfect   = 1;
  task->dithering = screen_DITHER_NEAREST; /* painted colours stay exact */
  task->zoom      = 1;

  /* the palette starts as a copy of the desktop's, then is the document's */
  task->swatches.columns = OB_SWATCHES;
  task->swatches.gap     = OB_SWATCH_GAP;
  task->swatches.outline = 1;
  desktop = wuss_get_palette(task->base.wuss, &ndesktop);
  painting_palette_set(task, desktop, ndesktop);

  rc = paintingdoc_create_filled(params->size, params->bg, &task->doc);
  if (rc != result_OK)
    return rc;
  painting_docs++;

  stageconfig_init(&task->stagecfg);
  task->stagecfg.pasteboard_min = 128;
  task->stagecfg.margin = 1;

  stage = painting_stage_size(task);

  /* room for the toolbox on the left and the option bar below, which share
   * the canvas's outline */
  place          = task_place;
  place.extra.x0 = TB_W + 1;
  place.extra.x1 = LB_W + 1;
  place.extra.y1 = OB_H + 1;
  /* painting_redraw draws the stage and the canvas on it over the whole
   * content area */
  rc = wuss_window_create_at(delegate,
                             &place,
                             SIZE2D(MIN(stage.w, PAINTING_VIEW_W),
                                    MIN(stage.h, PAINTING_VIEW_H)),
                             "Painting",
                             wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                             wuss_NO_BACKDROP,
                             stage,
                             SIZE2D(0, 0),
                             &task->base.window);
  if (rc != result_OK)
    return rc;

  /* toolbars on the canvas's left and bottom edges: they move, stack, hide
   * and close with the canvas, and pass the input focus to it. The toolbox
   * is nothing but icons */
  place = task_place_after(task->base.window);
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
    rc = wuss_window_attach(task->toolbox, task->base.window, wuss_EDGE_LEFT,
                            wuss_ALIGN_TOP, SB_H(task->base.wuss) + 1);
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
    rc = wuss_window_attach(task->optionbar, task->base.window,
                            wuss_EDGE_BOTTOM, wuss_ALIGN_LEFT, 0);
  /* the layers bar on the right edge, dropped as the toolbox is. Its size
   * and extent fit the layers there are */
  if (rc == result_OK)
    rc = wuss_window_create_at(delegate,
                               &place,
                               SIZE2D(LB_W, LB_H(1)),
                               NULL,
                               wuss_WINDOW_NO_TITLEBAR,
                               wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                               SIZE2D(LB_W, LB_H(1)),
                               SIZE2D(0, 0),
                               &task->layerbar);
  if (rc == result_OK)
    rc = wuss_window_attach(task->layerbar, task->base.window,
                            wuss_EDGE_RIGHT, wuss_ALIGN_TOP,
                            SB_H(task->base.wuss) + 1);
  /* the size bar is a pane across the top of the canvas, which is then
   * resized so that the view keeps its size below it */
  if (rc == result_OK)
    rc = wuss_window_create_at(delegate,
                               &place,
                               SIZE2D(SB_W, SB_H(task->base.wuss)),
                               NULL,
                               wuss_WINDOW_NO_TITLEBAR |
                               wuss_WINDOW_NO_OUTLINE  |
                               wuss_WINDOW_NO_REDRAW,
                               wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                               SIZE2D(SB_W, SB_H(task->base.wuss)),
                               SIZE2D(0, 0),
                               &task->sizebar);
  if (rc == result_OK)
    rc = wuss_window_attach_pane(task->sizebar, task->base.window,
                                 wuss_EDGE_TOP);
  if (rc == result_OK)
    rc = wuss_window_resize(task->base.window,
                            SIZE2D(MIN(stage.w, PAINTING_VIEW_W),
                                   MIN(stage.h, PAINTING_VIEW_H)));
  if (rc != result_OK)
    return rc;

  rc = painting_toolbar_icons(task);
  if (rc != result_OK)
    return rc;

  rc = wuss_scaleview_create(&task->scaleview, task->base.wuss,
                             painting_scaleview_choice, task);
  if (rc != result_OK)
    return rc;

  dithermenu_init(&task->dither, task->dithering);
  dithermenu_add_scale(&task->dither, wuss_scaleview_window(task->scaleview));
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_NONE, &task->dither.display);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, PAINTING_MENU_UNDO, "Undo",
                          wuss_MENU_ITEM_DASHED, "F8");
  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, PAINTING_MENU_REDO, "Redo",
                          wuss_MENU_ITEM_NONE, "F9");

  taskapp_save_row(&task->base, task->menu_items, PAINTING_MENU_SAVE,
                   wuss_MENU_ITEM_DASHED);

  WUSS_MENU_ITEM_SHORTCUT(task->edit_items, PAINTING_EDIT_CUT, "Cut",
                          wuss_MENU_ITEM_NONE, "^X");
  WUSS_MENU_ITEM_SHORTCUT(task->edit_items, PAINTING_EDIT_COPY, "Copy",
                          wuss_MENU_ITEM_NONE, "^C");
  WUSS_MENU_ITEM_SHORTCUT(task->edit_items, PAINTING_EDIT_PASTE, "Paste",
                          wuss_MENU_ITEM_DASHED, "^V");
  WUSS_MENU_ITEM_SHORTCUT(task->edit_items, PAINTING_EDIT_FILL, "Fill",
                          wuss_MENU_ITEM_NONE, "F");
  WUSS_MENU_ITEM_SHORTCUT(task->edit_items, PAINTING_EDIT_CLEAR, "Clear",
                          wuss_MENU_ITEM_NONE, "DEL");
  WUSS_MENU_TITLE(task->edit_menu, "Edit", task->edit_items,
                  NELEMS(task->edit_items));
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_EDIT, "Edit",
                      wuss_MENU_ITEM_NONE, &task->edit_menu);

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

  rc = paintingnew_create(&task->resize, task->base.wuss, "Canvas size",
                          "Resize", painting_resize_accept, task);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < paintingdoc_XFORM__COUNT; i++)
    WUSS_MENU_ITEM(task->xform_items, i, painting_xform_names[i],
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->xform_items, PAINTING_XFORM_CROP, "Crop to selection",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM_WINDOW(task->xform_items, PAINTING_XFORM_SIZE, "Canvas size",
                        wuss_MENU_ITEM_PRE_OPEN,
                        paintingnew_window(task->resize));
  WUSS_MENU_TITLE(task->xform_menu, "Transform", task->xform_items,
                  NELEMS(task->xform_items));
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_TRANSFORM, "Transform",
                      wuss_MENU_ITEM_NONE, &task->xform_menu);

  text = filetype_from_riscos(filetype_RISCOS_TEXT);
  rc   = wuss_saveas_create(&task->pal_saveas, task->base.wuss, &text,
                            "palette.hex", painting_palette_save, task);
  if (rc != result_OK)
    return rc;

  WUSS_MENU_ITEM(task->pal_items, PAINTING_PAL_ADD, "Add foreground",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->pal_items, PAINTING_PAL_EDIT, "Edit colour",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->pal_items, PAINTING_PAL_REMOVE, "Remove colour",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->pal_items, PAINTING_PAL_DESKTOP, "Desktop palette",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_ITEM_WINDOW(task->pal_items, PAINTING_PAL_SAVE, "Save palette",
                        wuss_MENU_ITEM_NONE,
                        wuss_saveas_window(task->pal_saveas));
  WUSS_MENU_TITLE(task->pal_menu, "Palette", task->pal_items,
                  NELEMS(task->pal_items));
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_PALETTE, "Palette",
                      wuss_MENU_ITEM_NONE, &task->pal_menu);

  for (i = 0; i < paintingdoc_NOZZLE__COUNT; i++)
    WUSS_MENU_ITEM(task->nozzle_items, i, painting_nozzle_names[i],
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->nozzle_menu, "Nozzle", task->nozzle_items,
                  NELEMS(task->nozzle_items));
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_NOZZLE, "Nozzle",
                      wuss_MENU_ITEM_DASHED, &task->nozzle_menu);
  /* the shortcut, or a click on the row, steps to the next nozzle */
  task->menu_items[PAINTING_MENU_NOZZLE].shortcut = "N";

  WUSS_MENU_ITEM(task->sym_items, PAINTING_SYM_HORIZONTAL, "Horizontal",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->sym_items, PAINTING_SYM_VERTICAL, "Vertical",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->sym_items, PAINTING_SYM_TILED, "Tiled",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_TITLE(task->sym_menu, "Symmetry", task->sym_items,
                  NELEMS(task->sym_items));
  WUSS_MENU_ITEM_MENU(task->menu_items, PAINTING_MENU_SYMMETRY, "Symmetry",
                      wuss_MENU_ITEM_NONE, &task->sym_menu);

  WUSS_MENU_TITLE(task->base.menu, "Painting", task->menu_items,
                  NELEMS(task->menu_items));

  painting_title(task); /* "<Untitled>: Painting" */

  return result_OK;
}

/* the New dialogue: made when first wanted, as a child of the app's task so
 * Quit takes it too, which also clears this */
static struct
{
  paintingnew_t *painting_new;
}
g;

static result_t painting_create(wuss_t *wuss, void **out);
static result_t painting_new_dialogue(wuss_t *wuss);
static wuss_window_fn_t painting_handle;

static const taskapp_desc_t painting_desc =
{
  .name          = "Painting",
  .purpose       = "Basic painting",
  .create        = painting_create,
  .data_load     = (taskapp_data_load_fn_t) painting_load,
  .new_dialogue  = painting_new_dialogue,
  .instance_size = sizeof(painting_task_t),
  .handle        = painting_handle,
  .all_windows   = true, /* the toolbars take clicks too */
  .save_name     = "painting.png",
  .save          = painting_saveas_save
};

/* as painting_create, but with a canvas of the given size and colour */
static result_t painting_create_with(wuss_t                     *wuss,
                                     const paintingnew_params_t *params,
                                     void                      **out)
{
  result_t         rc;
  void            *instance;
  painting_task_t *task;

  rc = taskapp_instance_create(wuss, &painting_desc, &instance);
  if (rc != result_OK)
    return rc;

  task = instance;

  rc = painting_build(task, params);
  if (rc != result_OK)
  {
    wuss_task_destroy(task->base.delegate); /* its QUIT frees the block */
    return rc;
  }

  if (out)
    *out = task;

  return result_OK;
}

static result_t painting_create(wuss_t *wuss, void **out)
{
  paintingnew_params_t params;

  params.size = SIZE2D(PAINTING_CANVAS_W, PAINTING_CANVAS_H);
  params.bg   = colour_rgb(0xFF, 0xFF, 0xFF);
  params.fg   = colour_rgb(0x00, 0x00, 0x00);

  return painting_create_with(wuss, &params, out);
}

/* taskapp_spawn_fn_t: param is the dialogue's paintingnew_params_t */
static result_t painting_spawn_new(wuss_t *wuss, void *param, void **out)
{
  NOT_USED(out);

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
    rc = paintingnew_create(&g.painting_new, wuss, "New Painting", "Create",
                            painting_accept_new, wuss);
    if (rc != result_OK)
      return rc;
  }

  return paintingnew_open(g.painting_new);
}

result_t painting_app_create(wuss_t *wuss)
{
  return taskapp_install(wuss, &painting_desc);
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

/* a line along each edge between canvas pixels, zoomed by z, that falls in
 * bounds */
static void painting_draw_grid(screen_t    *scr,
                               const box_t *bounds,
                               const box_t *canvas,
                               int          z)
{
  colour_t grey;
  box_t    clip;
  int      i, i0, i1;

  grey = colour_rgb(0x80, 0x80, 0x80);
  if (box_intersection(bounds, canvas, &clip))
    return; /* none of the canvas is in bounds */

  i0 = (clip.x0 - canvas->x0 + z - 1) / z;
  i1 = (clip.x1 - 1 - canvas->x0) / z;
  for (i = MAX(i0, 1); i <= i1; i++)
    screen_draw_line(scr, canvas->x0 + i * z, clip.y0,
                     canvas->x0 + i * z, clip.y1 - 1, grey);

  i0 = (clip.y0 - canvas->y0 + z - 1) / z;
  i1 = (clip.y1 - 1 - canvas->y0) / z;
  for (i = MAX(i0, 1); i <= i1; i++)
    screen_draw_line(scr, clip.x0, canvas->y0 + i * z,
                     clip.x1 - 1, canvas->y0 + i * z, grey);
}

static result_t painting_redraw(painting_task_t    *pt,
                                const wuss_event_t *event)
{
  result_t        rc;
  const box_t    *bounds;
  screen_t       *scr;
  int             i, ox, oy, z, c;
  const bitmap_t *bm;
  screen_scale_t  scale;
  int             t, tx, ty;
  box_t           canvas;
  point_t         a, b;
  int             size;

  scr    = event->data.redraw.scr;
  bounds = event->data.redraw.bounds;
  ox     = bounds->x0 - event->data.redraw.scroll.x;
  oy     = bounds->y0 - event->data.redraw.scroll.y;

  painting_redraw_stage(pt, scr, ox, oy);

  /* from here on, draw relative to the canvas */
  ox    += pt->page.x;
  oy    += pt->page.y;
  z      = pt->zoom;
  c      = z / 2; /* a canvas pixel's centre, zoomed */
  bm     = paintingdoc_bitmap(pt->doc);
  scale.xmul = scale.ymul = z;
  scale.xdiv = scale.ydiv = 1;

  /* the canvas, and tiled its copy on every side. the checks show through
   * wherever the canvas is transparent */
  for (t = 0; t < 9; t++)
  {
    if (t != 4 && !(pt->symmetry & (1u << PAINTING_SYM_TILED)))
      continue;

    tx     = ox + (t % 3 - 1) * bm->size.w * z;
    ty     = oy + (t / 3 - 1) * bm->size.h * z;
    canvas = (box_t) BOX_POS_SIZE(tx, ty, bm->size.w * z, bm->size.h * z);
    task_draw_checks(scr, &canvas, POINT(tx, ty));
    rc = screen_copy_bitmap_scaled_dithered(scr, &canvas, bm, &scale,
                                            pt->dithering);
    if (rc != result_OK)
      return rc;
  }

  canvas = (box_t) BOX_POS_SIZE(ox, oy, bm->size.w * z, bm->size.h * z);

  if (z >= PAINTING_GRID_ZOOM)
    painting_draw_grid(scr, bounds, &canvas, z);

  /* ponytail: the overlays are drawn at window resolution through zoomed
   * pixel centres, so zoomed they only approximate the result's edges */
  if (pt->held && painting_is_rubber_band(pt))
  {
    painting_band_ends(pt, &a, &b);
    a    = POINT(a.x * z + c + ox, a.y * z + c + oy);
    b    = POINT(b.x * z + c + ox, b.y * z + c + oy);
    size = pt->size * z;

    if (pt->tool == painting_TOOL_LINE)
      paintingdoc_draw_stroke(scr, a, b, size, painting_stroke_colour(pt));
    else if (pt->tool == painting_TOOL_GRADIENT)
      paintingdoc_draw_stroke(scr, a, b, 1, colour_rgb(0x00, 0x00, 0x00));
    else if (pt->tool == painting_TOOL_SELECT)
      paintingdoc_draw_rect(scr, a, b, 1, 0, colour_rgb(0x00, 0x00, 0x00));
    else if (pt->tool == painting_TOOL_ELLIPSE ||
             pt->tool == painting_TOOL_ELLIPSE_FILL)
      paintingdoc_draw_ellipse(scr, a, b, size,
                               pt->tool == painting_TOOL_ELLIPSE_FILL,
                               painting_stroke_colour(pt));
    else
      paintingdoc_draw_rect(scr, a, b, size, pt->tool == painting_TOOL_RECT_FILL,
                         painting_stroke_colour(pt));
  }

  /* ponytail: a path is previewed as thin, open lines, as the lasso is;
   * draw it at its width, closed, if that misleads */
  if (pt->held && painting_collects_points(pt))
  {
    for (i = 1; i < pt->nlasso; i++)
      screen_draw_line(scr,
                       pt->lasso[i - 1].x * z + c + ox,
                       pt->lasso[i - 1].y * z + c + oy,
                       pt->lasso[i].x * z + c + ox,
                       pt->lasso[i].y * z + c + oy,
                       painting_is_path(pt) ? painting_stroke_colour(pt) :
                                              colour_rgb(0x00, 0x00, 0x00));
  }

  painting_draw_ants(pt, scr, ox, oy);

  return result_OK;
}

/* v / d, rounding down either side of 0 */
static int painting_floor_div(int v, int d)
{
  return v >= 0 ? v / d : -((d - 1 - v) / d);
}

/* tiled, how far p lies off the canvas in whole canvases, else nothing */
static point_t painting_wrap(const painting_task_t *pt, point_t p)
{
  const bitmap_t *bm;

  if (!(pt->symmetry & (1u << PAINTING_SYM_TILED)))
    return POINT(0, 0);

  bm = paintingdoc_bitmap(pt->doc);
  return POINT(painting_floor_div(p.x, bm->size.w) * bm->size.w,
               painting_floor_div(p.y, bm->size.h) * bm->size.h);
}

/* a MOVE always reports the SELECT button, hovering or not, so a stroke in
 * progress is tracked in pt->held instead */
static result_t painting_mouse(painting_task_t    *pt,
                               const wuss_event_t *event)
{
  box_t    dirty = BOX_INIT;
  box_t    old;
  point_t  p;
  colour_t colour;
  int      i;

  p = POINT(painting_floor_div(event->data.mouse.point.x - pt->page.x,
                               pt->zoom),
            painting_floor_div(event->data.mouse.point.y - pt->page.y,
                               pt->zoom));

  /* tiled, a stroke pressed on a copy of the canvas lands on the canvas, and
   * keeps the same shift as it runs on over the edges */
  if (event->data.mouse.action == wuss_MOUSE_DOWN && !pt->held)
    pt->wrap = painting_wrap(pt, p);
  p = POINT(p.x - pt->wrap.x, p.y - pt->wrap.y);

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
    painting_steady_reset(pt, p);

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
      dirty    = painting_fill(pt, p);
      pt->held = 0; /* a click, not a stroke */
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

    case painting_TOOL_PENCIL:
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
      for (i = 0; i < PAINTING_COPIES; i++)
        pt->trail[i].n = 0;
      pt->pot.seed    = rng_xorshift32(&pt->rng); /* a refilled pot */
      pt->pot.load    = pt->dry ? PAINTING_POT_STAMPS / pt->dry : 0;
      pt->pot.left    = pt->pot.load;
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

    /* a freehand stroke follows the stabilised pointer, and has nothing new
     * to draw until that moves. the spray still counts the move */
    if (painting_is_stabilised(pt))
    {
      p = painting_steady(pt, p);
      if (p.x == pt->last.x && p.y == pt->last.y &&
          pt->tool != painting_TOOL_SPRAY)
        return result_OK;
    }

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

    /* a freehand stroke ends where the stabilised pointer had got to */
    if (painting_is_stabilised(pt))
      p = pt->last;

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

      dirty      = painting_path_commit(pt);
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

/* shade Undo and Redo when there is nothing to undo or redo, Clear
 * selection, Cut, Copy, Fill and Clear when nothing is selected and Paste
 * when nothing is copied, also on the menu if it is open (an ADJUST pick
 * keeps it open). A shaded Fill lets its F shortcut through to pick the Fill
 * tool */
static void painting_menu_shade(painting_task_t *pt);

/* shade Add when the palette is full, Edit colour when no swatch is
 * selected and Remove colour then or when one colour is left */
static void painting_palette_shade(painting_task_t *pt)
{
  int disable[PAINTING_PAL_DESKTOP];
  int i;

  disable[PAINTING_PAL_ADD]    = pt->npalette == PAINTING_PALETTE_MAX;
  disable[PAINTING_PAL_EDIT]   = pt->swatches.selected < 0;
  disable[PAINTING_PAL_REMOVE] = pt->swatches.selected < 0 ||
                                 pt->npalette == 1;

  for (i = 0; i < PAINTING_PAL_DESKTOP; i++)
  {
    pt->pal_items[i].flags = disable[i] ? wuss_MENU_ITEM_DISABLED :
                                          wuss_MENU_ITEM_NONE;
    wuss_menu_disable_item_live(pt->base.menu_handle, &pt->pal_menu, i,
                                disable[i]);
  }
}

static void painting_menu_shade(painting_task_t *pt)
{
  int no_undo, no_redo, no_sel, no_clip, i;

  no_undo = !paintingdoc_can_undo(pt->doc);
  no_redo = !paintingdoc_can_redo(pt->doc);
  no_sel  = !paintingdoc_has_selection(pt->doc);
  no_clip = painting_clip.mask == NULL;

  pt->menu_items[PAINTING_MENU_UNDO].flags = wuss_MENU_ITEM_DASHED |
                                          (no_undo ? wuss_MENU_ITEM_DISABLED :
                                                     wuss_MENU_ITEM_NONE);
  pt->menu_items[PAINTING_MENU_REDO].flags = no_redo ?
                                          wuss_MENU_ITEM_DISABLED :
                                          wuss_MENU_ITEM_NONE;
  pt->sel_items[PAINTING_SEL_NONE].flags   = no_sel ?
                                          wuss_MENU_ITEM_DISABLED :
                                          wuss_MENU_ITEM_NONE;
  for (i = PAINTING_EDIT_CUT; i < PAINTING_EDIT__COUNT; i++)
  {
    if (i == PAINTING_EDIT_PASTE)
      continue;
    pt->edit_items[i].flags = no_sel ? wuss_MENU_ITEM_DISABLED :
                                       wuss_MENU_ITEM_NONE;
    wuss_menu_disable_item_live(pt->base.menu_handle, &pt->edit_menu, i,
                                no_sel);
  }
  pt->edit_items[PAINTING_EDIT_PASTE].flags = wuss_MENU_ITEM_DASHED |
                                          (no_clip ? wuss_MENU_ITEM_DISABLED :
                                                     wuss_MENU_ITEM_NONE);
  wuss_menu_disable_item_live(pt->base.menu_handle, &pt->edit_menu,
                              PAINTING_EDIT_PASTE, no_clip);

  wuss_menu_disable_item_live(pt->base.menu_handle, &pt->base.menu,
                              PAINTING_MENU_UNDO, no_undo);
  wuss_menu_disable_item_live(pt->base.menu_handle, &pt->base.menu,
                              PAINTING_MENU_REDO, no_redo);
  wuss_menu_disable_item_live(pt->base.menu_handle, &pt->sel_menu, PAINTING_SEL_NONE,
                              no_sel);
  pt->xform_items[PAINTING_XFORM_CROP].flags = wuss_MENU_ITEM_DASHED |
                                          (no_sel ? wuss_MENU_ITEM_DISABLED :
                                                    wuss_MENU_ITEM_NONE);
  wuss_menu_disable_item_live(pt->base.menu_handle, &pt->xform_menu,
                              PAINTING_XFORM_CROP, no_sel);

  painting_palette_shade(pt);
}

/* redraw dirty after a change that may have resized the canvas from was,
 * refitting the window to the new size if so */
static result_t painting_changed(painting_task_t *pt,
                                 size2d_t         was,
                                 box_t           *dirty)
{
  result_t rc;
  size2d_t now;

  rc  = result_OK;
  now = paintingdoc_bitmap(pt->doc)->size;
  if (now.w != was.w || now.h != was.h)
  {
    rc = painting_fit(pt);
    wuss_window_invalidate_visible(pt->base.window);
  }
  else
  {
    painting_invalidate_box(pt, dirty);
  }

  painting_layers_changed(pt);
  painting_menu_shade(pt);
  painting_title(pt);

  return rc;
}

/* undo, or if redo, redo the last step undone */
static result_t painting_undo(painting_task_t *pt, int redo)
{
  size2d_t was;
  box_t    dirty;

  pt->held = 0; /* abandon any stroke in progress */

  was   = paintingdoc_bitmap(pt->doc)->size;
  dirty = redo ? paintingdoc_redo(pt->doc) : paintingdoc_undo(pt->doc);

  return painting_changed(pt, was, &dirty);
}

/* flip or turn the whole canvas */
static result_t painting_transform(painting_task_t *pt, int xform)
{
  size2d_t was;
  box_t    dirty;

  pt->held = 0; /* abandon any stroke in progress */

  was = paintingdoc_bitmap(pt->doc)->size;
  if (xform == PAINTING_XFORM_SIZE)
    return result_OK; /* the leaf's own row: its dialogue does the work */
  if (xform == PAINTING_XFORM_CROP)
    dirty = paintingdoc_resize(pt->doc, paintingdoc_selection_bounds(pt->doc),
                               pt->bg);
  else
    dirty = paintingdoc_transform(pt->doc, (paintingdoc_xform_t) xform);

  return painting_changed(pt, was, &dirty);
}

/* paintingnew_accept_fn_t: opaque is the painting_task_t. the canvas grows
 * or shrinks about its centre, any new area taking the chosen background */
static result_t painting_resize_accept(const paintingnew_params_t *params,
                                       void                       *opaque)
{
  painting_task_t *pt;
  size2d_t         was;
  box_t            box;
  box_t            dirty;

  pt = opaque;
  if (pt->doc == NULL)
    return result_OK;

  pt->held = 0;

  was    = paintingdoc_bitmap(pt->doc)->size;
  box.x0 = (was.w - params->size.w) / 2;
  box.y0 = (was.h - params->size.h) / 2;
  box.x1 = box.x0 + params->size.w;
  box.y1 = box.y0 + params->size.h;
  dirty  = paintingdoc_resize(pt->doc, box, params->bg);

  return painting_changed(pt, was, &dirty);
}

#define PAINTING_ZOOM_MAX 32

/* Set the zoom. A stroke in progress is abandoned, as its points are in the
 * old zoom's canvas. */
static result_t painting_set_zoom(painting_task_t *pt, int zoom)
{
  zoom = CLAMP(zoom, 1, PAINTING_ZOOM_MAX);
  if (zoom == pt->zoom)
    return result_OK;

  pt->held = 0;
  pt->zoom = zoom;
  wuss_window_invalidate_visible(pt->base.window);
  return painting_fit(pt);
}

/* Toggle a Symmetry row. Tiling changes the stage, so as for a zoom a stroke
 * in progress is abandoned. */
static result_t painting_toggle_symmetry(painting_task_t *pt, int row)
{
  result_t rc;
  point_t  was;

  pt->symmetry ^= 1u << row;
  wuss_menu_tick_item_live(pt->base.menu_handle, &pt->sym_menu, row,
                           (pt->symmetry >> row) & 1);
  if (row != PAINTING_SYM_TILED)
    return result_OK;

  pt->held = 0;
  was      = pt->page;
  wuss_window_invalidate_visible(pt->base.window);
  rc = wuss_window_set_doc(pt->base.window, painting_stage_size(pt));
  if (rc != result_OK)
    return rc;

  /* keep the canvas where it was in the window */
  wuss_window_scroll_by(pt->base.window,
                        POINT(pt->page.x - was.x, pt->page.y - was.y));

  return result_OK;
}

/* Step the zoom by step levels, or to 1:1 if step is 0 */
static result_t painting_zoom(painting_task_t *pt, int step)
{
  static const int zooms[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };

  int i;

  for (i = 0; i < (int) NELEMS(zooms) - 1 && zooms[i] < pt->zoom; i++)
    ;
  if (step > 0 && zooms[i] > pt->zoom)
    i--; /* between levels, as the Scale view leaves it */
  i = step ? CLAMP(i + step, 0, (int) NELEMS(zooms) - 1) : 0;

  return painting_set_zoom(pt, zooms[i]);
}

/* The greatest zoom at which the stage fits avail, or 1 */
static int painting_fit_zoom(painting_task_t *pt, size2d_t avail)
{
  int      keep;
  int      zoom;
  size2d_t size;

  keep = pt->zoom;
  for (zoom = PAINTING_ZOOM_MAX; zoom > 1; zoom--)
  {
    pt->zoom = zoom;
    size     = painting_stage_size(pt);
    if (size.w <= avail.w && size.h <= avail.h)
      break;
  }
  pt->zoom = keep;
  (void) painting_stage_size(pt); /* put pt->page back */

  return zoom;
}

/* The "Scale view" dialogue's choice. The canvas zooms by whole steps only,
 * so a pointer lands on one canvas pixel: a percentage rounds to the nearest
 * one, and below 150% is 1:1. */
static void painting_scaleview_choice(wuss_scaleview_choice_t choice,
                                      int                     percent,
                                      void                   *opaque)
{
  painting_task_t *pt;
  size2d_t         avail;
  box_t            visible;
  box_t            content;
  int              zoom;

  pt = opaque;
  if (pt->base.window == NULL)
    return;

  if (choice == wuss_SCALEVIEW_FIT_SCREEN)
  {
    /* a resize stops at the screen's edge, so start from its top left, then
     * open the window to the stage and place it for its new size */
    wuss_window_get_visible_bounds(pt->base.window, &visible);
    wuss_window_get_content_bounds(pt->base.window, &content);
    wuss_window_move(pt->base.window, POINT(content.x0 - visible.x0,
                                            content.y0 - visible.y0));
    wuss_window_get_max_content(pt->base.window, &avail);
    if (painting_set_zoom(pt, painting_fit_zoom(pt, avail)) != result_OK)
      return;
    if (wuss_window_resize(pt->base.window,
                           painting_stage_size(pt)) == result_OK)
      (void) wuss_window_place(pt->base.window, &task_place);
    return;
  }

  if (choice == wuss_SCALEVIEW_FIT_WINDOW)
  {
    wuss_window_get_content_bounds(pt->base.window, &content);
    avail = SIZE2D(content.x1 - content.x0, content.y1 - content.y0);
    zoom  = painting_fit_zoom(pt, avail);
  }
  else
  {
    zoom = (percent + 50) / 100;
  }

  (void) painting_set_zoom(pt, zoom);
}

/* B, H, E, L, R, X, C, D, N, M, F, G, P, S, O, W and I pick the matching
 * tool, [ and ] step the size down and up, + and - zoom, 1 returns to
 * actual size, Escape drops the selection. Anything else is passed back
 * unclaimed */
static result_t painting_key(painting_task_t *pt, int code)
{
  painting_tool_t tool;
  box_t           box;

  switch (code)
  {
  case 'b': case 'B': tool = painting_TOOL_PENCIL;       break;
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
  case '=': case '+': return painting_zoom(pt, +1);
  case '-':           return painting_zoom(pt, -1);
  case '1':           return painting_zoom(pt, 0);
  case wuss_KEY_ESCAPE:
    box = paintingdoc_selection_bounds(pt->doc);
    paintingdoc_select_none(pt->doc);
    painting_invalidate_box(pt, &box);
    return result_OK;
  default:            return result_WUSS_KEY_UNCLAIMED;
  }

  return painting_set_tool(pt, tool);
}

/* paint the whole selection with colour, as one undo step */
static void painting_selection_fill(painting_task_t *pt, colour_t colour)
{
  box_t box;

  if (!paintingdoc_has_selection(pt->doc))
    return;

  box = paintingdoc_selection_bounds(pt->doc);
  box = paintingdoc_rect(pt->doc, POINT(box.x0, box.y0),
                         POINT(box.x1 - 1, box.y1 - 1), 1, 1, colour);
  painting_invalidate_box(pt, &box);
  painting_title(pt);
}

/* apply an Edit menu row. a paste lands where its pixels were copied from,
 * kept on the canvas, and picks the Move tool to drag it into place */
static void painting_edit(painting_task_t *pt, int op)
{
  size2d_t size;
  point_t  p;
  box_t    old;
  box_t    now;

  switch (op)
  {
  case PAINTING_EDIT_CUT:
  case PAINTING_EDIT_COPY:
    if (paintingdoc_copy(pt->doc, &painting_clip) == result_OK &&
        op == PAINTING_EDIT_CUT)
      painting_selection_fill(pt, pt->bg);
    break;

  case PAINTING_EDIT_PASTE:
    size = paintingdoc_bitmap(pt->doc)->size;
    p.x  = CLAMP(painting_clip.at.x, 0,
                 MAX(size.w - painting_clip.bm.size.w, 0));
    p.y  = CLAMP(painting_clip.at.y, 0,
                 MAX(size.h - painting_clip.bm.size.h, 0));
    old  = paintingdoc_selection_bounds(pt->doc);
    now  = paintingdoc_paste(pt->doc, &painting_clip, p);
    painting_invalidate_box(pt, &old);
    painting_invalidate_box(pt, &now);
    painting_title(pt);
    (void) painting_set_tool(pt, painting_TOOL_MOVE);
    break;

  default:
    painting_selection_fill(pt, op == PAINTING_EDIT_FILL ? pt->fg : pt->bg);
    break;
  }

  painting_menu_shade(pt);
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
  result_t rc;

  painting_menu_shade(pt);
  paintingnew_set_size(pt->resize, paintingdoc_bitmap(pt->doc)->size);

  rc = wuss_scaleview_set_percent(pt->scaleview, pt->zoom * 100);
  if (rc != result_OK)
    return rc;

  wuss_menu_tick_exclusive(&pt->nozzle_menu, pt->nozzle);

  return wuss_menu_open_at_pointer(pt->base.delegate, &pt->base.menu,
                                   &pt->base.menu_handle);
}

static result_t painting_handle(wuss_window_t      *window,
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
    if (window == pt->layerbar)
      return painting_layerbar_redraw(pt, event);
    return painting_redraw(pt, event);

  case wuss_EVENT_MOUSE:
    if (event->data.mouse.action == wuss_MOUSE_DOWN &&
        (event->data.mouse.button & wuss_BUTTON_MENU))
      return window == pt->base.window ? painting_menu_open(pt) : result_OK;

    if (window == pt->optionbar)
      return painting_optionbar_mouse(pt, event);
    if (window == pt->layerbar)
    {
      result_t rc;
      box_t    dirty;

      rc = painting_layerbar_mouse(pt, event, &dirty);
      if (rc == result_OK && !box_is_empty(&dirty))
        rc = painting_changed(pt, paintingdoc_bitmap(pt->doc)->size, &dirty);
      return rc;
    }
    if (window == pt->toolbox || window == pt->sizebar)
      return result_OK; /* between the buttons */
    return painting_mouse(pt, event);

  case wuss_EVENT_ICON:
    return painting_toolbar_icon(pt, event);

  case wuss_EVENT_RELAYOUT:
    if (window == pt->base.window && pt->sizebar != NULL)
      return painting_sizebar_relayout(pt);
    return result_OK;

  case wuss_EVENT_KEY:
  {
    result_t rc;

    painting_menu_shade(pt); /* the selection tools leave the shading stale */
    if (!task_key_is_plain(pt->base.delegate, &pt->base.menu, event, &rc))
      return rc;

    return painting_key(pt, event->data.key.code);
  }

  case wuss_EVENT_MENU_SELECT:
    if (event->data.menu_select.menu == &pt->edit_menu)
    {
      painting_edit(pt, event->data.menu_select.index);
      return result_OK;
    }
    if (event->data.menu_select.menu == &pt->sel_menu)
      return painting_selection_op(pt, event->data.menu_select.index);
    if (event->data.menu_select.menu == &pt->xform_menu)
      return painting_transform(pt, event->data.menu_select.index);
    if (event->data.menu_select.menu == &pt->pal_menu)
    {
      result_t rc;

      rc = painting_palette_op(pt, event->data.menu_select.index);
      painting_palette_shade(pt);
      return rc;
    }
    if (event->data.menu_select.menu == &pt->nozzle_menu)
    {
      painting_set_nozzle(pt, event->data.menu_select.index);
      return result_OK;
    }
    if (event->data.menu_select.menu == &pt->sym_menu)
      return painting_toggle_symmetry(pt, event->data.menu_select.index);
    if (event->data.menu_select.menu == &pt->dither.menu)
    {
      pt->dithering = dithermenu_select(&pt->dither, pt->base.menu_handle,
                                        event->data.menu_select.index);
      wuss_window_invalidate_visible(pt->base.window);
      return result_OK;
    }
    if (event->data.menu_select.index == PAINTING_MENU_UNDO ||
        event->data.menu_select.index == PAINTING_MENU_REDO)
      return painting_undo(pt, event->data.menu_select.index ==
                               PAINTING_MENU_REDO);
    if (event->data.menu_select.index == PAINTING_MENU_NOZZLE)
    {
      painting_set_nozzle(pt, (pt->nozzle + 1) % paintingdoc_NOZZLE__COUNT);
      return result_OK;
    }
    return result_OK;

  case wuss_EVENT_MESSAGE:
    if (pt->base.window == NULL || window != pt->base.window)
      return result_OK;
    return taskapp_window_data_load(pt->base.wuss, pt->base.delegate, pt,
                                    (taskapp_data_load_fn_t) painting_load,
                                    event->data.message);

  case wuss_EVENT_IDLE:
    if (pt->base.window == NULL)
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
      pt->ants_tick  = 0;
      pt->ants_phase = (pt->ants_phase + 1) & 7;
      painting_invalidate_ants(pt);
    }
    return result_OK;

  case wuss_EVENT_CLOSE_ADJUST:
    if (window != pt->base.window)
      return result_OK;
    return task_close_adjust(pt->base.wuss, pt->base.delegate, window,
                             pt->path, event);

  case wuss_EVENT_CLOSE:
    /* the toolbars and pane belong to the canvas, so they close with it */
    pt->base.window = NULL;
    pt->toolbox     = NULL;
    pt->optionbar   = NULL;
    pt->sizebar     = NULL;
    pt->layerbar    = NULL;
    /* the picker is on a task of its own, so would outlive the canvas */
    wuss_colourpicker_destroy(pt->picker);
    pt->picker      = NULL;
    return result_OK;

  case wuss_EVENT_QUIT:
    wuss_colourpicker_destroy(pt->picker);
    wuss_scaleview_destroy(pt->scaleview);
    paintingnew_destroy(pt->resize);
    wuss_saveas_destroy(pt->pal_saveas);
    free(pt->lasso);
    if (pt->doc != NULL && --painting_docs == 0)
      paintingdoc_clip_free(&painting_clip);
    paintingdoc_destroy(pt->doc);
    return result_OK;

  default:
    return result_OK;
  }
}

#endif /* WUSS_APP */
