/* wuss/tasks/texturing/texturing-gen.h -- texture generation as a node graph */

#ifndef TASKS_TEXTURING_GEN_H
#define TASKS_TEXTURING_GEN_H

#include <stdint.h>

#include "base/result.h"
#include "datastruct/nodegraph.h"

/* Texture edge in pixels, as a power of two so that every lattice step is a
 * shift and a mask. */
#define TEXTURING_LOG2_SIZE 8
#define TEXTURING_SIZE      (1 << TEXTURING_LOG2_SIZE)
#define TEXTURING_NPIXELS   (TEXTURING_SIZE * TEXTURING_SIZE)

/* Most parameters any node has. Matches CONFDLG_MAX_ROWS. */
#define TEXTURING_NPARAMS 5

/* Node kinds: the generators, then the filters. */
enum
{
  TEXTURING_KIND_CLOUDS,
  TEXTURING_KIND_MARBLE,
  TEXTURING_KIND_WOOD,
  TEXTURING_KIND_CELLS,
  TEXTURING_KIND_STONE,
  TEXTURING_KIND_WEAVE,
  TEXTURING_KIND_VEINS,
  TEXTURING_NKINDS,
  TEXTURING_NODE_TONE = TEXTURING_NKINDS, /* grain, level; one input */
  TEXTURING_NODE_BLEND,                   /* amount; inputs A and B */
  TEXTURING_NODE_FILL,                    /* level; no inputs */
  TEXTURING_NODE_OUTPUT,                  /* values into the pixels; scheme */
  TEXTURING_NODE_COLOURS,                 /* scheme; no inputs */
  TEXTURING_NNODES
};

/* Tone's parameters, laid out as a node's params[]: per-pixel grain, the
 * noise amplitude it adds to a value, and a level added to every value. */
typedef struct texturing_tone
{
  int grain; /* amplitude, 0..TEXTURING_GRAIN_MAX */
  int level;
}
texturing_tone_t;

#define TEXTURING_GRAIN_MAX 128

/* What every node of one graph shares: the seed, and the 8-bit pixels the
 * Output node writes. */
typedef struct texturing_ctx
{
  uint32_t       seed;
  unsigned char *pixels; /* TEXTURING_NPIXELS */
}
texturing_ctx_t;

/* A node's state. Its output is values[]: unclamped, so a level added
 * downstream sees what a clamp would have lost. */
typedef struct texturing_node
{
  const texturing_ctx_t *ctx;
  int                    kind;
  int                    params[TEXTURING_NPARAMS];
  int                    values[TEXTURING_NPIXELS];
}
texturing_node_t;

/* A preset: a generator and its parameters, the tone and a colour scheme. */
typedef struct texturing_preset
{
  const char      *name;
  int              kind;
  int              params[TEXTURING_NPARAMS];
  texturing_tone_t tone;
  int              scheme;
}
texturing_preset_t;

extern const char              *texturing_node_names[TEXTURING_NNODES];
extern const int                texturing_defaults[TEXTURING_NNODES]
                                                  [TEXTURING_NPARAMS];
extern const texturing_preset_t texturing_presets[];
extern const int                texturing_npresets;

/* Add a node of kind with params (or the kind's defaults if NULL) at x,y. */
result_t texturing_node_add(nodegraph_t           *graph,
                            const texturing_ctx_t *ctx,
                            int                    kind,
                            const int             *params,
                            int                    x,
                            int                    y,
                            nodegraph_node_t      *node);

/* The node's state, as passed to texturing_node_add. */
texturing_node_t *texturing_node(nodegraph_t *graph, nodegraph_node_t node);

/* The Output's input for the colour scheme, from a Colours node. The pixels
 * do not depend on it: the task reads the scheme from the linked node. */
#define TEXTURING_OUTPUT_COLOURS 1

/* Empty graph and fill it with a generator of kind feeding a Tone feeding
 * the Output, and a Colours node of scheme feeding the Output too. */
result_t texturing_chain(nodegraph_t            *graph,
                         const texturing_ctx_t  *ctx,
                         int                     kind,
                         const int              *params,
                         const texturing_tone_t *tone,
                         int                     scheme);

/* Dirty every node, as after a reseed. */
void texturing_dirty_all(nodegraph_t *graph);

/* The texture straight from a kind and a tone, without a graph, as before
 * the graph: kept as the reference the graph's output is tested against. */
result_t texturing_reference(uint32_t                seed,
                             int                     kind,
                             const int              *params,
                             const texturing_tone_t *tone,
                             unsigned char          *pixels);

#endif /* TASKS_TEXTURING_GEN_H */
