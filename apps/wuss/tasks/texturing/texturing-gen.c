/* wuss/tasks/texturing/texturing-gen.c -- texture generation as a node graph */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "datastruct/nodegraph.h"
#include "utils/rng.h"

#include "texturing-gen.h"

/* Lattice cells across the tile in the coarsest octave of the noise behind
 * Stone and Weave's broad patches (log2). */
#define TEXTURING_LOG2_BASE 2

/* Port types: a tile of unclamped values, and a colour scheme. */
#define TEXTURING_TYPE_VALUES 0
#define TEXTURING_TYPE_SCHEME 1

const char *texturing_node_names[TEXTURING_NNODES] =
{
  "Clouds", "Marble", "Wood", "Cells", "Stone", "Weave", "Veins",
  "Tone", "Blend", "Fill", "Output", "Colours"
};

const int texturing_defaults[TEXTURING_NNODES][TEXTURING_NPARAMS] =
{
  { 2, 100,   7,   0      }, /* Clouds */
  { 2,   4,   4,   0      }, /* Marble */
  { 2,  16,   4,   7      }, /* Wood */
  { 3, 100, 100,   0      }, /* Cells */
  { 5, 100,  25, 315, 100 }, /* Stone */
  { 3, 100,   4,   0      }, /* Weave */
  { 6,  30,   0,  60      }, /* Veins */
  { 48,  0                }, /* Tone: grain, level */
  { 50                    }, /* Blend: amount, % of B */
  { 128                   }, /* Fill: level */
  { 0                     }, /* Output */
  { 0                     }  /* Colours: scheme */
};

/* Presets after the RISC OS desktop textures: a kind and its parameters, the
 * tone and a stepped scheme. Reseeding keeps the look. */
const texturing_preset_t texturing_presets[] =
{
  { "Plaster",  TEXTURING_KIND_VEINS,  { 6,  30,  0,  60      }, { 20,  0 }, 6 },
  { "Pebbled",  TEXTURING_KIND_STONE,  { 6, 100,  0, 315, 100 }, { 20,  0 }, 6 },
  { "Streaked", TEXTURING_KIND_VEINS,  { 4,  14,  2,  70      }, { 24, -4 }, 6 },
  { "Granite",  TEXTURING_KIND_CLOUDS, { 5,  60,  3,   0      }, { 32,  0 }, 6 },
  { "Veined",   TEXTURING_KIND_VEINS,  { 4,   3,  0,   0      }, { 48, 30 }, 7 },
  { "Rough",    TEXTURING_KIND_STONE,  { 6, 250, 25, 315,   0 }, { 40, 16 }, 7 }
};

const int texturing_npresets = (int) NELEMS(texturing_presets);

/* Weave patterns: which thread lies on top in each thread-wide square. */
enum
{
  TEXTURING_PATTERN_PLAIN,  /* alternate every square: a checkerboard */
  TEXTURING_PATTERN_TWILL,  /* over two, under two, stepping each row */
  TEXTURING_PATTERN_BASKET  /* plain, but in pairs of threads */
};

/* Light directions, as Stone's Light / 45: the step toward the light. */
static const signed char texturing_light[8][2] =
{
  {  0, -1 }, {  1, -1 }, {  1,  0 }, {  1,  1 },
  {  0,  1 }, { -1,  1 }, { -1,  0 }, { -1, -1 }
};

/* ----------------------------------------------------------------------- */

/* Lattice value 0..255 at cell (cx,cy) of a lattice 2^log2nx cells across
 * and 2^log2ny down that wraps. */
static int texturing_lattice(uint32_t seed,
                             int      cx,
                             int      cy,
                             int      log2nx,
                             int      log2ny)
{
  int maskx, masky;

  maskx = (1 << log2nx) - 1;
  masky = (1 << log2ny) - 1;

  return (int) (rng_hash32(seed, ((cy & masky) << log2nx) | (cx & maskx)) &
                255);
}

/* Smoothstep over a 0..256 fraction. */
static int texturing_ease(int f)
{
  return f * f * (768 - 2 * f) >> 16;
}

/* Bilinear smooth value noise, 0..255, on a lattice 2^log2nx cells across
 * the tile and 2^log2ny down. */
static int texturing_noise_xy(uint32_t seed,
                              int      x,
                              int      y,
                              int      log2nx,
                              int      log2ny)
{
  int log2cx, log2cy, cx, cy, fx, fy, a, b, c, d, top, bot;

  log2cx = TEXTURING_LOG2_SIZE - log2nx; /* log2 of the cell size in pixels */
  log2cy = TEXTURING_LOG2_SIZE - log2ny;
  cx     = x >> log2cx;
  cy     = y >> log2cy;
  fx     = texturing_ease(((x & ((1 << log2cx) - 1)) << 8) >> log2cx);
  fy     = texturing_ease(((y & ((1 << log2cy) - 1)) << 8) >> log2cy);

  a = texturing_lattice(seed, cx,     cy,     log2nx, log2ny);
  b = texturing_lattice(seed, cx + 1, cy,     log2nx, log2ny);
  c = texturing_lattice(seed, cx,     cy + 1, log2nx, log2ny);
  d = texturing_lattice(seed, cx + 1, cy + 1, log2nx, log2ny);

  top = a + ((b - a) * fx >> 8);
  bot = c + ((d - c) * fx >> 8);

  return top + ((bot - top) * fy >> 8);
}

/* Bilinear smooth value noise, 0..255, on a 2^log2n lattice across the
 * tile. */
static int texturing_noise(uint32_t seed, int x, int y, int log2n)
{
  return texturing_noise_xy(seed, x, y, log2n, log2n);
}

/* Fractal sum of up to octaves noise octaves, 0..255, starting from a
 * 2^log2base lattice: each octave doubles the lattice and halves the
 * weight, stopping at one-pixel cells. The last octave takes the weight
 * left over, so the weights always sum to 256. */
static int texturing_fbm(uint32_t seed,
                         int      x,
                         int      y,
                         int      log2base,
                         int      octaves)
{
  int sum, log2n, last, weight;

  sum    = 0;
  last   = MIN(log2base + octaves - 1, TEXTURING_LOG2_SIZE);
  weight = 128;
  for (log2n = log2base; log2n <= last; log2n++)
  {
    sum    += texturing_noise(seed + (uint32_t) log2n, x, y, log2n) *
              (log2n == last ? weight * 2 : weight);
    weight >>= 1;
  }

  return sum >> 8;
}

/* The broad light and dark patches under Stone and Weave, -128..127. */
static int texturing_patches(uint32_t seed, int x, int y)
{
  return texturing_fbm(seed ^ 0x5A5Au, x, y, TEXTURING_LOG2_BASE,
                       TEXTURING_LOG2_SIZE) - 128;
}

/* Scale v about 128 by an 8.8 multiplier. */
static int texturing_contrast(int v, int mul)
{
  return 128 + ((v - 128) * mul >> 8);
}

/* Fold a value into a 0..255 triangle wave of period 512. */
static int texturing_fold(int v)
{
  v &= 511;
  return v < 256 ? v : 511 - v;
}

/* Squared distance from (x,y) to the nearest feature point, one per cell
 * of a wrapping 2^log2n grid, scaled to 0..255. Each point strays from its
 * cell's centre by the 8.8 jitter multiplier. The 8.8 edges multiplier
 * blends toward the gap between the nearest and second nearest points,
 * which is dark along the borders between cells. */
static int texturing_cells(uint32_t seed,
                           int      x,
                           int      y,
                           int      log2n,
                           int      jitter,
                           int      edges)
{
  int log2c, c, mask, shift, cx, cy, best, second, i, j;

  log2c  = TEXTURING_LOG2_SIZE - log2n; /* log2 of the cell size in pixels */
  c      = 1 << log2c;
  mask   = (1 << log2n) - 1;
  shift  = 2 * log2c - 8; /* takes a cell size squared to 256 */
  cx     = x >> log2c;
  cy     = y >> log2c;
  best   = INT_MAX;
  second = INT_MAX;

  for (j = -1; j <= 1; j++)
    for (i = -1; i <= 1; i++)
    {
      uint32_t h;
      int      px, py, dx, dy, d;

      /* point position in unwrapped pixels, hashed from the wrapped cell */
      h  = rng_hash32(seed, (((cy + j) & mask) << log2n) | ((cx + i) & mask));
      px = (int) (h & (unsigned) (c - 1)) - c / 2;
      py = (int) ((h >> 16) & (unsigned) (c - 1)) - c / 2;
      px = (cx + i) * c + c / 2 + (px * jitter >> 8);
      py = (cy + j) * c + c / 2 + (py * jitter >> 8);
      dx = px - x;
      dy = py - y;
      d  = dx * dx + dy * dy;
      if (d < best)
      {
        second = best;
        best   = d;
      }
      else if (d < second)
      {
        second = d;
      }
    }

  /* ponytail: nearest points rarely sit more than a cell away, so the
   * clamps seldom bite */
  second -= best;
  best    = shift >= 0 ? best >> shift : best << -shift;
  second  = shift >= 0 ? second >> shift : second << -shift;
  best    = MIN(best, 255);
  second  = MIN(second, 255);
  return best + ((second - best) * edges >> 8);
}

/* Rough stone: an embossed noise height field on a 2^log2n lattice and the
 * two finer ones, the slope taken between the neighbours toward and away
 * from the light (wrapped so it still tiles) and scaled by an 8.8 relief
 * multiplier, over broad light and dark patches scaled by another. light
 * indexes texturing_light. Centred on 128. */
static int texturing_rough(uint32_t seed, int x, int y, int log2n)
{
  return (texturing_noise(seed,     x, y, log2n) * 2 +
          texturing_noise(seed + 1, x, y, log2n + 1) +
          texturing_noise(seed + 2, x, y, log2n + 2)) >> 2;
}

static int texturing_stone(uint32_t seed,
                           int      x,
                           int      y,
                           int      log2n,
                           int      relief,
                           int      patches,
                           int      light,
                           int      highlights)
{
  int m, dx, dy, slope;

  m     = TEXTURING_SIZE - 1;
  dx    = texturing_light[light][0];
  dy    = texturing_light[light][1];
  slope = texturing_rough(seed, (x + dx) & m, (y + dy) & m, log2n) -
          texturing_rough(seed, (x - dx) & m, (y - dy) & m, log2n);
  if (slope > 0)
    slope = slope * highlights >> 8;

  return 128 + (slope * relief >> 8) +
         (texturing_patches(seed, x, y) * patches >> 8);
}

/* Height of a weave of 2^log2t-pixel threads, crossing over and under in
 * the given TEXTURING_PATTERN_*. Each visible thread is highest along its
 * middle and drops toward its edges and toward the ends of each square,
 * where it may dive under its neighbour. The coordinates wobble with
 * noise, up to wobble pixels, so the threads wander like yarn; the wobble
 * repeats with the tile, so the weave still tiles. */
static int texturing_weave_height(uint32_t seed,
                                  int      x,
                                  int      y,
                                  int      log2t,
                                  int      wobble,
                                  int      pattern)
{
  int cx, cy, t, over, across, along, a, b;

  x += (texturing_noise(seed + 7, x, y, 4) - 128) * wobble >> 7;
  y += (texturing_noise(seed + 8, x, y, 4) - 128) * wobble >> 7;
  x &= TEXTURING_SIZE - 1;
  y &= TEXTURING_SIZE - 1;

  cx = x >> log2t;
  cy = y >> log2t;
  switch (pattern) /* over is nonzero where the weft is on top */
  {
  default:
  case TEXTURING_PATTERN_PLAIN:
    over = (cx + cy) & 1;
    break;
  case TEXTURING_PATTERN_TWILL:
    over = ((cx - cy) & 3) < 2;
    break;
  case TEXTURING_PATTERN_BASKET:
    over = ((cx >> 1) + (cy >> 1)) & 1;
    break;
  }

  t      = 1 << log2t;
  across = over ? (y & (t - 1)) : (x & (t - 1));
  along  = over ? (x & (t - 1)) : (y & (t - 1));
  a      = across < t / 2 ? across : t - 1 - across; /* 0 at an edge */
  b      = along  < t / 2 ? along  : t - 1 - along;  /* 0 where it dips */

  return a * 24 + b * 12 + (texturing_rough(seed, x, y, 5) >> 2);
}

/* Woven fabric, embossed from the top left as Stone is. Centred on 128. */
static int texturing_weave(uint32_t seed,
                           int      x,
                           int      y,
                           int      log2t,
                           int      relief,
                           int      wobble,
                           int      pattern)
{
  int m, slope;

  m     = TEXTURING_SIZE - 1;
  slope = texturing_weave_height(seed, (x - 1) & m, (y - 1) & m, log2t,
                                 wobble, pattern) -
          texturing_weave_height(seed, (x + 1) & m, (y + 1) & m, log2t,
                                 wobble, pattern);

  return 128 + (slope * relief >> 8) + texturing_patches(seed, x, y) / 4;
}

/* Thin light veins, a step brighter, where a noise on a 2^log2n lattice
 * comes within width of its middle, over a fine dark mottling scaled by an
 * 8.8 multiplier. A stretch turns the lattice to the diagonals and makes it
 * 2^stretch times finer across them than along, so the veins run that way;
 * (x+y, x-y) still wraps with the tile. Centred a little below 128, as the
 * veins brighten. */
static int texturing_veins(uint32_t seed,
                           int      x,
                           int      y,
                           int      log2n,
                           int      width,
                           int      stretch,
                           int      dark)
{
  int m, u, w, n;

  m = TEXTURING_SIZE - 1;
  u = stretch ? (x + y) & m : x;
  w = stretch ? (x - y) & m : y;
  n = texturing_noise_xy(seed, u, w, log2n,
                         MIN(log2n + stretch, TEXTURING_LOG2_SIZE));

  return 112 + (abs(n - 128) < width ? 55 : 0) +
         ((texturing_fbm(seed ^ 0x77u, x, y, 5, 3) - 128) * dark >> 8);
}

/* Fill out[] with a generator kind's unclamped values. */
static void texturing_fill(uint32_t   seed,
                           int        kind,
                           const int *param,
                           int       *out)
{
  int mul, mul2, mul3;
  int light;
  int x, y, v;

  mul   = param[1] * 256 / 100; /* percentages as 8.8 multipliers */
  mul2  = param[2] * 256 / 100;
  mul3  = param[3] * 256 / 100;
  light = CLAMP(param[3] / 45, 0, 7); /* Stone's Light row */
  for (y = 0; y < TEXTURING_SIZE; y++)
    for (x = 0; x < TEXTURING_SIZE; x++)
    {
      switch (kind)
      {
      default:
      case TEXTURING_KIND_CLOUDS:
      {
        int wx, wy;

        /* warp: push the lookup about by a coarse noise */
        wx = (x + ((texturing_noise(seed + 11, x, y, 3) - 128) *
                   param[3] >> 7)) & (TEXTURING_SIZE - 1);
        wy = (y + ((texturing_noise(seed + 12, x, y, 3) - 128) *
                   param[3] >> 7)) & (TEXTURING_SIZE - 1);
        v  = texturing_contrast(texturing_fbm(seed, wx, wy, param[0],
                                              param[2]), mul);
        break;
      }

      case TEXTURING_KIND_MARBLE:
        /* veins across the tile, slanted, bent by turbulence */
        v = texturing_fold((x * param[1] + y * param[3]) *
                           (512 / TEXTURING_SIZE) +
                           texturing_fbm(seed, x, y, param[0],
                                         TEXTURING_LOG2_SIZE) * param[2]);
        break;

      case TEXTURING_KIND_WOOD:
        /* noise sliced into rings, roughened by a fine grain */
        v = texturing_fold(texturing_fbm(seed, x, y, param[0], param[3]) *
                           param[1] +
                           (texturing_noise(seed ^ 0x55u, x, y, 6) *
                            param[2] >> 4));
        break;

      case TEXTURING_KIND_CELLS:
        v = texturing_contrast(texturing_cells(seed, x, y, param[0], mul2,
                                               mul3), mul);
        break;

      case TEXTURING_KIND_STONE:
        v = texturing_stone(seed, x, y, param[0], mul, mul2, light,
                            param[4] * 256 / 100);
        break;

      case TEXTURING_KIND_WEAVE:
        v = texturing_weave(seed, x, y, param[0], mul, param[2], param[3]);
        break;

      case TEXTURING_KIND_VEINS:
        v = texturing_veins(seed, x, y, param[0], param[1], param[2], mul3);
        break;
      }

      *out++ = v;
    }
}

/* Add the tone to in[] (NULL for a flat 128) into out[]: the level, then
 * per-pixel grain keyed by position so it tiles too. */
static void texturing_tone(uint32_t                seed,
                           const texturing_tone_t *tone,
                           const int              *in,
                           int                    *out)
{
  int i, v;

  for (i = 0; i < TEXTURING_NPIXELS; i++)
  {
    v = (in ? in[i] : 128) + tone->level;
    if (tone->grain)
      v += ((int) (rng_hash32(seed ^ 0xC3A5u, (uint32_t) i) & 255) - 128) *
           tone->grain >> 7;
    out[i] = v;
  }
}

static void texturing_clamp(const int *in, unsigned char *pixels)
{
  int i;

  for (i = 0; i < TEXTURING_NPIXELS; i++)
    pixels[i] = (unsigned char) CLAMP(in ? in[i] : 128, 0, 255);
}

result_t texturing_reference(uint32_t                seed,
                             int                     kind,
                             const int              *params,
                             const texturing_tone_t *tone,
                             unsigned char          *pixels)
{
  int *values;

  values = malloc(TEXTURING_NPIXELS * sizeof(*values));
  if (values == NULL)
    return result_OOM;

  texturing_fill(seed, kind, params, values);
  texturing_tone(seed, tone, values, values);
  texturing_clamp(values, pixels);
  free(values);

  return result_OK;
}

/* ----------------------------------------------------------------------- */

/* nodegraph_evaluate_t for every kind */
static result_t texturing_evaluate(void       *state,
                                   void *const inputs[],
                                   void      **output)
{
  texturing_node_t *n;
  const int        *a, *b;
  int               mul, i, va;

  n = state;
  a = inputs[0];
  b = inputs[1];

  switch (n->kind)
  {
  case TEXTURING_NODE_TONE:
    texturing_tone(n->ctx->seed, (const texturing_tone_t *) n->params, a,
                   n->values);
    break;

  case TEXTURING_NODE_BLEND:
    mul = n->params[0] * 256 / 100;
    for (i = 0; i < TEXTURING_NPIXELS; i++)
    {
      va           = a ? a[i] : 128;
      n->values[i] = va + (((b ? b[i] : 128) - va) * mul >> 8);
    }
    break;

  case TEXTURING_NODE_FILL:
    for (i = 0; i < TEXTURING_NPIXELS; i++)
      n->values[i] = n->params[0];
    break;

  case TEXTURING_NODE_OUTPUT:
    texturing_clamp(a, n->ctx->pixels);
    break;

  case TEXTURING_NODE_COLOURS:
    *output = n->params; /* the task reads the scheme, not this */
    return result_OK;

  default:
    texturing_fill(n->ctx->seed, n->kind, n->params, n->values);
    break;
  }

  *output = n->values;
  return result_OK;
}

static void texturing_destroy(void *state)
{
  free(state);
}

#define TEXTURING_GENERATOR(name) \
  { name, 0, { 0 }, TEXTURING_TYPE_VALUES, texturing_evaluate, \
    texturing_destroy }

static const nodegraph_class_t texturing_classes[TEXTURING_NNODES] =
{
  TEXTURING_GENERATOR("Clouds"),
  TEXTURING_GENERATOR("Marble"),
  TEXTURING_GENERATOR("Wood"),
  TEXTURING_GENERATOR("Cells"),
  TEXTURING_GENERATOR("Stone"),
  TEXTURING_GENERATOR("Weave"),
  TEXTURING_GENERATOR("Veins"),
  { "Tone", 1, { TEXTURING_TYPE_VALUES }, TEXTURING_TYPE_VALUES,
    texturing_evaluate, texturing_destroy },
  { "Blend", 2, { TEXTURING_TYPE_VALUES, TEXTURING_TYPE_VALUES },
    TEXTURING_TYPE_VALUES, texturing_evaluate, texturing_destroy },
  TEXTURING_GENERATOR("Fill"),
  { "Output", 2, { TEXTURING_TYPE_VALUES, TEXTURING_TYPE_SCHEME },
    nodegraph_NO_OUTPUT, texturing_evaluate, texturing_destroy },
  { "Colours", 0, { 0 }, TEXTURING_TYPE_SCHEME, texturing_evaluate,
    texturing_destroy }
};

result_t texturing_node_add(nodegraph_t           *graph,
                            const texturing_ctx_t *ctx,
                            int                    kind,
                            const int             *params,
                            int                    x,
                            int                    y,
                            nodegraph_node_t      *node)
{
  result_t          rc;
  texturing_node_t *n;

  n = malloc(sizeof(*n));
  if (n == NULL)
    return result_OOM;

  n->ctx  = ctx;
  n->kind = kind;
  memcpy(n->params, params ? params : texturing_defaults[kind],
         sizeof(n->params));

  rc = nodegraph_add(graph, &texturing_classes[kind], n, x, y, node);
  if (rc != result_OK)
    free(n);

  return rc;
}

texturing_node_t *texturing_node(nodegraph_t *graph, nodegraph_node_t node)
{
  return nodegraph_state(graph, node);
}

result_t texturing_chain(nodegraph_t            *graph,
                         const texturing_ctx_t  *ctx,
                         int                     kind,
                         const int              *params,
                         const texturing_tone_t *tone,
                         int                     scheme)
{
  result_t         rc;
  nodegraph_node_t n, gen, tn, out, col;
  int              tparams[TEXTURING_NPARAMS];
  int              cparams[TEXTURING_NPARAMS];

  while ((n = nodegraph_next(graph, nodegraph_NONE)) != nodegraph_NONE)
    (void) nodegraph_remove(graph, n);

  memset(tparams, 0, sizeof(tparams));
  memcpy(tparams, tone, sizeof(*tone));
  memset(cparams, 0, sizeof(cparams));
  cparams[0] = scheme;

  /* a row with room between nodes for the links to show */
  rc = texturing_node_add(graph, ctx, kind, params, 24, 32, &gen);
  if (rc == result_OK)
    rc = texturing_node_add(graph, ctx, TEXTURING_NODE_TONE, tparams, 184,
                            32, &tn);
  if (rc == result_OK)
    rc = texturing_node_add(graph, ctx, TEXTURING_NODE_OUTPUT, NULL, 344, 32,
                            &out);
  if (rc == result_OK)
    rc = texturing_node_add(graph, ctx, TEXTURING_NODE_COLOURS, cparams, 184,
                            120, &col);
  if (rc == result_OK)
    rc = nodegraph_link(graph, gen, tn, 0);
  if (rc == result_OK)
    rc = nodegraph_link(graph, tn, out, 0);
  if (rc == result_OK)
    rc = nodegraph_link(graph, col, out, TEXTURING_OUTPUT_COLOURS);

  return rc;
}

void texturing_dirty_all(nodegraph_t *graph)
{
  nodegraph_node_t n;

  for (n = nodegraph_next(graph, nodegraph_NONE); n != nodegraph_NONE;
       n = nodegraph_next(graph, n))
    nodegraph_dirty(graph, n);
}
