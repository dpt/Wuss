/* wuss/tasks/icons/icons.c -- work-area icons task */

#ifdef WUSS_APP
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/palettes.h"
#include "framebuf/pattern.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/point.h"
#include "geom/size.h"
#include "io/path.h"
#include "wuss/gadget/colourset.h"
#include "wuss/gadget/stringset.h"
#include "wuss/icon-spec.h"
#include "wuss/icon.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

/* demonstrates wuss-managed work-area icons: labelled groups covering every
 * icon type (buttons, radios/options, bitmaps, a pattern swatch, display
 * fields, sliders, a menu-entry strip, a string set gadget) plus a button
 * placed far down the document to show icons scroll with the content and
 * stay clickable */
typedef struct icons_task
{
  taskapp_instance_t base;
  bmfont_t      *font;
  colour_t       label;  /* axis coordinate text */
  colour_t       paper;  /* window bg, for bmfont_draw glyph blending */
  wuss_icon_t   *button;  /* "Press me" */
  wuss_icon_t   *counter; /* label showing hit count */
  int            count;
  wuss_icon_t   *opt;     /* standalone option button */
  wuss_icon_t   *state;   /* label echoing radio/option selection */
  bitmap_t       sprite;  /* borrowed by the two BITMAP icons; freed on close */
  int            has_sprite;
  wuss_icon_t   *hotspot;      /* the interactive BITMAP icon */
  wuss_icon_t   *slider_horiz; /* horizontal slider */
  wuss_icon_t   *slider_vert;  /* vertical slider */
  wuss_icon_t   *slider_state; /* label echoing whichever slider last moved */
  wuss_icon_t   *tally;        /* display field mirroring the counter */
  wuss_icon_t   *echo;         /* label echoing whichever writable last changed */
  wuss_stringset_t *sset;      /* gadget; its icons belong to the window */
  wuss_icon_t   *sset_echo;    /* label echoing the string set's pick */
  wuss_colourset_t *cset;      /* gadget; its icons belong to the window */
  wuss_icon_t   *cset_echo;    /* label echoing the colour set's pick */
  wuss_icon_t   *drag_echo;    /* label echoing where the last drag ended */
}
icons_task_t;

#define ICONS_DOC_W    260
#define ICONS_DOC_H    1670 /* taller than the window, so scrolling is exercised */
#define ICONS_MARGIN   28  /* left edge of everything except frame captions */
#define ICONS_ROW      20  /* vertical pitch between stacked simple icons */

/* Maximum spec count across every group icons_create can lay out; the sprite
 * pair is the only optional part (skipped when the PNG fails to load). Kept
 * as one enum so a miscount between this and the groups below fails loudly
 * (an array bound, not a silent overrun) rather than corrupting the heap. */
enum
{
  ICONS_N_INTRO   = 3, /* heading, counter button, counter label */
  ICONS_N_BUTTONS = 5, /* frame + 2x2 grid: default/normal x plain/disabled */
  ICONS_N_RADIOS  = 8, /* frame + 3 radios + option + state label + 2 justified labels */
  ICONS_N_BITMAPS = 4, /* frame + decorative + interactive + disabled bitmap */
  ICONS_N_ICONSET = 5, /* frame + opton + optoff + radon + radoff from the loaded set */
  ICONS_N_PATTERN = 2, /* frame + one PATTERN swatch */
  ICONS_N_BORDERS = 6, /* frame + GROOVE + RIDGE + ACTION + DIVIDER + PLAIN labels */
  ICONS_N_DISPLAY = 3, /* frame + static + counter-tracking display */
  ICONS_N_SLIDERS = 4, /* frame + horizontal + vertical slider + state label */
  ICONS_N_MENU    = 8, /* frame + plain, ticked, swatch, submenu, disabled, rule, separator entry */
  ICONS_N_WRITE   = 7, /* frame + five writables (incl. left/centre/right) + echo label */
  ICONS_N_SSET    = 2, /* frame + echo label; the gadget adds its own icons */
  ICONS_N_CSET    = 2, /* frame + echo label; the gadget adds its own icons */
  ICONS_N_DRAG    = 3, /* frame + draggable icon + echo label */
  ICONS_NSPECS    = ICONS_N_INTRO + ICONS_N_BUTTONS + ICONS_N_RADIOS +
                    ICONS_N_BITMAPS + ICONS_N_ICONSET + ICONS_N_PATTERN +
                    ICONS_N_BORDERS + ICONS_N_DISPLAY + ICONS_N_SLIDERS +
                    ICONS_N_MENU + ICONS_N_WRITE + ICONS_N_SSET + ICONS_N_CSET +
                    ICONS_N_DRAG
};

/* Running state threaded through the icons_add_* helpers: where to write the
 * next spec, and how far down the document the next group should start. */
typedef struct icons_layout
{
  wuss_icon_spec_t *specs;
  int               n;    /* specs[0..n) are filled in */
  int               y;    /* document y of the next group */
  wuss_colour_t     black;
  wuss_colour_t     window; /* wuss_COLOUR_WINDOW: the standard work-area fill */
  wuss_colour_t     red;    /* menu swatch demo colour */
}
icons_layout_t;

/* ----------------------------------------------------------------------- */

/* [0] a heading, [1] the button that bumps the counter (shown as a default
 * action button, so it carries the accent styling), [2] the counter label
 * beside it. Returns the indices of [1] and [2] via button/counter. */
static void icons_add_intro(icons_layout_t *lay, int *button, int *counter)
{
  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, lay->y, 220, 14),
                       "Work-area icons:", wuss_ICON_FONT(1));
  lay->y += 30;

  *button = lay->n;
  /* 4px larger on every side than a plain button to seat the DEFAULT icon's
   * 6px action surround */
  wuss_icon_spec_action(&lay->specs[lay->n++],
                        (box_t) BOX_POS_SIZE(ICONS_MARGIN - 4, lay->y - 4, 88, 30),
                        "Press me", 1);

  *counter = lay->n;
  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN + 92, lay->y, 120, 22),
                       "0", 0);
  lay->y += 46;
}

/* A grouping frame captioned "Buttons", holding a 2x2 grid: column 0 is the
 * DEFAULT (accent) button, column 1 the plain one; the bottom row adds
 * DISABLED. The DEFAULT column is inset 4px on every side to seat the accent
 * icon's 6px action surround. */
static void icons_add_buttons(icons_layout_t *lay)
{
  wuss_icon_spec_t *s;
  int               top;
  int               col;
  int               row;
  int               x;
  int               y;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 90),
                       "Buttons");

  for (row = 0; row < 2; row++)
  {
    for (col = 0; col < 2; col++)
    {
      x = ICONS_MARGIN + 10 + col * 100;
      y = top + 22 + row * 34;

      s = &lay->specs[lay->n++];
      if (col == 0)
        wuss_icon_spec_action(s, (box_t) BOX_POS_SIZE(x - 4, y - 4, 64, 30),
                              "Default", 1);
      else
        wuss_icon_spec_action(s, (box_t) BOX_POS_SIZE(x, y, 56, 22),
                              "Normal", 0);

      if (row == 1)
        s->flags |= wuss_ICON_FLAGS_DISABLED;
    }
  }

  lay->y = top + 104;
}

/* A grouping frame captioned "Radios & options", with two justified labels,
 * three radios sharing group 1, a standalone option and a label echoing
 * whichever control last changed. Returns the indices of the option and the
 * echo label via opt/state. */
static void icons_add_radios(icons_layout_t *lay, int *opt, int *state)
{
  int top;
  int r;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 170),
                       "Radios & options");

  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20, 180, 14),
                       "right", wuss_ICON_FLAGS_JUSTIFY_RIGHT);

  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 40, 180, 14),
                       "centre", wuss_ICON_FLAGS_JUSTIFY_CENTRE);

  for (r = 0; r < 3; r++)
    wuss_icon_spec_radio(&lay->specs[lay->n++],
                         (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 66 + r * 20, 180, 16),
                         (r == 0) ? "Red" : (r == 1) ? "Green" : "Blue",
                         1);

  *opt = lay->n;
  wuss_icon_spec_option(&lay->specs[lay->n++],
                        (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 130, 180, 16),
                        "Wireframe");

  *state = lay->n;
  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 150, 180, 14),
                       "(no selection)", 0);

  lay->y = top + 190;
}

/* A grouping frame captioned "Bitmaps", holding a decorative sprite, (to
 * its right) an interactive one that bumps the counter and a disabled one
 * faded toward the window ground -- only laid out if
 * the sprite loaded. Returns the interactive icon's index via *hotspot, or
 * leaves it unset if sprite is NULL. */
static void icons_add_bitmaps(icons_layout_t *lay,
                              const bitmap_t *sprite,
                              int            *hotspot)
{
  wuss_icon_spec_t *s;
  int               top;

  if (sprite == NULL)
    return;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200,
                                            sprite->size.h + 30),
                       "Bitmaps");

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20,
                                   sprite->size.w, sprite->size.h);
  s->type   = wuss_ICON_TYPE_BITMAP;
  s->u.bitmap.image = sprite;
  lay->n++;

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 20 + sprite->size.w, top + 20,
                                   sprite->size.w, sprite->size.h);
  s->type   = wuss_ICON_TYPE_BITMAP;
  s->u.bitmap.image = sprite;
  s->flags  = wuss_ICON_FLAGS_INTERACTIVE;
  *hotspot  = lay->n;
  lay->n++;

  /* disabled: the image fades toward the window ground */
  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 30 + 2 * sprite->size.w,
                                   top + 20, sprite->size.w, sprite->size.h);
  s->type   = wuss_ICON_TYPE_BITMAP;
  s->u.bitmap.image = sprite;
  s->flags  = wuss_ICON_FLAGS_DISABLED;
  lay->n++;

  lay->y = top + sprite->size.h + 50;
}

/* A grouping frame captioned "Icon pool", holding the four fixtures loaded by
 * wuss_iconpool_load -- opton/optoff/radon/radoff -- each drawn as a BITMAP
 * icon that references the pool by name. Laid out only when the pool loaded
 * (all four looked up); skipped otherwise. */
static void icons_add_iconset(icons_layout_t *lay, const wuss_t *wuss)
{
  static const char *const names[4] = { "opton", "optoff", "radon", "radoff" };

  wuss_icon_spec_t *s;
  int               top;
  int               idx[4];
  int               i;

  for (i = 0; i < 4; i++)
  {
    idx[i] = wuss_iconpool_lookup(wuss, names[i]);
    if (idx[i] < 0)
      return;
  }

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 44),
                       "Icon pool");

  for (i = 0; i < 4; i++)
  {
    const bitmap_t *bm;
    int             w;
    int             h;

    bm = wuss_iconpool_bitmap(wuss, idx[i]);
    w  = bm ? bm->size.w : 16;
    h  = bm ? bm->size.h : 16;

    s           = &lay->specs[lay->n];
    s->bbox     = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10 + i * (w + 6),
                                       top + 20, w, h);
    s->type     = wuss_ICON_TYPE_BITMAP;
    s->u.bitmap.name = names[i];
    lay->n++;
  }

  lay->y = top + 60;
}

/* A grouping frame captioned "Pattern", holding one PATTERN-filled swatch. */
static void icons_add_pattern(icons_layout_t *lay)
{
  wuss_icon_spec_t *s;
  int               top;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 60),
                       "Pattern");

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20, 180, 26);
  s->type   = wuss_ICON_TYPE_PATTERN;
  s->fg     = lay->black;
  s->bg     = lay->window;
  s->u.pattern.tile = screen_PATTERN_DIAGONAL;
  lay->n++;

  lay->y = top + 76;
}

/* A grouping frame captioned "Borders", holding a GROOVE-bordered label (a
 * sunken RISC OS display field), a RIDGE-bordered one (raised), an
 * ACTION-bordered one -- a 6px surround: raised outset, accent moat, raised
 * inset, like a default-action button -- then a DIVIDER-bordered one: a 4px
 * surround, outer sunken ring around inner raised, in lighter shades -- and
 * a PLAIN-bordered one: a 1px fg outline, as a writable draws. */
static void icons_add_borders(icons_layout_t *lay)
{
  wuss_icon_spec_t *s;
  int               top;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 176),
                       "Borders");

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20, 180, 22);
  s->type   = wuss_ICON_TYPE_LABEL;
  s->text   = "Groove";
  s->fg     = lay->black;
  s->bg     = lay->window;
  s->u.label.border = wuss_ICON_BORDER_GROOVE;
  s->flags  = wuss_ICON_FLAGS_JUSTIFY_CENTRE;
  lay->n++;

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 46, 180, 22);
  s->type   = wuss_ICON_TYPE_LABEL;
  s->text   = "Ridge";
  s->fg     = lay->black;
  s->bg     = lay->window;
  s->u.label.border = wuss_ICON_BORDER_RIDGE;
  s->flags  = wuss_ICON_FLAGS_JUSTIFY_CENTRE;
  lay->n++;

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 72, 180, 34);
  s->type   = wuss_ICON_TYPE_LABEL;
  s->text   = "Action";
  s->fg     = lay->black;
  s->bg     = lay->window;
  s->u.label.border = wuss_ICON_BORDER_ACTION;
  s->flags  = wuss_ICON_FLAGS_JUSTIFY_CENTRE;
  lay->n++;

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 110, 180, 30);
  s->type   = wuss_ICON_TYPE_LABEL;
  s->text   = "Divider";
  s->fg     = lay->black;
  s->bg     = lay->window;
  s->u.label.border = wuss_ICON_BORDER_DIVIDER;
  s->flags  = wuss_ICON_FLAGS_JUSTIFY_CENTRE;
  lay->n++;

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 144, 180, 22);
  s->type   = wuss_ICON_TYPE_LABEL;
  s->text   = "Plain";
  s->fg     = lay->black;
  s->bg     = lay->window;
  s->u.label.border = wuss_ICON_BORDER_PLAIN;
  s->flags  = wuss_ICON_FLAGS_JUSTIFY_CENTRE;
  lay->n++;

  lay->y = top + 192;
}

/* A grouping frame captioned "Display fields", holding two read-only
 * DISPLAY fields: a static left-justified one and a right-justified one the
 * task rewrites with the "Press me" hit count. Returns the latter's index via
 * *tally. */
static void icons_add_display(icons_layout_t *lay, int *tally)
{
  int top;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 72),
                       "Display fields");

  wuss_icon_spec_display(&lay->specs[lay->n],
                         (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20,
                                              180, 20),
                         "Read-only value", 0);
  lay->n++;

  *tally = lay->n;
  wuss_icon_spec_display(&lay->specs[lay->n],
                         (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 44,
                                              180, 20),
                         "0", wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  lay->n++;

  lay->y = top + 88;
}

/* A grouping frame captioned "Sliders", holding a horizontal slider, a
 * vertical slider beside it and a label echoing whichever last moved.
 * Returns the horizontal/vertical slider and echo-label indices via
 * horiz/vert/state. */
static void icons_add_sliders(icons_layout_t *lay,
                              int            *horiz,
                              int            *vert,
                              int            *state)
{
  int top;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 130),
                       "Sliders");

  *horiz = lay->n;
  wuss_icon_spec_slider(&lay->specs[lay->n++],
                        (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20, 160, 20),
                        wuss_SLIDER_HORIZONTAL, 0, 100, 25);

  *vert = lay->n;
  wuss_icon_spec_slider(&lay->specs[lay->n++],
                        (box_t) BOX_POS_SIZE(ICONS_MARGIN + 160, top + 50, 20, 70),
                        wuss_SLIDER_VERTICAL, 0, 10, 10);

  *state = lay->n;
  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 50, 140, 14),
                       "horizontal: 25", 0);

  lay->y = top + 146;
}

/* A grouping frame captioned "Menu", holding a menu-entry strip: plain,
 * ticked, a swatch entry, a submenu entry, a disabled entry, then a dashed
 * rule and a SEPARATOR-flagged entry below it. Hover the pointer over any live
 * entry to see the highlight track; the rule stays inert. Returns the "Show
 * grid" index (started ticked) via *ticked. */
static void icons_add_menu(icons_layout_t *lay, int *ticked)
{
  wuss_icon_spec_t *s;
  int               top;
  int               row;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, ICONS_ROW * 7 + 30),
                       "Menu");

  row     = top + 20;
  s       = &lay->specs[lay->n];
  s->bbox = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, row, 180, 16);
  s->type = wuss_ICON_TYPE_MENU_ENTRY;
  s->text = "Open";
  s->fg   = lay->black;
  s->bg   = wuss_NO_BACKGROUND;
  lay->n++;

  s       = &lay->specs[lay->n];
  s->bbox = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, row + ICONS_ROW, 180, 16);
  s->type = wuss_ICON_TYPE_MENU_ENTRY;
  s->text = "Show grid";
  s->fg   = lay->black;
  s->bg   = wuss_NO_BACKGROUND;
  *ticked = lay->n;
  lay->n++;

  s         = &lay->specs[lay->n];
  s->bbox   = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, row + ICONS_ROW * 2, 180, 16);
  s->type   = wuss_ICON_TYPE_MENU_ENTRY;
  s->text   = "Layer colour";
  s->fg     = lay->black;
  s->bg     = wuss_NO_BACKGROUND;
  s->u.menu_entry.swatch = lay->red;
  s->flags  = wuss_ICON_FLAGS_SWATCH;
  lay->n++;

  s        = &lay->specs[lay->n];
  s->bbox  = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, row + ICONS_ROW * 3, 180, 16);
  s->type  = wuss_ICON_TYPE_MENU_ENTRY;
  s->text  = "Export";
  s->fg    = lay->black;
  s->bg    = wuss_NO_BACKGROUND;
  s->flags = wuss_ICON_FLAGS_SUBMENU;
  lay->n++;

  s        = &lay->specs[lay->n];
  s->bbox  = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, row + ICONS_ROW * 4, 180, 16);
  s->type  = wuss_ICON_TYPE_MENU_ENTRY;
  s->text  = "Disabled";
  s->fg    = lay->black;
  s->bg    = wuss_NO_BACKGROUND;
  s->flags = wuss_ICON_FLAGS_DISABLED;
  lay->n++;

  s       = &lay->specs[lay->n];
  s->bbox = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, row + ICONS_ROW * 5, 180, 10);
  s->type = wuss_ICON_TYPE_RULE;
  s->fg   = lay->black;
  s->bg   = wuss_NO_BACKGROUND;
  lay->n++;

  s        = &lay->specs[lay->n];
  s->bbox  = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, row + ICONS_ROW * 6, 180, 16);
  s->type  = wuss_ICON_TYPE_MENU_ENTRY;
  s->text  = "Quit";
  s->fg    = lay->black;
  s->bg    = wuss_NO_BACKGROUND;
  s->flags = wuss_ICON_FLAGS_SEPARATOR;
  lay->n++;

  lay->y = top + ICONS_ROW * 7 + 46;
}

/* A grouping frame captioned "Writables" holding two editable fields -- the
 * second small enough to fill up -- then three left, centre and right
 * justified fields, and a label echoing whichever was last edited. Click a field for the caret; Tab / Shift-Tab hop between them.
 * Returns the echo label's index via *echo. */
static void icons_add_writables(icons_layout_t *lay, int *echo)
{
  int top;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 156),
                       "Writables");

  wuss_icon_spec_writable(&lay->specs[lay->n++],
                          (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20, 180, 18),
                          "Edit me", 64, 0);
  wuss_icon_spec_writable(&lay->specs[lay->n++],
                          (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 42, 60, 18),
                          NULL, 6, 0);
  wuss_icon_spec_writable(&lay->specs[lay->n++],
                          (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 64, 180, 18),
                          "Left", 100, wuss_ICON_FLAGS_JUSTIFY_LEFT);
  wuss_icon_spec_writable(&lay->specs[lay->n++],
                          (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 86, 180, 18),
                          "Centre", 100, wuss_ICON_FLAGS_JUSTIFY_CENTRE);
  wuss_icon_spec_writable(&lay->specs[lay->n++],
                          (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 108, 180, 18),
                          "Right", 100, wuss_ICON_FLAGS_JUSTIFY_RIGHT);

  *echo = lay->n;
  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 130, 180, 14),
                       "", 0);

  lay->y = top + 172;
}

/* A frame captioned "caption" around a gadget and a label echoing its picks.
 * The gadget itself is created after the icon array, in *gadget. Returns the
 * echo label's index via *echo. */
static void icons_add_gadget(icons_layout_t *lay,
                             const char     *caption,
                             int            *echo,
                             box_t          *gadget)
{
  int top;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, 68),
                       caption);

  *gadget = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20, 180, 18);

  *echo = lay->n;
  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 42, 180, 14),
                       "", 0);

  lay->y = top + 84;
}

/* A grouping frame captioned "Draggable", holding a DRAGGABLE icon (the pool's
 * "radon" bitmap) and a label below it reporting where the last drag ended.
 * Returns the label's index via *echo. */
static void icons_add_draggable(icons_layout_t *lay,
                                const wuss_t   *wuss,
                                int            *echo)
{
  wuss_icon_spec_t *s;
  const bitmap_t   *bm;
  int               top;
  int               w;
  int               h;

  bm = wuss_iconpool_bitmap(wuss, wuss_iconpool_lookup(wuss, "radon"));
  w  = bm ? bm->size.w : 16;
  h  = bm ? bm->size.h : 16;

  top = lay->y;
  wuss_icon_spec_frame(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN, top, 200, h + 48),
                       "Draggable");

  s                = &lay->specs[lay->n++];
  s->bbox          = (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 20, w, h);
  s->type          = wuss_ICON_TYPE_DRAGGABLE;
  s->u.bitmap.name = "radon";

  *echo = lay->n;
  wuss_icon_spec_label(&lay->specs[lay->n++],
                       (box_t) BOX_POS_SIZE(ICONS_MARGIN + 10, top + 24 + h, 180, 14),
                       "Drag the icon", 0);

  lay->y = top + h + 64;
}

static result_t icons_sset_changed(wuss_stringset_t *stringset,
                                   int               index,
                                   void             *opaque)
{
  icons_task_t *tcx;
  char          buf[32];

  NOT_USED(stringset);

  tcx = opaque;
  snprintf(buf, sizeof(buf), "picked entry %d", index);
  return wuss_icon_set_text(tcx->base.window, tcx->sset_echo, buf);
}

static result_t icons_cset_changed(wuss_colourset_t *colourset,
                                   wuss_colour_t     colour,
                                   void             *opaque)
{
  icons_task_t *tcx;
  char          buf[32];

  NOT_USED(colourset);

  tcx = opaque;
  snprintf(buf, sizeof(buf), "picked colour %d", colour);
  return wuss_icon_set_text(tcx->base.window, tcx->cset_echo, buf);
}

/* ----------------------------------------------------------------------- */

static result_t icons_init(icons_task_t *task)
{
  result_t         rc;
  wuss_t          *wuss;
  wuss_icon_spec_t specs[ICONS_NSPECS];
  wuss_icon_t     *made[ICONS_NSPECS];
  icons_layout_t   lay;
  const char      *resources;
  const char      *sprite_path;
  int              i_button, i_counter, i_opt, i_state, i_hotspot, i_ticked;
  int              i_shoriz, i_svert, i_sstate;
  int              i_tally, i_echo, i_sset_echo, i_cset_echo, i_drag_echo;
  box_t            sset_box, cset_box;

  wuss = task->base.wuss;

  task->font  = wuss_get_font_n(wuss, 0);
  task->label = colour_rgb(0x00, 0x00, 0x00);
  /* only the ruler-text glyph blend; approximates the wuss_COLOUR_WINDOW /
   * wuss_COLOUR_BACKDROP crosshatch the rulers sit on -- no public call
   * resolves a symbolic wuss_colour_t to a concrete colour_t here */
  task->paper = colour_rgb(0xDD, 0xDD, 0xDD);

  resources   = wuss_get_resources(wuss);
  sprite_path = pathf("%s/resources/wuss/ninepatch.png", resources);
  if (bitmap_load_png(&task->sprite, sprite_path) == result_OK)
    task->has_sprite = 1;

  /* the wuss-wide icon set is loaded once by wuss_create itself */

  memset(&lay, 0, sizeof(lay));
  lay.black  = wuss_COLOUR_BLACK;
  lay.window = wuss_COLOUR_WINDOW; /* the standard work-area fill */
  lay.red    = wuss_nearest_colour(wuss, 0xCC, 0x33, 0x33); /* off-primary; no symbol */

  /* crosshatch the standard window colour over the standard backdrop colour,
   * so the texture tracks the chrome config rather than fixed greys */
  rc = wuss_window_create_at(task->base.delegate,
                             &task_place,
                             SIZE2D(ICONS_DOC_W, 200),
                             "Icons",
                             wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                             wuss_BACKDROP_PATTERN(wuss_COLOUR_GREY,
                                                   screen_PATTERN_CROSSHATCH,
                                                   wuss_COLOUR_WINDOW),
                             SIZE2D(ICONS_DOC_W, ICONS_DOC_H),
                             SIZE2D(0, 0),
                             &task->base.window);
  if (rc != result_OK)
    return rc;

  memset(specs, 0, sizeof(specs));
  lay.specs = specs;
  lay.n     = 0;
  lay.y     = 28; /* icons sit past the ruler gutter -- the axis labels
                    * drawn by icons_redraw keep the top/left strip to
                    * themselves */

  i_hotspot = -1;

  icons_add_intro(&lay, &i_button, &i_counter);
  icons_add_buttons(&lay);
  icons_add_radios(&lay, &i_opt, &i_state);
  icons_add_bitmaps(&lay, task->has_sprite ? &task->sprite : NULL, &i_hotspot);
  icons_add_iconset(&lay, wuss);
  icons_add_pattern(&lay);
  icons_add_borders(&lay);
  icons_add_display(&lay, &i_tally);
  icons_add_sliders(&lay, &i_shoriz, &i_svert, &i_sstate);
  icons_add_menu(&lay, &i_ticked);
  icons_add_writables(&lay, &i_echo);
  icons_add_gadget(&lay, "String set", &i_sset_echo, &sset_box);
  icons_add_gadget(&lay, "Colour set", &i_cset_echo, &cset_box);
  icons_add_draggable(&lay, wuss, &i_drag_echo);

  rc = wuss_icon_create_array(task->base.window, specs, lay.n, made);
  if (rc != result_OK)
    return rc;

  task->button  = made[i_button];
  task->counter = made[i_counter];
  task->opt     = made[i_opt];
  task->state   = made[i_state];
  if (i_hotspot >= 0)
    task->hotspot = made[i_hotspot];
  task->slider_horiz = made[i_shoriz];
  task->slider_vert  = made[i_svert];
  task->slider_state = made[i_sstate];
  task->tally        = made[i_tally];
  task->echo         = made[i_echo];
  task->sset_echo    = made[i_sset_echo];
  task->cset_echo    = made[i_cset_echo];
  task->drag_echo    = made[i_drag_echo];
  /* "Show grid" starts ticked */
  wuss_icon_set_selected(task->base.window, made[i_ticked], 1);

  {
    static const char *const sizes[] = { "Small", "Medium", "Large", "Huge" };

    rc = wuss_stringset_create(&task->sset, task->base.window, sset_box, "Size",
                               0, sizes, NELEMS(sizes), icons_sset_changed, task);
    if (rc != result_OK)
      return rc;
  }

  return wuss_colourset_create(&task->cset, task->base.window, cset_box, "Fill",
                               0, lay.red, icons_cset_changed, task);
}

#define ICONS_GRID      16 /* document-space pitch of the backdrop grid */
#define ICONS_AXIS_LABEL 64 /* label every Nth grid line along each axis */

/* Screen coordinate of the first grid line at or after lo. Grid lines sit at
 * document multiples of ICONS_GRID; the screen coordinate of document d is
 * origin - scroll + d, so lines fall on screen coordinates congruent to
 * (origin - scroll) modulo ICONS_GRID. */
static int icons_grid_first(int origin, int scroll, int lo)
{
  int phase;

  phase = (origin - scroll) % ICONS_GRID;
  if (phase < 0)
    phase += ICONS_GRID;

  return lo - ((lo - phase) % ICONS_GRID + ICONS_GRID) % ICONS_GRID;
}

static result_t icons_redraw(const wuss_event_t *event, void *task_data)
{
  icons_task_t *tcx;
  screen_t     *scr;
  const box_t  *content;
  const box_t  *bounds;
  point_t       scroll;
  point_t       pos;
  char          buf[16];
  int           ox;     /* screen x of document x=0 */
  int           oy;     /* screen y of document y=0 */
  int           doc;
  int           x;
  int           y;
  int           ascent;

  tcx = task_data;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;
  scroll  = event->data.redraw.scroll;

  ox = bounds->x0 - scroll.x;
  oy = bounds->y0 - scroll.y;

  bmfont_get_info(tcx->font, NULL, NULL, &ascent, NULL);

  /* Everything this task paints is anchored to the document, not the window,
   * so it scrolls rigidly with the content -- which is what Wuss's scroll
   * blit assumes. Nothing here is pinned to a window edge. Every draw is
   * clipped to the dirty rectangle (content) so partial redraws stay cheap. */

  /* the faint crosshatch backdrop is now the window's own bg (a
   * wuss_backdrop_t pattern fill), painted by Wuss before this event and
   * phase-locked to the scroll origin -- nothing to draw here */

  /* x-axis ruler: document x printed just below the y=0 line, at each
   * labelled grid column. Scrolls with the document like the grid. */
  for (x = icons_grid_first(bounds->x0, scroll.x, content->x0);
       x < content->x1;
       x += ICONS_GRID)
  {
    doc = x - ox;
    if (doc <= 0 || doc % ICONS_AXIS_LABEL != 0)
      continue;

    snprintf(buf, sizeof(buf), "%d", doc);
    pos = POINT(x + 2, oy + 2 + ascent);
    bmfont_draw(tcx->font, scr, buf, (int) strlen(buf),
                tcx->label, tcx->paper, NULL, &pos, NULL);
  }

  /* y-axis ruler: document y printed just right of the x=0 line, at each
   * labelled grid row. */
  for (y = icons_grid_first(bounds->y0, scroll.y, content->y0);
       y < content->y1;
       y += ICONS_GRID)
  {
    doc = y - oy;
    if (doc <= 0 || doc % ICONS_AXIS_LABEL != 0)
      continue;

    snprintf(buf, sizeof(buf), "%d", doc);
    pos = POINT(ox + 2, y + 2 + ascent);
    bmfont_draw(tcx->font, scr, buf, (int) strlen(buf),
                tcx->label, tcx->paper, NULL, &pos, NULL);
  }

  return result_OK;
}

static result_t icons_icon(const wuss_event_t *event, void *task_data)
{
  result_t      rc;
  icons_task_t *tcx;
  wuss_icon_t  *icon;
  char          buf[48];

  tcx  = task_data;
  icon = event->data.icon.icon;

  if (tcx->sset != NULL && wuss_stringset_handle_event(tcx->sset, event, &rc))
    return rc;

  if (tcx->cset != NULL && wuss_colourset_handle_event(tcx->cset, event, &rc))
    return rc;

  if (icon == tcx->slider_horiz || icon == tcx->slider_vert)
  {
    const char *name;

    name = (icon == tcx->slider_horiz) ? "horizontal" : "vertical";
    snprintf(buf, sizeof(buf), "%s: %d", name, event->data.icon.value);
    return wuss_icon_set_text(tcx->base.window, tcx->slider_state, buf);
  }

  /* a writable reports every edit */
  if (wuss_icon_get_type(icon) == wuss_ICON_TYPE_WRITABLE)
  {
    if (event->data.icon.button != wuss_BUTTON_NONE)
      return result_OK; /* just a click placing the caret */

    snprintf(buf, sizeof(buf), "\"%s\"", wuss_icon_get_text(icon));
    return wuss_icon_set_text(tcx->base.window, tcx->echo, buf);
  }

  /* a radio/option latches on MOUSE_UP -- report the state then */
  if (event->data.icon.action == wuss_MOUSE_UP &&
      (wuss_icon_get_type(icon) == wuss_ICON_TYPE_RADIO ||
       wuss_icon_get_type(icon) == wuss_ICON_TYPE_OPTION))
  {
    snprintf(buf, sizeof(buf), "%s: %s%s",
             wuss_icon_get_text(icon),
             wuss_icon_get_selected(icon) ? "on" : "off",
             (icon == tcx->opt) ? "" : " (radio)");
    return wuss_icon_set_text(tcx->base.window, tcx->state, buf);
  }

  if (event->data.icon.action != wuss_MOUSE_DOWN)
    return result_OK;

  if (icon != tcx->button && icon != tcx->hotspot)
    return result_OK;

  tcx->count++;
  snprintf(buf, sizeof(buf), "%d", tcx->count);

  rc = wuss_icon_set_text(tcx->base.window, tcx->tally, buf);
  if (rc != result_OK)
    return rc;

  return wuss_icon_set_text(tcx->base.window, tcx->counter, buf);
}

static result_t icons_handle(wuss_window_t      *window,
                             const wuss_event_t *event,
                             void               *task_data)
{
  icons_task_t *tcx;

  NOT_USED(window);

  tcx = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return icons_redraw(event, task_data);

  case wuss_EVENT_ICON:
    return icons_icon(event, task_data);

  case wuss_EVENT_MENU_SELECT:
  {
    result_t rc;

    if (tcx->sset != NULL && wuss_stringset_handle_event(tcx->sset, event, &rc))
      return rc;

    if (tcx->cset != NULL && wuss_colourset_handle_event(tcx->cset, event, &rc))
      return rc;

    return result_OK;
  }

  case wuss_EVENT_MENU_CLOSED:
  {
    result_t rc;

    if (tcx->sset != NULL) /* never consumed; lets it drop its own handle */
      (void) wuss_stringset_handle_event(tcx->sset, event, &rc);
    if (tcx->cset != NULL)
      (void) wuss_colourset_handle_event(tcx->cset, event, &rc);
    return result_OK;
  }

  case wuss_EVENT_PALETTE:
  {
    result_t rc;

    if (tcx->cset != NULL) /* never consumed; refills its field */
      (void) wuss_colourset_handle_event(tcx->cset, event, &rc);
    return result_OK;
  }

  case wuss_EVENT_DRAG_END:
  {
    char buf[48];

    if (event->data.drag_end.cancelled)
      snprintf(buf, sizeof(buf), "cancelled");
    else if (event->data.drag_end.drop == NULL)
      snprintf(buf, sizeof(buf), "dropped on the backdrop");
    else
      snprintf(buf, sizeof(buf), "dropped on \"%s\"",
               wuss_window_get_title(event->data.drag_end.drop));

    return wuss_icon_set_text(tcx->base.window, tcx->drag_echo, buf);
  }

  case wuss_EVENT_QUIT:
    wuss_stringset_destroy(tcx->sset);
    wuss_colourset_destroy(tcx->cset);
    if (tcx->has_sprite)
      free(tcx->sprite.base);
    return result_OK;

  case wuss_EVENT_KEY:
    return result_WUSS_KEY_UNCLAIMED;

  default:
    return result_OK;
  }
}

result_t icons_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Icons",
    .purpose       = "Every work-area icon type",
    .singleton     = true,
    .instance_size = sizeof(icons_task_t),
    .init          = (taskapp_init_fn_t) icons_init,
    .handle        = icons_handle
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
