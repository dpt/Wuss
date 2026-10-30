/* wuss/tasks/painting/painting-bars.c -- Painting's toolbars */

#ifdef WUSS_APP

#include <stdio.h>
#include <string.h>

#include "base/utils.h"
#include "framebuf/pattern.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/box.h"

#include "painting.h"
#include "painting-bars.h"
#include "../common/common.h"

/* the tools' images, in the task's icon pool under
 * resources/wuss-tasks/painting/iconpool */
static const char *painting_tool_names[painting_TOOL__COUNT] =
{
  "draw",
  "brush",
  "wobble",
  "erase",
  "line",
  "rectangle",
  "rectangle-fill",
  "ellipse",
  "ellipse-fill",
  "path",
  "path-fill",
  "fill",
  "gradient",
  "spray",
  "select",
  "lasso",
  "wand",
  "pick",
  "move"
};

/* the paintbrush's nozzle rows, indexed by paintingdoc_nozzle_t */
const char *const painting_nozzle_names[paintingdoc_NOZZLE__COUNT] =
{
  "Round", "Square", "Flat", "Tall", "Slash", "Backslash", "Bristles"
};

/* the size bar's settings: the label, then for a slider its range and its
 * readout's format (NULL for "%d"). The nozzle is a button instead */
static const struct
{
  const char *label;
  int         min, max;
  const char *fmt;
}
painting_settings[painting_SETTING__COUNT] =
{
  [painting_SETTING_SIZE]      = { "Size", PAINTING_SIZE_MIN, PAINTING_SIZE_MAX,
                                   NULL },
  [painting_SETTING_NOZZLE]    = { "Nozzle", 0, 0, NULL },
  [painting_SETTING_DENSITY]   = { "Density", 1, 20, "%d%%" },
  [painting_SETTING_RATE]      = { "Rate", 20, 100, "%d%%" },
  [painting_SETTING_EDGE]      = { "Wobble", 0, 200, "%d%%" },
  [painting_SETTING_WANDER]    = { "Wander", 0, 200, "%d%%" },
  [painting_SETTING_TOLERANCE] = { "Tolerance", 0, 255, NULL }
};

#define SETTING(s) (1u << painting_SETTING_##s)

/* the settings each tool shows on the size bar. a filled shape has no
 * outline, so no size */
static const unsigned int painting_tool_settings[painting_TOOL__COUNT] =
{
  [painting_TOOL_BRUSH]      = SETTING(SIZE),
  [painting_TOOL_PAINTBRUSH] = SETTING(SIZE) | SETTING(NOZZLE),
  [painting_TOOL_WOBBLE]     = SETTING(SIZE) | SETTING(EDGE) |
                               SETTING(WANDER),
  [painting_TOOL_ERASER]     = SETTING(SIZE),
  [painting_TOOL_LINE]       = SETTING(SIZE),
  [painting_TOOL_RECT]       = SETTING(SIZE),
  [painting_TOOL_ELLIPSE]    = SETTING(SIZE),
  [painting_TOOL_PATH]       = SETTING(SIZE),
  [painting_TOOL_SPRAY]      = SETTING(SIZE) | SETTING(DENSITY) |
                               SETTING(RATE),
  [painting_TOOL_WAND]       = SETTING(TOLERANCE)
};

#undef SETTING

/* where each tool's button sits in the toolbox: its column, row and group.
 * outline shapes are on the left with their filled forms beside them */
static const struct
{
  int col, row, group;
}
painting_tool_cells[painting_TOOL__COUNT] =
{
  [painting_TOOL_SELECT]       = { 0, 0, 0 },
  [painting_TOOL_LASSO]        = { 1, 0, 0 },
  [painting_TOOL_WAND]         = { 0, 1, 0 },
  [painting_TOOL_PICK]         = { 1, 1, 0 },
  [painting_TOOL_MOVE]         = { 0, 2, 0 },

  [painting_TOOL_BRUSH]        = { 0, 3, 1 },
  [painting_TOOL_PAINTBRUSH]   = { 1, 3, 1 },
  [painting_TOOL_WOBBLE]       = { 0, 4, 1 },
  [painting_TOOL_SPRAY]        = { 1, 4, 1 },
  [painting_TOOL_ERASER]       = { 0, 5, 1 },
  [painting_TOOL_FILL]         = { 1, 5, 1 },
  [painting_TOOL_GRADIENT]     = { 0, 6, 1 },

  [painting_TOOL_RECT]         = { 0, 7, 2 },
  [painting_TOOL_RECT_FILL]    = { 1, 7, 2 },
  [painting_TOOL_ELLIPSE]      = { 0, 8, 2 },
  [painting_TOOL_ELLIPSE_FILL] = { 1, 8, 2 },
  [painting_TOOL_PATH]         = { 0, 9, 2 },
  [painting_TOOL_PATH_FILL]    = { 1, 9, 2 },
  [painting_TOOL_LINE]         = { 0, 10, 2 }
};

/* the tool's button box in toolbox content space */
static box_t painting_tool_box(int i)
{
  return (box_t) BOX_POS_SIZE(TB_MARGIN +
                              painting_tool_cells[i].col * TB_TOOL_CELL,
                              TB_MARGIN +
                              painting_tool_cells[i].row * TB_TOOL_CELL +
                              painting_tool_cells[i].group * TB_GROUP_GAP,
                              TB_TOOL_SIZE, TB_TOOL_SIZE);
}

/* ----------------------------------------------------------------------- */

/* where a slider setting's value lives */
static int *painting_setting_value(painting_task_t *pt, painting_setting_t s)
{
  switch (s)
  {
  case painting_SETTING_DENSITY:   return &pt->density;
  case painting_SETTING_RATE:      return &pt->rate;
  case painting_SETTING_EDGE:      return &pt->edge;
  case painting_SETTING_WANDER:    return &pt->wander;
  case painting_SETTING_TOLERANCE: return &pt->tolerance;
  default:                         return &pt->size;
  }
}

/* replace the size bar's icons with the current tool's settings, or a note
 * that it has none */
static result_t painting_sizebar_build(painting_task_t *pt)
{
  result_t         rc;
  int              i;
  unsigned int     settings;
  wuss_icon_spec_t specs[PAINTING_SIZEBAR_ICONS];
  int              x, n, s;
  int              at[painting_SETTING__COUNT]; /* each setting's control */
  const char      *label;
  int              w;
  box_t            control;
  char             widest[WUSS_SLIDER_ROW_BUF];
  int              rw;
  wuss_icon_t     *icons[PAINTING_SIZEBAR_ICONS];

  for (i = 0; i < pt->nsizebar_icons; i++)
    wuss_icon_delete(pt->sizebar, pt->sizebar_icons[i]);
  pt->nsizebar_icons = 0;
  pt->nozzle_icon    = NULL;

  settings = painting_tool_settings[pt->tool];
  memset(specs, 0, sizeof(specs));
  x = SB_MARGIN;
  n = 0;

  for (s = 0; s < painting_SETTING__COUNT; s++)
  {
    if ((settings & (1u << s)) == 0)
      continue;

    /* a glyph's ink can overrun its advance, as Density's y does, so pad the
     * label's box to have it all cleared when a tool change deletes it */
    label = painting_settings[s].label;
    w     = task_text_width(pt->wuss, label) + 2;

    wuss_icon_spec_label(&specs[n++],
                         (box_t) BOX_POS_SIZE(x, SB_MARGIN, w, SB_ROW_H),
                         label, 0);
    x += w + SB_LABEL_GAP;

    at[s] = n;
    if (s == painting_SETTING_NOZZLE)
    {
      control = (box_t) BOX_POS_SIZE(x, SB_MARGIN, SB_NOZZLE_W, SB_ROW_H);
      wuss_icon_spec_action(&specs[n++], control,
                            painting_nozzle_names[pt->nozzle], 0);
      x += SB_NOZZLE_W;
    }
    else
    {
      /* size the readout to its widest value, the max, so a short one
       * leaves no gap */
      snprintf(widest, sizeof(widest),
               painting_settings[s].fmt ? painting_settings[s].fmt : "%d",
               painting_settings[s].max);
      rw = task_text_width(pt->wuss, widest) + 2;

      wuss_icon_spec_slider_row(&specs[n], &specs[n + 1],
                                (box_t) BOX_POS_SIZE(x, SB_MARGIN,
                                                     SB_SLIDER_W, SB_ROW_H),
                                (box_t) BOX_POS_SIZE(x + SB_SLIDER_W + 2,
                                                     SB_MARGIN,
                                                     rw, SB_ROW_H),
                                wuss_SLIDER_HORIZONTAL,
                                painting_settings[s].min,
                                painting_settings[s].max,
                                *painting_setting_value(pt,
                                                        (painting_setting_t) s),
                                painting_settings[s].fmt, 0,
                                pt->row_text[s], sizeof(pt->row_text[s]));
      n += 2;
      x += SB_SLIDER_W + 2 + rw;
    }
    x += SB_SETTING_GAP;
  }

  if (n == 0)
    wuss_icon_spec_label(&specs[n++],
                         (box_t) BOX_POS_SIZE(SB_MARGIN, SB_MARGIN,
                                              SB_W - 2 * SB_MARGIN, SB_ROW_H),
                         "No settings", 0);

  rc = wuss_icon_create_array(pt->sizebar, specs, n, icons);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < n; i++)
    pt->sizebar_icons[i] = icons[i];
  pt->nsizebar_icons = n;

  for (s = 0; s < painting_SETTING__COUNT; s++)
  {
    if ((settings & (1u << s)) == 0)
      continue;

    if (s == painting_SETTING_NOZZLE)
      pt->nozzle_icon = icons[at[s]];
    else
      wuss_slider_row_bind(&pt->rows[s], icons[at[s]], icons[at[s] + 1],
                           painting_settings[s].fmt,
                           painting_settings[s].min,
                           painting_settings[s].max, 0);
  }

  return result_OK;
}

/* the tool buttons on the toolbox and the tool's settings on the size bar */
result_t painting_toolbar_icons(painting_task_t *pt)
{
  result_t         rc;
  wuss_icon_spec_t specs[painting_TOOL__COUNT];
  wuss_icon_t     *icons[painting_TOOL__COUNT];
  int              i, n;

  rc = wuss_task_iconpool_load_resource(pt->delegate,
                                        wuss_get_resources(pt->wuss));
  if (rc != result_OK)
    return rc;

  memset(specs, 0, sizeof(specs));
  n = 0;

  for (i = 0; i < painting_TOOL__COUNT; i++)
  {
    box_t cell;

    cell = painting_tool_box(i);

    specs[n].bbox = cell;
    specs[n].type             = wuss_ICON_TYPE_BITMAP;
    specs[n].flags            = wuss_ICON_FLAGS_INTERACTIVE;
    specs[n].u.bitmap.border  = wuss_ICON_BORDER_RIDGE;
    specs[n].u.bitmap.group   = 1;
    specs[n].bg               = wuss_COLOUR_BUTTON_PRESSED; /* the latched fill */
    specs[n].u.bitmap.name    = painting_tool_names[i];
    n++;
  }

  rc = wuss_icon_create_array(pt->toolbox, specs, n, icons);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < painting_TOOL__COUNT; i++)
    pt->tool_icons[i] = icons[i];

  wuss_icon_set_selected(pt->toolbox, pt->tool_icons[pt->tool], 1);

  return painting_sizebar_build(pt);
}

/* ----------------------------------------------------------------------- */

/* a colour chip at (x, y): a transparent colour shows as the checks. on a
 * paletted screen a colour the palette lacks is a Bayer mix of two entries,
 * as wuss_swatches_draw does */
static void painting_draw_chip(screen_t *scr, int x, int y, colour_t colour)
{
  box_t     chip;
  pattern_t pat;

  chip = (box_t) BOX_POS_SIZE(x, y, OB_CHIP, OB_CHIP);

  /* ponytail: a translucent colour is drawn solid; blit a 1x1 bgra8888
   * bitmap tiled over the checks if a tint is wanted */
  if (colour_get_alpha(&colour) == 0)
  {
    task_draw_checks(scr, &chip, POINT(x, y));
  }
  else if (scr->palette != NULL)
  {
    pat        = pattern_from_colour(scr->palette,
                                     pixelfmt_paletted_nentries(scr->format),
                                     colour);
    pat.origin = POINT(x, y);
    screen_fill_pattern(scr, &chip, &pat);
  }
  else
  {
    screen_fill_rect(scr, x, y, SIZE2D(OB_CHIP, OB_CHIP), colour);
  }
}

/* the background colour behind the foreground colour and the swatch row. The
 * icons draw over the window's own backdrop */
result_t painting_optionbar_redraw(painting_task_t    *pt,
                                   const wuss_event_t *event)
{
  screen_t    *scr;
  const box_t *bounds;
  colour_t     black;
  int          ox, oy;

  scr    = event->data.redraw.scr;
  bounds = event->data.redraw.bounds;
  ox     = bounds->x0 - event->data.redraw.scroll.x;
  oy     = bounds->y0 - event->data.redraw.scroll.y;
  black  = colour_rgb(0x00, 0x00, 0x00);

  wuss_swatches_draw(&pt->swatches, event);

  painting_draw_chip(scr, ox + OB_MARGIN + OB_CHIP_STEP,
                     oy + OB_MARGIN + OB_CHIP_STEP, pt->bg);
  screen_draw_rect(scr, ox + OB_MARGIN + OB_CHIP_STEP,
                   oy + OB_MARGIN + OB_CHIP_STEP,
                   SIZE2D(OB_CHIP, OB_CHIP), black);
  painting_draw_chip(scr, ox + OB_MARGIN, oy + OB_MARGIN, pt->fg);
  screen_draw_rect(scr, ox + OB_MARGIN, oy + OB_MARGIN,
                   SIZE2D(OB_CHIP, OB_CHIP), black);

  return result_OK;
}

/* Select sets the foreground colour from the swatch under the pointer, Adjust
 * the background colour */
result_t painting_optionbar_mouse(painting_task_t    *pt,
                                  const wuss_event_t *event)
{
  int i;

  if (event->data.mouse.action != wuss_MOUSE_DOWN)
    return result_OK;

  i = wuss_swatches_hit_test(&pt->swatches, event->data.mouse.point);
  if (i < 0)
    return result_OK;

  if (event->data.mouse.button & wuss_BUTTON_SELECT)
    pt->fg = pt->swatches.colours[i];
  else if (event->data.mouse.button & wuss_BUTTON_ADJUST)
    pt->bg = pt->swatches.colours[i];
  else
    return result_OK;

  wuss_window_invalidate_visible(pt->optionbar);

  return result_OK;
}

/* switch tool, showing its settings on the size bar */
result_t painting_set_tool(painting_task_t *pt, painting_tool_t tool)
{
  unsigned int was;

  was      = painting_tool_settings[pt->tool];
  pt->tool = tool;
  wuss_icon_set_selected(pt->toolbox, pt->tool_icons[tool], 1);

  if (pt->sizebar == NULL || painting_tool_settings[tool] == was)
    return result_OK;

  return painting_sizebar_build(pt);
}

/* pick the paintbrush's nozzle, keeping the size bar button and the menu, if
 * open, in step */
void painting_set_nozzle(painting_task_t *pt, int nozzle)
{
  pt->nozzle = (paintingdoc_nozzle_t) nozzle;

  if (pt->nozzle_icon != NULL)
    (void) wuss_icon_set_text(pt->sizebar, pt->nozzle_icon,
                              painting_nozzle_names[pt->nozzle]);
  wuss_menu_tick_exclusive_live(pt->menu_handle, &pt->nozzle_menu,
                                pt->nozzle);
}

result_t painting_toolbar_icon(painting_task_t    *pt,
                               const wuss_event_t *event)
{
  unsigned int settings;
  int          s, value;
  wuss_icon_t *icon;
  int          i;

  /* only the shown sliders' rows are live: the others' icons are gone */
  settings = painting_tool_settings[pt->tool];
  for (s = 0; s < painting_SETTING__COUNT; s++)
  {
    if (s == painting_SETTING_NOZZLE || (settings & (1u << s)) == 0)
      continue;

    if (wuss_slider_row_event(pt->sizebar, &pt->rows[s], event, &value))
    {
      *painting_setting_value(pt, (painting_setting_t) s) = value;
      return result_OK;
    }
  }

  /* a radio, option or button acts on MOUSE_UP */
  if (event->data.icon.action != wuss_MOUSE_UP)
    return result_OK;

  icon = event->data.icon.icon;

  /* Select steps to the next nozzle, Adjust back to the previous one */
  if (icon != NULL && icon == pt->nozzle_icon)
  {
    painting_set_nozzle(pt, (pt->nozzle +
                             ((event->data.icon.button & wuss_BUTTON_ADJUST) ?
                              paintingdoc_NOZZLE__COUNT - 1 : 1)) %
                            paintingdoc_NOZZLE__COUNT);
    return result_OK;
  }

  for (i = 0; i < painting_TOOL__COUNT; i++)
  {
    if (icon == pt->tool_icons[i])
      return painting_set_tool(pt, (painting_tool_t) i);
  }

  return result_OK;
}

/* step the brush size by delta, keeping the size bar slider, if shown, in
 * step */
void painting_step_size(painting_task_t *pt, int delta)
{
  pt->size = CLAMP(pt->size + delta, PAINTING_SIZE_MIN, PAINTING_SIZE_MAX);
  if (painting_tool_settings[pt->tool] & (1u << painting_SETTING_SIZE))
    (void) wuss_slider_row_set(pt->sizebar, &pt->rows[painting_SETTING_SIZE],
                               pt->size);
}

#endif /* WUSS_APP */
