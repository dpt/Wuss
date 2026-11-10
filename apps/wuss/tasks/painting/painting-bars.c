/* wuss/tasks/painting/painting-bars.c -- Painting's toolbars */

#ifdef WUSS_APP

#include <stdio.h>
#include <string.h>

#include "base/utils.h"
#include "framebuf/palettes.h"
#include "framebuf/pattern.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "utils/array.h"

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
  [painting_SETTING_DRY]       = { "Dry", 0, 100, "%d%%" },
  [painting_SETTING_DENSITY]   = { "Density", 1, 20, "%d%%" },
  [painting_SETTING_RATE]      = { "Rate", 20, 100, "%d%%" },
  [painting_SETTING_EDGE]      = { "Wobble", 0, 200, "%d%%" },
  [painting_SETTING_WANDER]    = { "Wander", 0, 200, "%d%%" },
  [painting_SETTING_TOLERANCE] = { "Tolerance", 0, 255, NULL },
  [painting_SETTING_STABILISE] = { "Stabilise", 0, PAINTING_STABILISE_MAX,
                                   NULL },
  [painting_SETTING_PERFECT]   = { "Pixel-perfect", 0, 0, NULL }
};

/* whether the setting is a slider, rather than a button or an option */
#define IS_SLIDER(s) ((s) != painting_SETTING_NOZZLE && \
                      (s) != painting_SETTING_PERFECT)

#define SETTING(s) (1u << painting_SETTING_##s)

/* the settings each tool shows on the size bar. a filled shape has no
 * outline, so no size */
static const unsigned int painting_tool_settings[painting_TOOL__COUNT] =
{
  [painting_TOOL_PENCIL]     = SETTING(SIZE) | SETTING(NOZZLE) |
                               SETTING(STABILISE) | SETTING(PERFECT),
  [painting_TOOL_PAINTBRUSH] = SETTING(SIZE) | SETTING(NOZZLE) |
                               SETTING(DRY) | SETTING(STABILISE),
  [painting_TOOL_WOBBLE]     = SETTING(SIZE) | SETTING(EDGE) |
                               SETTING(WANDER) | SETTING(STABILISE),
  [painting_TOOL_ERASER]     = SETTING(SIZE) | SETTING(STABILISE),
  [painting_TOOL_LINE]       = SETTING(SIZE),
  [painting_TOOL_RECT]       = SETTING(SIZE),
  [painting_TOOL_ELLIPSE]    = SETTING(SIZE),
  [painting_TOOL_PATH]       = SETTING(SIZE) | SETTING(STABILISE),
  [painting_TOOL_PATH_FILL]  = SETTING(STABILISE),
  [painting_TOOL_SPRAY]      = SETTING(SIZE) | SETTING(DENSITY) |
                               SETTING(RATE) | SETTING(STABILISE),
  [painting_TOOL_LASSO]      = SETTING(STABILISE),
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

  [painting_TOOL_PENCIL]       = { 0, 3, 1 },
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
  case painting_SETTING_DRY:       return &pt->dry;
  case painting_SETTING_DENSITY:   return &pt->density;
  case painting_SETTING_RATE:      return &pt->rate;
  case painting_SETTING_EDGE:      return &pt->edge;
  case painting_SETTING_WANDER:    return &pt->wander;
  case painting_SETTING_TOLERANCE: return &pt->tolerance;
  case painting_SETTING_STABILISE: return &pt->stabilise;
  default:                         return &pt->size;
  }
}

/* the pot gauge's images, indexed by level: empty to full */
static const char *painting_pot_names[PAINTING_POT_LEVELS] =
{
  "pot-00", "pot-01", "pot-02", "pot-03", "pot-04", "pot-05",
  "pot-06", "pot-07", "pot-08", "pot-09", "pot-10"
};

/* the paintbrush pot's level, 0 (dry) to PAINTING_POT_LEVELS - 1 (full),
 * rounded up so only a dry pot shows empty. full until a stroke starts, and
 * always if it never runs dry */
static int painting_pot_level(const painting_task_t *pt)
{
  if (pt->pot.load <= 0)
    return PAINTING_POT_LEVELS - 1;
  if (pt->pot.left <= 0)
    return 0;

  return (pt->pot.left * (PAINTING_POT_LEVELS - 1) + pt->pot.load - 1) /
         pt->pot.load;
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
  int              row_h;
  int              line_h;
  int              y;
  int              nozzle_w;
  int              pot_at;
  int              level;
  wuss_icon_t     *icons[PAINTING_SIZEBAR_ICONS];

  row_h    = SB_ROW_H(pt->base.wuss);
  line_h   = SB_LINE_H(pt->base.wuss);
  y        = SB_MARGIN + (line_h - row_h) / 2; /* centred in the line */
  /* the nozzle button fits its widest name */
  nozzle_w = task_button_width(pt->base.wuss, "Backslash",
                               wuss_STD_SECONDARY_BUTTON_BORDER);

  for (i = 0; i < pt->nsizebar_icons; i++)
    wuss_icon_delete(pt->sizebar, pt->sizebar_icons[i]);
  pt->nsizebar_icons = 0;
  pt->nozzle_icon    = NULL;
  pt->perfect_icon   = NULL;
  pt->pot_icons[0]   = NULL;

  settings = painting_tool_settings[pt->tool];
  memset(specs, 0, sizeof(specs));
  x      = SB_MARGIN;
  n      = 0;
  pot_at = -1;

  for (s = 0; s < painting_SETTING__COUNT; s++)
  {
    if ((settings & (1u << s)) == 0)
      continue;

    /* an option carries its own label, after its tick box */
    if (s == painting_SETTING_PERFECT)
    {
      label = painting_settings[s].label;
      w     = wuss_STD_GLYPH_SIZE + task_text_width(pt->base.wuss, label) + 8;
      at[s] = n;
      wuss_icon_spec_option(&specs[n++],
                            (box_t) BOX_POS_SIZE(x, SB_MARGIN, w, line_h),
                            label);
      x += w + SB_SETTING_GAP;
      continue;
    }

    /* a glyph's ink can overrun its advance, as Density's y does, so pad the
     * label's box to have it all cleared when a tool change deletes it */
    label = painting_settings[s].label;
    w     = task_text_width(pt->base.wuss, label) + 2;

    wuss_icon_spec_label(&specs[n++],
                         (box_t) BOX_POS_SIZE(x, y, w, row_h),
                         label, 0);
    x += w + SB_LABEL_GAP;

    at[s] = n;
    if (s == painting_SETTING_NOZZLE)
    {
      control = (box_t) BOX_POS_SIZE(x, y, nozzle_w, row_h);
      wuss_icon_spec_action(&specs[n++], control,
                            painting_nozzle_names[pt->nozzle], 0);
      x += nozzle_w;
    }
    else
    {
      /* size the readout to its widest value, the max, so a short one
       * leaves no gap */
      snprintf(widest, sizeof(widest),
               painting_settings[s].fmt ? painting_settings[s].fmt : "%d",
               painting_settings[s].max);
      rw = task_text_width(pt->base.wuss, widest) + 2;

      wuss_icon_spec_slider_row(&specs[n], &specs[n + 1],
                                (box_t) BOX_POS_SIZE(x, y,
                                                     SB_SLIDER_W, row_h),
                                (box_t) BOX_POS_SIZE(x + SB_SLIDER_W + 2, y,
                                                     rw, row_h),
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

    if (s == painting_SETTING_DRY)
    {
      /* the pot gauge after the readout: every level's image in one box,
       * all but the current level hidden */
      pot_at  = n;
      level   = painting_pot_level(pt);
      control = (box_t) BOX_POS_SIZE(x + SB_LABEL_GAP,
                                     SB_MARGIN + (line_h - SB_POT) / 2,
                                     SB_POT, SB_POT);
      for (i = 0; i < PAINTING_POT_LEVELS; i++)
      {
        specs[n].bbox            = control;
        specs[n].type            = wuss_ICON_TYPE_BITMAP;
        specs[n].flags           = i == level ? 0 : wuss_ICON_FLAGS_HIDDEN;
        specs[n++].u.bitmap.name = painting_pot_names[i];
      }
      pt->pot_shown = level;
      x += SB_LABEL_GAP + SB_POT;
    }
    x += SB_SETTING_GAP;
  }

  if (n == 0)
    wuss_icon_spec_label(&specs[n++],
                         (box_t) BOX_POS_SIZE(SB_MARGIN, y,
                                              SB_W - 2 * SB_MARGIN, row_h),
                         "No settings", 0);

  rc = wuss_icon_create_array(pt->sizebar, specs, n, icons);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < n; i++)
    pt->sizebar_icons[i] = icons[i];
  pt->nsizebar_icons = n;

  /* the new settings start from the left, not part-scrolled away */
  wuss_window_set_scroll(pt->sizebar, POINT(0, 0));

  if (pot_at >= 0)
    for (i = 0; i < PAINTING_POT_LEVELS; i++)
      pt->pot_icons[i] = icons[pot_at + i];

  for (s = 0; s < painting_SETTING__COUNT; s++)
  {
    if ((settings & (1u << s)) == 0)
      continue;

    if (s == painting_SETTING_NOZZLE)
    {
      pt->nozzle_icon = icons[at[s]];
    }
    else if (s == painting_SETTING_PERFECT)
    {
      pt->perfect_icon = icons[at[s]];
      wuss_icon_set_selected(pt->sizebar, pt->perfect_icon, pt->perfect);
    }
    else
    {
      wuss_slider_row_bind(&pt->rows[s], icons[at[s]], icons[at[s] + 1],
                           painting_settings[s].fmt,
                           painting_settings[s].min,
                           painting_settings[s].max, 0);
    }
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

  rc = wuss_task_iconpool_load_resource(pt->base.delegate,
                                        wuss_get_resources(pt->base.wuss));
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

/* wuss_colourpicker_changed_fn_t: the edited chip takes the colour */
static void painting_picker_changed(colour_t colour, void *opaque)
{
  painting_task_t *pt;

  pt = opaque;

  *pt->picking = colour;
  if (pt->optionbar != NULL)
    wuss_window_invalidate_visible(pt->optionbar);
}

/* open the colour picker on the chip at colour */
static result_t painting_pick_chip(painting_task_t *pt, colour_t *colour)
{
  result_t rc;

  if (pt->picker == NULL)
  {
    rc = wuss_colourpicker_create(&pt->picker, pt->base.wuss, NULL,
                                  painting_picker_changed, pt);
    if (rc != result_OK)
      return rc;
  }

  pt->picking = colour;

  return wuss_colourpicker_open(pt->picker, *colour);
}

/* ADJUST on the foreground or background chip opens the colour picker on
 * it. SELECT on a swatch sets the foreground colour from it, ADJUST the
 * background colour */
result_t painting_optionbar_mouse(painting_task_t    *pt,
                                  const wuss_event_t *event)
{
  box_t   chip;
  point_t p;
  int     i;

  if (event->data.mouse.action != wuss_MOUSE_DOWN)
    return result_OK;

  /* the fg chip overlaps the bg chip, so is tested first */
  chip = (box_t) BOX_POS_SIZE(OB_MARGIN, OB_MARGIN, OB_CHIP, OB_CHIP);
  p    = event->data.mouse.point;
  if (event->data.mouse.button & wuss_BUTTON_ADJUST)
  {
    if (box_contains_point(&chip, p.x, p.y))
      return painting_pick_chip(pt, &pt->fg);
    box_translated(&chip, OB_CHIP_STEP, OB_CHIP_STEP, &chip);
    if (box_contains_point(&chip, p.x, p.y))
      return painting_pick_chip(pt, &pt->bg);
  }

  i = wuss_swatches_hit_test(&pt->swatches, p);
  if (i < 0)
    return result_OK;

  if (event->data.mouse.button & wuss_BUTTON_SELECT)
  {
    pt->fg                = pt->swatches.colours[i];
    pt->swatches.selected = i;
  }
  else if (event->data.mouse.button & wuss_BUTTON_ADJUST)
  {
    pt->bg = pt->swatches.colours[i];
  }
  else
  {
    return result_OK;
  }

  wuss_window_invalidate_visible(pt->optionbar);

  return result_OK;
}

/* ----------------------------------------------------------------------- */

/* close the picker if it is editing a swatch, which is about to move */
static void painting_palette_unpick(painting_task_t *pt)
{
  if (pt->picker == NULL || pt->picking == &pt->fg || pt->picking == &pt->bg)
    return;

  wuss_colourpicker_destroy(pt->picker);
  pt->picker  = NULL;
  pt->picking = NULL;
}

void painting_palette_changed(painting_task_t *pt)
{
  int rows;
  int h;

  rows = (pt->npalette + OB_SWATCHES - 1) / OB_SWATCHES;
  h    = rows * (OB_SWATCH + OB_SWATCH_GAP) - OB_SWATCH_GAP;

  pt->swatches.bbox     = (box_t) BOX_POS_SIZE(OB_SWATCH_X, (OB_H - h) / 2,
                                               OB_SWATCH_ROW, h);
  pt->swatches.colours  = pt->palette;
  pt->swatches.ncolours = pt->npalette;
  if (pt->swatches.selected >= pt->npalette)
    pt->swatches.selected = -1;

  if (pt->optionbar != NULL)
    wuss_window_invalidate_visible(pt->optionbar);
}

void painting_palette_set(painting_task_t *pt,
                          const colour_t  *colours,
                          int              n)
{
  n = CLAMP(n, 0, PAINTING_PALETTE_MAX);

  painting_palette_unpick(pt);
  memcpy(pt->palette, colours, (size_t) n * sizeof(*colours));
  pt->npalette          = n;
  pt->swatches.selected = -1;
  painting_palette_changed(pt);
}

result_t painting_palette_load(painting_task_t *pt, const char *path)
{
  result_t rc;
  colour_t colours[PAINTING_PALETTE_MAX];
  int      n;

  rc = palettes_load_hex(path, colours, PAINTING_PALETTE_MAX, &n);
  if (rc != result_OK)
    return rc;

  painting_palette_set(pt, colours, n);

  return result_OK;
}

result_t painting_palette_save(const char *path, void *opaque)
{
  painting_task_t *pt;

  pt = opaque;

  return palettes_save_hex(path, pt->palette, pt->npalette);
}

result_t painting_palette_op(painting_task_t *pt, int op)
{
  int             sel;
  int             i;
  const colour_t *desktop;
  int             n;

  sel = pt->swatches.selected;

  switch (op)
  {
  case PAINTING_PAL_ADD:
    /* a colour already in the palette is selected, not added again */
    for (i = 0; i < pt->npalette; i++)
      if (pt->palette[i].primary == pt->fg.primary)
        break;
    if (i < pt->npalette)
    {
      pt->swatches.selected = i;
      break;
    }
    if (pt->npalette == PAINTING_PALETTE_MAX)
      return result_OK;
    pt->palette[pt->npalette] = pt->fg;
    pt->swatches.selected     = pt->npalette++;
    break;

  case PAINTING_PAL_EDIT:
    if (sel < 0)
      return result_OK;
    return painting_pick_chip(pt, &pt->palette[sel]);

  case PAINTING_PAL_REMOVE:
    if (sel < 0 || pt->npalette == 1)
      return result_OK;
    painting_palette_unpick(pt);
    array_delete_element(pt->palette, sizeof(pt->palette[0]), pt->npalette,
                         sel);
    pt->npalette--;
    pt->swatches.selected = -1;
    break;

  case PAINTING_PAL_DESKTOP:
    desktop = wuss_get_palette(pt->base.wuss, &n);
    painting_palette_set(pt, desktop, n);
    return result_OK;

  default:
    return result_OK;
  }

  painting_palette_changed(pt);

  return result_OK;
}

/* switch tool, showing its settings on the size bar */
result_t painting_set_tool(painting_task_t *pt, painting_tool_t tool)
{
  unsigned int was;

  /* the pencil keeps a size of its own, as it is mostly one pixel */
  if ((tool == painting_TOOL_PENCIL) != (pt->tool == painting_TOOL_PENCIL))
    SWAP(pt->size, pt->other_size);

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
  wuss_menu_tick_exclusive_live(pt->base.menu_handle, &pt->nozzle_menu,
                                pt->nozzle);
}

void painting_pot_gauge(painting_task_t *pt)
{
  int level;

  level = painting_pot_level(pt);
  if (pt->pot_icons[0] == NULL || level == pt->pot_shown)
    return;

  wuss_icon_set_hidden(pt->sizebar, pt->pot_icons[level], 0);
  wuss_icon_set_hidden(pt->sizebar, pt->pot_icons[pt->pot_shown], 1);
  pt->pot_shown = level;
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
    if (!IS_SLIDER(s) || (settings & (1u << s)) == 0)
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

  /* SELECT steps to the next nozzle, ADJUST back to the previous one */
  if (icon != NULL && icon == pt->nozzle_icon)
  {
    painting_set_nozzle(pt, (pt->nozzle +
                             ((event->data.icon.button & wuss_BUTTON_ADJUST) ?
                              paintingdoc_NOZZLE__COUNT - 1 : 1)) %
                            paintingdoc_NOZZLE__COUNT);
    return result_OK;
  }

  /* the option has already toggled itself */
  if (icon != NULL && icon == pt->perfect_icon)
  {
    pt->perfect = wuss_icon_get_selected(icon);
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

/* refit the size bar's height to the font, re-drop the toolbox to sit below
 * it and rebuild the settings */
result_t painting_sizebar_relayout(painting_task_t *pt)
{
  result_t rc;
  size2d_t size;

  size = SIZE2D(SB_W, SB_H(pt->base.wuss));

  rc = wuss_window_set_doc(pt->sizebar, size);
  if (rc == result_OK)
    rc = wuss_window_resize(pt->sizebar, size);
  if (rc != result_OK)
    return rc;

  wuss_window_detach(pt->toolbox);
  rc = wuss_window_attach(pt->toolbox, pt->base.window, wuss_EDGE_LEFT,
                          wuss_ALIGN_TOP, SB_H(pt->base.wuss) + 1);
  if (rc == result_OK && pt->layerbar != NULL)
  {
    wuss_window_detach(pt->layerbar);
    rc = wuss_window_attach(pt->layerbar, pt->base.window, wuss_EDGE_RIGHT,
                            wuss_ALIGN_TOP, SB_H(pt->base.wuss) + 1);
  }
  if (rc != result_OK)
    return rc;

  return painting_sizebar_build(pt);
}

/* ----------------------------------------------------------------------- */

/* the layers bar's buttons, in order across its top */
enum
{
  LB_ADD,
  LB_DELETE,
  LB_RAISE,
  LB_LOWER
};

/* whether button i applies to n layers with cur current */
static int painting_layer_can(int n, int cur, int i)
{
  switch (i)
  {
  case LB_ADD:
    return n < PAINTINGDOC_LAYERS_MAX;
  case LB_DELETE:
    return n > 1;
  case LB_RAISE:
    return cur < n - 1;
  default:
    return cur > 0;
  }
}

/* button i at (x, y): a plus, a minus or a chevron, grey when off */
static void painting_layer_button(screen_t *scr, int x, int y, int i, int on)
{
  colour_t ink;
  int      c, d;

  ink = on ? colour_rgb(0x00, 0x00, 0x00) : colour_rgb(0x99, 0x99, 0x99);
  c   = (LB_BUTTON - 1) / 2;
  d   = (i == LB_RAISE) ? 2 : -2;

  screen_draw_rect(scr, x, y, SIZE2D(LB_BUTTON, LB_BUTTON), ink);
  if (i == LB_ADD || i == LB_DELETE)
    screen_draw_line(scr, x + c - 3, y + c, x + c + 3, y + c, ink);
  if (i == LB_ADD)
    screen_draw_line(scr, x + c, y + c - 3, x + c, y + c + 3, ink);
  if (i == LB_RAISE || i == LB_LOWER)
  {
    screen_draw_line(scr, x + c - 4, y + c + d, x + c, y + c - d, ink);
    screen_draw_line(scr, x + c, y + c - d, x + c + 4, y + c + d, ink);
  }
}

result_t painting_layerbar_redraw(painting_task_t    *pt,
                                  const wuss_event_t *event)
{
  result_t                   rc;
  screen_t                  *scr;
  const box_t               *bounds;
  colour_t                   black;
  int                        ox, oy;
  int                        n, cur;
  int                        i, r, y;
  const paintingdoc_layer_t *layer;
  int                        m, tw, th;
  screen_scale_t             scale;
  box_t                      thumb;

  scr    = event->data.redraw.scr;
  bounds = event->data.redraw.bounds;
  black  = colour_rgb(0x00, 0x00, 0x00);
  ox     = bounds->x0 - event->data.redraw.scroll.x;
  oy     = bounds->y0 - event->data.redraw.scroll.y;
  n      = paintingdoc_layers(pt->doc);
  cur    = paintingdoc_layer(pt->doc);

  for (i = 0; i < LB_BUTTONS; i++)
    painting_layer_button(scr, ox + LB_MARGIN + i * (LB_BUTTON + LB_GAP),
                          oy + LB_MARGIN, i, painting_layer_can(n, cur, i));

  /* a row per layer, the top one first */
  for (r = 0; r < n; r++)
  {
    i     = n - 1 - r;
    y     = oy + LB_ROWS_Y + r * LB_ROW;
    layer = paintingdoc_layer_get(pt->doc, i);

    if (i == cur)
      screen_fill_rect(scr, ox, y, SIZE2D(LB_W, LB_ROW),
                       colour_rgb(0xFF, 0xFF, 0xFF));

    /* the show box, filled when shown */
    if (layer->visible)
      screen_fill_rect(scr, ox + LB_EYE_X + 2, y + (LB_ROW - LB_EYE) / 2 + 2,
                       SIZE2D(LB_EYE - 4, LB_EYE - 4), black);
    screen_draw_rect(scr, ox + LB_EYE_X, y + (LB_ROW - LB_EYE) / 2,
                     SIZE2D(LB_EYE, LB_EYE), black);

    /* the thumbnail, fitted into a square over the checks */
    m           = MAX(layer->bm.size.w, layer->bm.size.h);
    tw          = MAX(layer->bm.size.w * LB_THUMB / m, 1);
    th          = MAX(layer->bm.size.h * LB_THUMB / m, 1);
    scale.xmul  = scale.ymul = LB_THUMB;
    scale.xdiv  = scale.ydiv = m;
    thumb       = (box_t) BOX_POS_SIZE(ox + LB_THUMB_X + (LB_THUMB - tw) / 2,
                                       y + (LB_ROW - th) / 2, tw, th);
    task_draw_checks(scr, &thumb, POINT(thumb.x0, thumb.y0));
    rc = screen_copy_bitmap_scaled_dithered(scr, &thumb, &layer->bm, &scale,
                                            pt->dithering);
    if (rc != result_OK)
      return rc;
    screen_draw_rect(scr, thumb.x0 - 1, thumb.y0 - 1,
                     SIZE2D(tw + 2, th + 2), black);

    /* the opacity bar, filled in proportion */
    y += (LB_ROW - LB_BAR_H) / 2;
    if (layer->opacity > 0)
      screen_fill_rect(scr, ox + LB_BAR_X, y,
                       SIZE2D(layer->opacity * LB_BAR_W / 255, LB_BAR_H),
                       colour_rgb(0x66, 0x66, 0x99));
    screen_draw_rect(scr, ox + LB_BAR_X, y, SIZE2D(LB_BAR_W, LB_BAR_H),
                     black);
  }

  return result_OK;
}

/* SELECT on a button runs it. on a layer's row: on its show box shows or
 * hides it, on its opacity bar sets its opacity, elsewhere makes it
 * current. dirty is set to the canvas if it changed, else empty */
result_t painting_layerbar_mouse(painting_task_t    *pt,
                                 const wuss_event_t *event,
                                 box_t              *dirty)
{
  result_t                   rc;
  point_t                    p;
  int                        n, cur;
  int                        i;
  const paintingdoc_layer_t *layer;

  *dirty = (box_t) BOX_INIT;

  if (event->data.mouse.action != wuss_MOUSE_DOWN ||
      (event->data.mouse.button & wuss_BUTTON_SELECT) == 0)
    return result_OK;

  rc  = result_OK;
  p   = event->data.mouse.point;
  n   = paintingdoc_layers(pt->doc);
  cur = paintingdoc_layer(pt->doc);

  if (p.y < LB_ROWS_Y)
  {
    if (p.x < LB_MARGIN)
      return result_OK;
    i = (p.x - LB_MARGIN) / (LB_BUTTON + LB_GAP);
    if (i >= LB_BUTTONS || !painting_layer_can(n, cur, i))
      return result_OK;

    switch (i)
    {
    case LB_ADD:
      rc = paintingdoc_layer_add(pt->doc);
      break;
    case LB_DELETE:
      rc = paintingdoc_layer_delete(pt->doc);
      break;
    default:
      rc = paintingdoc_layer_move(pt->doc, i == LB_RAISE ? 1 : -1);
      break;
    }
  }
  else
  {
    i = n - 1 - (p.y - LB_ROWS_Y) / LB_ROW;
    if (i < 0)
      return result_OK;

    layer = paintingdoc_layer_get(pt->doc, i);
    if (p.x >= LB_EYE_X && p.x < LB_EYE_X + LB_EYE)
      paintingdoc_layer_show(pt->doc, i, !layer->visible, layer->opacity);
    else if (p.x >= LB_BAR_X && p.x < LB_BAR_X + LB_BAR_W)
      paintingdoc_layer_show(pt->doc, i, layer->visible,
                             CLAMP((p.x - LB_BAR_X) * 255 / (LB_BAR_W - 1),
                                   0, 255));
    else
      paintingdoc_layer_select(pt->doc, i);
  }

  layer  = paintingdoc_layer_get(pt->doc, 0);
  *dirty = (box_t) BOX_POS_SIZE(0, 0, layer->bm.size.w, layer->bm.size.h);

  return rc;
}

/* the extent is kept to the visible size, so the bar never scrolls */
void painting_layers_changed(painting_task_t *pt)
{
  size2d_t size;

  if (pt->layerbar == NULL)
    return;

  size = SIZE2D(LB_W, LB_H(paintingdoc_layers(pt->doc)));
  (void) wuss_window_set_doc(pt->layerbar, size);
  (void) wuss_window_resize(pt->layerbar, size);
  wuss_window_invalidate_visible(pt->layerbar);
}

void painting_layer_thumb_changed(painting_task_t *pt)
{
  box_t row;
  int   r;

  if (pt->layerbar == NULL)
    return;

  r   = paintingdoc_layers(pt->doc) - 1 - paintingdoc_layer(pt->doc);
  row = (box_t) BOX_POS_SIZE(LB_THUMB_X - 1, LB_ROWS_Y + r * LB_ROW,
                             LB_THUMB + 2, LB_ROW);
  wuss_window_invalidate(pt->layerbar, &row);
}

#endif /* WUSS_APP */
