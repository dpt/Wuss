/* wuss/tasks/texturing/texturing.c -- tileable procedural texture task */

#ifdef WUSS_APP

#include <stddef.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "datastruct/nodegraph.h"
#include "framebuf/bitmap.h"
#include "framebuf/colour.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "utils/rng.h"
#include "wuss/gadget/nodeview.h"
#include "wuss/menu.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/common.h"
#include "../common/confdlg.h"
#include "../common/dithermenu.h"
#include "../common/taskapp.h"

#include "texturing-gen.h"

/* The window shows the texture repeated this many times each way, so that
 * any seam would show. */
#define TEXTURING_REPEAT 2

/* Fills the content area with a procedural texture that tiles seamlessly:
 * every noise lattice wraps at the tile edge. The texture is the output of a
 * node graph (see texturing-gen.h): generators feed Tone and Blend nodes,
 * which feed the one Output node, whose 8-bit values go into a pixelfmt_p8
 * bitmap; the colour scheme is only its palette, so a scheme change redraws
 * without regenerating.
 *
 * SELECT reseeds; ADJUST steps the colour scheme. Menu > Display >
 * Dithering picks how the tile is dithered to the screen. Menu > Kind and
 * Menu > Presets replace the graph with a chain of a generator, a Tone and
 * the Output; Menu > Colours picks the scheme; Menu > Configuration shows
 * the selected node's parameters as sliders, or the first generator's, and
 * Menu > Tone the first Tone's grain and level; Menu > Mutate (M) nudges the
 * selected node's parameters, or every generator's; Menu > Randomise picks a
 * fresh seed, kind and scheme; Menu > Graph opens the graph window; Menu >
 * Save writes a single tile as a PNG. Once a click has given the window the
 * input focus, Space reseeds, Left/Right step the colour scheme, K steps the
 * kind and G steps the grain through None, Light and Heavy presets.
 *
 * In the graph window, Menu > Add puts a node at the pointer and Menu >
 * Delete (Delete) removes the selected node, though never the Output. A
 * double-click on a node opens its parameters. */

/* Colour schemes, as 0xRRGGBB. A smooth scheme ramps through its three
 * stops at values 0, 128 and 255. A stepped scheme maps each fifth of the
 * value range to one of its five stops exactly, giving the few close tones
 * of the RISC OS desktop textures; grain then speckles between them. */
static const struct
{
  const char  *name;
  int          stepped;
  unsigned int stops[5];
}
texturing_schemes[] =
{
  { "Sky",     0, { 0x1E4FA0, 0x7FB0E8, 0xFFFFFF } },
  { "Slate",   0, { 0x202020, 0x808078, 0xE8E8E0 } },
  { "Wood",    0, { 0x3A1E0C, 0x8A5528, 0xD8A868 } },
  { "Lava",    0, { 0x000000, 0xC02000, 0xFFE040 } },
  { "Moss",    0, { 0x102008, 0x4A7A20, 0xC8E088 } },
  { "Ice",     0, { 0x002838, 0x40A0C0, 0xF0FFFF } },
  /* the 16-colour desktop's greys */
  { "Desktop", 1, { 0x333333, 0x555555, 0x777777, 0x999999, 0xBBBBBB } },
  /* 4-bit greys, darker */
  { "Grey 16", 1, { 0x222222, 0x444444, 0x666666, 0x888888, 0xAAAAAA } },
  { "Linen",   1, { 0x6E6450, 0x8A806A, 0xA69C84, 0xC2B89E, 0xDDD4BA } },
  { "Denim",   1, { 0x1E2C44, 0x2E4060, 0x40567C, 0x566E96, 0x7088AE } }
};

#define TEXTURING_NSCHEMES ((int) NELEMS(texturing_schemes))

/* G steps the grain through these presets. */
static const int texturing_grain_presets[] = { 0, 48, 96 };

/* Each node's parameters are ints in its params[], edited by the dialogue
 * for its kind. A "Scale" is the log2 of the lattice cells across the tile,
 * so higher is finer; every lattice is a power of two and every band count
 * a whole number, so the texture always tiles. Percentages are turned into
 * 8.8 multipliers once per evaluation, not divided per pixel. */
#define TEXTURING_PARAM(i) ((i) * sizeof(int))

/* A dialogue per kind of node bar the Output, which has none. */
#define TEXTURING_NCONFS TEXTURING_NODE_OUTPUT

static const confdlg_row_t texturing_rows[TEXTURING_NCONFS][TEXTURING_NPARAMS] =
{
  { /* Clouds */
    { "Scale",      1,   5, 0, TEXTURING_PARAM(0) },
    { "Contrast",  25, 400, 0, TEXTURING_PARAM(1) },
    { "Detail",     1,   7, 0, TEXTURING_PARAM(2) },
    /* pixels the coordinates are pushed about by a second noise */
    { "Warp",       0,  32, 0, TEXTURING_PARAM(3) }
  },
  { /* Marble */
    { "Scale",      1,   5, 0, TEXTURING_PARAM(0) },
    { "Veins",      1,  16, 0, TEXTURING_PARAM(1) },
    { "Turbulence", 0,  16, 0, TEXTURING_PARAM(2) },
    /* veins crossed per tile height, tilting them */
    { "Slant",     -8,   8, 0, TEXTURING_PARAM(3) }
  },
  { /* Wood */
    { "Scale",      1,   5, 0, TEXTURING_PARAM(0) },
    { "Rings",      2,  32, 0, TEXTURING_PARAM(1) },
    { "Roughness",  0,  16, 0, TEXTURING_PARAM(2) },
    { "Detail",     1,   7, 0, TEXTURING_PARAM(3) }
  },
  { /* Cells */
    { "Scale",      1,   5, 0, TEXTURING_PARAM(0) },
    { "Contrast",  25, 400, 0, TEXTURING_PARAM(1) },
    /* how far each point strays from its cell's centre, % */
    { "Jitter",     0, 100, 0, TEXTURING_PARAM(2) },
    /* blend toward dark cell edges, % */
    { "Edges",      0, 100, 0, TEXTURING_PARAM(3) }
  },
  { /* Stone */
    { "Scale",      3,   6, 0, TEXTURING_PARAM(0) },
    { "Relief",     0, 400, 0, TEXTURING_PARAM(1) },
    { "Patches",    0, 100, 0, TEXTURING_PARAM(2) },
    /* degrees clockwise from the top that the light comes from */
    { "Light",      0, 315, 45, TEXTURING_PARAM(3) },
    /* strength of the lit slopes against the shaded ones, %: 0 leaves
     * only shadows, cut into a flat surface */
    { "Highlights", 0, 100, 0, TEXTURING_PARAM(4) }
  },
  { /* Weave: Thread is the log2 of the thread width in pixels */
    { "Thread",     2,   5, 0, TEXTURING_PARAM(0) },
    { "Relief",     0, 400, 0, TEXTURING_PARAM(1) },
    { "Wobble",     0,  16, 0, TEXTURING_PARAM(2) },
    /* plain, twill or basket */
    { "Pattern",    0,   2, 0, TEXTURING_PARAM(3) }
  },
  { /* Veins */
    { "Scale",      3,   6, 0, TEXTURING_PARAM(0) },
    /* how far from its middle the noise may be and still draw a vein */
    { "Width",      2,  40, 0, TEXTURING_PARAM(1) },
    /* log2 of how much finer the lattice is across the diagonal than
     * along it, 0 for veins that run every way */
    { "Stretch",    0,   3, 0, TEXTURING_PARAM(2) },
    /* fine dark mottling between the veins, % */
    { "Dark",       0, 100, 0, TEXTURING_PARAM(3) }
  },
  { /* Tone: the grain's amplitude, and a level added to every value, which
     * moves a stepped scheme's balance between its tones */
    { "Grain",      0, TEXTURING_GRAIN_MAX, 0,
      offsetof(texturing_tone_t, grain) },
    { "Level",    -64,  64, 0, offsetof(texturing_tone_t, level) }
  },
  { /* Blend: how much of B to mix into A, % */
    { "Amount",     0, 100, 0, TEXTURING_PARAM(0) }
  }
};

typedef struct texturing_task texturing_task_t;

static result_t texturing_applied(texturing_task_t *task, int old);

#define TEXTURING_CONF(kind, nrows) \
  { texturing_rows[kind], nrows, texturing_defaults[kind], \
    sizeof(texturing_defaults[kind]), \
    (confdlg_applied_fn_t) texturing_applied }

static const confdlg_desc_t texturing_confs[TEXTURING_NCONFS] =
{
  TEXTURING_CONF(TEXTURING_KIND_CLOUDS, 4),
  TEXTURING_CONF(TEXTURING_KIND_MARBLE, 4),
  TEXTURING_CONF(TEXTURING_KIND_WOOD,   4),
  TEXTURING_CONF(TEXTURING_KIND_CELLS,  4),
  TEXTURING_CONF(TEXTURING_KIND_STONE,  5),
  TEXTURING_CONF(TEXTURING_KIND_WEAVE,  4),
  TEXTURING_CONF(TEXTURING_KIND_VEINS,  4),
  TEXTURING_CONF(TEXTURING_NODE_TONE,   2),
  TEXTURING_CONF(TEXTURING_NODE_BLEND,  1)
};

enum
{
  TEXTURING_MENU_SAVE = 0,
  TEXTURING_MENU_DISPLAY,
  TEXTURING_MENU_COLOURS,
  TEXTURING_MENU_KIND,
  TEXTURING_MENU_PRESETS,
  TEXTURING_MENU_CONFIGURATION,
  TEXTURING_MENU_TONE,
  TEXTURING_MENU_MUTATE,
  TEXTURING_MENU_RANDOMISE,
  TEXTURING_MENU_GRAPH,
  TEXTURING_MENU__LIMIT
};

/* The graph window's menu. */
enum
{
  TEXTURING_GRAPH_ADD = 0,
  TEXTURING_GRAPH_DELETE,
  TEXTURING_GRAPH__LIMIT
};

/* Mutate nudges each slider by up to this percent of its range, and always
 * by at least a step. */
#define TEXTURING_MUTATE 15

/* Graph window's visible size; the gadget sets its document. */
#define TEXTURING_GRAPH_W 416
#define TEXTURING_GRAPH_H 192

struct texturing_task
{
  taskapp_instance_t base;
  texturing_ctx_t    ctx;    /* seed and pixels, shared by every node */
  rng_t              rng;    /* for Mutate */
  int                kind;   /* last kind picked, for K and the ticks */
  int                scheme; /* texturing_schemes[] row */
  nodegraph_t       *graph;
  nodegraph_node_t   output; /* the one Output node */
  int                scratch[TEXTURING_NPARAMS]; /* dialogues on no node */
  confdlg_t          confs[TEXTURING_NCONFS]; /* see texturing_confs */
  colour_t           palette[256];
  bitmap_t           bm;     /* wraps pixels[] and palette[] */
  unsigned char      pixels[TEXTURING_NPIXELS];
  screen_dither_t    dithering; /* how the tile is blitted to the screen */
  wuss_window_t     *graph_window;
  wuss_nodeview_t   *nodeview;
  point_t            add_at; /* where Menu > Add puts a node */
  wuss_menu_item_t   menu_items[TEXTURING_MENU__LIMIT];
  wuss_menu_item_t   kind_items[TEXTURING_NKINDS];
  wuss_menu_t        kind_menu;
  wuss_menu_item_t   preset_items[16];
  wuss_menu_t        preset_menu;
  wuss_menu_item_t   scheme_items[NELEMS(texturing_schemes)];
  wuss_menu_t        scheme_menu;
  dithermenu_t       dither;    /* "Display" submenu: screen dithering */
  wuss_menu_item_t   graph_items[TEXTURING_GRAPH__LIMIT];
  wuss_menu_t        graph_menu;
  wuss_menu_item_t   add_items[TEXTURING_NCONFS];
  wuss_menu_t        add_menu;
};

/* ----------------------------------------------------------------------- */

/* Build the 256-entry palette from the current scheme's stops. */
static void texturing_build_palette(texturing_task_t *task)
{
  const unsigned int *stops;
  int                 nstops;
  colour_t            c[5];
  int                 i;

  stops  = texturing_schemes[task->scheme].stops;
  nstops = texturing_schemes[task->scheme].stepped ? 5 : 3;
  for (i = 0; i < nstops; i++)
    c[i] = colour_rgb((stops[i] >> 16) & 0xFF,
                      (stops[i] >>  8) & 0xFF,
                       stops[i]        & 0xFF);

  for (i = 0; i < 256; i++)
    if (texturing_schemes[task->scheme].stepped)
      task->palette[i] = c[i * 5 >> 8];
    else
      task->palette[i] = i < 128 ? colour_mix(c[0], c[1], (unsigned) i, 128)
                                 : colour_mix(c[1], c[2], (unsigned) i - 128, 127);
}

/* ----------------------------------------------------------------------- */

static int texturing_kind_of(texturing_task_t *task, nodegraph_node_t node)
{
  return texturing_node(task->graph, node)->kind;
}

/* The selected node, or nodegraph_NONE. */
static nodegraph_node_t texturing_selected(texturing_task_t *task)
{
  return task->nodeview ? wuss_nodeview_get_selected(task->nodeview)
                        : nodegraph_NONE;
}

/* The first node whose kind is in lo..hi-1, or nodegraph_NONE. */
static nodegraph_node_t texturing_find(texturing_task_t *task,
                                       int               lo,
                                       int               hi)
{
  nodegraph_node_t n;
  int              kind;

  for (n = nodegraph_next(task->graph, nodegraph_NONE); n != nodegraph_NONE;
       n = nodegraph_next(task->graph, n))
  {
    kind = texturing_kind_of(task, n);
    if (kind >= lo && kind < hi)
      return n;
  }
  return nodegraph_NONE;
}

/* The Tone node G and Menu > Tone act on: the selected node if a Tone, else
 * the first. */
static nodegraph_node_t texturing_tone_node(texturing_task_t *task)
{
  nodegraph_node_t n;

  n = texturing_selected(task);
  if (n != nodegraph_NONE && texturing_kind_of(task, n) == TEXTURING_NODE_TONE)
    return n;
  return texturing_find(task, TEXTURING_NODE_TONE, TEXTURING_NODE_TONE + 1);
}

/* Point dialogue conf at node's parameters, or at scratch for none. */
static void texturing_bind(texturing_task_t *task,
                           int               conf,
                           nodegraph_node_t  node)
{
  task->confs[conf].config = node != nodegraph_NONE
                           ? texturing_node(task->graph, node)->params
                           : task->scratch;
}

/* Point every dialogue at scratch, before nodes go. */
static void texturing_unbind_all(texturing_task_t *task)
{
  int k;

  for (k = 0; k < TEXTURING_NCONFS; k++)
    task->confs[k].config = task->scratch;
}

/* Point Menu > Configuration at the selected node's dialogue, or the first
 * generator's, and Menu > Tone at the Tone node's. */
static void texturing_retarget(texturing_task_t *task)
{
  nodegraph_node_t n;
  int              conf;

  n = texturing_selected(task);
  if (n == nodegraph_NONE || texturing_kind_of(task, n) == TEXTURING_NODE_TONE ||
      texturing_kind_of(task, n) == TEXTURING_NODE_OUTPUT)
    n = texturing_find(task, 0, TEXTURING_NKINDS);
  conf = n != nodegraph_NONE ? texturing_kind_of(task, n) : task->kind;
  texturing_bind(task, conf, n);
  task->menu_items[TEXTURING_MENU_CONFIGURATION].window =
    wuss_dialogue_window(task->confs[conf].dialogue);

  texturing_bind(task, TEXTURING_NODE_TONE, texturing_tone_node(task));
}

/* Evaluate what changed and show it. */
static result_t texturing_update(texturing_task_t *task)
{
  result_t rc;

  rc = nodegraph_evaluate(task->graph);
  if (task->base.window != NULL)
    wuss_window_invalidate_extent(task->base.window);

  return rc;
}

/* ponytail: any parameter change re-evaluates every node, as the dialogue
 * does not say which node it edited; dirty only that node if big graphs
 * lag */
static result_t texturing_refresh_all(texturing_task_t *task)
{
  texturing_dirty_all(task->graph);
  return texturing_update(task);
}

/* Replace the graph with a chain of a generator of kind, a Tone and the
 * Output. */
static result_t texturing_rechain(texturing_task_t       *task,
                                  int                     kind,
                                  const int              *params,
                                  const texturing_tone_t *tone)
{
  result_t         rc;
  texturing_tone_t keep;
  nodegraph_node_t n;

  if (tone == NULL)
  {
    /* keep the current Tone's grain and level */
    n    = texturing_tone_node(task);
    keep = n != nodegraph_NONE
         ? *(const texturing_tone_t *) texturing_node(task->graph, n)->params
         : *(const texturing_tone_t *) texturing_defaults[TEXTURING_NODE_TONE];
    tone = &keep;
  }

  texturing_unbind_all(task);
  task->kind = kind;
  rc = texturing_chain(task->graph, &task->ctx, kind, params, tone);
  task->output = texturing_find(task, TEXTURING_NODE_OUTPUT,
                                TEXTURING_NODE_OUTPUT + 1);
  if (task->nodeview != NULL)
    wuss_nodeview_refresh(task->nodeview);
  if (rc != result_OK)
    return rc;

  return texturing_update(task);
}

/* ----------------------------------------------------------------------- */

static result_t texturing_redraw(const wuss_event_t *event,
                                 texturing_task_t   *task)
{
  const box_t *bounds;

  bounds = event->data.redraw.bounds;

  /* dithered, as the desktop is often 16 colours */
  return screen_copy_bitmap_tiled_dithered(event->data.redraw.scr,
                                           event->data.redraw.content,
                                           &task->bm,
                                           POINT(bounds->x0 - event->data.redraw.scroll.x,
                                                 bounds->y0 - event->data.redraw.scroll.y),
                                           task->dithering);
}

static result_t texturing_reseed(texturing_task_t *task)
{
  rng_lcg32(&task->ctx.seed);
  return texturing_refresh_all(task);
}

/* confdlg_applied_fn_t: a dialogue's Apply or Default changed a node */
static result_t texturing_applied(texturing_task_t *task, int old)
{
  NOT_USED(old);

  if (task->base.window == NULL)
    return result_OK; /* window closed while the dialogue lingers */

  return texturing_refresh_all(task);
}

static result_t texturing_set_scheme(texturing_task_t *task, int scheme)
{
  task->scheme = scheme;
  texturing_build_palette(task);
  wuss_window_invalidate_extent(task->base.window);

  return result_OK;
}

/* G: step the Tone's grain to the next preset above it, wrapping. */
static result_t texturing_next_grain(texturing_task_t *task)
{
  nodegraph_node_t n;
  int             *grain;
  int              i;

  n = texturing_tone_node(task);
  if (n == nodegraph_NONE)
    return result_OK;

  grain = &texturing_node(task->graph, n)->params[0];
  for (i = 0; i < (int) NELEMS(texturing_grain_presets); i++)
    if (texturing_grain_presets[i] > *grain)
      break;
  *grain = i < (int) NELEMS(texturing_grain_presets) ?
           texturing_grain_presets[i] : texturing_grain_presets[0];

  return texturing_applied(task, 0);
}

/* Nudge *v by up to TEXTURING_MUTATE percent of the row's range, in whole
 * snap steps and at least one, keeping it within the range. */
static void texturing_nudge(texturing_task_t    *task,
                            const confdlg_row_t *row,
                            int                 *v)
{
  int unit, d;

  unit = row->step ? row->step : 1;
  d    = MAX(1, (row->max - row->min) / unit * TEXTURING_MUTATE / 100);
  *v   = CLAMP(*v + (rng_range(&task->rng, 2 * d + 1) - d) * unit,
               row->min, row->max);
}

static void texturing_nudge_node(texturing_task_t *task, nodegraph_node_t n)
{
  texturing_node_t *node;
  int               i;

  node = texturing_node(task->graph, n);
  if (node->kind >= TEXTURING_NCONFS)
    return; /* the Output has no parameters */

  for (i = 0; i < texturing_confs[node->kind].nrows; i++)
    texturing_nudge(task, &texturing_rows[node->kind][i], &node->params[i]);
}

/* Mutate: nudge the selected node's parameters, or every generator's; the
 * seed and scheme are left alone. An open dialogue shows the new values
 * when next opened. */
static result_t texturing_mutate(texturing_task_t *task)
{
  nodegraph_node_t n;

  n = texturing_selected(task);
  if (n != nodegraph_NONE)
  {
    texturing_nudge_node(task, n);
  }
  else
  {
    for (n = nodegraph_next(task->graph, nodegraph_NONE);
         n != nodegraph_NONE; n = nodegraph_next(task->graph, n))
      if (texturing_kind_of(task, n) < TEXTURING_NKINDS)
        texturing_nudge_node(task, n);
  }

  return texturing_applied(task, 0);
}

/* Take on a preset's chain and scheme, keeping the seed. */
static result_t texturing_preset(texturing_task_t *task, int preset)
{
  task->scheme = texturing_presets[preset].scheme;
  texturing_build_palette(task);

  return texturing_rechain(task, texturing_presets[preset].kind,
                           texturing_presets[preset].params,
                           &texturing_presets[preset].tone);
}

/* Randomise: fresh seed, kind and scheme. Shared by the menu row and its
 * Ctrl-R shortcut. */
static result_t texturing_randomise(texturing_task_t *task)
{
  rng_t s;
  int   kind;

  rng_lcg32(&task->ctx.seed);
  rng_seed(&s, task->ctx.seed);
  kind         = rng_range(&s, TEXTURING_NKINDS);
  task->scheme = rng_range(&s, TEXTURING_NSCHEMES);
  texturing_build_palette(task);

  return texturing_rechain(task, kind, NULL, NULL);
}

/* ----------------------------------------------------------------------- */

/* wuss_nodeview_activate_fn_t: open the node's dialogue by the pointer */
static void texturing_activate(wuss_nodeview_t *nodeview,
                               nodegraph_node_t node,
                               void            *opaque)
{
  texturing_task_t *task;
  wuss_window_t    *win;
  int               conf;

  NOT_USED(nodeview);

  task = opaque;
  conf = texturing_kind_of(task, node);
  if (conf >= TEXTURING_NCONFS)
    return; /* the Output's settings are the Colours and Display menus */

  texturing_bind(task, conf, node);
  win = wuss_dialogue_window(task->confs[conf].dialogue);
  (void) wuss_window_set_hidden(win, 1); /* so showing refills it */
  wuss_window_move(win, wuss_get_pointer(task->base.wuss));
  (void) wuss_window_set_hidden(win, 0);
  wuss_window_restack(win, wuss_ZORDER_FRONT); /* shows where it last was */
}

/* wuss_nodeview_changed_fn_t: a link was made or broken */
static void texturing_changed(wuss_nodeview_t *nodeview, void *opaque)
{
  NOT_USED(nodeview);

  (void) texturing_update(opaque);
}

static result_t texturing_open_graph(texturing_task_t *task)
{
  result_t     rc;
  wuss_place_t place;

  if (task->graph_window != NULL)
    return result_OK;

  place = task_place_after(task->base.window);
  rc = wuss_window_create_at(task->base.delegate, &place,
                             SIZE2D(TEXTURING_GRAPH_W, TEXTURING_GRAPH_H),
                             "Texture graph",
                             wuss_WINDOW_DEFAULT | wuss_WINDOW_FOCUSABLE,
                             wuss_NO_BACKDROP,
                             SIZE2D(TEXTURING_GRAPH_W, TEXTURING_GRAPH_H),
                             SIZE2D(0, 0), &task->graph_window);
  if (rc != result_OK)
    return rc;

  return wuss_nodeview_create(&task->nodeview, task->graph_window,
                              task->graph, texturing_activate,
                              texturing_changed, task);
}

static result_t texturing_add(texturing_task_t *task, int kind)
{
  result_t         rc;
  nodegraph_node_t n;

  rc = texturing_node_add(task->graph, &task->ctx, kind, NULL,
                          MAX(task->add_at.x, 0), MAX(task->add_at.y, 0), &n);
  if (rc != result_OK)
    return rc;

  wuss_nodeview_select(task->nodeview, n);
  wuss_nodeview_refresh(task->nodeview);

  return texturing_update(task);
}

static result_t texturing_delete(texturing_task_t *task)
{
  result_t         rc;
  nodegraph_node_t n;
  int             *params;
  int              k;

  n = texturing_selected(task);
  if (n == nodegraph_NONE || n == task->output)
    return result_OK;

  params = texturing_node(task->graph, n)->params;
  for (k = 0; k < TEXTURING_NCONFS; k++)
    if (task->confs[k].config == params)
      task->confs[k].config = task->scratch;

  rc = nodegraph_remove(task->graph, n);
  wuss_nodeview_refresh(task->nodeview);
  if (rc != result_OK)
    return rc;

  return texturing_update(task);
}

static result_t texturing_graph_mouse(texturing_task_t   *task,
                                      const wuss_event_t *event)
{
  if (task->nodeview == NULL ||
      wuss_nodeview_handle_event(task->nodeview, event))
    return result_OK;

  if (event->data.mouse.action != wuss_MOUSE_DOWN ||
      !(event->data.mouse.button & wuss_BUTTON_MENU))
    return result_OK;

  task->add_at = event->data.mouse.point;
  return wuss_menu_open_ticked(task->base.delegate, &task->graph_menu, 0,
                               wuss_get_pointer(task->base.wuss),
                               &task->base.menu_handle);
}

/* ----------------------------------------------------------------------- */

static result_t texturing_key(texturing_task_t *task, int code)
{
  switch (code)
  {
  case ' ':
    return texturing_reseed(task);

  case 'k':
  case 'K':
    return texturing_rechain(task, (task->kind + 1) % TEXTURING_NKINDS, NULL,
                             NULL);

  case 'g':
  case 'G':
    return texturing_next_grain(task);

  case wuss_KEY_LEFT:
    return texturing_set_scheme(task, (task->scheme + TEXTURING_NSCHEMES - 1) %
                                      TEXTURING_NSCHEMES);

  case wuss_KEY_RIGHT:
    return texturing_set_scheme(task, (task->scheme + 1) % TEXTURING_NSCHEMES);

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }
}

static void texturing_tick(texturing_task_t *task)
{
  wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->kind_menu,
                                task->kind);
  wuss_menu_tick_exclusive_live(task->base.menu_handle, &task->scheme_menu,
                                task->scheme);
}

static result_t texturing_menu_select(texturing_task_t   *task,
                                      const wuss_event_t *event)
{
  result_t rc;

  if (event->data.menu_select.menu == &task->dither.menu)
  {
    /* display only: the texture itself is unchanged */
    task->dithering = dithermenu_select(&task->dither, task->base.menu_handle,
                                        event->data.menu_select.index);
    wuss_window_invalidate_visible(task->base.window);
    rc = result_OK;
  }
  else if (event->data.menu_select.menu == &task->kind_menu)
  {
    rc = texturing_rechain(task, event->data.menu_select.index, NULL, NULL);
    texturing_tick(task);
  }
  else if (event->data.menu_select.menu == &task->scheme_menu)
  {
    rc = texturing_set_scheme(task, event->data.menu_select.index);
    texturing_tick(task);
  }
  else if (event->data.menu_select.menu == &task->preset_menu)
  {
    rc = texturing_preset(task, event->data.menu_select.index);
    texturing_tick(task);
  }
  else if (event->data.menu_select.menu == &task->add_menu)
  {
    rc = texturing_add(task, event->data.menu_select.index);
  }
  else if (wuss_menu_select_is(event, &task->graph_menu,
                               TEXTURING_GRAPH_DELETE))
  {
    rc = texturing_delete(task);
  }
  else if (wuss_menu_select_is(event, &task->base.menu,
                               TEXTURING_MENU_MUTATE))
  {
    rc = texturing_mutate(task);
  }
  else if (wuss_menu_select_is(event, &task->base.menu,
                               TEXTURING_MENU_RANDOMISE))
  {
    rc = texturing_randomise(task);
    texturing_tick(task);
  }
  else if (wuss_menu_select_is(event, &task->base.menu, TEXTURING_MENU_GRAPH))
  {
    rc = texturing_open_graph(task);
  }
  else
  {
    return result_OK;
  }

  return rc;
}

static result_t texturing_handle(wuss_window_t      *window,
                                 const wuss_event_t *event,
                                 void               *task_data)
{
  result_t          rc;
  texturing_task_t *task;
  int               k;

  task = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_CLOSE:
    if (window == task->base.window && task->graph_window != NULL)
    {
      /* the graph is meaningless without its texture; its own CLOSE tears
       * down the gadget */
      wuss_window_close(task->graph_window);
    }
    else if (window == task->graph_window)
    {
      wuss_nodeview_destroy(task->nodeview);
      task->nodeview     = NULL;
      task->graph_window = NULL;
    }
    /* each dialogue ignores what is not its own window's */
    for (k = 0; k < TEXTURING_NCONFS; k++)
    {
      rc = confdlg_event(&task->confs[k], window, event);
      if (rc != result_OK)
        return rc;
    }
    return result_OK;

  case wuss_EVENT_QUIT:
    wuss_nodeview_destroy(task->nodeview);
    task->nodeview = NULL;
    nodegraph_destroy(task->graph);
    task->graph = NULL;
    /* FALLTHROUGH */

  case wuss_EVENT_ICON:
  case wuss_EVENT_RELAYOUT:
    for (k = 0; k < TEXTURING_NCONFS; k++)
    {
      rc = confdlg_event(&task->confs[k], window, event);
      if (rc != result_OK)
        return rc;
    }
    return result_OK;

  case wuss_EVENT_PRE_SHOW:
    /* only one dialogue may answer, as answering opens the menu row */
    for (k = 0; k < TEXTURING_NCONFS; k++)
      if (window == wuss_dialogue_window(task->confs[k].dialogue))
        break;
    if (k == TEXTURING_NCONFS)
      return result_OK;
    return confdlg_event(&task->confs[k], window, event);

  case wuss_EVENT_REDRAW:
    if (window == task->graph_window)
    {
      (void) wuss_nodeview_handle_event(task->nodeview, event);
      return result_OK;
    }
    return texturing_redraw(event, task);

  case wuss_EVENT_MOUSE:
    if (window == task->graph_window)
      return texturing_graph_mouse(task, event);
    if (window != task->base.window ||
        event->data.mouse.action != wuss_MOUSE_DOWN)
      return result_OK;
    if (event->data.mouse.button & wuss_BUTTON_MENU)
    {
      texturing_retarget(task);
      wuss_menu_tick_exclusive(&task->kind_menu, task->kind);
      wuss_menu_tick_exclusive(&task->scheme_menu, task->scheme);
      return wuss_menu_open_ticked(task->base.delegate, &task->base.menu, 0,
                                   wuss_get_pointer(task->base.wuss),
                                   &task->base.menu_handle);
    }
    if (event->data.mouse.button & wuss_BUTTON_SELECT)
      return texturing_reseed(task);
    if (event->data.mouse.button & wuss_BUTTON_ADJUST)
      return texturing_set_scheme(task, (task->scheme + 1) % TEXTURING_NSCHEMES);
    return result_OK;

  case wuss_EVENT_KEY:
    if (window == task->graph_window)
    {
      if (!task_key_is_plain(task->base.delegate, &task->graph_menu, event,
                             &rc))
        return rc;
      return result_WUSS_KEY_UNCLAIMED;
    }
    if (window != task->base.window)
      return result_WUSS_KEY_UNCLAIMED;
    if (!task_key_is_plain(task->base.delegate, &task->base.menu, event, &rc))
      return rc;

    return texturing_key(task, event->data.key.code);

  case wuss_EVENT_MENU_SELECT:
    return texturing_menu_select(task, event);

  default:
    return result_OK;
  }
}

/* wuss_saveas_save_fn_t: opaque is the texturing_task_t; writes one tile */
static result_t texturing_saveas_save(const char *path, void *opaque)
{
  texturing_task_t *task;

  task = opaque;

  return bitmap_save_png(&task->bm, path);
}

static result_t texturing_init(texturing_task_t *task)
{
  result_t       rc;
  int            i;
  wuss_window_t *win;

  task->ctx.seed   = 0x9E3779B9u;
  task->ctx.pixels = task->pixels;
  rng_seed(&task->rng, task->ctx.seed);
  task->scheme     = 0;
  task->dithering  = screen_DITHER_BLUE_NOISE;
  task->output     = nodegraph_NONE;

  rc = bitmap_init(&task->bm, SIZE2D(TEXTURING_SIZE, TEXTURING_SIZE),
                   pixelfmt_p8, TEXTURING_SIZE, NULL, task->pixels);
  if (rc != result_OK)
    return rc;

  /* borrow the palette: bitmap_init would allocate a copy that nothing
   * frees, as the instance block is freed whole */
  task->bm.palette = task->palette;

  texturing_build_palette(task);

  rc = nodegraph_create(&task->graph);
  if (rc != result_OK)
    return rc;

  rc = texturing_rechain(task, TEXTURING_KIND_CLOUDS, NULL,
                         (const texturing_tone_t *)
                         texturing_defaults[TEXTURING_NODE_TONE]);
  if (rc != result_OK)
    return rc;

  rc = task_window_create(task->base.delegate,
                          SIZE2D(TEXTURING_SIZE * TEXTURING_REPEAT,
                                 TEXTURING_SIZE * TEXTURING_REPEAT),
                          "Texturing", &task->base.window);
  if (rc != result_OK)
    return rc;

  /* Save, then View's rows (Display, Colours), then the texture's own rows:
   * mostly submenus, so all undivided, as menu-standard.md has it */
  taskapp_save_row(&task->base, task->menu_items, TEXTURING_MENU_SAVE,
                   wuss_MENU_ITEM_NONE);

  dithermenu_init(&task->dither, task->dithering);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_NONE, &task->dither.display);

  for (i = 0; i < TEXTURING_NSCHEMES; i++)
    WUSS_MENU_ITEM(task->scheme_items, i, texturing_schemes[i].name,
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->scheme_menu, "Colours", task->scheme_items,
                  TEXTURING_NSCHEMES);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_COLOURS, "Colours",
                      wuss_MENU_ITEM_NONE, &task->scheme_menu);

  for (i = 0; i < TEXTURING_NKINDS; i++)
    WUSS_MENU_ITEM(task->kind_items, i, texturing_node_names[i],
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->kind_menu, "Kind", task->kind_items, TEXTURING_NKINDS);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_KIND, "Kind",
                      wuss_MENU_ITEM_NONE, &task->kind_menu);

  for (i = 0; i < texturing_npresets; i++)
    WUSS_MENU_ITEM(task->preset_items, i, texturing_presets[i].name,
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->preset_menu, "Presets", task->preset_items,
                  texturing_npresets);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_PRESETS, "Presets",
                      wuss_MENU_ITEM_NONE, &task->preset_menu);

  WUSS_MENU_ITEM(task->menu_items, TEXTURING_MENU_CONFIGURATION,
                 "Configuration",
                 wuss_MENU_ITEM_BORROWED_SUBMENU | wuss_MENU_ITEM_PRE_OPEN);

  WUSS_MENU_ITEM(task->menu_items, TEXTURING_MENU_TONE, "Tone",
                 wuss_MENU_ITEM_BORROWED_SUBMENU | wuss_MENU_ITEM_PRE_OPEN);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, TEXTURING_MENU_MUTATE, "Mutate",
                          wuss_MENU_ITEM_NONE, "M");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, TEXTURING_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_NONE, "^R");

  WUSS_MENU_ITEM(task->menu_items, TEXTURING_MENU_GRAPH, "Graph",
                 wuss_MENU_ITEM_NONE);

  WUSS_MENU_TITLE(task->base.menu, "Texturing", task->menu_items,
                  NELEMS(task->menu_items));

  for (i = 0; i < TEXTURING_NCONFS; i++)
    WUSS_MENU_ITEM(task->add_items, i, texturing_node_names[i],
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->add_menu, "Add", task->add_items, TEXTURING_NCONFS);
  WUSS_MENU_ITEM_MENU(task->graph_items, TEXTURING_GRAPH_ADD, "Add",
                      wuss_MENU_ITEM_NONE, &task->add_menu);
  WUSS_MENU_ITEM_SHORTCUT(task->graph_items, TEXTURING_GRAPH_DELETE,
                          "Delete", wuss_MENU_ITEM_NONE, "DEL");
  WUSS_MENU_TITLE(task->graph_menu, "Graph", task->graph_items,
                  TEXTURING_GRAPH__LIMIT);

  for (i = 0; i < TEXTURING_NCONFS; i++)
  {
    rc = confdlg_create(&task->confs[i], &task->base, &texturing_confs[i],
                        task->scratch);
    if (rc != result_OK)
      return rc;
  }

  win = wuss_dialogue_window(task->confs[TEXTURING_NODE_TONE].dialogue);
  wuss_window_set_title(win, "Tone");
  task->menu_items[TEXTURING_MENU_TONE].window = win;
  win = wuss_dialogue_window(task->confs[TEXTURING_NODE_BLEND].dialogue);
  wuss_window_set_title(win, "Blend");

  texturing_retarget(task);

  return result_OK;
}

result_t texturing_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Texturing",
    .purpose       = "Tileable procedural textures",
    .instance_size = sizeof(texturing_task_t),
    .init          = (taskapp_init_fn_t) texturing_init,
    .handle        = texturing_handle,
    .all_windows   = true, /* the graph window takes clicks and keys too */
    .save_name     = "texture.png",
    .save          = texturing_saveas_save
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
