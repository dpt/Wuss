/* wuss/tasks/painting/painting-bars.h -- Painting's toolbars */

#ifndef TASKS_PAINTING_BARS_H
#define TASKS_PAINTING_BARS_H

#ifdef WUSS_APP

#include "painting.h"

/* Toolbox layout, in its window's content space: two columns of tool buttons
 * in groups -- selection, painting, shapes -- placed by painting_tool_cells,
 * with a gap between groups */
#define TB_MARGIN    2
#define TB_TOOL_CELL 24  /* pitch of the tool buttons */
#define TB_TOOL_SIZE 24  /* button size, including its bevel */
#define TB_GROUP_GAP 2   /* extra space between groups */
#define TB_COLUMNS   2
#define TB_ROWS      11  /* rows used by painting_tool_cells */
#define TB_GROUPS    3   /* groups used by painting_tool_cells */
#define TB_W         (TB_MARGIN + TB_COLUMNS * TB_TOOL_CELL - \
                      (TB_TOOL_CELL - TB_TOOL_SIZE) + TB_MARGIN)
#define TB_H         (TB_MARGIN + TB_ROWS * TB_TOOL_CELL - \
                      (TB_TOOL_CELL - TB_TOOL_SIZE) + \
                      (TB_GROUPS - 1) * TB_GROUP_GAP + TB_MARGIN)

/* Option bar layout, in its window's content space: from the left, the
 * current colours and one or two rows of colour swatches, centred down the
 * bar. The swatches are a wuss_swatches_t gadget and the colour chips are
 * drawn by painting_optionbar_redraw, not icons */
#define OB_MARGIN      4
#define OB_CHIP        16  /* the square fg and bg colour chips */
#define OB_CHIP_STEP   8   /* the bg chip's offset down and right of the fg */
#define OB_SWATCHES    16  /* swatches in a row */
#define OB_SWATCH      14  /* swatch size */
#define OB_SWATCH_GAP  2
#define OB_SWATCH_X    (OB_MARGIN + OB_CHIP_STEP + OB_CHIP + 6)
#define OB_SWATCH_ROW  (OB_SWATCHES * (OB_SWATCH + OB_SWATCH_GAP) - \
                        OB_SWATCH_GAP) /* swatch row's width */
#define OB_W           (OB_SWATCH_X + OB_SWATCH_ROW + OB_MARGIN)
#define OB_H           32

/* Size bar layout, in its pane's content space: the current tool's settings
 * in a row from the left, each its label then a slider and its readout, or
 * the nozzle button. Placed by painting_sizebar_build */
#define SB_MARGIN      4
#define SB_ROW_H(wuss) wuss_std_height((wuss), wuss_STD_SLIDER)
#define SB_LABEL_GAP   4   /* between a setting's label and its control */
#define SB_SETTING_GAP 10  /* between settings */
#define SB_SLIDER_W    58
#define SB_POT         16  /* the pot gauge's images */
#define SB_W           580 /* wide enough for the widest tool's settings */
/* the row: tall enough for an option's tick box, which does not scale with
 * the font; the other controls are centred in it */
#define SB_LINE_H(wuss) MAX(SB_ROW_H(wuss), wuss_STD_GLYPH_SIZE)
#define SB_H(wuss)     (SB_MARGIN + SB_LINE_H(wuss) + SB_MARGIN)

/* Layers bar layout, in its window's content space: a row of buttons (add,
 * delete, raise, lower) across the top, then a row per layer, the top layer
 * first, each its show box, its thumbnail and its opacity bar. Drawn by
 * painting_layerbar_redraw, not icons */
#define LB_MARGIN   4
#define LB_BUTTON   16 /* the buttons' size */
#define LB_BUTTONS  4
#define LB_GAP      4  /* between the parts of a row */
#define LB_ROW      28 /* a layer's row */
#define LB_EYE      12 /* the show box */
#define LB_THUMB    24 /* the thumbnail's longer side */
#define LB_BAR_W    40 /* the opacity bar */
#define LB_BAR_H    8
#define LB_EYE_X    LB_MARGIN
#define LB_THUMB_X  (LB_EYE_X + LB_EYE + LB_GAP)
#define LB_BAR_X    (LB_THUMB_X + LB_THUMB + LB_GAP)
#define LB_ROWS_Y   (LB_MARGIN + LB_BUTTON + LB_MARGIN)
#define LB_W        (LB_BAR_X + LB_BAR_W + LB_MARGIN)
#define LB_H(n)     (LB_ROWS_Y + (n) * LB_ROW + LB_MARGIN) /* for n layers */

/* the paintbrush's nozzle rows, indexed by paintingdoc_nozzle_t */
extern const char *const painting_nozzle_names[paintingdoc_NOZZLE__COUNT];

/* make the toolbox's tool buttons and the size bar's settings */
result_t painting_toolbar_icons(painting_task_t *pt);

/* a wuss_EVENT_REDRAW or a wuss_EVENT_MOUSE on the option bar */
result_t painting_optionbar_redraw(painting_task_t    *pt,
                                   const wuss_event_t *event);
result_t painting_optionbar_mouse(painting_task_t    *pt,
                                  const wuss_event_t *event);

/* a wuss_EVENT_REDRAW or a wuss_EVENT_MOUSE on the layers bar. the mouse
 * sets dirty to the canvas box when the canvas changed, else empties it */
result_t painting_layerbar_redraw(painting_task_t    *pt,
                                  const wuss_event_t *event);
result_t painting_layerbar_mouse(painting_task_t    *pt,
                                 const wuss_event_t *event,
                                 box_t              *dirty);

/* fit the layers bar to the number of layers and redraw it */
void painting_layers_changed(painting_task_t *pt);

/* redraw the current layer's thumbnail */
void painting_layer_thumb_changed(painting_task_t *pt);

/* lay the swatches out for the palette's size, one row or two, and redraw
 * them */
void painting_palette_changed(painting_task_t *pt);

/* replace the palette with n colours, keeping at most PAINTING_PALETTE_MAX */
void painting_palette_set(painting_task_t *pt,
                          const colour_t  *colours,
                          int              n);

/* replace the palette with the .hex palette file at path. A failure keeps
 * the current palette */
result_t painting_palette_load(painting_task_t *pt, const char *path);

/* wuss_saveas_save_fn_t: write the palette as a .hex file. opaque is the
 * painting_task_t */
result_t painting_palette_save(const char *path, void *opaque);

/* apply a Palette menu row, PAINTING_PAL_* */
result_t painting_palette_op(painting_task_t *pt, int op);

/* switch tool, showing its settings on the size bar */
result_t painting_set_tool(painting_task_t *pt, painting_tool_t tool);

/* pick the paintbrush's nozzle, keeping the size bar and menu in step */
void painting_set_nozzle(painting_task_t *pt, int nozzle);

/* show on the pot gauge how full the paintbrush's pot is */
void painting_pot_gauge(painting_task_t *pt);

/* a wuss_EVENT_ICON on the toolbox or size bar */
result_t painting_toolbar_icon(painting_task_t    *pt,
                               const wuss_event_t *event);

/* step the brush size by delta, keeping the size bar in step */
void painting_step_size(painting_task_t *pt, int delta);

/* on wuss_EVENT_RELAYOUT: refit the size bar and the toolbox below it to
 * the new font */
result_t painting_sizebar_relayout(painting_task_t *pt);

#endif /* WUSS_APP */

#endif /* TASKS_PAINTING_BARS_H */
