/* wuss/test/texturing-gen-test.c -- texture node graph tests */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "datastruct/nodegraph.h"
#include "framebuf/colour.h"

#include "../../../apps/wuss/tasks/texturing/texturing-gen.h"

#include "wuss/test/all-tests.h"

/* ----------------------------------------------------------------------- */

#define SEED 0x9E3779B9u

/* The scheme every test colours by. */
#define SCHEME 0

/* Return the first pixel of got that differs from want's values coloured by
 * SCHEME, or -1 if none. */
static int texturinggen_differs(const pixelfmt_rgba8888_t *got,
                                const unsigned char       *want)
{
  colour_t palette[256];
  int      i;

  texturing_palette(SCHEME, palette);
  for (i = 0; i < TEXTURING_NPIXELS; i++)
    if (got[i] != palette[want[i]].primary)
      return i;

  return -1;
}

/* Build a chain of kind, evaluate it into got and compare it with the
 * reference generator's want. */
static result_t texturinggen_check(const char             *what,
                                   pixelfmt_rgba8888_t    *got,
                                   unsigned char          *want,
                                   int                     kind,
                                   const int              *params,
                                   const texturing_tone_t *tone)
{
  result_t        rc;
  texturing_ctx_t ctx;
  nodegraph_t    *graph;

  memset(&ctx, 0, sizeof(ctx));
  ctx.pixels = got;

  rc = nodegraph_create(&graph);
  if (rc != result_OK)
    return rc;

  memset(got, 0xAA, TEXTURING_NPIXELS * sizeof(*got));
  rc = texturing_chain(graph, &ctx, SEED, kind, params, tone, SCHEME);
  if (rc == result_OK)
    rc = nodegraph_evaluate(graph);
  nodegraph_destroy(graph);
  if (rc != result_OK)
    return rc;

  rc = texturing_reference(SEED, kind, params, tone, want);
  if (rc != result_OK)
    return rc;

  if (texturinggen_differs(got, want) >= 0)
  {
    printf("texturinggen: %s: graph differs from reference\n", what);
    return result_TEST_FAILED;
  }

  return result_OK;
}

/* An Output fed by a Colourise of a Fill of level, or of nothing when level
 * is NULL, through an Effects node with no hook, gives flat want. */
static result_t texturinggen_flat(pixelfmt_rgba8888_t *got,
                                  unsigned char       *want,
                                  const int           *level,
                                  int                  value)
{
  result_t         rc;
  texturing_ctx_t  ctx;
  nodegraph_t     *graph;
  nodegraph_node_t out, fx, cz, fill;
  int              i;

  memset(&ctx, 0, sizeof(ctx));
  ctx.pixels = got;

  rc = nodegraph_create(&graph);
  if (rc != result_OK)
    return rc;

  memset(got, 0, TEXTURING_NPIXELS * sizeof(*got));
  rc = texturing_node_add(graph, &ctx, TEXTURING_NODE_OUTPUT, NULL, 0, 0,
                          &out);
  if (rc == result_OK)
    rc = texturing_node_add(graph, &ctx, TEXTURING_NODE_EFFECTS, NULL, 0, 0,
                            &fx);
  if (rc == result_OK)
    rc = texturing_node_add(graph, &ctx, TEXTURING_NODE_COLOURISE, NULL, 0,
                            0, &cz);
  if (rc == result_OK)
    rc = nodegraph_link(graph, cz, fx, 0);
  if (rc == result_OK)
    rc = nodegraph_link(graph, fx, out, 0);
  if (rc == result_OK && level != NULL)
  {
    rc = texturing_node_add(graph, &ctx, TEXTURING_NODE_FILL, level, 0, 0,
                            &fill);
    if (rc == result_OK)
      rc = nodegraph_link(graph, fill, cz, 0);
  }
  if (rc == result_OK)
    rc = nodegraph_evaluate(graph);
  nodegraph_destroy(graph);
  if (rc != result_OK)
    return rc;

  memset(want, value, TEXTURING_NPIXELS);
  i = texturinggen_differs(got, want);
  if (i >= 0)
  {
    printf("texturinggen: flat Output: pixel %d is not value %d\n", i,
           value);
    return result_TEST_FAILED;
  }

  return result_OK;
}

static const int fill_low[TEXTURING_NPARAMS]  = { 40 };
static const int fill_high[TEXTURING_NPARAMS] = { 300 };

result_t texturinggen_test(const char *resources)
{
  result_t             rc;
  pixelfmt_rgba8888_t *got;
  unsigned char       *want;
  int                  k;

  NOT_USED(resources);

  got  = malloc(TEXTURING_NPIXELS * sizeof(*got));
  want = malloc(TEXTURING_NPIXELS);
  if (got == NULL || want == NULL)
  {
    rc = result_OOM;
    goto exit;
  }

  for (k = 0; k < TEXTURING_NKINDS; k++)
  {
    rc = texturinggen_check(texturing_node_names[k], got, want, k,
                            texturing_defaults[k],
                            (const texturing_tone_t *)
                            texturing_defaults[TEXTURING_NODE_TONE]);
    if (rc != result_OK)
      goto exit;
  }

  for (k = 0; k < texturing_npresets; k++)
  {
    rc = texturinggen_check(texturing_presets[k].name, got, want,
                            texturing_presets[k].kind,
                            texturing_presets[k].params,
                            &texturing_presets[k].tone);
    if (rc != result_OK)
      goto exit;
  }

  rc = texturinggen_flat(got, want, NULL, 128);
  if (rc == result_OK)
    rc = texturinggen_flat(got, want, fill_low, 40);
  if (rc == result_OK)
    rc = texturinggen_flat(got, want, fill_high, 255); /* clamped */

exit:
  free(want);
  free(got);

  return rc == result_OK ? result_TEST_PASSED : rc;
}
