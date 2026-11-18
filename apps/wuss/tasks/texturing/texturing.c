/* wuss/tasks/texturing/texturing.c -- tileable procedural texture task */

#ifdef WUSS_APP

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
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
#include "../common/effectstack.h"
#include "../common/taskapp.h"

#include "texturing-gen.h"

/* The window shows the texture repeated this many times each way, so that
 * any seam would show. */
#define TEXTURING_REPEAT 2

/* Arm length of the crosshair marking each tile's top-left. */
#define TEXTURING_CROSS 3

/* Fills the content area with a procedural texture that tiles seamlessly:
 * every noise lattice wraps at the tile edge. The texture is the output of a
 * node graph (see texturing-gen.h): generators and constant Fills feed Tone
 * and Blend nodes, whose values a Colourise node turns into an image in the
 * scheme of the Colours node linked to it. Effects nodes each run their own
 * effect stack (see effectstack.h) over an image, so may be chained, and the
 * one Output node's image is what the window shows and Save writes. Seed
 * nodes hold the randomness: each feeds the generators and Tones linked to
 * it, and one left unlinked reads as zero.
 *
 * A SELECT drag moves the tiles, a crosshair marking each tile's top-left;
 * ADJUST steps the colour scheme. Menu >
 * Graph opens the graph window; Menu > Save writes a single tile as a PNG;
 * Menu > Display > Dithering picks how the tile is dithered to the screen;
 * Menu > Presets replaces the graph with a chain of a generator, a Tone, a
 * Colourise and the Output, with a Seed and a Colours node; Menu > Mutate
 * (M) nudges the selected node's parameters, or every generator's; Menu >
 * Randomise picks a fresh seed, kind and scheme. Once a click has given the
 * window the input focus, Space reseeds, Left/Right step the colour scheme,
 * K steps the kind and G steps the grain through None, Light and Heavy
 * presets. The scheme stepped is the selected Colours node's, else the
 * first Colours node's.
 *
 * In the graph window, Menu > Add puts a node at the pointer, from
 * submenus by what the node does, and Menu > Delete (Delete) removes the
 * selected node, though never the Output. A double-click on a node opens
 * its parameters, or for a Colours node the list of schemes, or reseeds a
 * Seed node, or opens an Effects node's stack, whose own menu adds, edits
 * and deletes effects and sets the level. */

/* The seed of the first chain, and of any chain made without a Seed node to
 * take it from. */
#define TEXTURING_SEED 0x9E3779B9u

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
  },
  { /* Fill: one value everywhere, so one colour of the scheme */
    { "Level",      0, 255, 0, TEXTURING_PARAM(0) }
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
  TEXTURING_CONF(TEXTURING_NODE_BLEND,  1),
  TEXTURING_CONF(TEXTURING_NODE_FILL,   1)
};

enum
{
  TEXTURING_MENU_GRAPH = 0,
  TEXTURING_MENU_SAVE,
  TEXTURING_MENU_DISPLAY,
  TEXTURING_MENU_PRESETS,
  TEXTURING_MENU_MUTATE,
  TEXTURING_MENU_RANDOMISE,
  TEXTURING_MENU__LIMIT
};

/* The graph window's menu. */
enum
{
  TEXTURING_GRAPH_ADD = 0,
  TEXTURING_GRAPH_DELETE,
  TEXTURING_GRAPH__LIMIT
};

/* Menu > Add's submenus: the node kinds grouped by what they do. */
static const struct
{
  const char *name;
  int         nkinds;
  int         kinds[TEXTURING_NKINDS];
}
texturing_add_groups[] =
{
  { "Input",    3, { TEXTURING_NODE_FILL, TEXTURING_NODE_COLOURS,
                     TEXTURING_NODE_SEED } },
  { "Generate", TEXTURING_NKINDS,
    { TEXTURING_KIND_CLOUDS, TEXTURING_KIND_MARBLE, TEXTURING_KIND_WOOD,
      TEXTURING_KIND_CELLS, TEXTURING_KIND_STONE, TEXTURING_KIND_WEAVE,
      TEXTURING_KIND_VEINS } },
  { "Adjust",   1, { TEXTURING_NODE_TONE } },
  { "Combine",  1, { TEXTURING_NODE_BLEND } },
  { "Image",    2, { TEXTURING_NODE_COLOURISE, TEXTURING_NODE_EFFECTS } }
};

#define TEXTURING_NGROUPS ((int) NELEMS(texturing_add_groups))

/* Mutate nudges each slider by up to this percent of its range, and always
 * by at least a step. */
#define TEXTURING_MUTATE 15

/* Graph window's visible size; the gadget sets its document. */
#define TEXTURING_GRAPH_W 480
#define TEXTURING_GRAPH_H 224

/* An Effects node's client: its own stack, in its own list window. */
typedef struct texturing_effects
{
  texturing_task_t *task;
  nodegraph_node_t  node;
  int               rechain; /* zero when only the level moved */
  effectstack_t     stack;
}
texturing_effects_t;

struct texturing_task
{
  taskapp_instance_t base;
  texturing_ctx_t    ctx;    /* pixels and hooks, shared by every node */
  rng_t              rng;    /* for Mutate, Randomise and new seeds */
  int                kind;   /* last kind chained, for K */
  nodegraph_t       *graph;
  nodegraph_node_t   output; /* the one Output node */
  nodegraph_node_t   colours; /* the Colours node the scheme menu sets */
  int                scratch[TEXTURING_NPARAMS]; /* dialogues on no node */
  confdlg_t          confs[TEXTURING_NCONFS]; /* see texturing_confs */
  bitmap_t           bm;     /* wraps pixels[] */
  pixelfmt_rgba8888_t pixels[TEXTURING_NPIXELS]; /* the Output's image */
  texturing_effects_t *effects_active; /* whose rows effects_menu holds */
  wuss_menu_item_t   effects_items[EFFECTSTACK_NROWS];
  wuss_menu_t        effects_menu; /* an Effects node's list window's menu */
  screen_dither_t    dithering; /* how the tile is blitted to the screen */
  wuss_window_t     *graph_window;
  wuss_nodeview_t   *nodeview;
  colour_t           type_colours[TEXTURING_NTYPES]; /* ports and links */
  point_t            add_at; /* where Menu > Add puts a node */
  point_t            origin; /* a tile's top-left, within one tile */
  point_t            grab;   /* pointer position at the last drag event */
  int                dragging; /* SELECT held: pointer moves drag origin */
  wuss_menu_item_t   menu_items[TEXTURING_MENU__LIMIT];
  wuss_menu_item_t   preset_items[16];
  wuss_menu_t        preset_menu;
  wuss_menu_item_t   scheme_items[TEXTURING_NSCHEMES]; /* stepped, smooth */
  wuss_menu_t        stepped_menu; /* the stepped run of scheme_items */
  wuss_menu_t        smooth_menu;  /* the smooth run of scheme_items */
  wuss_menu_item_t   depth_items[2];
  wuss_menu_t        scheme_menu;  /* Stepped and Smooth submenus */
  dithermenu_t       dither;    /* "Display" submenu: screen dithering */
  wuss_menu_item_t   graph_items[TEXTURING_GRAPH__LIMIT];
  wuss_menu_t        graph_menu;
  wuss_menu_item_t   add_items[TEXTURING_NGROUPS];
  wuss_menu_t        add_menu;
  wuss_menu_item_t   group_items[TEXTURING_NGROUPS][TEXTURING_NKINDS];
  wuss_menu_t        group_menus[TEXTURING_NGROUPS]; /* Add's submenus */
};

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

/* The Colours node the scheme keys act on: the selected node if a Colours,
 * else the first, or nodegraph_NONE. */
static nodegraph_node_t texturing_colours_node(texturing_task_t *task)
{
  nodegraph_node_t n;

  n = texturing_selected(task);
  if (n != nodegraph_NONE &&
      texturing_kind_of(task, n) == TEXTURING_NODE_COLOURS)
    return n;
  return texturing_find(task, TEXTURING_NODE_COLOURS,
                        TEXTURING_NODE_COLOURS + 1);
}

/* That Colours node's scheme, else the first. */
static int texturing_scheme(texturing_task_t *task)
{
  nodegraph_node_t n;

  n = texturing_colours_node(task);
  return n != nodegraph_NONE ? texturing_node(task->graph, n)->params[0] : 0;
}

/* The Tone node G acts on: the selected node if a Tone, else the first. */
static nodegraph_node_t texturing_tone_node(texturing_task_t *task)
{
  nodegraph_node_t n;

  n = texturing_selected(task);
  if (n != nodegraph_NONE && texturing_kind_of(task, n) == TEXTURING_NODE_TONE)
    return n;
  return texturing_find(task, TEXTURING_NODE_TONE, TEXTURING_NODE_TONE + 1);
}

/* The first Seed node's seed, which a new chain keeps, or TEXTURING_SEED. */
static uint32_t texturing_seed(texturing_task_t *task)
{
  nodegraph_node_t n;

  n = texturing_find(task, TEXTURING_NODE_SEED, TEXTURING_NODE_SEED + 1);
  return n != nodegraph_NONE
       ? (uint32_t) texturing_node(task->graph, n)->params[0]
       : TEXTURING_SEED;
}

/* Give Seed node n a fresh seed, dirtying what it feeds. */
static void texturing_reroll(texturing_task_t *task, nodegraph_node_t n)
{
  texturing_node(task->graph, n)->params[0] =
    (int) rng_xorshift32(&task->rng);
  nodegraph_dirty(task->graph, n);
}

/* Give every Seed node a fresh seed. */
static void texturing_reroll_all(texturing_task_t *task)
{
  nodegraph_node_t n;

  for (n = nodegraph_next(task->graph, nodegraph_NONE); n != nodegraph_NONE;
       n = nodegraph_next(task->graph, n))
    if (texturing_kind_of(task, n) == TEXTURING_NODE_SEED)
      texturing_reroll(task, n);
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

/* Evaluate what changed and show it. */
static result_t texturing_update(texturing_task_t *task)
{
  result_t rc;

  rc = nodegraph_evaluate(task->graph);
  if (task->base.window != NULL)
    wuss_window_invalidate_extent(task->base.window);

  return rc;
}

/* ----------------------------------------------------------------------- */

/* texturing_effect_fn_t: run an Effects node's stack over in into out.
 *
 * ponytail: effects that spread (blur, sharpen) clamp at the tile's edges,
 * so the tile can seam; wrap the effects if that shows */
static result_t texturing_effect(void                      *client,
                                 const pixelfmt_rgba8888_t *in,
                                 pixelfmt_rgba8888_t       *out)
{
  result_t             rc;
  texturing_effects_t *fx;
  bitmap_t             src;

  fx = client;
  rc = bitmap_init(&src, SIZE2D(TEXTURING_SIZE, TEXTURING_SIZE),
                   pixelfmt_rgba8888, TEXTURING_SIZE * 4, NULL, (void *) in);
  if (rc == result_OK)
    rc = effectstack_run(&fx->stack, &src, fx->rechain);
  fx->rechain = 1; /* anything else that dirties the node changes in */
  if (rc != result_OK)
    return rc;

  memcpy(out, fx->stack.result.base, TEXTURING_NPIXELS * sizeof(*out));
  return result_OK;
}

/* effectstack_changed_fn_t: opaque is the texturing_effects_t */
static void texturing_effects_changed(void *opaque, int rechain)
{
  texturing_effects_t *fx;

  fx          = opaque;
  fx->rechain = rechain;
  nodegraph_dirty(fx->task->graph, fx->node);
  (void) texturing_update(fx->task);
}

/* texturing_forget_fn_t: an Effects node goes, so its stack does. At QUIT
 * the graph is detached first and the windows go by themselves. */
static void texturing_forget(void *client)
{
  texturing_effects_t *fx;

  fx = client;
  if (fx->task->effects_active == fx)
    fx->task->effects_active = NULL;
  if (fx->task->graph != NULL)
    effectstack_close(&fx->stack);
  effectstack_destroy(&fx->stack);
  free(fx);
}

/* Give new Effects node n its stack. */
static result_t texturing_effects_attach(texturing_task_t *task,
                                         nodegraph_node_t  n)
{
  result_t             rc;
  texturing_effects_t *fx;

  fx = malloc(sizeof(*fx));
  if (fx == NULL)
    return result_OOM;

  fx->task    = task;
  fx->node    = n;
  fx->rechain = 1;
  rc = effectstack_init(&fx->stack, &task->base, texturing_effects_changed,
                        fx);
  if (rc != result_OK)
  {
    free(fx);
    return rc;
  }

  texturing_node(task->graph, n)->client = fx;
  return result_OK;
}

/* Open the menu of fx's list window, its rows bound to fx's stack. */
static result_t texturing_effects_menu(texturing_task_t    *task,
                                       texturing_effects_t *fx)
{
  task->effects_active = fx;
  effectstack_menu_rows(&fx->stack, &task->effects_menu, task->effects_items,
                        0);
  task->effects_items[EFFECTSTACK_ROW_LEVEL].flags |= wuss_MENU_ITEM_DASHED;
  effectstack_menu_update(&fx->stack);

  return wuss_menu_open_ticked(task->base.delegate, &task->effects_menu, 0,
                               wuss_get_pointer(task->base.wuss),
                               &task->base.menu_handle);
}

/* Hand event to the Effects nodes' stacks: menu events only to the stack
 * whose menu is open, the rest to each. Return non-zero when one claimed
 * it, with *rc set. */
static int texturing_effects_event(texturing_task_t   *task,
                                   wuss_window_t      *window,
                                   const wuss_event_t *event,
                                   result_t           *rc)
{
  texturing_effects_t *fx;
  nodegraph_node_t     n;

  *rc = result_OK;
  if (task->graph == NULL)
    return 0;

  if (event->kind == wuss_EVENT_MENU_SELECT ||
      event->kind == wuss_EVENT_PRE_SUBMENU_OPEN ||
      event->kind == wuss_EVENT_MENU_CLOSED)
  {
    fx = task->effects_active;
    return fx != NULL && effectstack_event(&fx->stack, window, event, rc);
  }

  for (n = nodegraph_next(task->graph, nodegraph_NONE); n != nodegraph_NONE;
       n = nodegraph_next(task->graph, n))
  {
    fx = texturing_node(task->graph, n)->client;
    if (fx == NULL)
      continue;
    if (effectstack_event(&fx->stack, window, event, rc))
      return 1;
    if (event->kind == wuss_EVENT_MOUSE && window != NULL &&
        window == fx->stack.list_window)
    {
      /* the stack claims all but MENU */
      *rc = texturing_effects_menu(task, fx);
      return 1;
    }
  }

  return 0;
}

/* Close every Effects node's list window. */
static void texturing_effects_close_all(texturing_task_t *task)
{
  texturing_effects_t *fx;
  nodegraph_node_t     n;

  for (n = nodegraph_next(task->graph, nodegraph_NONE); n != nodegraph_NONE;
       n = nodegraph_next(task->graph, n))
  {
    fx = texturing_node(task->graph, n)->client;
    if (fx != NULL)
      effectstack_close(&fx->stack);
  }
}

/* ponytail: any parameter change re-evaluates every node, as the dialogue
 * does not say which node it edited; dirty only that node if big graphs
 * lag */
static result_t texturing_refresh_all(texturing_task_t *task)
{
  texturing_dirty_all(task->graph);
  return texturing_update(task);
}

/* Replace the graph with a chain of a generator of kind, a Tone, a
 * Colourise and the Output, with a Seed node of the current seed and a
 * Colours node of scheme, or of the current scheme if negative. */
static result_t texturing_rechain(texturing_task_t       *task,
                                  int                     kind,
                                  const int              *params,
                                  const texturing_tone_t *tone,
                                  int                     scheme)
{
  result_t         rc;
  texturing_tone_t keep;
  nodegraph_node_t n;

  if (scheme < 0)
    scheme = texturing_scheme(task);

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
  task->colours = nodegraph_NONE;
  rc = texturing_chain(task->graph, &task->ctx, texturing_seed(task), kind,
                       params, tone, scheme);
  task->output = texturing_find(task, TEXTURING_NODE_OUTPUT,
                                TEXTURING_NODE_OUTPUT + 1);
  if (task->nodeview != NULL)
    wuss_nodeview_refresh(task->nodeview);
  if (rc != result_OK)
    return rc;

  return texturing_update(task);
}

/* ----------------------------------------------------------------------- */

/* Draw a small crosshair centred on p: black, with a white centre line, so
 * it shows on any texture. */
static void texturing_crosshair(screen_t *scr, point_t p)
{
  const int r = TEXTURING_CROSS;

  screen_fill_rect(scr, p.x - r, p.y - 1, SIZE2D(2 * r + 1, 3),
                   colour_rgb(0x00, 0x00, 0x00));
  screen_fill_rect(scr, p.x - 1, p.y - r, SIZE2D(3, 2 * r + 1),
                   colour_rgb(0x00, 0x00, 0x00));
  screen_fill_hline(scr, p.x - r + 1, p.y, 2 * r - 1,
                    colour_rgb(0xFF, 0xFF, 0xFF));
  screen_fill_rect(scr, p.x, p.y - r + 1, SIZE2D(1, 2 * r - 1),
                   colour_rgb(0xFF, 0xFF, 0xFF));
}

static result_t texturing_redraw(const wuss_event_t *event,
                                 texturing_task_t   *task)
{
  result_t     rc;
  const box_t *bounds;
  const box_t *content;
  point_t      base;
  int          x, y;

  bounds  = event->data.redraw.bounds;
  content = event->data.redraw.content;
  base    = POINT(bounds->x0 - event->data.redraw.scroll.x + task->origin.x,
                  bounds->y0 - event->data.redraw.scroll.y + task->origin.y);

  /* dithered, as the desktop is often 16 colours */
  rc = screen_copy_bitmap_tiled_dithered(event->data.redraw.scr, content,
                                         &task->bm, base, task->dithering);
  if (rc != result_OK)
    return rc;

  /* origin is within one tile, so the tile before base starts off the
   * content's top-left */
  for (y = base.y - TEXTURING_SIZE; y <= content->y1 + TEXTURING_CROSS;
       y += TEXTURING_SIZE)
    for (x = base.x - TEXTURING_SIZE; x <= content->x1 + TEXTURING_CROSS;
         x += TEXTURING_SIZE)
      texturing_crosshair(event->data.redraw.scr, POINT(x, y));

  return result_OK;
}

static result_t texturing_reseed(texturing_task_t *task)
{
  texturing_reroll_all(task);
  return texturing_update(task);
}

/* confdlg_applied_fn_t: a dialogue's Apply or Default changed a node */
static result_t texturing_applied(texturing_task_t *task, int old)
{
  NOT_USED(old);

  if (task->base.window == NULL)
    return result_OK; /* window closed while the dialogue lingers */

  return texturing_refresh_all(task);
}

/* Tick scheme in its Colours submenu and that submenu's row, unticking the
 * rest; handle is the open chain to redraw, or NULL. */
static void texturing_tick_scheme(texturing_task_t  *task,
                                  wuss_menu_handle_t handle,
                                  int                scheme)
{
  int stepped;

  stepped = scheme < TEXTURING_NSTEPPED;
  wuss_menu_tick_exclusive_live(handle, &task->scheme_menu, stepped ? 0 : 1);
  wuss_menu_tick_exclusive_live(handle, &task->stepped_menu,
                                stepped ? scheme : -1);
  wuss_menu_tick_exclusive_live(handle, &task->smooth_menu,
                                stepped ? -1 : scheme - TEXTURING_NSTEPPED);
}

/* Set Colours node n's scheme. */
static result_t texturing_set_scheme(texturing_task_t *task,
                                     nodegraph_node_t  n,
                                     int               scheme)
{
  if (n == nodegraph_NONE || nodegraph_class(task->graph, n) == NULL)
    return result_OK;

  texturing_node(task->graph, n)->params[0] = scheme;
  nodegraph_dirty(task->graph, n);

  return texturing_update(task);
}

/* ADJUST, Left and Right: step the Colours node's scheme by delta. */
static result_t texturing_step_scheme(texturing_task_t *task, int delta)
{
  return texturing_set_scheme(task, texturing_colours_node(task),
                              (texturing_scheme(task) + delta +
                               TEXTURING_NSCHEMES) % TEXTURING_NSCHEMES);
}

/* MENU opens the menu, ADJUST steps the colour scheme and a SELECT drag
 * moves the tile origin. */
static result_t texturing_mouse(texturing_task_t   *task,
                                const wuss_event_t *event)
{
  point_t point;

  point = event->data.mouse.point;

  if (event->data.mouse.action == wuss_MOUSE_UP)
  {
    task->dragging = 0;
  }
  else if (event->data.mouse.action == wuss_MOUSE_DOWN)
  {
    if (event->data.mouse.button & wuss_BUTTON_MENU)
      return wuss_menu_open_ticked(task->base.delegate, &task->base.menu, 0,
                                   wuss_get_pointer(task->base.wuss),
                                   &task->base.menu_handle);
    if (event->data.mouse.button & wuss_BUTTON_SELECT)
    {
      task->dragging = 1;
      task->grab     = point;
    }
    if (event->data.mouse.button & wuss_BUTTON_ADJUST)
      return texturing_step_scheme(task, 1);
  }
  else if (task->dragging) /* wuss_MOUSE_MOVE */
  {
    /* the tile size is a power of two, so masking wraps both signs */
    task->origin.x = (task->origin.x + point.x - task->grab.x) &
                     (TEXTURING_SIZE - 1);
    task->origin.y = (task->origin.y + point.y - task->grab.y) &
                     (TEXTURING_SIZE - 1);
    task->grab     = point;
    wuss_window_invalidate_visible(task->base.window);
  }

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
  return texturing_rechain(task, texturing_presets[preset].kind,
                           texturing_presets[preset].params,
                           &texturing_presets[preset].tone,
                           texturing_presets[preset].scheme);
}

/* Randomise: fresh seeds, kind and scheme. Shared by the menu row and its
 * Ctrl-R shortcut. */
static result_t texturing_randomise(texturing_task_t *task)
{
  int kind, scheme;

  texturing_reroll_all(task);
  kind   = rng_range(&task->rng, TEXTURING_NKINDS);
  scheme = rng_range(&task->rng, TEXTURING_NSCHEMES);

  return texturing_rechain(task, kind, NULL, NULL, scheme);
}

/* ----------------------------------------------------------------------- */

/* wuss_nodeview_activate_fn_t: open the node's dialogue by the pointer, or
 * a Colours node's scheme menu, or reroll a Seed node, or open an Effects
 * node's stack */
static void texturing_activate(wuss_nodeview_t *nodeview,
                               nodegraph_node_t node,
                               void            *opaque)
{
  texturing_task_t    *task;
  int                  conf;
  texturing_effects_t *fx;
  wuss_window_t       *win;

  NOT_USED(nodeview);

  task = opaque;
  conf = texturing_kind_of(task, node);
  if (conf == TEXTURING_NODE_EFFECTS)
  {
    fx = texturing_node(task->graph, node)->client;
    if (fx != NULL)
      (void) effectstack_open(&fx->stack, task->graph_window);
    return;
  }
  if (conf == TEXTURING_NODE_COLOURS)
  {
    task->colours = node;
    texturing_tick_scheme(task, NULL,
                          texturing_node(task->graph, node)->params[0]);
    (void) wuss_menu_open(task->base.delegate, &task->scheme_menu,
                          wuss_get_pointer(task->base.wuss),
                          &task->base.menu_handle);
    return;
  }
  if (conf == TEXTURING_NODE_SEED)
  {
    texturing_reroll(task, node);
    (void) texturing_update(task);
    return;
  }
  if (conf >= TEXTURING_NCONFS)
    return; /* the Output's settings are the Display menu */

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

  rc = wuss_nodeview_create(&task->nodeview, task->graph_window,
                            task->graph, texturing_activate,
                            texturing_changed, task);
  if (rc != result_OK)
    return rc;

  /* PICO-8 colours, so they stay exact on the demo's 16-colour screen */
  task->type_colours[TEXTURING_TYPE_VALUES] = colour_rgb(0x29, 0xAD, 0xFF);
  task->type_colours[TEXTURING_TYPE_SCHEME] = colour_rgb(0x7E, 0x25, 0x53);
  task->type_colours[TEXTURING_TYPE_SEED]   = colour_rgb(0xFF, 0xA3, 0x00);
  task->type_colours[TEXTURING_TYPE_IMAGE]  = colour_rgb(0x00, 0x87, 0x51);
  wuss_nodeview_set_type_colours(task->nodeview, task->type_colours,
                                 TEXTURING_NTYPES);
  return result_OK;
}

static result_t texturing_add(texturing_task_t *task, int kind)
{
  result_t         rc;
  nodegraph_node_t n;

  rc = texturing_node_add(task->graph, &task->ctx, kind, NULL,
                          MAX(task->add_at.x, 0), MAX(task->add_at.y, 0), &n);
  if (rc != result_OK)
    return rc;

  if (kind == TEXTURING_NODE_EFFECTS)
  {
    rc = texturing_effects_attach(task, n);
    if (rc != result_OK)
    {
      (void) nodegraph_remove(task->graph, n);
      return rc;
    }
  }

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
                             NULL, -1);

  case 'g':
  case 'G':
    return texturing_next_grain(task);

  case wuss_KEY_LEFT:
    return texturing_step_scheme(task, -1);

  case wuss_KEY_RIGHT:
    return texturing_step_scheme(task, 1);

  default:
    return result_WUSS_KEY_UNCLAIMED;
  }
}

static result_t texturing_menu_select(texturing_task_t   *task,
                                      const wuss_event_t *event)
{
  result_t rc;
  int      g;
  int      scheme;

  for (g = 0; g < TEXTURING_NGROUPS; g++)
    if (event->data.menu_select.menu == &task->group_menus[g])
      return texturing_add(task, texturing_add_groups[g].kinds
                                 [event->data.menu_select.index]);

  if (event->data.menu_select.menu == &task->dither.menu)
  {
    /* display only: the texture itself is unchanged */
    task->dithering = dithermenu_select(&task->dither, task->base.menu_handle,
                                        event->data.menu_select.index);
    wuss_window_invalidate_visible(task->base.window);
    rc = result_OK;
  }
  else if (event->data.menu_select.menu == &task->stepped_menu ||
           event->data.menu_select.menu == &task->smooth_menu)
  {
    scheme = event->data.menu_select.index;
    if (event->data.menu_select.menu == &task->smooth_menu)
      scheme += TEXTURING_NSTEPPED;
    rc = texturing_set_scheme(task, task->colours, scheme);
    texturing_tick_scheme(task, task->base.menu_handle, scheme);
  }
  else if (event->data.menu_select.menu == &task->preset_menu)
  {
    rc = texturing_preset(task, event->data.menu_select.index);
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
  nodegraph_t      *graph;

  task = task_data;

  if (event->kind != wuss_EVENT_QUIT &&
      texturing_effects_event(task, window, event, &rc))
    return rc;

  switch (event->kind)
  {
  case wuss_EVENT_CLOSE:
    if (window == task->base.window)
    {
      /* the graph and the effects are meaningless without their texture;
       * their own CLOSEs tear down the gadgets */
      if (task->graph_window != NULL)
        wuss_window_close(task->graph_window);
      texturing_effects_close_all(task);
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
    /* detached first, so texturing_forget leaves the windows be */
    graph       = task->graph;
    task->graph = NULL;
    nodegraph_destroy(graph);
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
    if (window != task->base.window)
      return result_OK;
    return texturing_mouse(task, event);

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
  int            i, g;
  wuss_window_t *win;

  task->ctx.pixels = task->pixels;
  task->ctx.effect = texturing_effect;
  task->ctx.forget = texturing_forget;
  rng_seed(&task->rng, TEXTURING_SEED);
  task->dithering  = screen_DITHER_BLUE_NOISE;
  task->output     = nodegraph_NONE;
  task->colours    = nodegraph_NONE;

  rc = bitmap_init(&task->bm, SIZE2D(TEXTURING_SIZE, TEXTURING_SIZE),
                   pixelfmt_rgba8888, TEXTURING_SIZE * 4, NULL,
                   task->pixels);
  if (rc != result_OK)
    return rc;

  rc = nodegraph_create(&task->graph);
  if (rc != result_OK)
    return rc;

  rc = texturing_rechain(task, TEXTURING_KIND_CLOUDS, NULL,
                         (const texturing_tone_t *)
                         texturing_defaults[TEXTURING_NODE_TONE],
                         TEXTURING_SCHEME_SKY);
  if (rc != result_OK)
    return rc;

  rc = task_window_create(task->base.delegate,
                          SIZE2D(TEXTURING_SIZE * TEXTURING_REPEAT,
                                 TEXTURING_SIZE * TEXTURING_REPEAT),
                          "Texturing", &task->base.window);
  if (rc != result_OK)
    return rc;

  /* Graph first, as the graph is the main control. Then Save, View's
   * Display and the texture's own rows: mostly submenus, so undivided, as
   * menu-standard.md has it */
  WUSS_MENU_ITEM(task->menu_items, TEXTURING_MENU_GRAPH, "Graph",
                 wuss_MENU_ITEM_NONE);

  /* the rows are bound to a stack as its list window's menu opens */
  WUSS_MENU_TITLE(task->effects_menu, "Effects", task->effects_items,
                  EFFECTSTACK_NROWS);

  taskapp_save_row(&task->base, task->menu_items, TEXTURING_MENU_SAVE,
                   wuss_MENU_ITEM_DASHED);

  dithermenu_init(&task->dither, task->dithering);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_DISPLAY, "Display",
                      wuss_MENU_ITEM_NONE, &task->dither.display);

  for (i = 0; i < TEXTURING_NSCHEMES; i++)
    WUSS_MENU_ITEM(task->scheme_items, i, texturing_schemes[i].name,
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->stepped_menu, "Stepped", task->scheme_items,
                  TEXTURING_NSTEPPED);
  WUSS_MENU_TITLE(task->smooth_menu, "Smooth",
                  task->scheme_items + TEXTURING_NSTEPPED,
                  TEXTURING_NSCHEMES - TEXTURING_NSTEPPED);
  WUSS_MENU_ITEM_MENU(task->depth_items, 0, "Stepped", wuss_MENU_ITEM_NONE,
                      &task->stepped_menu);
  WUSS_MENU_ITEM_MENU(task->depth_items, 1, "Smooth", wuss_MENU_ITEM_NONE,
                      &task->smooth_menu);
  WUSS_MENU_TITLE(task->scheme_menu, "Colours", task->depth_items, 2);

  for (i = 0; i < texturing_npresets; i++)
    WUSS_MENU_ITEM(task->preset_items, i, texturing_presets[i].name,
                   wuss_MENU_ITEM_NONE);
  WUSS_MENU_TITLE(task->preset_menu, "Presets", task->preset_items,
                  texturing_npresets);
  WUSS_MENU_ITEM_MENU(task->menu_items, TEXTURING_MENU_PRESETS, "Presets",
                      wuss_MENU_ITEM_NONE, &task->preset_menu);

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, TEXTURING_MENU_MUTATE, "Mutate",
                          wuss_MENU_ITEM_NONE, "M");

  WUSS_MENU_ITEM_SHORTCUT(task->menu_items, TEXTURING_MENU_RANDOMISE,
                          "Randomise", wuss_MENU_ITEM_NONE, "^R");

  WUSS_MENU_TITLE(task->base.menu, "Texturing", task->menu_items,
                  NELEMS(task->menu_items));

  for (g = 0; g < TEXTURING_NGROUPS; g++)
  {
    for (i = 0; i < texturing_add_groups[g].nkinds; i++)
      WUSS_MENU_ITEM(task->group_items[g], i,
                     texturing_node_names[texturing_add_groups[g].kinds[i]],
                     wuss_MENU_ITEM_NONE);
    WUSS_MENU_TITLE(task->group_menus[g], texturing_add_groups[g].name,
                    task->group_items[g], texturing_add_groups[g].nkinds);
    WUSS_MENU_ITEM_MENU(task->add_items, g, texturing_add_groups[g].name,
                        wuss_MENU_ITEM_NONE, &task->group_menus[g]);
  }
  WUSS_MENU_TITLE(task->add_menu, "Add", task->add_items, TEXTURING_NGROUPS);
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
  win = wuss_dialogue_window(task->confs[TEXTURING_NODE_BLEND].dialogue);
  wuss_window_set_title(win, "Blend");
  win = wuss_dialogue_window(task->confs[TEXTURING_NODE_FILL].dialogue);
  wuss_window_set_title(win, "Fill");

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
