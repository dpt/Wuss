/* wuss/tasks/painting/painting.h -- basic painting task */

#ifndef TASKS_PAINTING_H
#define TASKS_PAINTING_H

#ifdef WUSS_APP

#include "framebuf/colour.h"
#include "geom/point.h"
#include "geom/stage.h"
#include "io/path.h"
#include "wuss/icon.h"
#include "wuss/icon-spec.h"
#include "wuss/menu.h"
#include "wuss/component/colourpicker.h"
#include "wuss/component/saveas.h"
#include "wuss/component/scaleview.h"
#include "wuss/gadget/swatches.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../common/dithermenu.h"
#include "../common/taskapp.h"

#include "painting-doc.h"
#include "painting-new.h"

#define PAINTING_SIZE_MIN 1
#define PAINTING_SIZE_MAX 32

#define PAINTING_STABILISE_MAX 50

/* the stamps a pot holds at a dryness of 1%; at d% it holds this / d */
#define PAINTING_POT_STAMPS 4000

/* the settings a tool may show on the size bar, left to right in this order */
typedef enum painting_setting
{
  painting_SETTING_SIZE,      /* brush and outline width, spray radius */
  painting_SETTING_NOZZLE,    /* the paintbrush's nozzle, a button */
  painting_SETTING_DRY,       /* how soon the paintbrush's pot runs dry,
                               * percent; 0 never */
  painting_SETTING_DENSITY,   /* the spray's dots per call, as a percentage */
  painting_SETTING_RATE,      /* the share of moves and idles the held spray
                               * fires on */
  painting_SETTING_EDGE,      /* how far the wobbly brush's edges wobble,
                               * percent of the usual */
  painting_SETTING_WANDER,    /* how far the wobbly brush drifts across its
                               * path, percent of the usual */
  painting_SETTING_TOLERANCE, /* the wand's colour tolerance, 0..255 */
  painting_SETTING_STABILISE, /* how far a freehand stroke trails the
                               * pointer, in pixels */
  painting_SETTING_PERFECT,   /* the pencil's pixel-perfect switch, an
                               * option */
  painting_SETTING_SYMMETRY,  /* a label, then an option per mirror */
  painting_SETTING__COUNT
}
painting_setting_t;

/* the pot gauge's levels, empty to full: one image each */
#define PAINTING_POT_LEVELS 11

/* the most icons the size bar holds: a label, slider and readout each, and
 * the pot gauge's images */
#define PAINTING_SIZEBAR_ICONS (3 * painting_SETTING__COUNT + \
                                PAINTING_POT_LEVELS)

/* the toolbox's radio buttons, in this order */
typedef enum painting_tool
{
  painting_TOOL_PENCIL,     /* a freehand line, one pixel wide by default */
  painting_TOOL_PAINTBRUSH, /* stamp the chosen nozzle along the stroke */
  painting_TOOL_WOBBLE,     /* a freehand stroke with wobbly edges */
  painting_TOOL_ERASER,
  painting_TOOL_LINE,
  painting_TOOL_RECT,
  painting_TOOL_RECT_FILL, /* as RECT, but solid */
  painting_TOOL_ELLIPSE,
  painting_TOOL_ELLIPSE_FILL, /* as ELLIPSE, but solid */
  painting_TOOL_PATH,      /* drag out a freeform closed outline */
  painting_TOOL_PATH_FILL, /* as PATH, but solid */
  painting_TOOL_FILL,
  painting_TOOL_GRADIENT, /* drag a linear gradient over the selection */
  painting_TOOL_SPRAY, /* scatter dots around the pointer while held */
  painting_TOOL_SELECT, /* rubber-band a rectangular selection */
  painting_TOOL_LASSO,  /* drag out a freeform selection */
  painting_TOOL_WAND,   /* select the similar colour around a click */
  painting_TOOL_PICK,   /* take the fg (SELECT) or bg (ADJUST) from the canvas */
  painting_TOOL_MOVE,   /* drag the selection's pixels: SELECT moves them,
                         * leaving bg behind, and ADJUST copies them */
  painting_TOOL__COUNT
}
painting_tool_t;

/* the MENU click menu's rows, laid out by menu-standard.md: File reduced to
 * Save, then Edit, View and the object menus */
enum
{
  PAINTING_MENU_SAVE,
  PAINTING_MENU_EDIT,      /* submenu: PAINTING_EDIT_* */
  PAINTING_MENU_VIEW,      /* submenu: PAINTING_VIEW_* */
  PAINTING_MENU_SELECTION, /* submenu: PAINTING_SEL_* */
  PAINTING_MENU_TRANSFORM, /* submenu: PAINTING_XFORM_* */
  PAINTING_MENU_PALETTE,   /* submenu: PAINTING_PAL_* */
  PAINTING_MENU__COUNT
};

/* the View submenu's rows: bars, each ticked while it shows, then the grid,
 * tiling and the display */
enum
{
  PAINTING_VIEW_TOOLBOX,   /* the tools, on the left */
  PAINTING_VIEW_SIZEBAR,   /* the tool's settings, across the top */
  PAINTING_VIEW_OPTIONBAR, /* the colours, along the bottom */
  PAINTING_VIEW_LAYERBAR,  /* the layers, on the right */
  PAINTING_VIEW_GRID,      /* submenu: one row per painting_grid_sizes entry */
  PAINTING_VIEW_TILED,     /* ticked while PAINTING_SYM_TILED is on */
  PAINTING_VIEW_DISPLAY,   /* submenu: dithermenu_t's Display */
  PAINTING_VIEW__COUNT
};

/* the symmetry's bits, each set in symmetry by 1 << this. the mirrors are
 * size bar options, tiling a View row */
enum
{
  PAINTING_SYM_HORIZONTAL, /* mirror left and right */
  PAINTING_SYM_VERTICAL,   /* mirror top and bottom */
  PAINTING_SYM_TILED,      /* show the canvas tiled and paint across its
                            * edges onto the far side */
  PAINTING_SYM__COUNT
};

/* the Grid submenu's rows: off, then grids of this many canvas pixels */
#define PAINTING_GRID__COUNT 4

/* the copies an op paints: four mirrorings by nine tilings */
#define PAINTING_COPIES (4 * 9)

/* the Transform submenu's rows: one per paintingdoc_xform_t, then these */
enum
{
  PAINTING_XFORM_CROP = paintingdoc_XFORM__COUNT, /* crop to the selection's
                                                   * bounds */
  PAINTING_XFORM_TRIM, /* crop to the content's bounds */
  PAINTING_XFORM_SCALE, /* submenu: one row per painting_scales entry */
  PAINTING_XFORM_SIZE, /* leaf: the Canvas size dialogue */
  PAINTING_XFORM__COUNT
};

/* the Scale submenu's rows */
#define PAINTING_SCALE__COUNT 4

/* the most colours a document's palette holds: two rows of swatches */
#define PAINTING_PALETTE_MAX 32

/* the Palette submenu's rows */
enum
{
  PAINTING_PAL_ADD,     /* append the foreground colour */
  PAINTING_PAL_EDIT,    /* open the colour picker on the selected swatch */
  PAINTING_PAL_REMOVE,  /* remove the selected swatch */
  PAINTING_PAL_DESKTOP, /* replace the palette with the desktop's */
  PAINTING_PAL_SAVE,    /* leaf: Save As for a .hex palette file */
  PAINTING_PAL__COUNT
};

/* the Edit submenu's rows */
enum
{
  PAINTING_EDIT_UNDO,  /* step back through the history */
  PAINTING_EDIT_REDO,  /* step forward through the history */
  PAINTING_EDIT_CUT,   /* copy the selection, then clear it */
  PAINTING_EDIT_COPY,  /* copy the selection to the pixel clipboard */
  PAINTING_EDIT_PASTE, /* paste the pixel clipboard, floating */
  PAINTING_EDIT_FILL,  /* fill the selection with the foreground colour */
  PAINTING_EDIT_CLEAR, /* fill the selection with the background colour */
  PAINTING_EDIT__COUNT
};

/* the Selection submenu's rows */
enum
{
  PAINTING_SEL_ALL,
  PAINTING_SEL_NONE,
  PAINTING_SEL_INVERT,
  PAINTING_SEL__COUNT
};

/* a 640x480 white canvas in a scrollable window, with a toolbox of tools on
 * its left edge, an option bar of colours on its bottom edge and a size bar
 * pane holding the current tool's settings across its top. SELECT paints
 * with the foreground colour and ADJUST with the background colour; clicking
 * an option bar swatch sets either. The canvas has alpha: a checkerboard
 * shows through its transparent pixels, which the eraser makes. Closing the
 * canvas window closes the toolbars and pane too */
typedef struct painting_task
{
  taskapp_instance_t base;      /* window: the canvas; NULL once closed */
  wuss_window_t     *toolbox;  /* the tools toolbar; NULL once closed */
  wuss_window_t     *optionbar; /* the colours and options toolbar; NULL once
                                 * closed */
  wuss_window_t     *sizebar;   /* the tool settings pane; NULL once closed */
  wuss_window_t     *layerbar;  /* the layers toolbar; NULL once closed */
  paintingdoc_t     *doc;
  stageconfig_t      stagecfg;  /* the pasteboard, border and shadow around
                                 * the canvas */
  point_t            page;      /* the canvas's origin in the window */
  int                zoom;      /* window pixels per canvas pixel */
  char              path[DPTLIB_MAXPATH]; /* document's file; "" until
                                            * loaded or saved */
  colour_t           fg, bg;
  painting_tool_t    tool;
  paintingdoc_nozzle_t nozzle;  /* the paintbrush and pencil's nozzle */
  int                size;      /* brush and outline width in pixels */
  int                other_size; /* the pencil's size while another tool is
                                  * current, or the others' while the pencil
                                  * is */
  int                dry;       /* how soon the paintbrush runs dry, percent;
                                 * 0 never */
  int                density;   /* the spray's density, percent */
  int                rate;      /* the spray's rate, percent */
  int                edge;      /* the wobbly brush's edge wobble, percent */
  int                wander;    /* the wobbly brush's drift, percent */
  int                spray_tick; /* rate summed over moves and idles; sprays
                                  * at 100 */
  int                tolerance; /* the wand's colour tolerance, 0..255 */
  int                stabilise; /* the stabiliser's string length, pixels */
  int                perfect;   /* non-zero: the pencil drops L corners */
  double             steady_x, steady_y; /* the stabilised pointer */
  int                held;      /* button of the stroke in progress, or 0 */
  point_t            anchor;    /* where the stroke began */
  point_t            last;      /* where the stroke last reached */
  wuss_key_modifiers_t mods;    /* keys held as the stroke last reached */
  int                space;     /* non-zero: Space is down, so a drag pans */
  int                panning;   /* non-zero: a Space drag is panning */
  point_t            pan;       /* the content point the pan holds under the
                                 * pointer */
  point_t           *lasso;     /* points dragged out so far by the lasso or
                                 * path */
  int                nlasso, caplasso;
  int                ants_tick;  /* idles since the ants last moved */
  int                ants_phase; /* where the ants have marched, 0..7 */
  rng_t              rng;        /* the spray's scatter */
  paintingdoc_wobble_t wobble;   /* the wobbly stroke in progress */
  paintingdoc_trail_t trail[PAINTING_COPIES]; /* the pencil stroke in
                                  * progress, one per copy */
  unsigned int       symmetry;   /* bit 1 << PAINTING_SYM_* set when on */
  point_t            wrap;       /* tiled: the whole canvases the stroke's
                                  * press lay off the canvas, taken off its
                                  * every point */
  paintingdoc_pot_t  pot;        /* the paintbrush stroke's paint, refilled
                                  * as each stroke starts */
  wuss_icon_t       *tool_icons[painting_TOOL__COUNT];
  wuss_icon_t       *sizebar_icons[PAINTING_SIZEBAR_ICONS]; /* the current
                                 * tool's settings, which a tool change
                                 * replaces */
  int                nsizebar_icons;
  wuss_icon_t       *nozzle_icon; /* the nozzle button, or NULL if not shown */
  wuss_icon_t       *perfect_icon; /* the pixel-perfect option, or NULL if not
                                    * shown */
  wuss_icon_t       *sym_icons[PAINTING_SYM_TILED]; /* the mirror options by
                                 * PAINTING_SYM_*, or NULL if not shown */
  wuss_icon_t       *pot_icons[PAINTING_POT_LEVELS]; /* the pot gauge's
                                 * images, empty to full, all but one hidden;
                                 * [0] NULL if not shown */
  int                pot_shown; /* the level the gauge shows */
  wuss_slider_row_t  rows[painting_SETTING__COUNT]; /* live only for the
                                 * slider settings the current tool shows */
  wuss_menu_item_t   menu_items[PAINTING_MENU__COUNT];
  wuss_menu_item_t   view_items[PAINTING_VIEW__COUNT];
  wuss_menu_t        view_menu;
  unsigned int       hidden_bars; /* bit 1 << PAINTING_VIEW_* set when that
                                   * bar is hidden */
  wuss_menu_item_t   edit_items[PAINTING_EDIT__COUNT];
  wuss_menu_t        edit_menu;
  wuss_menu_item_t   sel_items[PAINTING_SEL__COUNT];
  wuss_menu_t        sel_menu;
  wuss_menu_item_t   xform_items[PAINTING_XFORM__COUNT];
  wuss_menu_t        xform_menu;
  wuss_menu_item_t   scale_items[PAINTING_SCALE__COUNT];
  wuss_menu_t        scale_menu;
  paintingnew_t     *resize;    /* owned: Transform's "Canvas size" leaf */
  wuss_menu_item_t   pal_items[PAINTING_PAL__COUNT];
  wuss_menu_t        pal_menu;
  wuss_saveas_t     *pal_saveas; /* owned: Palette's "Save" leaf */
  colour_t           palette[PAINTING_PALETTE_MAX]; /* the document's own
                                 * palette, which the swatches show */
  int                npalette;
  wuss_menu_item_t   grid_items[PAINTING_GRID__COUNT];
  wuss_menu_t        grid_menu;
  int                grid;      /* the user grid's cell in canvas pixels, or 0
                                 * for none */
  dithermenu_t       dither;
  wuss_scaleview_t  *scaleview; /* owned: "Display"'s "Scale view" leaf */
  screen_dither_t    dithering; /* how the canvas is blitted */
  char               row_text[painting_SETTING__COUNT][WUSS_SLIDER_ROW_BUF];
  wuss_swatches_t    swatches;  /* the palette, as a colour grid; selected is
                                 * the swatch Edit and Remove act on */
  wuss_colourpicker_t *picker;  /* edits a chip; made on first use, else NULL */
  colour_t          *picking;   /* &fg or &bg: the chip the picker edits */
}
painting_task_t;

/* install the icon bar app that opens paint windows on demand */
result_t painting_app_create(wuss_t *wuss);

#endif /* WUSS_APP */

#endif /* TASKS_PAINTING_H */
