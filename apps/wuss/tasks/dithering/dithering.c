/* wuss/tasks/dithering/dithering.c -- error-diffusion dithering demo task */

#ifdef WUSS_APP

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "framebuf/dither.h"
#include "framebuf/octree.h"
#include "io/namelist.h"
#include "io/path.h"
#include "wuss/gadget/swatches.h"
#include "wuss/icon.h"
#include "wuss/menu.h"

#include "../common/common.h"
#include "../palette/palette.h"

#include "dithering.h"
#include "../common/snapshot.h"
#include "../common/taskapp.h"

/* ----------------------------------------------------------------------- */

/* indexed by dither_method_t */
static const char *const dithering_method_names[dither__LIMIT] =
{
  [dither_NONE]                = "Nearest",

  [dither_CLUSTERED]           = "Clustered dot",
  [dither_RANDOM]              = "Random",
  [dither_BAYER_2]             = "Bayer 2x2",
  [dither_BAYER_4]             = "Bayer 4x4",
  [dither_BAYER_8]             = "Bayer 8x8",
  [dither_BAYER_16]            = "Bayer 16x16",
  [dither_BLUE_NOISE]          = "Blue noise",
  [dither_KNOLL]               = "Knoll",

  [dither_FLOYD_STEINBERG]     = "Floyd-Steinberg",
  [dither_JARVIS_JUDICE_NINKE] = "Jarvis-Judice-Ninke",
  [dither_STUCKI]              = "Stucki",
  [dither_ATKINSON]            = "Atkinson",
  [dither_BURKES]              = "Burkes",
  [dither_SIERRA]              = "Sierra",
  [dither_SIERRA_TWO_ROW]      = "Sierra Two-Row",
  [dither_SIERRA_LITE]         = "Sierra Lite",
  [dither_FAN]                 = "Fan",
  [dither_SHIAU_FAN]           = "Shiau-Fan",
  [dither_SHIAU_FAN_2]         = "Shiau-Fan 2",
  [dither_RIEMERSMA]           = "Riemersma",
  [dither_OSTROMOUKHOV]        = "Ostromoukhov",
  [dither_ZHOU_FANG]           = "Zhou-Fang"
};

/* every method but Nearest in menu order: the ordered family, then the error
 * diffusion family, each oldest first */
static const dither_method_t dithering_family_methods[] =
{
  dither_CLUSTERED,           /* printed halftones, 1880s */
  dither_RANDOM,              /* Roberts, 1962 */
  dither_BAYER_2,             /* 1973 */
  dither_BAYER_4,
  dither_BAYER_8,
  dither_BAYER_16,
  dither_BLUE_NOISE,          /* Ulichney, 1993 */
  dither_KNOLL,               /* Knoll, 1999 */

  dither_FLOYD_STEINBERG,     /* 1976 */
  dither_JARVIS_JUDICE_NINKE, /* 1976 */
  dither_STUCKI,              /* 1981 */
  dither_ATKINSON,            /* 1984 */
  dither_BURKES,              /* 1988 */
  dither_SIERRA,              /* 1989 */
  dither_SIERRA_TWO_ROW,      /* 1990 */
  dither_SIERRA_LITE,         /* 1990 */
  dither_FAN,                 /* 1992 */
  dither_SHIAU_FAN,           /* 1996 */
  dither_SHIAU_FAN_2,         /* 1996 */
  dither_RIEMERSMA,           /* 1998 */
  dither_OSTROMOUKHOV,        /* 2001 */
  dither_ZHOU_FANG            /* 2003 */
};

#define DITHERING_NORDERED 8 /* leading ordered entries in the table above */

/* a method's index in dithering_family_methods, or -1 for Nearest */
static int dithering_family_index(dither_method_t method)
{
  int i;

  for (i = 0; i < (int) NELEMS(dithering_family_methods); i++)
    if (dithering_family_methods[i] == method)
      return i;

  return -1;
}

/* ordered methods, Knoll's included, carry no error between pixels, so
 * Serpentine and Linear light do nothing for them. Riemersma walks a curve
 * rather than rows, so it has no use for Serpentine alone. */
static int dithering_ordered(dither_method_t method)
{
  int i;

  i = dithering_family_index(method);
  return i >= 0 && i < DITHERING_NORDERED;
}

/* the Method submenu row a method sits on or under */
static int dithering_method_row(dither_method_t method)
{
  if (method == dither_NONE)
    return DITHERING_METHOD_NEAREST;

  return dithering_ordered(method) ? DITHERING_METHOD_ORDERED
                                   : DITHERING_METHOD_DIFFUSION;
}

#define dithering_rows(method) \
  (!dithering_ordered(method) && (method) != dither_RIEMERSMA)

/* ----------------------------------------------------------------------- */

/* Colours slider position to palette size: logarithmic, so each doubling
 * gets the same travel and every power of two is exact */
static int dithering_colours(int pos)
{
  return (int) floor(pow(2.0, (double) pos / DITHERING_COLOURS_STEPS) + 0.5);
}

static void dithering_free_dithered(dithering_task_t *dt)
{
  if (dt->dithered == NULL)
    return;

  free(dt->dithered->base);
  free(dt->dithered->palette);
  free(dt->dithered);
  dt->dithered = NULL;
}

/* Build the chosen palette and dither the source with it. If keep_palette is
 * set and the last dither succeeded, reuse its palette instead, sparing an
 * octree build when only the strength changed. On failure the window shows
 * the original instead. */
static result_t dithering_dither(dithering_task_t *dt, int keep_palette)
{
  result_t         rc;
  colour_t         palette[256];
  int              ncolours;
  const colour_t  *desktop;
  const char      *name;
  dither_options_t opts;
  bitmap_t        *out;

  if (dt->source.base == NULL)
    return result_OK;

  if (keep_palette && dt->ntarget > 0)
  {
    ncolours = dt->ntarget;
    memcpy(palette, dt->target, ncolours * sizeof(*palette));
    rc = result_OK;
  }
  else if (dt->palette == DITHERING_PALETTE_OPTIMAL)
  {
    rc = octree_make_palette(&dt->source, dithering_colours(dt->colours_pos),
                             palette, &ncolours);
  }
  else if (dt->palette == DITHERING_PALETTE_DESKTOP)
  {
    desktop  = wuss_get_palette(dt->wuss, &ncolours);
    ncolours = MIN(ncolours, 256);
    memcpy(palette, desktop, ncolours * sizeof(*palette));
    rc = result_OK;
  }
  else
  {
    name = dt->palettes[dt->palette - DITHERING_PALETTE_FIRST_HEX];
    rc = palette_load_hex_n(wuss_get_resources(dt->wuss), name, palette,
                            NELEMS(palette), &ncolours);
  }

  if (rc == result_OK)
  {
    opts.method     = dt->method;
    opts.palette    = palette;
    opts.ncolours   = ncolours;
    opts.strength   = dt->strength;
    opts.serpentine = dt->serpentine;
    opts.linear     = dt->linear;
    rc = dither_bitmap(&dt->source, &opts, &out);
  }

  dithering_free_dithered(dt);
  dt->ntarget = 0;
  if (rc == result_OK)
  {
    dt->dithered = out;
    memcpy(dt->target, palette, ncolours * sizeof(*palette));
    dt->ntarget  = ncolours;
  }
  else
  {
    logf_warning("dithering: dither failed (rc=0x%X)", rc);
  }

  if (dt->window)
    wuss_window_invalidate_visible(dt->window);
  if (dt->palette_window)
    wuss_window_invalidate_visible(dt->palette_window);

  return result_OK;
}

static result_t dithering_apply(dithering_task_t *dt)
{
  return dithering_dither(dt, 0);
}

/* wuss_saveas_save_fn_t: opaque is the dithering_task_t */
static result_t dithering_saveas_save(const char *path, void *opaque)
{
  dithering_task_t *dt;

  dt = opaque;
  if (dt->dithered == NULL)
    return result_BAD_ARG; /* the last dither failed; nothing to save */

  return bitmap_save_png(dt->dithered, path);
}

/* Seed Save As with "<image>-dithered.png". */
static void dithering_saveas_name(dithering_task_t *dt)
{
  char leafname[DITHERING_MAX_NAME_LEN + 16];

  snprintf(leafname, sizeof(leafname), "%s-dithered.png", dt->name);
  wuss_saveas_set_leafname(dt->saveas, leafname);
}

/* Load the PNG at path and dither it, resizing the window to fit. A load
 * failure keeps the current image and is returned. */
static result_t dithering_show(dithering_task_t *dt, const char *path)
{
  result_t rc;
  bitmap_t next;
  char     name[DITHERING_MAX_NAME_LEN];

  if (!path_leaf_strip_ext(path_leaf(path), ".png", name, sizeof(name)))
    return result_BAD_ARG;

  rc = bitmap_load_png_deep(&next, path, pixelfmt_rgbx8888);
  if (rc != result_OK)
  {
    logf_warning("dithering: cannot load \"%s\" (rc=0x%X)", path, rc);
    return rc;
  }

  free(dt->source.base);
  dt->source = next;
  strcpy(dt->name, name);
  dithering_saveas_name(dt);

  /* the window opens with the first image, sized and placed for it */
  rc = task_window_fit(dt->delegate, &dt->window, "Dithering",
                       wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                       wuss_NO_BACKDROP, /* the image covers it all */
                       dt->source.size, SIZE2D(0, 0));
  if (rc != result_OK)
    return rc;

  snprintf(dt->path, sizeof(dt->path), "%s", path);
  task_set_doc_title(dt->window, path_leaf(path), "Dithering", 0);
  return dithering_apply(dt);
}

/* ----- slider dialogues ----- */

static result_t dithering_strength_fillout(void *opaque)
{
  dithering_task_t *dt;

  dt = opaque;

  return wuss_slider_row_set(wuss_dialogue_window(dt->strength_dialogue),
                             &dt->strength_row, dt->strength);
}

/* The Colours row echoes its slider position; show the palette size
 * instead. */
static result_t dithering_colours_label(dithering_task_t *dt)
{
  char buf[WUSS_SLIDER_ROW_BUF];

  snprintf(buf, sizeof(buf), "%d", dithering_colours(dt->colours_pos));

  return wuss_icon_set_text(wuss_dialogue_window(dt->colours_dialogue),
                            dt->colours_row.value, buf);
}

static result_t dithering_colours_fillout(void *opaque)
{
  result_t          rc;
  dithering_task_t *dt;

  dt = opaque;

  rc = wuss_slider_row_set(wuss_dialogue_window(dt->colours_dialogue),
                           &dt->colours_row, dt->colours_pos);
  if (rc != result_OK)
    return rc;

  return dithering_colours_label(dt);
}

/* Build a slider dialogue titled and labelled title: the label, a min..max
 * slider and its value echo (fmt, sized to fit widest) in one row. Hover on
 * the menu row opens it as a submenu; every drag re-dithers at once. */
static result_t dithering_slider_create(dithering_task_t           *dt,
                                        const char                 *title,
                                        int                         min,
                                        int                         max,
                                        int                         value,
                                        const char                 *fmt,
                                        const char                 *widest,
                                        wuss_dialogue_fillout_fn_t *fillout,
                                        wuss_dialogue_t           **dialogue,
                                        wuss_slider_row_t          *row)
{
  result_t         rc;
  wuss_icon_spec_t specs[3];
  wuss_icon_t     *made[3];
  char             buf[WUSS_SLIDER_ROW_BUF];
  int              label_w, value_w, width, height;
  box_t            label_box, slider_box, value_box;

  label_w = task_text_width(dt->wuss, title);
  /* +2: a label insets its text a pixel either side, and an overhang would
   * be left behind when the text shrinks */
  value_w = task_text_width(dt->wuss, widest) + 2;
  width   = 2 * wuss_STD_INSET + label_w + DITHERING_SLIDER_W + value_w +
            2 * wuss_STD_GAP;
  height  = 2 * wuss_STD_INSET + wuss_STD_SLIDER_HEIGHT;

  rc = wuss_dialogue_create(dialogue, dt->delegate, SIZE2D(width, height),
                            title, fillout, dt);
  if (rc != result_OK)
    return rc;

  wuss_dialogue_set_focusable(*dialogue, 0);

  label_box  = (box_t) BOX_POS_SIZE(wuss_STD_INSET, wuss_STD_INSET, label_w,
                                    wuss_STD_SLIDER_HEIGHT);
  slider_box = (box_t) BOX_POS_SIZE(label_box.x1 + wuss_STD_GAP,
                                    wuss_STD_INSET, DITHERING_SLIDER_W,
                                    wuss_STD_SLIDER_HEIGHT);
  value_box  = (box_t) BOX_POS_SIZE(slider_box.x1 + wuss_STD_GAP,
                                    wuss_STD_INSET, value_w,
                                    wuss_STD_SLIDER_HEIGHT);

  wuss_icon_spec_label(&specs[0], label_box, title,
                       wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  wuss_icon_spec_slider_row(&specs[1], &specs[2], slider_box, value_box,
                            wuss_SLIDER_HORIZONTAL, min, max, value, fmt, 0,
                            buf, sizeof(buf));

  rc = wuss_icon_create_array(wuss_dialogue_window(*dialogue), specs,
                              NELEMS(specs), made);
  if (rc != result_OK)
  {
    wuss_dialogue_destroy(*dialogue);
    *dialogue = NULL;
    return rc;
  }

  wuss_slider_row_bind(row, made[1], made[2], fmt, min, max, 0);

  return result_OK;
}

/* ----------------------------------------------------------------------- */

/* Sort palettes[] by entry count, keeping name order within a count and
 * dropping any file that won't load, then build one "N-colour" submenu per
 * count as a slice of hex_items. */
static void dithering_group_palettes(dithering_task_t *dt)
{
  const char        *resources;
  int                kept;
  int                i, j;
  colour_t           scratch[256];
  int                count;
  char               name[32];
  int                counts[PALETTE_MAX_FILES];
  dithering_group_t *grp;

  resources = wuss_get_resources(dt->wuss);

  /* insertion sort: name order is kept since only greater counts move */
  kept = 0;
  for (i = 0; i < dt->npalettes; i++)
  {
    if (palette_load_hex_n(resources, dt->palettes[i], scratch,
                           NELEMS(scratch), &count) != result_OK)
      continue;

    strcpy(name, dt->palettes[i]);
    for (j = kept; j > 0 && counts[j - 1] > count; j--)
    {
      strcpy(dt->palettes[j], dt->palettes[j - 1]);
      counts[j] = counts[j - 1];
    }
    strcpy(dt->palettes[j], name);
    counts[j] = count;
    kept++;
  }

  dt->npalettes = 0;
  dt->ngroups   = 0;
  for (i = 0; i < kept; i++)
  {
    if (dt->ngroups == 0 || counts[i] != dt->groups[dt->ngroups - 1].ncolours)
    {
      if (dt->ngroups == DITHERING_MAX_GROUPS)
        break;

      grp           = &dt->groups[dt->ngroups++];
      grp->ncolours = counts[i];
      grp->start    = i;
      grp->n        = 0;
      snprintf(grp->title, sizeof(grp->title), "%d-colour", counts[i]);
    }

    WUSS_MENU_ITEM(dt->hex_items, i, dt->palettes[i], wuss_MENU_ITEM_NONE);
    dt->groups[dt->ngroups - 1].n++;
    dt->npalettes++;
  }

  for (i = 0; i < dt->ngroups; i++)
  {
    grp = &dt->groups[i];
    WUSS_MENU_TITLE(grp->menu, grp->title, &dt->hex_items[grp->start],
                    grp->n);
    WUSS_MENU_ITEM_MENU(dt->fixed_items, DITHERING_FIXED_FIRST_GROUP + i,
                        grp->title,
                        i == 0 ? wuss_MENU_ITEM_DASHED : wuss_MENU_ITEM_NONE,
                        &grp->menu);
  }
}

/* the group holding the current palette, or -1 for Optimal or Desktop */
static int dithering_current_group(const dithering_task_t *dt)
{
  int file;
  int i;

  file = dt->palette - DITHERING_PALETTE_FIRST_HEX;
  for (i = 0; i < dt->ngroups; i++)
    if (file >= dt->groups[i].start &&
        file < dt->groups[i].start + dt->groups[i].n)
      return i;

  return -1;
}

/* the Palette submenu row the current palette sits under */
static int dithering_palette_row(const dithering_task_t *dt)
{
  return dt->palette == DITHERING_PALETTE_OPTIMAL
         ? DITHERING_PALETTE_ROW_COMPUTED : DITHERING_PALETTE_ROW_FIXED;
}

/* the Fixed submenu row the current palette sits on or under, or -1 */
static int dithering_fixed_row(const dithering_task_t *dt)
{
  int group;

  if (dt->palette == DITHERING_PALETTE_DESKTOP)
    return DITHERING_FIXED_DESKTOP;

  group = dithering_current_group(dt);
  return group < 0 ? -1 : DITHERING_FIXED_FIRST_GROUP + group;
}

result_t dithering_create(wuss_t *wuss, dithering_task_t **out)
{
  result_t          rc;
  dithering_task_t *task;
  wuss_task_desc_t  delegate_desc;
  int               i;

  task = calloc(1, sizeof(*task));
  if (task == NULL)
    return result_OOM;

  task->wuss       = wuss;
  task->method     = dither_FLOYD_STEINBERG;
  task->palette    = DITHERING_PALETTE_OPTIMAL;
  task->colours_pos = 4 * DITHERING_COLOURS_STEPS; /* 16 */
  task->strength   = 100;
  task->strength_step = DITHERING_STRENGTH_STEP;
  task->serpentine = 1;

  namelist_scan(pathf("%s/resources/wuss-tasks/palettes",
                      wuss_get_resources(wuss)),
                ".hex", task->palettes[0], sizeof(task->palettes[0]),
                PALETTE_MAX_FILES, 1 /* sorted */, &task->npalettes);

  /* a diffusion method's row sits after the options' rows, the first of
   * them below a rule */
  for (i = 0; i < (int) NELEMS(dithering_family_methods); i++)
    WUSS_MENU_ITEM(task->family_items,
                   i < DITHERING_NORDERED ? i : i + DITHERING_DIFFUSION_FIRST,
                   dithering_method_names[dithering_family_methods[i]],
                   i == DITHERING_NORDERED ? wuss_MENU_ITEM_DASHED
                                           : wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->family_items,
                 DITHERING_NORDERED + DITHERING_DIFFUSION_SERPENTINE,
                 "Serpentine", wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(task->family_items,
                 DITHERING_NORDERED + DITHERING_DIFFUSION_LINEAR,
                 "Linear light", wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->ordered_menu, "Ordered", task->family_items,
                  DITHERING_NORDERED);
  WUSS_MENU_TITLE(task->diffusion_menu, "Error diffusion",
                  &task->family_items[DITHERING_NORDERED],
                  DITHERING_DIFFUSION_FIRST +
                  (int) NELEMS(dithering_family_methods) - DITHERING_NORDERED);

  WUSS_MENU_ITEM(task->method_items, DITHERING_METHOD_NEAREST,
                 dithering_method_names[dither_NONE], wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM_MENU(task->method_items, DITHERING_METHOD_ORDERED,
                      "Ordered", wuss_MENU_ITEM_NONE, &task->ordered_menu);
  WUSS_MENU_ITEM_MENU(task->method_items, DITHERING_METHOD_DIFFUSION,
                      "Error diffusion", wuss_MENU_ITEM_NONE,
                      &task->diffusion_menu);
  WUSS_MENU_TITLE(task->method_menu, "Method", task->method_items,
                  DITHERING_METHOD__LIMIT);

  WUSS_MENU_ITEM(task->computed_items, DITHERING_COMPUTED_OPTIMAL, "Optimal",
                 wuss_MENU_ITEM_NONE);
  /* window set once the dialogue exists */
  WUSS_MENU_ITEM(task->computed_items, DITHERING_COMPUTED_COLOURS, "Colours",
                 wuss_MENU_ITEM_BORROWED_SUBMENU | wuss_MENU_ITEM_PRE_OPEN);
  WUSS_MENU_TITLE(task->computed_menu, "Computed", task->computed_items,
                  DITHERING_COMPUTED__LIMIT);

  WUSS_MENU_ITEM(task->fixed_items, DITHERING_FIXED_DESKTOP, "Desktop",
                 wuss_MENU_ITEM_NONE);
  dithering_group_palettes(task);
  WUSS_MENU_TITLE(task->fixed_menu, "Fixed", task->fixed_items,
                  DITHERING_FIXED_FIRST_GROUP + task->ngroups);

  WUSS_MENU_ITEM_MENU(task->palette_items, DITHERING_PALETTE_ROW_COMPUTED,
                      "Computed", wuss_MENU_ITEM_NONE, &task->computed_menu);
  WUSS_MENU_ITEM_MENU(task->palette_items, DITHERING_PALETTE_ROW_FIXED,
                      "Fixed", wuss_MENU_ITEM_NONE, &task->fixed_menu);
  WUSS_MENU_TITLE(task->palette_menu, "Palette", task->palette_items,
                  DITHERING_PALETTE_ROW__LIMIT);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, DITHERING_MENU_SAVE, "Save",
                          wuss_MENU_ITEM_NONE, "F3");
  /* window set once saveas exists */
  WUSS_MENU_ITEM_MENU(task->menu_items, DITHERING_MENU_METHOD, "Method",
                      wuss_MENU_ITEM_DASHED, &task->method_menu);
  WUSS_MENU_ITEM_MENU(task->menu_items, DITHERING_MENU_PALETTE, "Palette",
                      wuss_MENU_ITEM_NONE, &task->palette_menu);
  WUSS_MENU_ITEM(task->menu_items, DITHERING_MENU_SHOW_PALETTE,
                 "Show palette", wuss_MENU_ITEM_NONE);
  /* window set once the dialogue exists */
  WUSS_MENU_ITEM(task->menu_items, DITHERING_MENU_STRENGTH, "Strength",
                 wuss_MENU_ITEM_BORROWED_SUBMENU | wuss_MENU_ITEM_PRE_OPEN);
  WUSS_MENU_ITEM(task->menu_items, DITHERING_MENU_ANIMATE,
                 "Animate strength", wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->menu, "Dithering", task->menu_items,
                  DITHERING_MENU__LIMIT);

  delegate_desc.handle    = dithering_handle;
  delegate_desc.task_data = task;
  delegate_desc.name      = "dithering";
  rc = wuss_task_create(wuss, &delegate_desc, &task->delegate);
  if (rc != result_OK)
  {
    free(task); /* nothing registered yet; nobody else owns it */
    return rc;
  }

  /* closing the window reaps the task; its QUIT frees the block. The window
   * opens with the first image */
  wuss_task_set_autoclose(task->delegate, 1);

  rc = snapshot_saveas_create(&task->saveas, wuss, NULL, dithering_saveas_save,
                              task);
  if (rc != result_OK)
  {
    wuss_task_destroy(task->delegate); /* QUIT frees the block */
    return rc;
  }
  /* hover opens the Save As dialogue as a submenu; F3 shows it standalone */
  task->menu_items[DITHERING_MENU_SAVE].window =
    wuss_saveas_window(task->saveas);

  rc = dithering_slider_create(task, "Strength", 0, 100, task->strength,
                               "%d%%", "100%", dithering_strength_fillout,
                               &task->strength_dialogue, &task->strength_row);
  if (rc != result_OK)
  {
    wuss_task_destroy(task->delegate); /* QUIT frees the block */
    return rc;
  }
  task->menu_items[DITHERING_MENU_STRENGTH].window =
    wuss_dialogue_window(task->strength_dialogue);

  /* 2..256 colours; the fillout replaces the position echo with the size */
  rc = dithering_slider_create(task, "Colours", 1 * DITHERING_COLOURS_STEPS,
                               8 * DITHERING_COLOURS_STEPS, task->colours_pos,
                               "%d", "256", dithering_colours_fillout,
                               &task->colours_dialogue, &task->colours_row);
  if (rc != result_OK)
  {
    wuss_task_destroy(task->delegate); /* QUIT frees the block */
    return rc;
  }
  task->computed_items[DITHERING_COMPUTED_COLOURS].window =
    wuss_dialogue_window(task->colours_dialogue);

  if (out)
    *out = task;

  return result_OK;
}

result_t dithering_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    "Dithering",
    "Error-diffusion image dithering",
    (taskapp_create_fn_t) dithering_create,
    (taskapp_data_load_fn_t) dithering_show,
    true
  };

  return taskapp_install(wuss, &desc);
}

void dithering_destroy(dithering_task_t *task)
{
  wuss_dialogue_destroy(task->colours_dialogue);
  wuss_dialogue_destroy(task->strength_dialogue);
  wuss_saveas_destroy(task->saveas);
  dithering_free_dithered(task);
  free(task->source.base);
  free(task);
}

/* ----------------------------------------------------------------------- */

/* Show palette: open the swatches window beside the image, or close it */
static result_t dithering_toggle_palette_window(dithering_task_t *dt)
{
  wuss_place_t place;

  if (dt->palette_window != NULL)
  {
    wuss_window_close(dt->palette_window); /* fires no CLOSE */
    dt->palette_window = NULL;
    return result_OK;
  }

  place = task_place_after(dt->window);
  return wuss_window_create_at(dt->delegate, &place,
                               SIZE2D(DITHERING_SWATCHES_W,
                                      DITHERING_SWATCHES_W),
                               "Target palette",
                               wuss_WINDOW_DEFAULT | wuss_WINDOW_NO_RESIZE_BLIT, /* the grid spans the window, so a resize redraws all of it */
                               wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                               SIZE2D(DITHERING_SWATCHES_W,
                                      DITHERING_SWATCHES_W),
                               SIZE2D(0, 0), &dt->palette_window);
}

/* a roughly-square grid of the target palette across the visible content */
static void dithering_redraw_palette(dithering_task_t   *dt,
                                     const wuss_event_t *event)
{
  const box_t    *bounds;
  wuss_swatches_t swatches;

  if (dt->ntarget == 0)
    return; /* the backdrop shows */

  bounds = event->data.redraw.bounds;

  swatches.bbox     = (box_t) BOX_POS_SIZE(0, 0, bounds->x1 - bounds->x0,
                                           bounds->y1 - bounds->y0);
  swatches.columns  = 0;
  swatches.gap      = 0;
  swatches.outline  = 0;
  swatches.colours  = dt->target;
  swatches.ncolours = dt->ntarget;

  wuss_swatches_draw(&swatches, event);
}

static result_t dithering_redraw(dithering_task_t   *dt,
                                 wuss_window_t      *window,
                                 const wuss_event_t *event)
{
  result_t        rc;
  const box_t    *bounds;
  const bitmap_t *bm;
  int             x, y;
  bmfont_t       *font;
  int             ascent;
  point_t         pos;

  if (window == dt->palette_window)
  {
    dithering_redraw_palette(dt, event);
    return result_OK;
  }

  bounds = event->data.redraw.bounds;
  if (dt->source.base == NULL)
    return result_OK; /* unreachable: the window opens with the first image */

  bm = (dt->show_original || dt->dithered == NULL) ? &dt->source
                                                       : dt->dithered;

  x  = bounds->x0 - event->data.redraw.scroll.x;
  y  = bounds->y0 - event->data.redraw.scroll.y;
  rc = screen_copy_bitmap(event->data.redraw.scr, x, y, bm);
  if (rc != result_OK || !dt->show_original)
    return rc;

  /* Nearest does no diffusion: say so over the image's top left */
  font = wuss_get_font(dt->wuss);
  if (font == NULL)
    return result_OK;

  bmfont_get_info(font, NULL, NULL, &ascent, NULL);
  pos = POINT(x + 2, y + 2 + ascent);
  return wuss_text_draw(dt->wuss, 0, event->data.redraw.scr, "(Dither disabled)", 17,
                        colour_rgb(0x00, 0x00, 0x00),
                        colour_rgb(0xFF, 0xFF, 0xFF), &pos, NULL);
}

/* Set or clear item's disabled flag. */
static void dithering_menu_disable(wuss_menu_item_t *item, int disable)
{
  if (disable)
    item->flags |= wuss_MENU_ITEM_DISABLED;
  else
    item->flags &= ~wuss_MENU_ITEM_DISABLED;
}

/* the Error diffusion submenu row of a method, or -1 if it has none */
static int dithering_diffusion_row(dither_method_t method)
{
  int family;

  family = dithering_family_index(method);
  if (family < DITHERING_NORDERED)
    return -1;

  return family - DITHERING_NORDERED + DITHERING_DIFFUSION_FIRST;
}

/* Set the submenus' ticks and the rows' disabled states from the task. Run
 * on open and after every pick: a submenu reopened within the same chain
 * (hover away and back) is built afresh from these items, so the live ticks
 * alone would go stale. Colours only applies to the optimal palette, and
 * Serpentine only to methods which scan rows, so each is disabled
 * otherwise. */
static void dithering_menu_refresh(dithering_task_t *dt)
{
  int family;
  int group;
  int i;

  dithering_menu_disable(&dt->computed_items[DITHERING_COMPUTED_COLOURS],
                         dt->palette != DITHERING_PALETTE_OPTIMAL);
  dithering_menu_disable(&dt->family_items[DITHERING_NORDERED +
                                           DITHERING_DIFFUSION_SERPENTINE],
                         !dithering_rows(dt->method));

  /* a method's tick shows on both its row and its family's row */
  family = dithering_family_index(dt->method);
  wuss_menu_tick_exclusive(&dt->method_menu,
                           dithering_method_row(dt->method));
  wuss_menu_tick_exclusive(&dt->ordered_menu,
                           family < DITHERING_NORDERED ? family : -1);
  wuss_menu_tick_exclusive(&dt->diffusion_menu,
                           dithering_diffusion_row(dt->method));
  wuss_menu_tick_item(&dt->diffusion_menu, DITHERING_DIFFUSION_SERPENTINE,
                      dt->serpentine);
  wuss_menu_tick_item(&dt->diffusion_menu, DITHERING_DIFFUSION_LINEAR,
                      dt->linear);

  /* a palette's tick shows on its own row and on every row leading to it */
  group = dithering_current_group(dt);
  wuss_menu_tick_exclusive(&dt->palette_menu, dithering_palette_row(dt));
  wuss_menu_tick_exclusive(&dt->computed_menu,
                           dt->palette == DITHERING_PALETTE_OPTIMAL
                           ? DITHERING_COMPUTED_OPTIMAL : -1);
  wuss_menu_tick_exclusive(&dt->fixed_menu, dithering_fixed_row(dt));
  for (i = 0; i < dt->ngroups; i++)
    wuss_menu_tick_exclusive(&dt->groups[i].menu,
                             i == group ? dt->palette -
                                          DITHERING_PALETTE_FIRST_HEX -
                                          dt->groups[i].start
                                        : -1);
}

static result_t dithering_menu_open(dithering_task_t *dt)
{
  unsigned int ticks;

  dithering_menu_refresh(dt);

  ticks = 0;
  if (dt->palette_window != NULL)
    ticks |= 1u << DITHERING_MENU_SHOW_PALETTE;
  if (dt->animate)
    ticks |= 1u << DITHERING_MENU_ANIMATE;

  return wuss_menu_open_ticked(dt->delegate, &dt->menu, ticks,
                               wuss_get_pointer(dt->wuss), &dt->menu_handle);
}

static result_t dithering_menu_select(dithering_task_t   *dt,
                                      const wuss_event_t *event)
{
  result_t           rc;
  const wuss_menu_t *menu;
  int                index;
  int                group;

  menu  = event->data.menu_select.menu;
  index = event->data.menu_select.index;

  /* clicking the row, rather than hovering, shows Save As standalone */
  if (menu == &dt->menu && index == DITHERING_MENU_SAVE)
    return wuss_saveas_open(dt->saveas);

  if (menu == &dt->menu && index == DITHERING_MENU_SHOW_PALETTE)
  {
    rc = dithering_toggle_palette_window(dt);
    wuss_menu_tick_item_live(dt->menu_handle, menu, index,
                             dt->palette_window != NULL);
    return rc;
  }

  if (menu == &dt->menu && index == DITHERING_MENU_ANIMATE)
  {
    dt->animate = !dt->animate;
    wuss_menu_tick_item_live(dt->menu_handle, menu, index, dt->animate);
    return result_OK;
  }

  for (group = 0; group < dt->ngroups; group++)
    if (menu == &dt->groups[group].menu)
      break;

  if (menu == &dt->method_menu && index == DITHERING_METHOD_NEAREST)
    dt->method = dither_NONE;
  else if (menu == &dt->ordered_menu)
    dt->method = dithering_family_methods[index];
  else if (menu == &dt->diffusion_menu &&
           index == DITHERING_DIFFUSION_SERPENTINE)
    dt->serpentine = !dt->serpentine;
  else if (menu == &dt->diffusion_menu && index == DITHERING_DIFFUSION_LINEAR)
    dt->linear = !dt->linear;
  else if (menu == &dt->diffusion_menu)
    dt->method = dithering_family_methods[DITHERING_NORDERED + index -
                                          DITHERING_DIFFUSION_FIRST];
  else if (menu == &dt->computed_menu && index == DITHERING_COMPUTED_OPTIMAL)
    dt->palette = DITHERING_PALETTE_OPTIMAL;
  else if (menu == &dt->fixed_menu && index == DITHERING_FIXED_DESKTOP)
    dt->palette = DITHERING_PALETTE_DESKTOP;
  else if (group < dt->ngroups)
    dt->palette = DITHERING_PALETTE_FIRST_HEX + dt->groups[group].start +
                  index;
  else
    return result_OK; /* includes a click on a submenu row */

  dithering_menu_refresh(dt);
  wuss_menu_disable_item_live(dt->menu_handle, &dt->computed_menu,
                              DITHERING_COMPUTED_COLOURS,
                              dt->palette != DITHERING_PALETTE_OPTIMAL);
  wuss_menu_disable_item_live(dt->menu_handle, &dt->diffusion_menu,
                              DITHERING_DIFFUSION_SERPENTINE,
                              !dithering_rows(dt->method));
  if (menu == &dt->diffusion_menu)
  {
    /* the method's tick and the options' ticks are independent */
    wuss_menu_tick_exclusive_live(dt->menu_handle, menu,
                                  dithering_diffusion_row(dt->method));
    wuss_menu_tick_item_live(dt->menu_handle, menu,
                             DITHERING_DIFFUSION_SERPENTINE, dt->serpentine);
    wuss_menu_tick_item_live(dt->menu_handle, menu,
                             DITHERING_DIFFUSION_LINEAR, dt->linear);
  }
  else
  {
    wuss_menu_tick_exclusive_live(dt->menu_handle, menu, index);
  }
  if (menu == &dt->computed_menu || menu == &dt->fixed_menu ||
      group < dt->ngroups)
  {
    /* the other kind's submenu is closed: refresh's data ticks cover it */
    wuss_menu_tick_exclusive_live(dt->menu_handle, &dt->palette_menu,
                                  dithering_palette_row(dt));
    wuss_menu_tick_exclusive_live(dt->menu_handle, &dt->fixed_menu,
                                  dithering_fixed_row(dt));
  }
  if (menu == &dt->ordered_menu || menu == &dt->diffusion_menu)
    wuss_menu_tick_exclusive_live(dt->menu_handle, &dt->method_menu,
                                  dithering_method_row(dt->method));

  return dithering_apply(dt);
}

/* Animate strength: bounce strength between 0 and 100 a step per idle tick,
 * re-dithering each time. The dialogue's slider follows, in case it is
 * open. */
static result_t dithering_idle(dithering_task_t *dt)
{
  result_t rc;
  int      next;

  /* the dialogues outlive the main window until they close too */
  if (!dt->animate || dt->window == NULL || dt->source.base == NULL)
    return result_OK;

  next = dt->strength + dt->strength_step;
  if (next < 0 || next > 100)
  {
    dt->strength_step = -dt->strength_step;
    next = dt->strength + dt->strength_step;
  }
  dt->strength = next;

  rc = wuss_slider_row_set(wuss_dialogue_window(dt->strength_dialogue),
                           &dt->strength_row, dt->strength);
  if (rc != result_OK)
    return rc;

  return dithering_dither(dt, 1);
}

result_t dithering_handle(wuss_window_t      *window,
                          const wuss_event_t *event,
                          void               *task_data)
{
  result_t          rc;
  dithering_task_t *dt;

  dt = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return dithering_redraw(dt, window, event);

  case wuss_EVENT_MOUSE:
    if (window != dt->window || event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;

    if (event->data.mouse.button & wuss_BUTTON_SELECT)
    {
      dt->show_original = !dt->show_original;
      wuss_window_invalidate_visible(dt->window);
    }
    else if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      return dithering_menu_open(dt);
    }
    return result_OK;

  case wuss_EVENT_MENU_SELECT:
    return dithering_menu_select(dt, event);

  case wuss_EVENT_IDLE:
    return dithering_idle(dt);

  case wuss_EVENT_ICON:
    if (window == wuss_dialogue_window(dt->strength_dialogue) &&
        wuss_slider_row_event(window, &dt->strength_row, event,
                              &dt->strength))
      return dithering_dither(dt, 1);

    if (window == wuss_dialogue_window(dt->colours_dialogue) &&
        wuss_slider_row_event(window, &dt->colours_row, event,
                              &dt->colours_pos))
    {
      rc = dithering_colours_label(dt);
      if (rc != result_OK)
        return rc;

      return dithering_apply(dt);
    }
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    if (window == wuss_dialogue_window(dt->strength_dialogue))
      rc = wuss_dialogue_handle_pre_show(dt->strength_dialogue);
    else if (window == wuss_dialogue_window(dt->colours_dialogue))
      rc = wuss_dialogue_handle_pre_show(dt->colours_dialogue);
    else
      rc = result_OK;
    if (rc != result_OK)
      return rc;

    if (event->data.pre_show.handle == NULL)
      return result_OK;
    return wuss_menu_open_window_now(event->data.pre_show.handle,
                                     event->data.pre_show.index);

  case wuss_EVENT_MENU_CLOSED:
    dt->menu_handle = NULL; /* wuss closed the chain under us */
    return result_OK;

  case wuss_EVENT_MESSAGE:
    if (dt->window == NULL || window != dt->window)
      return result_OK;
    return taskapp_window_data_load(dt->wuss, dt->delegate, dt,
                                    (taskapp_data_load_fn_t) dithering_show,
                                    event->data.message);

  case wuss_EVENT_PALETTE:
    if (dt->palette == DITHERING_PALETTE_DESKTOP)
      return dithering_apply(dt);
    return result_OK;

  case wuss_EVENT_CLOSE_ADJUST:
    if (window != dt->window)
      return result_OK;
    return task_close_adjust(dt->wuss, dt->delegate, window, dt->path,
                             event);

  case wuss_EVENT_CLOSE:
    wuss_dialogue_handle_close(dt->strength_dialogue, window);
    wuss_dialogue_handle_close(dt->colours_dialogue, window);
    if (window == dt->palette_window)
      dt->palette_window = NULL;
    if (window == dt->window)
    {
      dt->window = NULL;
      /* the swatches are meaningless without their image */
      wuss_window_close(dt->palette_window);
      dt->palette_window = NULL;
      /* the hidden dialogues are more windows on this autoclose task:
       * close them too, else the task outlives its main window */
      wuss_dialogue_destroy(dt->strength_dialogue);
      dt->strength_dialogue = NULL;
      wuss_dialogue_destroy(dt->colours_dialogue);
      dt->colours_dialogue = NULL;
    }
    return result_OK;

  case wuss_EVENT_QUIT:
    dithering_destroy(dt);
    return result_OK;

  case wuss_EVENT_KEY:
    return wuss_menu_dispatch_shortcut(dt->delegate, &dt->menu, event);

  default:
    return result_OK;
  }
}

#endif /* WUSS_APP */
