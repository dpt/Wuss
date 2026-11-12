/* wuss/tasks/pinboard/pinboard.c -- backdrop window of pinned files */

#ifdef WUSS_APP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "framebuf/screen.h"
#include "io/filetype.h"
#include "io/path.h"
#include "utils/array.h"
#include "wuss/component/colourmenu.h"
#include "wuss/component/error.h"
#include "wuss/gadget/rubberband.h"
#include "wuss/menu.h"
#include "wuss/message.h"

#include "../apps.h"
#include "../common/common.h"
#include "../common/dithermenu.h"
#include "../tasks.h"

#define PINBOARD_CELL_W 96
/* the glyph, its pads and the label lozenge's border, then a line of text */
#define PINBOARD_CELL_H(wuss) (54 + wuss_std_height((wuss), wuss_STD_LABEL))
#define PINBOARD_PAD     4 /* above the glyph and below the label */
#define PINBOARD_MARGIN  8 /* screen edge to the first cell */

/* ponytail: the icon bar's height, hard-coded so pins stay clear of it; ask
 * wuss for it if the bar ever changes size */
#define PINBOARD_ICONBAR 68

#define PINBOARD_ELLIPSIS "..."

typedef struct pin
{
  char       path[DPTLIB_MAXPATH];
  point_t    pos; /* top-left of its cell, screen space */
  int        is_dir;
  filetype_t type;
  int        selected;
}
pin_t;

/* ponytail: a fixed table of actors, slots freed in place so a pending press
 * or drag keeps its index; raise it if more tasks want actors at once */
#define PINBOARD_MAX_ACTORS 16

/* a bitmap another task shows on the backdrop; see wuss_pinboard_actor_t */
typedef struct actor
{
  const wuss_task_t *owner; /* NULL for a free slot */
  int                id;
  const bitmap_t    *bitmap; /* borrowed from the owner */
  point_t            pos;
  int                depth;
}
actor_t;

enum
{
  PINBOARD_IMAGE_NONE,
  PINBOARD_IMAGE_CENTRE,
  PINBOARD_IMAGE_TILE,
  PINBOARD_IMAGE_SCALE,
  PINBOARD_IMAGE__LIMIT
};

enum
{
  PINBOARD_BD_COLOUR,
  PINBOARD_BD_PATTERN,
  PINBOARD_BD_IMAGE,
  PINBOARD_BD_CLEAR,
  PINBOARD_BD__LIMIT
};

enum
{
  PINBOARD_SEL_OPEN,
  PINBOARD_SEL_REVEAL,
  PINBOARD_SEL_REMOVE,
  PINBOARD_SEL__LIMIT
};

enum
{
  PINBOARD_MENU_DISPLAY,
  PINBOARD_MENU_BACKDROP,
  PINBOARD_MENU_SELECTION,
  PINBOARD_MENU_SELECT_ALL,
  PINBOARD_MENU_CLEAR_SELECTION,
  PINBOARD_MENU_TIDY,
  PINBOARD_MENU__LIMIT
};

/* the Pattern submenu's rows */
static const struct
{
  const char      *name;
  screen_pattern_t pattern;
}
pinboard_patterns[] =
{
  { "Solid",      screen_PATTERN_SOLID      },
  { "Grey",       screen_PATTERN_GREY50     },
  { "Diagonal",   screen_PATTERN_DIAGONAL   },
  { "Dots",       screen_PATTERN_DOTS       },
  { "Grid",       screen_PATTERN_GRID       },
  { "Crosshatch", screen_PATTERN_CROSSHATCH }
};

typedef struct pinboard
{
  wuss_t            *wuss;
  wuss_task_t       *task;
  wuss_window_t     *window;

  pin_t             *pins;
  int                npins;
  int                allocated;

  /* a SELECT or ADJUST press: on a pin (press >= 0) it may become a drag;
   * on bare backdrop it draws a rubber band */
  int                press;
  point_t            press_point;
  wuss_rubberband_t  band;
  int                drag;    /* pin being dragged, or -1 */
  point_t            hotspot; /* the press point, relative to its cell */
  unsigned int       open_retry; /* my_ref of a DataOpen re-sent after
                                  * starting the app for its type */

  actor_t            actors[PINBOARD_MAX_ACTORS];
  int                actor_press; /* actor under a SELECT press, or -1 */
  int                actor_drag;  /* actor being dragged, hidden, or -1 */

  wuss_backdrop_t    initial; /* as configured at start-up, for Clear */
  wuss_backdrop_t    backdrop;
  int                have_image;
  bitmap_t           image;
  bitmap_t          *scaled; /* image at screen size, made on demand */
  int                image_mode;
  screen_dither_t    dithering; /* how the image is drawn on paletted screens */

  wuss_colour_t      label_fg;
  wuss_colour_t      label_bg;
  wuss_colour_t      label_accent; /* a selected label's text */

  const bitmap_t    *glyph_dir;

  dithermenu_t       dither; /* "Display" submenu */
  wuss_menu_item_t   pattern_items[NELEMS(pinboard_patterns)];
  wuss_menu_t        pattern_menu;
  wuss_menu_item_t   image_items[PINBOARD_IMAGE__LIMIT];
  wuss_menu_t        image_menu;
  wuss_menu_item_t   bd_items[PINBOARD_BD__LIMIT];
  wuss_menu_t        bd_menu;
  wuss_menu_item_t   sel_items[PINBOARD_SEL__LIMIT];
  wuss_menu_t        sel_menu;
  wuss_menu_item_t   menu_items[PINBOARD_MENU__LIMIT];
  wuss_menu_t        menu;
  wuss_menu_handle_t menu_handle;
}
pinboard_t;

static pinboard_t *g_pinboard;

/* ----------------------------------------------------------------------- */

/* cells run down columns from the top-left, clear of the icon bar */
static int pinboard_rows(const pinboard_t *pb)
{
  size2d_t screen;

  screen = wuss_get_screen_size(pb->wuss);

  return MAX((screen.h - PINBOARD_ICONBAR - PINBOARD_MARGIN) /
             PINBOARD_CELL_H(pb->wuss), 1);
}

static int pinboard_columns(const pinboard_t *pb)
{
  size2d_t screen;

  screen = wuss_get_screen_size(pb->wuss);

  return MAX((screen.w - 2 * PINBOARD_MARGIN) / PINBOARD_CELL_W, 1);
}

static point_t pinboard_cell_pos(const pinboard_t *pb, int cell)
{
  int rows;

  rows = pinboard_rows(pb);

  return POINT(PINBOARD_MARGIN + cell / rows * PINBOARD_CELL_W,
               PINBOARD_MARGIN + cell % rows * PINBOARD_CELL_H(pb->wuss));
}

/* the cell under screen point p, clamped to the grid */
static int pinboard_cell_at(const pinboard_t *pb, point_t p)
{
  int col, row;

  col = CLAMP((p.x - PINBOARD_MARGIN) / PINBOARD_CELL_W, 0,
              pinboard_columns(pb) - 1);
  row = CLAMP((p.y - PINBOARD_MARGIN) / PINBOARD_CELL_H(pb->wuss), 0,
              pinboard_rows(pb) - 1);

  return col * pinboard_rows(pb) + row;
}

/* the first cell from 'from' on, wrapping, that holds no pin but 'except';
 * 'from' itself if every cell is taken */
static int pinboard_free_cell(const pinboard_t *pb, int from, int except)
{
  int     ncells, n, i, cell;
  point_t pos;

  ncells = pinboard_rows(pb) * pinboard_columns(pb);
  for (n = 0; n < ncells; n++)
  {
    cell = (from + n) % ncells;
    pos  = pinboard_cell_pos(pb, cell);
    for (i = 0; i < pb->npins; i++)
      if (i != except && pb->pins[i].pos.x == pos.x &&
          pb->pins[i].pos.y == pos.y)
        break;
    if (i == pb->npins)
      return cell;
  }

  return from;
}

static box_t pinboard_pin_box(const pinboard_t *pb, const pin_t *pin)
{
  box_t box = BOX_POS_SIZE(pin->pos.x, pin->pos.y, PINBOARD_CELL_W,
                           PINBOARD_CELL_H(pb->wuss));

  return box;
}

static void pinboard_invalidate_pin(pinboard_t *pb, int i)
{
  box_t box;

  box = pinboard_pin_box(pb, &pb->pins[i]);
  wuss_window_invalidate(pb->window, &box);
}

/* the pin under screen point p, or -1 */
static int pinboard_hit(const pinboard_t *pb, point_t p)
{
  int   i;
  box_t box;

  for (i = pb->npins - 1; i >= 0; i--)
  {
    box = pinboard_pin_box(pb, &pb->pins[i]);
    if (box_contains_point(&box, p.x, p.y))
      return i;
  }

  return -1;
}

static int pinboard_nselected(const pinboard_t *pb)
{
  int i, n;

  n = 0;
  for (i = 0; i < pb->npins; i++)
    n += pb->pins[i].selected != 0;

  return n;
}

static void pinboard_select(pinboard_t *pb, int i, int selected)
{
  if ((pb->pins[i].selected != 0) == (selected != 0))
    return;

  pb->pins[i].selected = selected;
  pinboard_invalidate_pin(pb, i);
}

static void pinboard_select_all(pinboard_t *pb, int selected)
{
  int i;

  for (i = 0; i < pb->npins; i++)
    pinboard_select(pb, i, selected);
}

/* ----------------------------------------------------------------------- */

/* Pin path in cell, or the first free cell for -1. A path already pinned
 * moves there instead of being pinned twice. */
static result_t pinboard_pin(pinboard_t *pb, const char *path, int cell)
{
  int         i;
  const char *dot;
  pin_t      *pin;

  for (i = 0; i < pb->npins; i++)
    if (strcmp(pb->pins[i].path, path) == 0)
      break;

  if (i == pb->npins)
  {
    if (array_grow((void **) &pb->pins, sizeof(*pb->pins), pb->npins,
                   &pb->allocated, 1, 8))
      return result_OOM;

    pin = &pb->pins[pb->npins++];
    memset(pin, 0, sizeof(*pin));
    snprintf(pin->path, sizeof(pin->path), "%s", path);

    /* a file that cannot be read now is pinned as a plain file */
    pin->is_dir = path_is_dir(path);
    dot         = strrchr(path_leaf(path), '.');
    pin->type   = (dot != NULL && !pin->is_dir) ? filetype_from_ext(dot)
                                                : filetype_DATA;
  }
  else
  {
    pinboard_invalidate_pin(pb, i);
  }

  pb->pins[i].pos = pinboard_cell_pos(pb,
                                      pinboard_free_cell(pb,
                                                         cell < 0 ? 0 : cell,
                                                         i));
  pinboard_invalidate_pin(pb, i);

  return result_OK;
}

/* unpin the selected pins; their files are left alone */
static void pinboard_remove_selection(pinboard_t *pb)
{
  int i, j;

  j = 0;
  for (i = 0; i < pb->npins; i++)
  {
    if (pb->pins[i].selected)
      pinboard_invalidate_pin(pb, i);
    else
      pb->pins[j++] = pb->pins[i];
  }
  pb->npins = j;
}

/* move every pin, in pinning order, into the first cells */
static void pinboard_tidy(pinboard_t *pb)
{
  int i;

  for (i = 0; i < pb->npins; i++)
  {
    pinboard_invalidate_pin(pb, i);
    pb->pins[i].pos = pinboard_cell_pos(pb, i);
    pinboard_invalidate_pin(pb, i);
  }
}

/* as Filing does: a directory goes to the Filer, a file to whichever app
 * claims its DataOpen */
static void pinboard_open(pinboard_t *pb, int i)
{
  const pin_t     *pin;
  wuss_data_open_t open;

  pin = &pb->pins[i];
  if (pin->is_dir)
  {
    (void) wuss_send(pb->wuss, pb->task, NULL, wuss_MESSAGE_FILER_OPEN_DIR,
                     pin->path, strlen(pin->path) + 1, 0);
    return;
  }

  open.filetype = pin->type;
  snprintf(open.path, sizeof(open.path), "%s", pin->path);
  (void) wuss_send_recorded(pb->wuss, pb->task, NULL, wuss_MESSAGE_DATA_OPEN,
                            &open, sizeof(open), 0, NULL);
}

/* as Filing does: a DataOpen no running app claimed starts the app for its
 * type and is sent once more; if there is none, or the second send bounced
 * too, nothing opens the file */
static void pinboard_open_bounced(pinboard_t *pb, const wuss_message_t *msg)
{
  if (msg->action != wuss_MESSAGE_DATA_OPEN)
    return;

  if (msg->my_ref != pb->open_retry &&
      tasks_run_type(((const wuss_data_open_t *) msg->data)->filetype) ==
        result_OK)
  {
    (void) wuss_send_recorded(pb->wuss, pb->task, NULL,
                              wuss_MESSAGE_DATA_OPEN, msg->data, msg->size,
                              0, &pb->open_retry);
    return;
  }

  pb->open_retry = 0;
  (void) wuss_error_report(pb->wuss, "file_unk", "Pinboard",
                           "No application to open this file",
                           wuss_ERROR_BUTTONS_OK, NULL, NULL);
}

static void pinboard_open_selection(pinboard_t *pb)
{
  int i;

  for (i = 0; i < pb->npins; i++)
    if (pb->pins[i].selected)
      pinboard_open(pb, i);
}

/* open a Filer window on the directory holding each selected pin. A root pin
 * has no such directory, so is skipped. */
static void pinboard_reveal_selection(pinboard_t *pb)
{
  int  i;
  char dir[DPTLIB_MAXPATH];

  for (i = 0; i < pb->npins; i++)
    if (pb->pins[i].selected && path_parent(pb->pins[i].path, dir, sizeof(dir)))
      (void) wuss_send(pb->wuss, pb->task, NULL, wuss_MESSAGE_FILER_OPEN_DIR,
                       dir, strlen(dir) + 1, 0);
}

/* ----------------------------------------------------------------------- */

static void pinboard_drop_scaled(pinboard_t *pb)
{
  bitmap_destroy(pb->scaled);
  pb->scaled = NULL;
}

/* hand wuss the backdrop paint. wuss's own centred image is not dithered, so
 * pinboard_redraw draws the image in every mode, over the fill. */
static void pinboard_apply(pinboard_t *pb)
{
  wuss_backdrop_t backdrop;

  pinboard_drop_scaled(pb);

  backdrop       = pb->backdrop;
  backdrop.image = NULL;
  (void) wuss_set_backdrop(pb->wuss, &backdrop);
  wuss_window_invalidate_visible(pb->window);
}

static void pinboard_drop_image(pinboard_t *pb)
{
  if (!pb->have_image)
    return;

  free(pb->image.base);
  pb->have_image = 0;
  pb->image_mode = PINBOARD_IMAGE_NONE;
}

static result_t pinboard_set_image(pinboard_t *pb, const char *path)
{
  result_t rc;
  bitmap_t image;

  rc = bitmap_load_deep(&image, path, pixelfmt_rgba8888);
  if (rc != result_OK)
    return rc;

  pinboard_drop_image(pb);

  pb->image      = image;
  pb->have_image = 1;
  pb->image_mode = PINBOARD_IMAGE_CENTRE;
  pinboard_apply(pb);

  return result_OK;
}

/* ----------------------------------------------------------------------- */

static const bitmap_t *pinboard_glyph(const pinboard_t *pb, const pin_t *pin)
{
  if (pin->is_dir)
    return pb->glyph_dir;

  return task_file_glyph(pb->task, pin->type.riscos);
}

/* the glyph centred at the top of the cell, the leafname below it in a
 * lozenge drawn as a Grid view's selected label is, always present so that it
 * reads against the backdrop; a name too wide for the cell ends in an
 * ellipsis. All clipped to the cell. */
static void pinboard_draw_pin(pinboard_t  *pb,
                              screen_t    *scr,
                              const pin_t *pin)
{
  box_t           cell, clip, saved;
  const bitmap_t *glyph;
  bmfont_t       *font;
  const colour_t *palette;
  colour_t        fg, bg;
  const char     *leaf;
  int             len, height, ascent, end_w, avail, truncated, capsule_w, x, y;
  bmfont_width_t  w, ellipsis_w;
  point_t         pos, end;

  cell = pinboard_pin_box(pb, pin);
  if (box_intersection(&scr->clip, &cell, &clip))
    return;

  saved     = scr->clip;
  scr->clip = clip;

  glyph = pinboard_glyph(pb, pin);
  if (glyph != NULL)
    screen_copy_bitmap(scr, cell.x0 + (PINBOARD_CELL_W - glyph->size.w) / 2,
                       cell.y0 + PINBOARD_PAD, glyph);

  font = wuss_get_font(pb->wuss);
  if (font != NULL)
  {
    palette = wuss_get_palette(pb->wuss, NULL);
    fg      = palette[pin->selected ? pb->label_accent : pb->label_fg];
    bg      = palette[pin->selected ? pb->label_fg : pb->label_bg];

    leaf = path_leaf(pin->path);
    len  = (int) strlen(leaf);
    bmfont_get_info(font, NULL, &height, &ascent, NULL);
    end_w = height / 2;
    avail = PINBOARD_CELL_W - 2 * PINBOARD_PAD - 2 * end_w;

    (void) wuss_text_measure(pb->wuss, 0, leaf, len, INT_MAX, NULL, &w);
    truncated = (int) w > avail;
    if (truncated)
    {
      (void) wuss_text_measure(pb->wuss, 0, PINBOARD_ELLIPSIS,
                               (int) strlen(PINBOARD_ELLIPSIS), INT_MAX, NULL,
                               &ellipsis_w);
      (void) wuss_text_measure(pb->wuss, 0, leaf, len,
                               (bmfont_width_t) MAX(avail - (int) ellipsis_w,
                                                    0),
                               &len, &w);
      w += ellipsis_w;
    }

    capsule_w = (int) w + 2 * end_w;
    x         = cell.x0 + (PINBOARD_CELL_W - capsule_w) / 2;
    y         = cell.y1 - PINBOARD_PAD - height;
    screen_fill_rounded_rect(scr, x - 2, y - 2,
                             SIZE2D(capsule_w + 4, height + 4), end_w - 1, bg);

    pos.x = x + end_w;
    pos.y = y + ascent;
    (void) wuss_text_draw(pb->wuss, 0, scr, leaf, len, fg, bg, &pos, &end);
    if (truncated)
      (void) wuss_text_draw(pb->wuss, 0, scr, PINBOARD_ELLIPSIS,
                            (int) strlen(PINBOARD_ELLIPSIS), fg, bg, &end,
                            NULL);
  }

  scr->clip = saved;
}

/* nonzero if actor a draws over actor b: a deeper layer, or the same one
 * and lower down the screen */
static int pinboard_actor_over(const actor_t *a, const actor_t *b)
{
  if (a->depth != b->depth)
    return a->depth > b->depth;

  return a->pos.y + a->bitmap->size.h > b->pos.y + b->bitmap->size.h;
}

/* Fill order with the shown actors' slots, furthest first, and return how
 * many. Insertion sort: there are few actors and they move little between
 * frames. Equals keep slot order. */
static int pinboard_actor_order(const pinboard_t *pb,
                                int               order[PINBOARD_MAX_ACTORS])
{
  int n;
  int i, j;

  n = 0;
  for (i = 0; i < PINBOARD_MAX_ACTORS; i++)
  {
    if (pb->actors[i].owner == NULL || i == pb->actor_drag)
      continue;
    for (j = n; j > 0 &&
         pinboard_actor_over(&pb->actors[order[j - 1]], &pb->actors[i]); j--)
      order[j] = order[j - 1];
    order[j] = i;
    n++;
  }

  return n;
}

static void pinboard_redraw(pinboard_t *pb, const wuss_event_t *event)
{
  screen_t       *scr;
  size2d_t        screen;
  int             i;
  int             order[PINBOARD_MAX_ACTORS];
  int             n;
  const colour_t *palette;

  scr    = event->data.redraw.scr;
  screen = wuss_get_screen_size(pb->wuss);

  if (pb->have_image && pb->image_mode == PINBOARD_IMAGE_CENTRE)
    (void) screen_copy_bitmap_dithered(scr,
                                       (screen.w - pb->image.size.w) / 2,
                                       (screen.h - pb->image.size.h) / 2,
                                       &pb->image, pb->dithering);

  if (pb->have_image && pb->image_mode == PINBOARD_IMAGE_TILE)
    (void) screen_copy_bitmap_tiled_dithered(scr, event->data.redraw.content,
                                             &pb->image, POINT(0, 0),
                                             pb->dithering);

  if (pb->have_image && pb->image_mode == PINBOARD_IMAGE_SCALE)
  {
    if (pb->scaled == NULL)
      (void) bitmap_resize(&pb->image, screen, bitmap_resample_BILINEAR, 0,
                           &pb->scaled);
    if (pb->scaled != NULL)
      (void) screen_copy_bitmap_dithered(scr, 0, 0, pb->scaled,
                                         pb->dithering);
  }

  for (i = 0; i < pb->npins; i++)
    pinboard_draw_pin(pb, scr, &pb->pins[i]);

  n = pinboard_actor_order(pb, order);
  for (i = 0; i < n; i++)
    (void) screen_copy_bitmap(scr, pb->actors[order[i]].pos.x,
                              pb->actors[order[i]].pos.y,
                              pb->actors[order[i]].bitmap);

  palette = wuss_get_palette(pb->wuss, NULL);
  wuss_rubberband_draw(&pb->band, event, palette[pb->label_fg]);
}

/* ----------------------------------------------------------------------- */

/* the band is released: select every pin it touches */
static void pinboard_band_end(pinboard_t *pb)
{
  int   i;
  box_t box, clipped;

  wuss_rubberband_end(&pb->band, pb->window);

  for (i = 0; i < pb->npins; i++)
  {
    box = pinboard_pin_box(pb, &pb->pins[i]);
    if (!box_intersection(&pb->band.box, &box, &clipped))
      pinboard_select(pb, i, 1);
  }
}

static void pinboard_drag_maybe(pinboard_t *pb, point_t p)
{
  result_t        rc;
  const pin_t    *pin;
  const bitmap_t *glyph;
  point_t         at;

  if (!wuss_drag_past_threshold(pb->wuss, pb->press_point, p))
    return;

  pin         = &pb->pins[pb->press];
  pb->hotspot = POINT(pb->press_point.x - pin->pos.x,
                      pb->press_point.y - pin->pos.y);

  glyph = pinboard_glyph(pb, pin);
  if (glyph != NULL)
  {
    at = POINT(pb->hotspot.x - (PINBOARD_CELL_W - glyph->size.w) / 2,
               pb->hotspot.y - PINBOARD_PAD);
    rc = wuss_drag_start_bitmap(pb->wuss, pb->window, glyph, at);
  }
  else
  {
    rc = wuss_drag_start(pb->wuss, pb->window,
                         SIZE2D(PINBOARD_CELL_W, PINBOARD_CELL_H(pb->wuss)),
                         pb->hotspot);
  }

  pb->drag  = rc == result_OK ? pb->press : -1;
  pb->press = -1;
}

/* a pin dragged onto the backdrop moves to the cell it was dropped on; onto
 * any other window it is offered to that window as a DataLoad, as a file
 * dragged from Filing is */
static void pinboard_drag_end(pinboard_t *pb, const wuss_event_t *event)
{
  int               i;
  point_t           p;
  wuss_msg_target_t target;

  i        = pb->drag;
  pb->drag = -1;
  if (i < 0 || i >= pb->npins || event->data.drag_end.cancelled ||
      event->data.drag_end.drop == NULL)
    return;

  if (event->data.drag_end.drop == pb->window)
  {
    p = wuss_get_pointer(pb->wuss);
    p = POINT(p.x - pb->hotspot.x + PINBOARD_CELL_W / 2,
              p.y - pb->hotspot.y + PINBOARD_CELL_H(pb->wuss) / 2);
    pinboard_invalidate_pin(pb, i);
    pb->pins[i].pos = pinboard_cell_pos(pb,
                                        pinboard_free_cell(pb,
                                                           pinboard_cell_at(pb, p),
                                                           i));
    pinboard_invalidate_pin(pb, i);
    return;
  }

  target.window = event->data.drag_end.drop;
  target.icon   = event->data.drag_end.drop_icon;
  (void) wuss_send_to(pb->wuss, pb->task, &target, wuss_MESSAGE_DATA_LOAD,
                      pb->pins[i].path, strlen(pb->pins[i].path) + 1, 0);
}

/* ----------------------------------------------------------------------- */

static box_t pinboard_actor_box(const actor_t *a)
{
  box_t box = BOX_POS_SIZE(a->pos.x, a->pos.y, a->bitmap->size.w,
                           a->bitmap->size.h);

  return box;
}

static void pinboard_invalidate_actor(pinboard_t *pb, int i)
{
  box_t box;

  box = pinboard_actor_box(&pb->actors[i]);
  wuss_window_invalidate(pb->window, &box);
}

/* the slot holding owner's actor id, or -1 */
static int pinboard_actor_find(const pinboard_t  *pb,
                               const wuss_task_t *owner,
                               int                id)
{
  int i;

  for (i = 0; i < PINBOARD_MAX_ACTORS; i++)
    if (pb->actors[i].owner == owner && pb->actors[i].id == id)
      return i;

  return -1;
}

/* the topmost shown actor under screen point p, or -1
 * ponytail: hits the bitmap's box, transparent corners included; test the
 * pixel's alpha if actors get odd shapes */
static int pinboard_actor_hit(const pinboard_t *pb, point_t p)
{
  int   order[PINBOARD_MAX_ACTORS];
  int   i;
  box_t box;

  for (i = pinboard_actor_order(pb, order) - 1; i >= 0; i--)
  {
    box = pinboard_actor_box(&pb->actors[order[i]]);
    if (box_contains_point(&box, p.x, p.y))
      return order[i];
  }

  return -1;
}

static void pinboard_actor_remove(pinboard_t *pb, int i)
{
  if (i != pb->actor_drag)
    pinboard_invalidate_actor(pb, i);

  pb->actors[i].owner = NULL;
  if (pb->actor_press == i)
    pb->actor_press = -1;
  if (pb->actor_drag == i)
    pb->actor_drag = -1; /* its DRAG_END then finds nothing to drop */
}

/* a wuss_MESSAGE_PINBOARD_ACTOR from owner */
static void pinboard_actor_message(pinboard_t                  *pb,
                                   const wuss_task_t           *owner,
                                   const wuss_pinboard_actor_t *msg)
{
  int i;

  i = pinboard_actor_find(pb, owner, msg->id);

  if (msg->op == wuss_PINBOARD_ACTOR_REMOVE)
  {
    if (i >= 0)
      pinboard_actor_remove(pb, i);
    return;
  }

  if (msg->bitmap == NULL)
    return;

  if (i < 0)
  {
    for (i = 0; i < PINBOARD_MAX_ACTORS; i++)
      if (pb->actors[i].owner == NULL)
        break;
    if (i == PINBOARD_MAX_ACTORS)
      return; /* full: the actor just isn't shown */
  }
  else if (i != pb->actor_drag)
  {
    pinboard_invalidate_actor(pb, i);
  }

  pb->actors[i].owner  = owner;
  pb->actors[i].id     = msg->id;
  pb->actors[i].bitmap = msg->bitmap;
  pb->actors[i].pos    = msg->pos;
  pb->actors[i].depth  = msg->depth;

  if (i != pb->actor_drag)
    pinboard_invalidate_actor(pb, i);
}

/* broadcast an actor event about actor i: ref heads a payload of size
 * bytes, the rest of it filled in by the caller */
static void pinboard_actor_send(pinboard_t                *pb,
                                int                        i,
                                int                        action,
                                wuss_pinboard_actor_ref_t *ref,
                                size_t                     size)
{
  ref->owner = pb->actors[i].owner;
  ref->id    = pb->actors[i].id;
  (void) wuss_send(pb->wuss, pb->task, NULL, action, ref, size, 0);
}

/* a SELECT or ADJUST press on actor i: tell its owner */
static void pinboard_actor_click(pinboard_t   *pb,
                                 int           i,
                                 wuss_button_t button,
                                 point_t       p)
{
  wuss_pinboard_actor_click_t click;

  click.button = button;
  click.point  = p;
  pinboard_actor_send(pb, i, wuss_MESSAGE_PINBOARD_ACTOR_CLICK,
                      &click.actor, sizeof(click));
}

static void pinboard_actor_drag_maybe(pinboard_t *pb, point_t p)
{
  int            i;
  const actor_t *a;

  if (!wuss_drag_past_threshold(pb->wuss, pb->press_point, p))
    return;

  i               = pb->actor_press;
  pb->actor_press = -1;
  a               = &pb->actors[i];
  pb->hotspot     = POINT(pb->press_point.x - a->pos.x,
                          pb->press_point.y - a->pos.y);

  if (wuss_drag_start_bitmap(pb->wuss, pb->window, a->bitmap,
                             pb->hotspot) != result_OK)
    return;

  pinboard_invalidate_actor(pb, i); /* hidden while its image is dragged */
  pb->actor_drag = i;
}

/* an actor drag ended: show it where it was dropped, or where it was if
 * cancelled, and tell its owner */
static void pinboard_actor_drag_end(pinboard_t         *pb,
                                    const wuss_event_t *event)
{
  int                        i;
  point_t                    p;
  wuss_pinboard_actor_drop_t drop;

  i              = pb->actor_drag;
  pb->actor_drag = -1;

  if (!event->data.drag_end.cancelled)
  {
    p                 = wuss_get_pointer(pb->wuss);
    pb->actors[i].pos = POINT(p.x - pb->hotspot.x, p.y - pb->hotspot.y);
  }
  pinboard_invalidate_actor(pb, i);

  drop.pos       = pb->actors[i].pos;
  drop.cancelled = event->data.drag_end.cancelled;
  pinboard_actor_send(pb, i, wuss_MESSAGE_PINBOARD_ACTOR_DROP, &drop.actor,
                      sizeof(drop));
}

/* ----------------------------------------------------------------------- */

/* the first selected PNG or JPEG pin, which Image > Centre, Tile and Scale
 * take as the backdrop image, or -1 */
static int pinboard_selected_image(const pinboard_t *pb)
{
  int i;

  for (i = 0; i < pb->npins; i++)
    if (pb->pins[i].selected && !pb->pins[i].is_dir &&
        bitmap_loads_filetype(pb->pins[i].type.riscos))
      return i;

  return -1;
}

/* shade and tick the rows to match the Pinboard, in the descriptions for the
 * next open and in place on any level now open, as an ADJUST pick leaves
 * the menu open */
static void pinboard_menu_update(pinboard_t *pb)
{
  wuss_menu_handle_t h;
  int                i, set, no_image;

  h   = pb->menu_handle;
  set = pb->npins == 0;
  wuss_menu_disable_item_live(h, &pb->menu, PINBOARD_MENU_SELECT_ALL, set);
  wuss_menu_disable_item_live(h, &pb->menu, PINBOARD_MENU_TIDY, set);

  set = pinboard_nselected(pb) == 0;
  wuss_menu_disable_item_live(h, &pb->menu, PINBOARD_MENU_SELECTION, set);
  wuss_menu_disable_item_live(h, &pb->menu, PINBOARD_MENU_CLEAR_SELECTION,
                              set);
  for (i = 0; i < PINBOARD_SEL__LIMIT; i++)
    wuss_menu_disable_item_live(h, &pb->sel_menu, i, set);

  wuss_menu_disable_item_live(h, &pb->bd_menu, PINBOARD_BD_CLEAR,
                              pb->image_mode == PINBOARD_IMAGE_NONE);

  for (i = 0; i < (int) NELEMS(pinboard_patterns); i++)
    wuss_menu_tick_item_live(h, &pb->pattern_menu, i,
                             pb->backdrop.fill.pattern ==
                             pinboard_patterns[i].pattern);

  no_image = !pb->have_image && pinboard_selected_image(pb) < 0;
  for (i = 0; i < PINBOARD_IMAGE__LIMIT; i++)
  {
    wuss_menu_tick_item_live(h, &pb->image_menu, i, i == pb->image_mode);
    wuss_menu_disable_item_live(h, &pb->image_menu, i,
                                i != PINBOARD_IMAGE_NONE && no_image);
  }
}

static result_t pinboard_menu_open(pinboard_t *pb)
{
  pinboard_menu_update(pb);
  return wuss_menu_open_at_pointer(pb->task, &pb->menu, &pb->menu_handle);
}

static result_t pinboard_menu_select(pinboard_t         *pb,
                                     const wuss_event_t *event)
{
  result_t           rc;
  const wuss_menu_t *menu;
  int                index, ok, pin;
  wuss_colour_t      colour;

  menu  = event->data.menu_select.menu;
  index = event->data.menu_select.index;

  colour = wuss_colourmenu_selected(event, &ok);
  if (ok)
  {
    if (colour != wuss_NO_BACKGROUND)
    {
      pb->backdrop.fill.colour = colour;
      pinboard_apply(pb);
    }
    return result_OK;
  }

  if (menu == &pb->dither.menu)
  {
    pb->dithering = dithermenu_select(&pb->dither, pb->menu_handle, index);
    wuss_window_invalidate_visible(pb->window);
    return result_OK;
  }

  if (menu == &pb->pattern_menu)
  {
    pb->backdrop.fill.pattern = pinboard_patterns[index].pattern;
    if (pb->backdrop.fill.pattern_bg == wuss_NO_BACKGROUND)
      pb->backdrop.fill.pattern_bg = pb->label_bg;
    pinboard_apply(pb);
    return result_OK;
  }

  if (menu == &pb->image_menu)
  {
    /* a selected image pin replaces the image; a drop with SHIFT sets one
     * too */
    pin = pinboard_selected_image(pb);
    if (index != PINBOARD_IMAGE_NONE && pin >= 0)
    {
      rc = pinboard_set_image(pb, pb->pins[pin].path);
      if (rc != result_OK)
        return rc;
    }
    if (index != PINBOARD_IMAGE_NONE && !pb->have_image)
      return result_OK;
    pb->image_mode = index;
    pinboard_apply(pb);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &pb->bd_menu, PINBOARD_BD_CLEAR))
  {
    pb->image_mode = PINBOARD_IMAGE_NONE;
    pb->backdrop   = pb->initial;
    pinboard_apply(pb);
    pinboard_drop_image(pb);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &pb->sel_menu, PINBOARD_SEL_OPEN))
  {
    pinboard_open_selection(pb);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &pb->sel_menu, PINBOARD_SEL_REVEAL))
  {
    pinboard_reveal_selection(pb);
    return result_OK;
  }

  if (wuss_menu_select_is(event, &pb->sel_menu, PINBOARD_SEL_REMOVE))
  {
    pinboard_remove_selection(pb);
    return result_OK;
  }

  if (menu != &pb->menu)
    return result_OK;

  switch (index)
  {
  case PINBOARD_MENU_SELECT_ALL:
    pinboard_select_all(pb, 1);
    break;

  case PINBOARD_MENU_CLEAR_SELECTION:
    pinboard_select_all(pb, 0);
    break;

  case PINBOARD_MENU_TIDY:
    pinboard_tidy(pb);
    break;

  default:
    break;
  }

  return result_OK;
}

/* ----------------------------------------------------------------------- */

/* forget any press on a pin or an actor: it can no longer become a drag */
static void pinboard_press_clear(pinboard_t *pb)
{
  pb->press       = -1;
  pb->actor_press = -1;
}

static result_t pinboard_mouse(pinboard_t *pb, const wuss_event_t *event)
{
  point_t       p;
  wuss_button_t button;
  int           i;
  int           a;

  p      = event->data.mouse.point;
  button = event->data.mouse.button;

  switch (event->data.mouse.action)
  {
  case wuss_MOUSE_MOVE:
    /* hover moves carry no button state: only a captured move is a drag */
    if (!wuss_is_capturing(pb->wuss))
    {
      pinboard_press_clear(pb);
    }
    else if (pb->band.active)
    {
      wuss_rubberband_to(&pb->band, pb->window, p);
    }
    else if (pb->actor_press >= 0)
    {
      pinboard_actor_drag_maybe(pb, p);
    }
    else if (pb->press >= 0)
    {
      pinboard_drag_maybe(pb, p);
    }
    return result_OK;

  case wuss_MOUSE_UP:
    if (pb->band.active)
      pinboard_band_end(pb);
    pinboard_press_clear(pb);
    return result_OK;

  default:
    break;
  }

  i = pinboard_hit(pb, p);

  if (button & wuss_BUTTON_MENU)
  {
    /* as a Filer: MENU over a pin with nothing selected selects it */
    if (i >= 0 && pinboard_nselected(pb) == 0)
      pinboard_select(pb, i, 1);
    return pinboard_menu_open(pb);
  }

  if (!(button & (wuss_BUTTON_SELECT | wuss_BUTTON_ADJUST)))
    return result_OK;

  pb->press_point = p;

  /* actors sit over pins: a press on one is its owner's, not a selection */
  a = pinboard_actor_hit(pb, p);
  if (a >= 0)
  {
    pinboard_actor_click(pb, a, button, p);
    if (button & wuss_BUTTON_SELECT)
      pb->actor_press = a;
    return result_OK;
  }

  if (i < 0)
  {
    if (button & wuss_BUTTON_SELECT)
      pinboard_select_all(pb, 0);
    wuss_rubberband_start(&pb->band, pb->window, p);
    return result_OK;
  }

  pb->press = i;

  if (button & wuss_BUTTON_ADJUST)
  {
    pinboard_select(pb, i, !pb->pins[i].selected);
  }
  else if (!pb->pins[i].selected)
  {
    /* SELECT on a selected pin keeps the selection, so it can be dragged */
    pinboard_select_all(pb, 0);
    pinboard_select(pb, i, 1);
  }

  if (button & wuss_BUTTON_DOUBLE)
  {
    pinboard_select(pb, i, 0);
    pinboard_open(pb, i);
  }

  return result_OK;
}

static result_t pinboard_message(pinboard_t         *pb,
                                 wuss_window_t      *window,
                                 const wuss_event_t *event)
{
  const wuss_message_t *msg;
  char                  path[DPTLIB_MAXPATH];
  const char           *dot;
  int                   cell;
  int                   i;

  msg = event->data.message;

  if (msg->action == wuss_MESSAGE_TASK_CLOSE_DOWN)
  {
    for (i = 0; i < PINBOARD_MAX_ACTORS; i++)
      if (pb->actors[i].owner == msg->sender)
        pinboard_actor_remove(pb, i);
    return result_OK;
  }

  if (msg->action == wuss_MESSAGE_PINBOARD_ACTOR)
  {
    if (msg->size != sizeof(wuss_pinboard_actor_t))
      return result_OK;
    pinboard_actor_message(pb, msg->sender,
                           (const wuss_pinboard_actor_t *) msg->data);
    return wuss_acknowledge(pb->wuss, pb->task, msg);
  }

  if (msg->action != wuss_MESSAGE_PINBOARD_PIN &&
      (msg->action != wuss_MESSAGE_DATA_LOAD || window != pb->window))
    return result_OK; /* a DataSave is left to bounce: pins are references */

  snprintf(path, sizeof(path), "%.*s", (int) msg->size,
           (const char *) msg->data);
  if (path[0] == '\0')
    return result_OK;

  if (msg->action == wuss_MESSAGE_PINBOARD_PIN)
  {
    cell = -1;
  }
  else if (wuss_get_modifiers(pb->wuss) & wuss_KEY_MOD_SHIFT)
  {
    /* SHIFT and drop: an image becomes the backdrop instead of a pin */
    dot = strrchr(path_leaf(path), '.');
    if (dot == NULL ||
        !bitmap_loads_filetype(filetype_from_ext(dot).riscos) ||
        pinboard_set_image(pb, path) != result_OK)
      return result_OK;
    return wuss_acknowledge(pb->wuss, pb->task, msg);
  }
  else
  {
    cell = pinboard_cell_at(pb, wuss_get_pointer(pb->wuss));
  }

  if (pinboard_pin(pb, path, cell) != result_OK)
    return result_OK;

  return wuss_acknowledge(pb->wuss, pb->task, msg);
}

static void pinboard_destroy(pinboard_t *pb)
{
  if (g_pinboard == pb)
    g_pinboard = NULL;

  pinboard_drop_scaled(pb);
  pinboard_drop_image(pb);
  free(pb->pins);
  free(pb);
}

static result_t pinboard_handle(wuss_window_t      *window,
                                const wuss_event_t *event,
                                void               *task_data)
{
  result_t    rc;
  pinboard_t *pb;

  pb = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    pinboard_redraw(pb, event);
    return result_OK;

  case wuss_EVENT_MOUSE:
    return pinboard_mouse(pb, event);

  case wuss_EVENT_POINTER_EXIT:
    pinboard_press_clear(pb);
    return result_OK;

  case wuss_EVENT_RELAYOUT:
    /* a new screen size: the scaled image is remade at the next redraw */
    pinboard_drop_scaled(pb);
    wuss_window_invalidate_visible(pb->window);
    return result_OK;

  case wuss_EVENT_DRAG_END:
    if (pb->actor_drag >= 0)
      pinboard_actor_drag_end(pb, event);
    else
      pinboard_drag_end(pb, event);
    return result_OK;

  case wuss_EVENT_PRE_SUBMENU_OPEN:
    rc = wuss_colourmenu_open_rgb(pb->wuss, event, "Backdrop colour",
                                  wuss_get_palette(pb->wuss, NULL)
                                    [pb->backdrop.fill.colour]);
    return rc;

  case wuss_EVENT_MENU_SELECT:
    rc = pinboard_menu_select(pb, event);
    pinboard_menu_update(pb);
    return rc;

  case wuss_EVENT_MENU_CLOSED:
    pb->menu_handle = NULL;
    return result_OK;

  case wuss_EVENT_MESSAGE:
    return pinboard_message(pb, window, event);

  case wuss_EVENT_MESSAGE_BOUNCED:
    pinboard_open_bounced(pb, event->data.message);
    return result_OK;

  case wuss_EVENT_QUIT:
    pinboard_destroy(pb);
    return result_OK;

  default:
    return result_OK;
  }
}

/* ----------------------------------------------------------------------- */

static void pinboard_menus(pinboard_t *pb)
{
  int i;

  for (i = 0; i < (int) NELEMS(pinboard_patterns); i++)
    WUSS_MENU_ITEM(pb->pattern_items, i, pinboard_patterns[i].name,
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(pb->pattern_menu, "Pattern", pb->pattern_items,
                  NELEMS(pb->pattern_items));

  WUSS_MENU_ITEM(pb->image_items, PINBOARD_IMAGE_NONE, "None",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(pb->image_items, PINBOARD_IMAGE_CENTRE, "Centre",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(pb->image_items, PINBOARD_IMAGE_TILE, "Tile",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(pb->image_items, PINBOARD_IMAGE_SCALE, "Scale",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(pb->image_menu, "Image", pb->image_items,
                  NELEMS(pb->image_items));

  /* the colour row's submenu is the shared colourmenu, opened per use */
  WUSS_MENU_ITEM_MENU(pb->bd_items, PINBOARD_BD_COLOUR, "Colour",
                      wuss_MENU_ITEM_PRE_OPEN |
                      wuss_MENU_ITEM_BORROWED_SUBMENU,
                      wuss_colourmenu_menu(pb->wuss));
  WUSS_MENU_ITEM_MENU(pb->bd_items, PINBOARD_BD_PATTERN, "Pattern",
                      wuss_MENU_ITEM_NONE, &pb->pattern_menu);
  WUSS_MENU_ITEM_MENU(pb->bd_items, PINBOARD_BD_IMAGE, "Image",
                      wuss_MENU_ITEM_NONE, &pb->image_menu);
  WUSS_MENU_ITEM(pb->bd_items, PINBOARD_BD_CLEAR, "Clear",
                 wuss_MENU_ITEM_DASHED);
  WUSS_MENU_TITLE(pb->bd_menu, "Backdrop", pb->bd_items,
                  NELEMS(pb->bd_items));

  WUSS_MENU_ITEM(pb->sel_items, PINBOARD_SEL_OPEN, "Open",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(pb->sel_items, PINBOARD_SEL_REVEAL, "Reveal",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(pb->sel_items, PINBOARD_SEL_REMOVE, "Remove",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(pb->sel_menu, "Selection", pb->sel_items,
                  NELEMS(pb->sel_items));

  dithermenu_init(&pb->dither, pb->dithering);

  WUSS_MENU_ITEM_MENU(pb->menu_items, PINBOARD_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_NONE, &pb->dither.display);
  WUSS_MENU_ITEM_MENU(pb->menu_items, PINBOARD_MENU_BACKDROP, "Backdrop",
                      wuss_MENU_ITEM_NONE, &pb->bd_menu);
  WUSS_MENU_ITEM_MENU(pb->menu_items, PINBOARD_MENU_SELECTION, "Selection",
                      wuss_MENU_ITEM_NONE, &pb->sel_menu);
  WUSS_MENU_ITEM(pb->menu_items, PINBOARD_MENU_SELECT_ALL, "Select all",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(pb->menu_items, PINBOARD_MENU_CLEAR_SELECTION,
                 "Clear selection", wuss_MENU_ITEM_NONE);
  WUSS_MENU_ITEM(pb->menu_items, PINBOARD_MENU_TIDY, "Tidy",
                 wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(pb->menu, "Pinboard", pb->menu_items,
                  NELEMS(pb->menu_items));
}

result_t pinboard_app_create(wuss_t *wuss)
{
  result_t         rc;
  pinboard_t      *pb;
  wuss_config_t    config;
  wuss_task_desc_t desc;
  box_t            box;

  if (g_pinboard != NULL)
    return result_OK;

  pb = calloc(1, sizeof(*pb));
  if (pb == NULL)
    return result_OOM;

  wuss_get_config(wuss, &config);

  pb->wuss         = wuss;
  pb->press        = -1;
  pb->drag         = -1;
  pb->actor_press  = -1;
  pb->actor_drag   = -1;
  pb->initial      = config.backdrop;
  pb->backdrop     = config.backdrop;
  pb->label_fg     = config.button.fg;
  pb->label_bg     = config.body.window;
  pb->label_accent = config.accent.colour;
  pb->dithering    = screen_DITHER_BLUE_NOISE;

  pinboard_menus(pb);

  desc.handle    = pinboard_handle;
  desc.task_data = pb;
  desc.name      = "Pinboard";
  rc = wuss_task_create(wuss, &desc, &pb->task);
  if (rc != result_OK)
  {
    free(pb);
    return rc;
  }

  pb->glyph_dir = wuss_task_iconpool_find(pb->task, "file_dir");

  /* wuss sizes the backdrop window to the screen; the box is ignored */
  box = (box_t) BOX_POS_SIZE(0, 0, 1, 1);
  rc  = wuss_window_create(pb->task, &box, NULL, wuss_WINDOW_BACKDROP,
                           wuss_NO_BACKDROP, SIZE2D(1, 1), SIZE2D(1, 1),
                           &pb->window);
  if (rc != result_OK)
  {
    wuss_task_destroy(pb->task); /* its QUIT frees pb */
    return rc;
  }

  g_pinboard = pb;

  return result_OK;
}

#endif /* WUSS_APP */
