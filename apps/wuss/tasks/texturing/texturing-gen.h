/* wuss/tasks/texturing/texturing-gen.h -- texture generation as a node graph */

#ifndef TASKS_TEXTURING_GEN_H
#define TASKS_TEXTURING_GEN_H

#include <stdint.h>

#include "base/result.h"
#include "datastruct/nodegraph.h"
#include "framebuf/colour.h"
#include "framebuf/pixelfmt.h"

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
  TEXTURING_KIND_CHECKS,
  TEXTURING_NKINDS,
  TEXTURING_NODE_TONE = TEXTURING_NKINDS, /* grain, level; one input */
  TEXTURING_NODE_BLEND,                   /* amount; inputs A and B */
  TEXTURING_NODE_FILL,                    /* level; no inputs */
  TEXTURING_NODE_OUTPUT,                  /* an image into the pixels */
  TEXTURING_NODE_COLOURS,                 /* scheme; no inputs */
  TEXTURING_NODE_SEED,                    /* seed; no inputs */
  TEXTURING_NODE_COLOURISE,               /* values, scheme to an image */
  TEXTURING_NODE_EFFECTS,                 /* an image through effects */
  TEXTURING_NODE_PICTURE,                 /* an image from a file */
  TEXTURING_NNODES
};

/* Port types: a tile of unclamped values, a colour scheme, a seed and a
 * tile of rgba8888 pixels. */
#define TEXTURING_TYPE_VALUES 0
#define TEXTURING_TYPE_SCHEME 1
#define TEXTURING_TYPE_SEED   2
#define TEXTURING_TYPE_IMAGE  3
#define TEXTURING_NTYPES      4

/* Colourise's input for the colour scheme, from a Colours node. An unlinked
 * scheme input reads as the first scheme. */
#define TEXTURING_COLOURISE_SCHEME 1

/* The seed input of a generator, and of a Tone for its grain. An unlinked
 * seed input reads as zero. */
#define TEXTURING_GENERATOR_SEED 0
#define TEXTURING_TONE_SEED      1

/* Tone's parameters, laid out as a node's params[]: per-pixel grain, the
 * noise amplitude it adds to a value, and a level added to every value. */
typedef struct texturing_tone
{
  int grain; /* amplitude, 0..TEXTURING_GRAIN_MAX */
  int level;
}
texturing_tone_t;

#define TEXTURING_GRAIN_MAX 128

/* Run an Effects node's effects, kept in its client, over in into out. */
typedef result_t (texturing_effect_fn_t)(void                      *client,
                                         const pixelfmt_rgba8888_t *in,
                                         pixelfmt_rgba8888_t       *out);

/* Free an Effects node's client, as the node goes. */
typedef void (texturing_forget_fn_t)(void *client);

/* What every node of one graph shares: the pixels the Output node writes,
 * and the hooks behind Effects nodes. With effect NULL, an Effects node
 * passes its image through. */
typedef struct texturing_ctx
{
  pixelfmt_rgba8888_t   *pixels; /* TEXTURING_NPIXELS */
  texturing_effect_fn_t *effect;
  texturing_forget_fn_t *forget;
}
texturing_ctx_t;

/* A node's state. Its output is values[], unclamped, so a level added
 * downstream sees what a clamp would have lost, or for Colourise and
 * Effects an image. An Effects node's client is set by the task. */
typedef struct texturing_node
{
  const texturing_ctx_t *ctx;
  int                    kind;
  int                    params[TEXTURING_NPARAMS];
  void                  *client;
  union
  {
    int                 values[TEXTURING_NPIXELS];
    pixelfmt_rgba8888_t image[TEXTURING_NPIXELS];
  }
  out;
}
texturing_node_t;

/* A colour scheme, as 0xRRGGBB. A smooth scheme ramps through its first
 * three stops at values 0, 128 and 255. A stepped scheme maps each fifth of
 * the value range to one of its five stops exactly, giving the few close
 * tones of the RISC OS desktop textures; grain then speckles between
 * them. */
typedef struct texturing_scheme
{
  const char  *name;
  int          stepped;
  unsigned int stops[5];
}
texturing_scheme_t;

#define TEXTURING_NSCHEMES 23

/* The stepped schemes come first, then the smooth ones. */
#define TEXTURING_NSTEPPED 9

/* The first smooth scheme, which a new Colours node starts on. */
#define TEXTURING_SCHEME_SKY TEXTURING_NSTEPPED

extern const texturing_scheme_t texturing_schemes[TEXTURING_NSCHEMES];

/* Fill palette[] with scheme's colour for each value 0..255. */
void texturing_palette(int scheme, colour_t palette[256]);

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

/* Load the PNG or JPEG at path into a Picture node, resampled to fill the
 * tile and made opaque. Until a load succeeds the node gives flat grey. On
 * failure the node keeps what it had. The caller dirties the node. */
result_t texturing_picture_load(texturing_node_t *n, const char *path);

/* The node's state, as passed to texturing_node_add. */
texturing_node_t *texturing_node(nodegraph_t *graph, nodegraph_node_t node);

/* Empty graph and fill it with a generator of kind feeding a Tone feeding a
 * Colourise feeding the Output, a Seed node of seed feeding the generator
 * and the Tone, and a Colours node of scheme feeding the Colourise. */
result_t texturing_chain(nodegraph_t            *graph,
                         const texturing_ctx_t  *ctx,
                         uint32_t                seed,
                         int                     kind,
                         const int              *params,
                         const texturing_tone_t *tone,
                         int                     scheme);

/* Dirty every node, as after a parameter change. */
void texturing_dirty_all(nodegraph_t *graph);

/* The texture straight from a kind and a tone, without a graph, as before
 * the graph: kept as the reference the graph's output is tested against. */
result_t texturing_reference(uint32_t                seed,
                             int                     kind,
                             const int              *params,
                             const texturing_tone_t *tone,
                             unsigned char          *pixels);

#endif /* TASKS_TEXTURING_GEN_H */
