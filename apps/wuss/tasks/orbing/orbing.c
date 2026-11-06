/* wuss/tasks/orbing/orbing.c -- KPT Spheroid Designer-style lit sphere */

#ifdef WUSS_APP

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/colour.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/stack.h"
#include "utils/array.h"
#include "utils/rng.h"
#include "wuss/component/colourpicker.h"
#include "wuss/gadget/colourset.h"
#include "wuss/icon-spec.h"
#include "wuss/icon.h"
#include "wuss/menu.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/dithermenu.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define ORBING_NLIGHTS 4
#define ORBING_NROWS   5 /* control strip slider rows */
#define ORBING_NCOLOURS 3 /* control strip colour sets */

/* one of the fixed lights: a unit direction from the sphere's centre (x
 * right, y up, z towards the viewer; negative z is behind the sphere), a
 * colour, an intensity in percent (100 = the colour as-is) and the size and
 * sharpness of the highlight it makes */
typedef struct orbing_light
{
  int      on;
  double   x, y, z;
  colour_t colour;
  int      intensity;
  int      size;      /* 0..100: highlight size */
  int      sharpness; /* 0..100: highlight edge hardness */
}
orbing_light_t;

/* window task: a single sphere, shaded per pixel by up to four coloured lights,
 * in the spirit of Kai's Power Tools' Spheroid Designer. The sphere fills the
 * window beside a left hand pane, a control strip framed in three groups:
 * Background (its colour set), Sphere (ambient, glow and its colour set) and
 * the current light's (its highlight size and sharpness, its level, an On
 * option and its colour set); the sphere scales with the window. Each lit
 * light shows as a ring marker (dotted when behind the sphere, filled when current). A
 * Select click on a marker makes that light current; anywhere else it moves the
 * current light there (turning it on), and dragging keeps moving it. The arrow
 * keys move it too, a pixel at a time (eight with Shift). Inside the disc the
 * light faces the surface under the pointer; the band just outside wraps it
 * round to the back. Menu > Light (or keys 1-4) picks the current light; O
 * switches it on or off, as its On option does. Each colour set's button
 * opens a true-colour picker on its colour; Light colour... (C) opens it on
 * the current light's. Dithering picks how the preview is blitted to a paletted screen.
 * Mutate (M) nudges the sliders, the lit lights' directions and the colours'
 * hues; Randomise (^R) picks every setting afresh, keeping at least one light
 * on; Reset (R) restores the defaults; Save (F3) opens a Save As dialogue
 * that writes the sphere alone, transparent outside its edge. */
typedef struct orbing_task
{
  taskapp_instance_t base;
  wuss_window_t     *pane;          /* holds the control strip */
  wuss_menu_item_t   menu_items[7]; /* per-instance, not a shared static */
  /* "Light" submenu: one row per light */
  wuss_menu_item_t   light_items[ORBING_NLIGHTS];
  wuss_menu_t        light_menu;
  dithermenu_t       dither;     /* "Display" submenu */
  screen_dither_t    dithering;  /* how the preview is blitted */
  int                current;    /* index of the light a drag moves */
  int                dragging;   /* non-zero while a Select drag is live */
  orbing_light_t   lights[ORBING_NLIGHTS];
  colour_t           sphere;     /* surface colour */
  colour_t           background; /* colour outside the sphere */
  int                ambient;    /* 0..100 */
  int                glow;       /* 0..100: rim light in the surface colour */
  wuss_slider_row_t  rows[ORBING_NROWS];
  wuss_icon_t       *light_frame; /* captioned with the current light */
  wuss_icon_t       *on_icon;     /* the current light's On option */
  wuss_colourset_t  *colour_sets[ORBING_NCOLOURS]; /* sphere, background,
                                                     * current light */
  rng_t              rng;        /* for Mutate and Randomise */
  wuss_colourpicker_t *picker;      /* any 24-bit colour */
  int                  picker_which; /* ORBING_COLOUR_* it edits */
  int                  picker_light; /* index of the light it edits */
  unsigned int        *pixels;       /* preview paint buffer; only ever grows */
  int                  allocated;    /* elements in pixels */
}
orbing_task_t;

/* After Kai's Power Tools 3's Spheroid Designer (MetaTools, 1995): one big
 * sphere lit by up to four coloured lights, with knobs for the highlight's
 * size and sharpness, a rim glow and an ambient floor.
 *
 * Each pixel inside the sphere's disc takes its surface normal N straight
 * from its position (the sphere is viewed orthographically, so the viewer
 * direction V is always +z). Per light L:
 *
 *   diffuse  = max(0, N.L) * light colour * sphere colour
 *   specular = smoothstep(max(0, N.H) ^ e) * light colour, H = |L + V|
 *
 * where the highlight size picks the exponent e and its sharpness narrows
 * the smoothstep around 0.5. Glow adds (1 - N.V)^3 of the sphere colour at
 * the rim and ambient a flat share of it. The sum is clamped, then blended
 * over the background by the pixel's disc coverage so the edge is
 * anti-aliased.
 *
 * A light is placed by pointing at the sphere: inside the disc it takes the
 * surface normal under the pointer. A band ORBING_WRAP radii wide just
 * outside the disc wraps round to the back hemisphere, z falling from 0 at
 * the rim to -1 at the band's outer edge, so rim and back lighting stay in
 * reach. Markers use the inverse, so a back light's marker sits in the band
 * where the pointer that placed it was. */

#define ORBING_STRIP  176  /* control strip pane's width, px */
#define ORBING_MARGIN 8    /* px between the wrap band and the window edge */
#define ORBING_WRAP   0.25 /* wrap band width, in sphere radii */
#define ORBING_MARKER 4    /* marker ring radius, px */
#define ORBING_GRAB   6    /* px from a marker a click selects it */
#define ORBING_NUDGE  1    /* px an arrow key moves the current light */
#define ORBING_SHOVE  8    /* px a Shift-arrow moves it */

#define ORBING_DITHER_TILE 64 /* px; the blue-noise map's size */

#define ORBING_MUTATE_SLIDER 15   /* max slider nudge, % of its range */
#define ORBING_MUTATE_LIGHT  0.35 /* max direction nudge, ~sin(20 deg) */
#define ORBING_MUTATE_HUE    0.2  /* max hue turn, radians */

enum
{
  ORBING_MENU_DISPLAY = 0,
  ORBING_MENU_SAVE,
  ORBING_MENU_LIGHT,
  ORBING_MENU_LIGHT_COLOUR,
  ORBING_MENU_MUTATE,
  ORBING_MENU_RANDOMISE,
  ORBING_MENU_RESET
};

/* the strip's colour sets */
enum
{
  ORBING_COLOUR_SPHERE,
  ORBING_COLOUR_BACKGROUND,
  ORBING_COLOUR_LIGHT /* the current light's */
};

/* control strip rows, top to bottom */
enum
{
  ORBING_ROW_AMBIENT,
  ORBING_ROW_GLOW,
  ORBING_ROW_SIZE,      /* this and the rest are the current light's */
  ORBING_ROW_SHARPNESS,
  ORBING_ROW_INTENSITY /* the current light's */
};

static const struct
{
  const char *label;
  int         max;
  const char *fmt;
}
orbing_rows[ORBING_NROWS] =
{
  { "Ambient", 100, NULL   },
  { "Glow",    100, NULL   },
  { "Size",    100, NULL   },
  { "Sharp",   100, NULL   },
  { "Level",   200, "%d%%" }
};

/* the Light frame's caption and the Light submenu's rows */
static const char *const orbing_light_names[ORBING_NLIGHTS] =
{
  "Light 1", "Light 2", "Light 3", "Light 4"
};

/* stack items for the control strip: a VBOX of three frames, the
 * background's colour set above the sphere's settings above the current
 * light's, the latter two a VBOX of label / slider / value rows; the Sphere
 * frame starts with its colour set, the Light frame with an On option and the
 * light's colour set, each on its own line. A box lays out its children in
 * index order, so these lines come before the rows */
enum
{
  SS_ROOT,
  SS_BACKDROP, /* frame round the background's colour set */
  SS_SPHERE,   /* frame round the global rows */
  SS_LIGHT,    /* frame round the per-light rows */
  SS_ON_LINE,  /* the On label and option, then a spacer taking the rest */
  SS_ON_LABEL,
  SS_ON,
  SS_ON_SPACER,
  SS_COLOUR,   /* the colour sets, in ORBING_COLOUR_* order */
  SS_ROW = SS_COLOUR + ORBING_NCOLOURS, /* first row; each row is SS_ROW +
                                           * 4 * n, then its three leaves */
  SS__LIMIT = SS_ROW + 4 * ORBING_NROWS
};

/* icons in the strip: the three frames, label / slider / value per row,
 * then the On option */
enum
{
  SI_BACKDROP,
  SI_SPHERE,
  SI_LIGHT,
  SI_ROW,
  SI_ON_LABEL = SI_ROW + 3 * ORBING_NROWS,
  SI_ON,
  SI__LIMIT
};

#define SS_G_LABEL      1 /* size groups, see stack_item_t::group */
#define SS_G_VALUE      2
#define SS_SLIDER_MIN_W (64)

#define SS_FRAME(parent_) \
  { .kind = stack_KIND_VBOX, .parent = (parent_), .axis_size = STACK_HUG, \
    .gap = wuss_STD_GAP, .align = stack_ALIGN_FILL, \
    .pad = wuss_STD_FRAME_INSETS }

/* a full-width line: a colour set */
#define SS_LINE(frame) \
  STACK_LEAF(frame, wuss_STD_SECONDARY_BUTTON_HEIGHT, 0, stack_ALIGN_FILL)

#define SS_ROW_ITEMS(n, frame) \
  [SS_ROW + 4 * (n)]     = STACK_HBOX(frame, wuss_STD_SLIDER_HEIGHT, wuss_STD_GAP, stack_ALIGN_START), \
  [SS_ROW + 4 * (n) + 1] = STACK_LEAF_GROUP(SS_ROW + 4 * (n), 0, 16, stack_ALIGN_CENTRE, SS_G_LABEL), \
  [SS_ROW + 4 * (n) + 2] = STACK_LEAF_EX(SS_ROW + 4 * (n), 0, wuss_STD_SLIDER_HEIGHT, stack_ALIGN_CENTRE, 1, SS_SLIDER_MIN_W, 0), \
  [SS_ROW + 4 * (n) + 3] = STACK_LEAF_GROUP(SS_ROW + 4 * (n), 0, 16, stack_ALIGN_CENTRE, SS_G_VALUE)

static const stack_item_t orbing_strip[SS__LIMIT] =
{
  [SS_ROOT]     = { .kind = stack_KIND_VBOX, .parent = -1,
                    .gap = wuss_STD_GAP, .pad = wuss_STD_INSETS },
  [SS_BACKDROP] = SS_FRAME(SS_ROOT),
  [SS_SPHERE]   = SS_FRAME(SS_ROOT),
  [SS_LIGHT]    = SS_FRAME(SS_ROOT),
  SS_ROW_ITEMS(ORBING_ROW_AMBIENT,   SS_SPHERE),
  SS_ROW_ITEMS(ORBING_ROW_GLOW,      SS_SPHERE),
  SS_ROW_ITEMS(ORBING_ROW_SIZE,      SS_LIGHT),
  SS_ROW_ITEMS(ORBING_ROW_SHARPNESS, SS_LIGHT),
  SS_ROW_ITEMS(ORBING_ROW_INTENSITY, SS_LIGHT),
  [SS_ON_LINE]   = STACK_HBOX(SS_LIGHT, wuss_STD_GLYPH_SIZE, wuss_STD_GAP, stack_ALIGN_FILL),
  [SS_ON_LABEL]  = STACK_LEAF_GROUP(SS_ON_LINE, 0, 16, stack_ALIGN_CENTRE, SS_G_LABEL),
  [SS_ON]        = STACK_LEAF(SS_ON_LINE, wuss_STD_GLYPH_SIZE, 0, stack_ALIGN_FILL),
  [SS_ON_SPACER] = STACK_SPACER(SS_ON_LINE, 1),
  [SS_COLOUR + ORBING_COLOUR_SPHERE]     = SS_LINE(SS_SPHERE),
  [SS_COLOUR + ORBING_COLOUR_BACKGROUND] = SS_LINE(SS_BACKDROP),
  [SS_COLOUR + ORBING_COLOUR_LIGHT]      = SS_LINE(SS_LIGHT)
};

/* Copies orbing_strip into items, giving each label and value its natural
 * width in the desktop font; the size groups widen the rest to match. */
static void orbing_strip_items(const wuss_t *wuss, stack_item_t *items)
{
  char text[WUSS_SLIDER_ROW_BUF];
  int  row;

  memcpy(items, orbing_strip, sizeof(orbing_strip));

  for (row = 0; row < ORBING_NROWS; row++)
  {
    items[SS_ROW + 4 * row + 1].axis_size =
      task_text_width(wuss, orbing_rows[row].label);

    snprintf(text, sizeof(text),
             orbing_rows[row].fmt ? orbing_rows[row].fmt : "%d",
             orbing_rows[row].max);
    items[SS_ROW + 4 * row + 3].axis_size = task_text_width(wuss, text);
  }

  items[SS_ON_LABEL].axis_size = task_text_width(wuss, "On");
}

/* per-redraw constants, worked out once rather than per pixel */
typedef struct orbing_frame
{
  double sphere[3];     /* surface colour, 0..1 */
  double background[3]; /* 0..1 */
  int    nlights;       /* only the lights that are on */
  double l[ORBING_NLIGHTS][3]; /* light direction */
  double h[ORBING_NLIGHTS][3]; /* Blinn half-vector, |L + V| */
  double c[ORBING_NLIGHTS][3]; /* colour * intensity, 0..2 */
  double exponent[ORBING_NLIGHTS];
  double lo[ORBING_NLIGHTS], hi[ORBING_NLIGHTS]; /* specular smoothstep
                                                      * edges */
  double ambient;
  double glow;
}
orbing_frame_t;

/* ----------------------------------------------------------------------- */

static void orbing_rgb(colour_t c, double scale, double out[3])
{
  unsigned int r, g, b;

  colour_get_rgb(&c, &r, &g, &b);
  out[0] = r / 255.0 * scale;
  out[1] = g / 255.0 * scale;
  out[2] = b / 255.0 * scale;
}

/* alpha and rgb, 0..1, as a bgra8888 pixel: 0xAARRGGBB, straight alpha */
static unsigned int orbing_pack(double a, const double rgb[3])
{
  return ((unsigned int) (a      * 255.0 + 0.5) << 24) |
         ((unsigned int) (rgb[0] * 255.0 + 0.5) << 16) |
         ((unsigned int) (rgb[1] * 255.0 + 0.5) << 8)  |
         ((unsigned int) (rgb[2] * 255.0 + 0.5));
}

static void orbing_set_light(orbing_light_t *light,
                             int             on,
                             double          x,
                             double          y,
                             double          z,
                             colour_t        colour,
                             int             intensity)
{
  double len;

  len = sqrt(x * x + y * y + z * z);

  light->on        = on;
  light->x         = x / len;
  light->y         = y / len;
  light->z         = z / len;
  light->colour    = colour;
  light->intensity = intensity;
}

static void orbing_defaults(orbing_task_t *task)
{
  int i;

  orbing_set_light(&task->lights[0], 1, -0.5,  0.6,  0.6,
                     colour_rgb(0xFF, 0xFF, 0xFF), 100);
  orbing_set_light(&task->lights[1], 1,  0.7, -0.6, -0.3,
                     colour_rgb(0x40, 0x60, 0xFF), 60);
  orbing_set_light(&task->lights[2], 0,  0.6,  0.6,  0.5,
                     colour_rgb(0xFF, 0x80, 0x40), 100);
  orbing_set_light(&task->lights[3], 0, -0.6, -0.6,  0.5,
                     colour_rgb(0x40, 0xFF, 0x80), 100);

  for (i = 0; i < ORBING_NLIGHTS; i++)
  {
    task->lights[i].size      = 40;
    task->lights[i].sharpness = 30;
  }

  task->sphere     = colour_rgb(0x80, 0x80, 0x80);
  task->background = colour_rgb(0x00, 0x00, 0x00);
  task->ambient    = 10;
  task->glow       = 15;
}

static void orbing_prepare(const orbing_task_t *task,
                             orbing_frame_t      *f)
{
  const orbing_light_t *light;
  double                len, w;
  int                   i, n;

  orbing_rgb(task->sphere, 1.0, f->sphere);
  orbing_rgb(task->background, 1.0, f->background);

  n = 0;
  for (i = 0; i < ORBING_NLIGHTS; i++)
  {
    light = &task->lights[i];
    if (!light->on)
      continue;

    f->l[n][0] = light->x;
    f->l[n][1] = light->y;
    f->l[n][2] = light->z;

    /* L + V with V = (0,0,1); only degenerate for a light dead behind */
    len = sqrt(light->x * light->x + light->y * light->y +
               (light->z + 1.0) * (light->z + 1.0));
    len = MAX(len, 1e-6);
    f->h[n][0] = light->x / len;
    f->h[n][1] = light->y / len;
    f->h[n][2] = (light->z + 1.0) / len;

    orbing_rgb(light->colour, light->intensity / 100.0, f->c[n]);

    /* 200..2 */
    f->exponent[n] = 2.0 * pow(100.0, (100 - light->size) / 100.0);

    w        = 1.0 - light->sharpness * 0.0098; /* 1..0.02 */
    f->lo[n] = 0.5 - w / 2.0;
    f->hi[n] = 0.5 + w / 2.0;
    n++;
  }
  f->nlights = n;

  f->ambient = task->ambient / 100.0;
  f->glow    = task->glow / 50.0;
}

/* the sphere's centre and radius for a content area of the given size, in
 * content-local coordinates: centred */
static void orbing_layout(size2d_t size, double *cx, double *cy, double *r)
{
  *cx = size.w / 2.0;
  *cy = size.h / 2.0;
  *r  = (MIN(size.w, size.h) / 2.0 - ORBING_MARGIN) / (1.0 + ORBING_WRAP);
}

/* the task field a strip row edits, taking a per-light row's from the given
 * light */
static int *orbing_field(orbing_task_t *task, int row, int light)
{
  switch (row)
  {
  case ORBING_ROW_AMBIENT:   return &task->ambient;
  case ORBING_ROW_GLOW:      return &task->glow;
  case ORBING_ROW_SIZE:      return &task->lights[light].size;
  case ORBING_ROW_SHARPNESS: return &task->lights[light].sharpness;
  default:                     return &task->lights[light].intensity;
  }
}

/* the task field a strip row edits now */
static int *orbing_row_field(orbing_task_t *task, int row)
{
  return orbing_field(task, row, task->current);
}

/* repaint the preview; the strip's icons are in the pane, so stay alone */
static void orbing_invalidate_preview(orbing_task_t *task)
{
  if (task->base.window == NULL)
    return;

  wuss_window_invalidate_visible(task->base.window);
}

/* point the light at content-local (px, py) */
static void orbing_aim(orbing_light_t *light,
                       double          cx,
                       double          cy,
                       double          r,
                       double          px,
                       double          py)
{
  double dx, dy, d, z, s;

  dx = (px - cx) / r;
  dy = (cy - py) / r;
  d  = sqrt(dx * dx + dy * dy);

  if (d <= 1.0)
  {
    light->x = dx;
    light->y = dy;
    light->z = sqrt(1.0 - d * d);
    return;
  }

  z = -MIN((d - 1.0) / ORBING_WRAP, 1.0);
  s = sqrt(1.0 - z * z);
  light->x = dx / d * s;
  light->y = dy / d * s;
  light->z = z;
}

/* the inverse of orbing_aim: where the light's marker sits */
static void orbing_marker_pos(const orbing_light_t *light,
                              double                cx,
                              double                cy,
                              double                r,
                              double               *mx,
                              double               *my)
{
  double s, d;

  if (light->z >= 0.0)
  {
    *mx = cx + light->x * r;
    *my = cy - light->y * r;
    return;
  }

  s = sqrt(light->x * light->x + light->y * light->y);
  d = (1.0 - light->z * ORBING_WRAP) * r;
  if (s < 1e-6)
  {
    /* dead behind: every direction round the band is equally right */
    *mx = cx;
    *my = cy - d;
    return;
  }

  *mx = cx + light->x / s * d;
  *my = cy - light->y / s * d;
}

/* a lit light's marker: a ring in its colour with a black outline for
 * contrast, dotted when behind the sphere, centre filled when current */
static void orbing_draw_marker(screen_t             *scr,
                               const orbing_light_t *light,
                               int                   x,
                               int                   y,
                               int                   current)
{
  colour_t black;
  int      i;

  black = colour_rgb(0x00, 0x00, 0x00);

  if (light->z >= 0.0)
  {
    screen_draw_circle(scr, x, y, ORBING_MARKER + 1, black);
    screen_draw_circle(scr, x, y, ORBING_MARKER, light->colour);
  }
  else
  {
    for (i = 0; i < 12; i++)
      screen_set_pixel(scr,
                       x + (int) floor(ORBING_MARKER * cos(i * M_PI / 6.0) + 0.5),
                       y + (int) floor(ORBING_MARKER * sin(i * M_PI / 6.0) + 0.5),
                       light->colour);
  }

  if (current)
    screen_fill_circle(scr, x, y, ORBING_MARKER - 2, light->colour);
}

/* shade the surface point with normal n into rgb (0..1, clamped) */
static void orbing_shade(const orbing_frame_t *f,
                         const double          n[3],
                         double                rgb[3])
{
  double ndl, ndh, spec, t, rim;
  int    i, k;

  rim = pow(1.0 - n[2], 3.0) * f->glow;
  for (k = 0; k < 3; k++)
    rgb[k] = f->sphere[k] * (f->ambient + rim);

  for (i = 0; i < f->nlights; i++)
  {
    ndl = n[0] * f->l[i][0] + n[1] * f->l[i][1] + n[2] * f->l[i][2];
    if (ndl <= 0.0)
      continue;

    ndh  = n[0] * f->h[i][0] + n[1] * f->h[i][1] + n[2] * f->h[i][2];
    spec = ndh > 0.0 ? pow(ndh, f->exponent[i]) : 0.0;
    t    = CLAMP((spec - f->lo[i]) / (f->hi[i] - f->lo[i]), 0.0, 1.0);
    spec = t * t * (3.0 - 2.0 * t);

    for (k = 0; k < 3; k++)
      rgb[k] += f->c[i][k] * (ndl * f->sphere[k] + spec);
  }

  for (k = 0; k < 3; k++)
    rgb[k] = CLAMP(rgb[k], 0.0, 1.0);
}

/* the disc coverage and surface normal of the pixel whose centre is (dx, dy)
 * from the sphere's centre; the normal is only set when the coverage is
 * non-zero */
static double orbing_normal(double dx, double dy, double r, double n[3])
{
  double cov, n2, len;

  cov = CLAMP(r - sqrt(dx * dx + dy * dy) + 0.5, 0.0, 1.0);
  if (cov == 0.0)
    return cov;

  /* screen y runs down, the model's runs up; an edge pixel whose centre lies
   * just outside the disc is pulled back onto the rim */
  n[0] =  dx / r;
  n[1] = -dy / r;
  n2   = n[0] * n[0] + n[1] * n[1];
  if (n2 > 1.0)
  {
    len   = sqrt(n2);
    n[0] /= len;
    n[1] /= len;
    n2    = 1.0;
  }
  n[2] = sqrt(1.0 - n2);

  return cov;
}

/* ----------------------------------------------------------------------- */

/* the field colour set "which" shows and sets */
static colour_t *orbing_colour_field(orbing_task_t *task, int which)
{
  switch (which)
  {
  case ORBING_COLOUR_SPHERE:     return &task->sphere;
  case ORBING_COLOUR_BACKGROUND: return &task->background;
  default:                         return &task->lights[task->current].colour;
  }
}

/* show each colour set's field as its exact colour */
static void orbing_sync_colours(orbing_task_t *task)
{
  int which;

  for (which = 0; which < ORBING_NCOLOURS; which++)
    wuss_colourset_set_rgb(task->colour_sets[which],
                           *orbing_colour_field(task, which));
}

/* open the picker on colour set "which"'s field. A light's stays on that
 * light even if another is made current meanwhile */
static result_t orbing_pick(orbing_task_t *task, int which)
{
  task->picker_which = which;
  task->picker_light = task->current;
  return wuss_colourpicker_open(task->picker,
                                *orbing_colour_field(task, which));
}

/* wuss_colourpicker_changed_fn_t: the picked field takes any 24-bit colour,
 * its colour set showing it exactly */
static void orbing_picker_changed(colour_t colour, void *opaque)
{
  orbing_task_t *task;

  task = opaque;

  if (task->base.window == NULL)
    return; /* closing: the QUIT that frees the picker is on its way */

  if (task->picker_which == ORBING_COLOUR_LIGHT)
    task->lights[task->picker_light].colour = colour;
  else
    *orbing_colour_field(task, task->picker_which) = colour;
  orbing_sync_colours(task);
  orbing_invalidate_preview(task);
}

/* build the control strip's icons in the pane, "height" tall */
static result_t orbing_strip_create(orbing_task_t *task, int height)
{
  result_t         rc;
  box_t            root;
  stack_item_t     items[SS__LIMIT];
  box_t            boxes[SS__LIMIT];
  box_t            line;
  wuss_icon_spec_t specs[SI__LIMIT];
  char             bufs[ORBING_NROWS][WUSS_SLIDER_ROW_BUF];
  wuss_icon_t     *made[SI__LIMIT];
  int              row, item, icon, which;

  orbing_strip_items(task->base.wuss, items);

  root = (box_t) BOX_POS_SIZE(0, 0, ORBING_STRIP, height);
  rc = stack_solve(items, NELEMS(items), &root, boxes);
  if (rc != result_OK)
    return rc;

  wuss_icon_spec_frame(&specs[SI_BACKDROP], boxes[SS_BACKDROP], "Background");
  wuss_icon_spec_frame(&specs[SI_SPHERE], boxes[SS_SPHERE], "Sphere");
  wuss_icon_spec_frame(&specs[SI_LIGHT], boxes[SS_LIGHT],
                       orbing_light_names[task->current]);

  for (row = 0; row < ORBING_NROWS; row++)
  {
    item = SS_ROW + 4 * row;
    icon = SI_ROW + 3 * row;
    wuss_icon_spec_label(&specs[icon], boxes[item + 1],
                         orbing_rows[row].label,
                         wuss_ICON_FLAGS_JUSTIFY_RIGHT);
    wuss_icon_spec_slider_row(&specs[icon + 1], &specs[icon + 2],
                              boxes[item + 2], boxes[item + 3],
                              wuss_SLIDER_HORIZONTAL,
                              0, orbing_rows[row].max,
                              *orbing_row_field(task, row),
                              orbing_rows[row].fmt, 0,
                              bufs[row], sizeof(bufs[row]));
  }

  /* On is a label and a bare glyph so each sits in its column */
  wuss_icon_spec_label(&specs[SI_ON_LABEL], boxes[SS_ON_LABEL], "On",
                       wuss_ICON_FLAGS_JUSTIFY_RIGHT);
  wuss_icon_spec_option(&specs[SI_ON], boxes[SS_ON], "");

  rc = wuss_icon_create_array(task->pane, specs, NELEMS(specs), made);
  if (rc != result_OK)
    return rc;

  task->light_frame = made[SI_LIGHT];
  task->on_icon     = made[SI_ON];
  wuss_icon_set_selected(task->pane, task->on_icon,
                         task->lights[task->current].on);

  for (which = 0; which < ORBING_NCOLOURS; which++)
  {
    /* end the field where the sliders end, the button in the value column */
    line     = boxes[SS_COLOUR + which];
    line.x1  = boxes[SS_ROW + 2].x1 + wuss_STD_GAP;
    line.x1 += wuss_colourset_button_width(task->base.wuss, line.y1 - line.y0);
    rc = wuss_colourset_create(&task->colour_sets[which], task->pane,
                               line, "Colour",
                               boxes[SS_ROW + 1].x1 - boxes[SS_ROW + 1].x0,
                               /* line up with the sliders */
                               0, NULL, NULL); /* its button opens the
                                                * picker instead */
    if (rc != result_OK)
      return rc;
  }
  orbing_sync_colours(task);

  for (row = 0; row < ORBING_NROWS; row++)
  {
    icon = SI_ROW + 3 * row;
    wuss_slider_row_bind(&task->rows[row], made[icon + 1], made[icon + 2],
                         orbing_rows[row].fmt,
                         0, orbing_rows[row].max, 0);
  }

  return result_OK;
}

static result_t orbing_init(orbing_task_t *task)
{
  result_t     rc;
  wuss_t      *wuss;
  stack_item_t items[SS__LIMIT];
  size2d_t     strip_sz;
  int          i;

  wuss = task->base.wuss;

  /* the strip sets the window's minimum height */
  orbing_strip_items(wuss, items);
  rc = stack_smallest(items, NELEMS(items), &strip_sz);
  if (rc != result_OK)
    return rc;

  task->dithering = screen_DITHER_BLUE_NOISE;
  orbing_defaults(task);
  rng_seed(&task->rng, (uint32_t) rand());

  /* no scrollbars: the sphere is laid out across the visible content, so a
   * resize must redraw all of it and doc is only the growth ceiling. The
   * redraw paints every pixel, so wuss need fill no background under it. The
   * width includes the room the pane and its one pixel divider will take */
  rc = wuss_window_create_at(task->base.delegate,
                             &task_place,
                             SIZE2D(ORBING_STRIP + 1 + 240,
                                    MAX(240, strip_sz.h)),
                             "Orbing",
                             wuss_WINDOW_CLOSE | wuss_WINDOW_BACK |
                             wuss_WINDOW_TOGGLE_SIZE | wuss_WINDOW_RESIZE |
                             wuss_WINDOW_NO_RESIZE_BLIT |
                             wuss_WINDOW_FOCUSABLE,
                             wuss_NO_BACKDROP,
                             SIZE2D(1024, 1024),
                             SIZE2D(64, strip_sz.h),
                             &task->base.window);
  if (rc != result_OK)
    return rc;

  rc = wuss_window_create_at(task->base.delegate,
                             &task_place,
                             SIZE2D(ORBING_STRIP, strip_sz.h),
                             NULL,
                             wuss_WINDOW_NO_TITLEBAR |
                             wuss_WINDOW_NO_OUTLINE  |
                             wuss_WINDOW_NO_REDRAW,
                             wuss_BACKDROP_COLOUR(wuss_COLOUR_WINDOW),
                             SIZE2D(ORBING_STRIP, 1024),
                             SIZE2D(0, 0),
                             &task->pane);
  if (rc != result_OK)
    return rc;

  rc = wuss_window_attach_pane(task->pane, task->base.window, wuss_EDGE_LEFT);
  if (rc != result_OK)
    return rc;

  rc = orbing_strip_create(task, strip_sz.h);
  if (rc != result_OK)
    return rc;

  rc = wuss_colourpicker_create(&task->picker, wuss, NULL,
                                orbing_picker_changed, task);
  if (rc != result_OK)
    return rc;

  dithermenu_init(&task->dither, task->dithering);

  WUSS_MENU_ITEM_MENU(task->menu_items, ORBING_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_NONE, &task->dither.display);

  taskapp_save_row(&task->base, task->menu_items, ORBING_MENU_SAVE,
                   wuss_MENU_ITEM_DASHED);

  for (i = 0; i < ORBING_NLIGHTS; i++)
  {
    static const char *const keys[ORBING_NLIGHTS] =
    {
      "1", "2", "3", "4"
    };

    WUSS_MENU_ITEM_SHORTCUT(task->light_items, i, orbing_light_names[i],
                            wuss_MENU_ITEM_NONE, keys[i]);
  }

  WUSS_MENU_TITLE(task->light_menu, "Light", task->light_items,
                  NELEMS(task->light_items));

  WUSS_MENU_ITEM_MENU(task->menu_items, ORBING_MENU_LIGHT, "Light",
                      wuss_MENU_ITEM_DASHED, &task->light_menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, ORBING_MENU_LIGHT_COLOUR,
                          "Light colour...", wuss_MENU_ITEM_NONE, "C");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, ORBING_MENU_MUTATE, "Mutate",
                          wuss_MENU_ITEM_DASHED, "M");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, ORBING_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_NONE, "^R");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, ORBING_MENU_RESET, "Reset",
                          wuss_MENU_ITEM_NONE, "R");

  WUSS_MENU_TITLE(task->base.menu, "Orbing", task->menu_items,
                  NELEMS(task->menu_items));

  return result_OK;
}

static void orbing_quit(orbing_task_t *task)
{
  int which;

  for (which = 0; which < ORBING_NCOLOURS; which++)
    wuss_colourset_destroy(task->colour_sets[which]);
  wuss_colourpicker_destroy(task->picker);
  free(task->pixels);
}

/* shade the dirty part of the preview into the task's bgra8888 buffer and
 * blit it dithered. The bitmap's top-left is pulled back to a whole number of
 * dither tiles from the content's corner, so the dither stays locked to the
 * content across partial redraws; the clip is narrowed to the dirty box so the
 * unshaded margin that adds, which holds stale pixels, is never drawn */
static result_t orbing_paint(orbing_task_t        *task,
                             screen_t             *scr,
                             const box_t          *content,
                             const box_t          *bounds,
                             const orbing_frame_t *f,
                             double                cx,
                             double                cy,
                             double                r)
{
  result_t      rc;
  int           ox, oy, w, h;
  unsigned int *pixels;
  int           x, y, k;
  double        cov;
  double        n[3], rgb[3];
  bitmap_t      bm;
  box_t         saved;

  if (content->x0 >= content->x1 || content->y0 >= content->y1)
    return result_OK;

  ox = content->x0 - (content->x0 - bounds->x0) % ORBING_DITHER_TILE;
  oy = content->y0 - (content->y0 - bounds->y0) % ORBING_DITHER_TILE;
  w  = content->x1 - ox;
  h  = content->y1 - oy;

  if (array_grow((void **) &task->pixels, sizeof(*task->pixels), 0,
                 &task->allocated, w * h, 1))
    return result_OOM;
  pixels = task->pixels;

  for (y = content->y0; y < content->y1; y++)
  {
    for (x = content->x0; x < content->x1; x++)
    {
      cov = orbing_normal(x + 0.5 - cx, y + 0.5 - cy, r, n);
      if (cov == 0.0)
      {
        pixels[(y - oy) * w + (x - ox)] = orbing_pack(1.0, f->background);
        continue;
      }

      orbing_shade(f, n, rgb);

      for (k = 0; k < 3; k++)
        rgb[k] = rgb[k] * cov + f->background[k] * (1.0 - cov);

      pixels[(y - oy) * w + (x - ox)] = orbing_pack(1.0, rgb);
    }
  }

  rc = bitmap_init(&bm, SIZE2D(w, h), pixelfmt_bgra8888,
                   w * (int) sizeof(*pixels), NULL, pixels);
  if (rc == result_OK)
  {
    saved = scr->clip;
    if (box_intersection(&saved, content, &scr->clip))
      rc = result_OK; /* nothing visible to draw */
    else
      rc = screen_copy_bitmap_dithered(scr, ox, oy, &bm, task->dithering);
    scr->clip = saved;
  }

  return rc;
}

static result_t orbing_redraw(const wuss_event_t *event,
                                orbing_task_t    *task)
{
  result_t       rc;
  screen_t      *scr;
  const box_t   *content, *bounds;
  orbing_frame_t f;
  double         cx, cy, r;
  int            i;
  double         mx, my;

  scr     = event->data.redraw.scr;
  content = event->data.redraw.content;
  bounds  = event->data.redraw.bounds;

  orbing_prepare(task, &f);

  orbing_layout(box_size(bounds), &cx, &cy, &r);
  cx += bounds->x0;
  cy += bounds->y0;

  rc = orbing_paint(task, scr, content, bounds, &f, cx, cy, r);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < ORBING_NLIGHTS; i++)
  {
    if (!task->lights[i].on)
      continue;

    orbing_marker_pos(&task->lights[i], cx, cy, r, &mx, &my);
    orbing_draw_marker(scr, &task->lights[i],
                         (int) floor(mx), (int) floor(my),
                         i == task->current);
  }

  return result_OK;
}

/* point the Light frame at the current light: its caption, its sliders, its
 * On option and its colour set (resyncing the other two with it) */
static void orbing_sync_light(orbing_task_t *task)
{
  int row;

  if (task->base.window == NULL)
    return;

  /* ponytail: an OOM here only leaves the caption or a value label stale */
  (void) wuss_icon_set_text(task->pane, task->light_frame,
                            orbing_light_names[task->current]);
  for (row = ORBING_ROW_SIZE; row < ORBING_NROWS; row++)
    (void) wuss_slider_row_set(task->pane, &task->rows[row],
                               *orbing_row_field(task, row));
  wuss_icon_set_selected(task->pane, task->on_icon,
                         task->lights[task->current].on);
  orbing_sync_colours(task);
}

/* turn the current light on, as moving it does, ticking its On option */
static void orbing_light_on(orbing_task_t *task)
{
  task->lights[task->current].on = 1;
  wuss_icon_set_selected(task->pane, task->on_icon, 1);
}

/* put every strip control back in step with its field */
static void orbing_sync_rows(orbing_task_t *task)
{
  int row;

  if (task->base.window == NULL)
    return;

  /* ponytail: an OOM here only leaves a value label stale */
  for (row = 0; row < ORBING_ROW_SIZE; row++)
    (void) wuss_slider_row_set(task->pane, &task->rows[row],
                               *orbing_row_field(task, row));
  orbing_sync_light(task);
}

/* a random value in [-1, 1] */
static double orbing_jitter(rng_t *rng)
{
  return rng_range(rng, 2001) / 1000.0 - 1.0;
}

/* turn the colour's hue by up to ORBING_MUTATE_HUE radians: a rotation
 * about the grey axis, which keeps its lightness roughly as it was */
static void orbing_turn_hue(colour_t *c, rng_t *rng)
{
  double       a, cs, sn, k, rgb[3], out[3];
  unsigned int v[3];
  int          i;

  a  = orbing_jitter(rng) * ORBING_MUTATE_HUE;
  cs = cos(a);
  sn = sin(a);
  k  = (1.0 - cs) / 3.0;

  orbing_rgb(*c, 1.0, rgb);

  /* Rodrigues' rotation about (1,1,1)/sqrt(3), multiplied out */
  for (i = 0; i < 3; i++)
    out[i] = rgb[i] * (cs + k) +
             rgb[(i + 1) % 3] * (k - sn / sqrt(3.0)) +
             rgb[(i + 2) % 3] * (k + sn / sqrt(3.0));

  for (i = 0; i < 3; i++)
    v[i] = (unsigned int) (CLAMP(out[i], 0.0, 1.0) * 255.0 + 0.5);

  *c = colour_rgb(v[0], v[1], v[2]);
}

/* nudge a strip row's field by up to ORBING_MUTATE_SLIDER percent of its
 * range, taking a per-light row's from the given light */
static void orbing_nudge(orbing_task_t *task, int row, int light)
{
  int d, *field;

  d      = orbing_rows[row].max * ORBING_MUTATE_SLIDER / 100;
  field  = orbing_field(task, row, light);
  *field = CLAMP(*field + rng_range(&task->rng, 2 * d + 1) - d,
                 0, orbing_rows[row].max);
}

/* nudge the global sliders, each lit light's sliders and direction, and the
 * colours' hues; which lights are on is left alone */
static void orbing_mutate(orbing_task_t *task)
{
  int             row, i;
  orbing_light_t *light;
  double          x, y, z;

  for (row = 0; row < ORBING_ROW_SIZE; row++)
    orbing_nudge(task, row, 0);

  for (i = 0; i < ORBING_NLIGHTS; i++)
  {
    light = &task->lights[i];
    if (!light->on)
      continue;

    for (row = ORBING_ROW_SIZE; row < ORBING_NROWS; row++)
      orbing_nudge(task, row, i);

    /* ponytail: an offset then renormalise, not a true bounded rotation;
     * turns are up to ~20 degrees, a little more along the diagonals */
    x = light->x + orbing_jitter(&task->rng) * ORBING_MUTATE_LIGHT;
    y = light->y + orbing_jitter(&task->rng) * ORBING_MUTATE_LIGHT;
    z = light->z + orbing_jitter(&task->rng) * ORBING_MUTATE_LIGHT;
    orbing_set_light(light, 1, x, y, z, light->colour, light->intensity);
    orbing_turn_hue(&light->colour, &task->rng);
  }

  orbing_turn_hue(&task->sphere, &task->rng);
}

/* pick every setting afresh: sliders across their full ranges, which lights
 * are on (at least one), their directions and every colour */
static void orbing_randomise(orbing_task_t *task)
{
  int             row, i, lit;
  orbing_light_t *light;
  double          x, y, z;

  for (row = 0; row < ORBING_ROW_SIZE; row++)
    *orbing_field(task, row, 0) = rng_range(&task->rng,
                                              orbing_rows[row].max + 1);

  lit = 0;
  for (i = 0; i < ORBING_NLIGHTS; i++)
  {
    light = &task->lights[i];

    for (row = ORBING_ROW_SIZE; row < ORBING_NROWS; row++)
      *orbing_field(task, row, i) = rng_range(&task->rng,
                                                orbing_rows[row].max + 1);

    /* a point in the unit ball, away from the centre, gives a direction
     * spread evenly over the sphere */
    do
    {
      x = orbing_jitter(&task->rng);
      y = orbing_jitter(&task->rng);
      z = orbing_jitter(&task->rng);
    }
    while (x * x + y * y + z * z > 1.0 || x * x + y * y + z * z < 0.01);

    orbing_set_light(light, rng_range(&task->rng, 2), x, y, z,
                       colour_random(&task->rng), light->intensity);
    lit |= light->on;
  }

  if (!lit)
    task->lights[rng_range(&task->rng, ORBING_NLIGHTS)].on = 1;

  task->background = colour_random(&task->rng);
  task->sphere     = colour_random_contrast(&task->rng, task->background);
}

/* write the sphere alone, sized to its current diameter, as an RGBA PNG:
 * alpha is the disc coverage, so the edge stays anti-aliased and everything
 * outside it is transparent */
static result_t orbing_save(orbing_task_t *task, const char *path)
{
  result_t       rc;
  box_t          content;
  orbing_frame_t f;
  double         cx, cy, r;
  int            d;
  unsigned int  *pixels;
  int            x, y;
  double         cov;
  double         n[3], rgb[3];
  bitmap_t       bm;

  if (task->base.window == NULL)
    return result_OK;

  wuss_window_get_content_bounds(task->base.window, &content);
  orbing_layout(box_size(&content), &cx, &cy, &r);
  if (r < 1.0)
    return result_OK; /* window too small to show a sphere */

  orbing_prepare(task, &f);

  d = (int) ceil(2.0 * r);
  pixels = calloc((size_t) d * d, sizeof(*pixels));
  if (pixels == NULL)
    return result_OOM;

  for (y = 0; y < d; y++)
  {
    for (x = 0; x < d; x++)
    {
      cov = orbing_normal(x + 0.5 - d / 2.0, y + 0.5 - d / 2.0, r, n);
      if (cov == 0.0)
        continue; /* calloc left it transparent */

      orbing_shade(&f, n, rgb);

      pixels[y * d + x] = orbing_pack(cov, rgb);
    }
  }

  rc = bitmap_init(&bm, SIZE2D(d, d), pixelfmt_bgra8888,
                   d * (int) sizeof(*pixels), NULL, pixels);
  if (rc == result_OK)
    rc = bitmap_save_png(&bm, path);
  if (rc != result_OK)
    logf_warning("orbing: saving \"%s\" failed (rc=0x%X)", path, rc);
  else
    logf_info("orbing: saved \"%s\"", path);

  free(pixels);

  return rc;
}

/* wuss_saveas_save_fn_t: opaque is the orbing_task_t */
static result_t orbing_saveas_save(const char *path, void *opaque)
{
  return orbing_save(opaque, path);
}

/* the lit light whose marker is within ORBING_GRAB px of (px, py), or -1 */
static int orbing_hit_marker(const orbing_task_t *task,
                             double               cx,
                             double               cy,
                             double               r,
                             int                  px,
                             int                  py)
{
  int    i;
  double mx, my;

  for (i = 0; i < ORBING_NLIGHTS; i++)
  {
    if (!task->lights[i].on)
      continue;

    orbing_marker_pos(&task->lights[i], cx, cy, r, &mx, &my);
    if ((px - mx) * (px - mx) + (py - my) * (py - my) <=
        ORBING_GRAB * ORBING_GRAB)
      return i;
  }

  return -1;
}

static result_t orbing_mouse(orbing_task_t      *task,
                             wuss_mouse_action_t action,
                             point_t             p,
                             wuss_button_t       button,
                             wuss_window_t      *window)
{

  box_t  content;
  double cx, cy, r;
  int    hit;

  if (action == wuss_MOUSE_UP)
  {
    task->dragging = 0;
    return result_OK;
  }

  if (action == wuss_MOUSE_DOWN && (button & wuss_BUTTON_MENU))
  {
    wuss_menu_tick_exclusive(&task->light_menu, task->current);
    wuss_menu_tick_exclusive(&task->dither.menu, (int) task->dithering);

    return wuss_menu_open_at_pointer(task->base.delegate, &task->base.menu,
                                     &task->base.menu_handle);
  }

  if (window != task->base.window)
    return result_OK; /* strip clicks land on its sliders, or nowhere */

  if (action == wuss_MOUSE_DOWN && !(button & wuss_BUTTON_SELECT))
    return result_OK;

  if (action == wuss_MOUSE_MOVE && !task->dragging)
    return result_OK;

  wuss_window_get_content_bounds(window, &content);
  orbing_layout(box_size(&content), &cx, &cy, &r);

  if (action == wuss_MOUSE_DOWN)
  {
    task->dragging = 1;

    hit = orbing_hit_marker(task, cx, cy, r, p.x, p.y);
    if (hit >= 0)
    {
      task->current = hit; /* grab it where it is; a move then drags it */
      orbing_sync_light(task);
      orbing_invalidate_preview(task);
      return result_OK;
    }
  }

  orbing_light_on(task);
  orbing_aim(&task->lights[task->current], cx, cy, r, p.x, p.y);
  orbing_invalidate_preview(task);

  return result_OK;
}

/* O switches the current light, as its On option does; an arrow key moves
 * its marker, turning the light on, as a drag would */
static result_t orbing_key(orbing_task_t    *task,
                             const wuss_event_t *event)
{
  box_t  content;
  double cx, cy, r;
  double step;
  double mx, my;
  double dx, dy, d, limit;

  if (event->data.key.modifiers & (wuss_KEY_MOD_CTRL | wuss_KEY_MOD_ALT))
    return result_WUSS_KEY_UNCLAIMED;

  if (event->data.key.code == 'O' || event->data.key.code == 'o')
  {
    task->lights[task->current].on = !task->lights[task->current].on;
    wuss_icon_set_selected(task->pane, task->on_icon,
                           task->lights[task->current].on);
    orbing_invalidate_preview(task);
    return result_OK;
  }

  wuss_window_get_content_bounds(task->base.window, &content);
  orbing_layout(box_size(&content), &cx, &cy, &r);

  step = (event->data.key.modifiers & wuss_KEY_MOD_SHIFT) ? ORBING_SHOVE
                                                          : ORBING_NUDGE;

  orbing_marker_pos(&task->lights[task->current], cx, cy, r, &mx, &my);

  switch (event->data.key.code)
  {
  case wuss_KEY_LEFT:  mx -= step; break;
  case wuss_KEY_RIGHT: mx += step; break;
  case wuss_KEY_UP:    my -= step; break;
  case wuss_KEY_DOWN:  my += step; break;
  default:             return result_WUSS_KEY_UNCLAIMED;
  }

  /* stop just short of the wrap band's outer edge: there the light is dead
   * behind and its marker would jump to the top of the band */
  dx    = mx - cx;
  dy    = my - cy;
  d     = sqrt(dx * dx + dy * dy);
  limit = (1.0 + ORBING_WRAP) * r - 0.5;
  if (d > limit)
  {
    mx = cx + dx / d * limit;
    my = cy + dy / d * limit;
  }

  orbing_light_on(task);
  orbing_aim(&task->lights[task->current], cx, cy, r, mx, my);
  orbing_invalidate_preview(task);

  return result_OK;
}

static result_t orbing_menu_select(orbing_task_t      *task,
                                   const wuss_event_t *event)
{
  int index;

  if (event->data.menu_select.menu == &task->base.menu)
  {
    switch (event->data.menu_select.index)
    {
    case ORBING_MENU_LIGHT_COLOUR:
      return orbing_pick(task, ORBING_COLOUR_LIGHT);

    case ORBING_MENU_MUTATE:
      orbing_mutate(task);
      break;

    case ORBING_MENU_RANDOMISE:
      orbing_randomise(task);
      break;

    case ORBING_MENU_RESET:
      orbing_defaults(task);
      break;

    default:
      return result_OK; /* a pick on a submenu row itself */
    }

    orbing_sync_rows(task);
    orbing_invalidate_preview(task);
    return result_OK;
  }

  index = event->data.menu_select.index;

  if (event->data.menu_select.menu == &task->dither.menu)
  {
    task->dithering = dithermenu_select(&task->dither, task->base.menu_handle,
                                        index);
    orbing_invalidate_preview(task);
    return result_OK;
  }

  if (event->data.menu_select.menu != &task->light_menu)
    return result_OK;

  task->current = index;
  orbing_sync_light(task);

  wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->light_menu,
                                index);

  orbing_invalidate_preview(task);

  return result_OK;
}

/* a strip click: a colour set's button opens the picker, the On option
 * toggles the current light, or a slider drag stores the value; each
 * reshades */
static result_t orbing_icon(orbing_task_t      *task,
                            wuss_window_t      *window,
                            const wuss_event_t *event)
{
  int which, row, value;

  for (which = 0; which < ORBING_NCOLOURS; which++)
  {
    if (event->data.icon.icon !=
        wuss_colourset_button(task->colour_sets[which]))
      continue;

    if (event->data.icon.action != wuss_MOUSE_UP ||
        !(event->data.icon.button &
          (wuss_BUTTON_SELECT | wuss_BUTTON_ADJUST)))
      return result_OK;

    return orbing_pick(task, which);
  }

  if (event->data.icon.icon == task->on_icon)
  {
    if (event->data.icon.action != wuss_MOUSE_UP)
      return result_OK;

    task->lights[task->current].on = !!wuss_icon_get_selected(task->on_icon);
    orbing_invalidate_preview(task);
    return result_OK;
  }

  for (row = 0; row < ORBING_NROWS; row++)
  {
    if (!wuss_slider_row_event(window, &task->rows[row], event, &value))
      continue;

    *orbing_row_field(task, row) = value;
    orbing_invalidate_preview(task);
    break;
  }

  return result_OK;
}

static result_t orbing_handle(wuss_window_t      *window,
                              const wuss_event_t *event,
                              void               *task_data)
{
  orbing_task_t *task;

  task = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_REDRAW:
    return orbing_redraw(event, task);

  case wuss_EVENT_MOUSE:
    return orbing_mouse(task, event->data.mouse.action,
                          event->data.mouse.point, event->data.mouse.button,
                          window);

  case wuss_EVENT_KEY:
  {
    result_t rc;

    rc = wuss_menu_dispatch_shortcut(task->base.delegate, &task->base.menu,
                                     event);
    if (rc != result_WUSS_KEY_UNCLAIMED)
      return rc;

    return orbing_key(task, event);
  }

  case wuss_EVENT_MENU_SELECT:
    return orbing_menu_select(task, event);

  case wuss_EVENT_ICON:
    if (window != task->pane)
      return result_OK;
    return orbing_icon(task, window, event);

  case wuss_EVENT_QUIT:
    orbing_quit(task);
    return result_OK;

  default:
    return result_OK;
  }
}

result_t orbing_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Orbing",
    .purpose       = "Lit sphere designer",
    .instance_size = sizeof(orbing_task_t),
    .init          = (taskapp_init_fn_t) orbing_init,
    .handle        = orbing_handle,
    .all_windows   = true, /* the pane opens the menu too */
    .save_name     = "orbing.png",
    .save          = orbing_saveas_save
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
