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
 * current colours and a single row of colour swatches. The swatches are a
 * wuss_swatches_t gadget and the colour chips are drawn by
 * painting_optionbar_redraw, not icons */
#define OB_MARGIN      4
#define OB_CHIP        16  /* the square fg and bg colour chips */
#define OB_CHIP_STEP   8   /* the bg chip's offset down and right of the fg */
#define OB_SWATCHES    16  /* the desktop palette's size */
#define OB_SWATCH      14  /* swatch size */
#define OB_SWATCH_GAP  2
#define OB_SWATCH_X    (OB_MARGIN + OB_CHIP_STEP + OB_CHIP + 6)
#define OB_SWATCH_Y    9
#define OB_SWATCH_ROW  (OB_SWATCHES * (OB_SWATCH + OB_SWATCH_GAP) - \
                        OB_SWATCH_GAP) /* swatch row's width */
#define OB_W           (OB_SWATCH_X + OB_SWATCH_ROW + OB_MARGIN)
#define OB_H           32

/* Size bar layout, in its pane's content space: the current tool's settings
 * in a row from the left, each its label then a slider and its readout, or
 * the nozzle button. Placed by painting_sizebar_build */
#define SB_MARGIN      4
#define SB_ROW_H       18
#define SB_LABEL_GAP   4   /* between a setting's label and its control */
#define SB_SETTING_GAP 10  /* between settings */
#define SB_SLIDER_W    58
#define SB_NOZZLE_W    76  /* the nozzle button, wide enough for "Backslash" */
#define SB_W           420 /* wide enough for the widest tool's settings */
#define SB_H           (SB_MARGIN + SB_ROW_H + SB_MARGIN)

/* the paintbrush's nozzle rows, indexed by paintingdoc_nozzle_t */
extern const char *const painting_nozzle_names[paintingdoc_NOZZLE__COUNT];

/* make the toolbox's tool buttons and the size bar's settings */
result_t painting_toolbar_icons(painting_task_t *pt);

/* a wuss_EVENT_REDRAW or a wuss_EVENT_MOUSE on the option bar */
result_t painting_optionbar_redraw(painting_task_t    *pt,
                                   const wuss_event_t *event);
result_t painting_optionbar_mouse(painting_task_t    *pt,
                                  const wuss_event_t *event);

/* switch tool, showing its settings on the size bar */
result_t painting_set_tool(painting_task_t *pt, painting_tool_t tool);

/* pick the paintbrush's nozzle, keeping the size bar and menu in step */
void painting_set_nozzle(painting_task_t *pt, int nozzle);

/* a wuss_EVENT_ICON on the toolbox or size bar */
result_t painting_toolbar_icon(painting_task_t    *pt,
                               const wuss_event_t *event);

/* step the brush size by delta, keeping the size bar in step */
void painting_step_size(painting_task_t *pt, int delta);

#endif /* WUSS_APP */

#endif /* TASKS_PAINTING_BARS_H */
