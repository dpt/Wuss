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

#include "../../../apps/wuss/tasks/texturing/texturing-gen.h"

#include "wuss/test/all-tests.h"

/* ----------------------------------------------------------------------- */

#define SEED 0x9E3779B9u

/* Build a chain of kind, evaluate it into got and compare it with the
 * reference generator's want. */
static result_t texturinggen_check(const char             *what,
                                   unsigned char          *got,
                                   unsigned char          *want,
                                   int                     kind,
                                   const int              *params,
                                   const texturing_tone_t *tone)
{
  result_t        rc;
  texturing_ctx_t ctx;
  nodegraph_t    *graph;

  ctx.seed   = SEED;
  ctx.pixels = got;

  rc = nodegraph_create(&graph);
  if (rc != result_OK)
    return rc;

  memset(got, 0xAA, TEXTURING_NPIXELS);
  rc = texturing_chain(graph, &ctx, kind, params, tone, 0);
  if (rc == result_OK)
    rc = nodegraph_evaluate(graph);
  nodegraph_destroy(graph);
  if (rc != result_OK)
    return rc;

  rc = texturing_reference(SEED, kind, params, tone, want);
  if (rc != result_OK)
    return rc;

  if (memcmp(got, want, TEXTURING_NPIXELS) != 0)
  {
    printf("texturinggen: %s: graph differs from reference\n", what);
    return result_TEST_FAILED;
  }

  return result_OK;
}

/* An Output fed by a Fill of level, or unconnected when level is NULL,
 * gives flat want. */
static result_t texturinggen_flat(unsigned char *got,
                                  const int     *level,
                                  int            want)
{
  result_t         rc;
  texturing_ctx_t  ctx;
  nodegraph_t     *graph;
  nodegraph_node_t out, fill;
  int              i;

  ctx.seed   = SEED;
  ctx.pixels = got;

  rc = nodegraph_create(&graph);
  if (rc != result_OK)
    return rc;

  memset(got, 0, TEXTURING_NPIXELS);
  rc = texturing_node_add(graph, &ctx, TEXTURING_NODE_OUTPUT, NULL, 0, 0,
                          &out);
  if (rc == result_OK && level != NULL)
  {
    rc = texturing_node_add(graph, &ctx, TEXTURING_NODE_FILL, level, 0, 0,
                            &fill);
    if (rc == result_OK)
      rc = nodegraph_link(graph, fill, out, 0);
  }
  if (rc == result_OK)
    rc = nodegraph_evaluate(graph);
  nodegraph_destroy(graph);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < TEXTURING_NPIXELS; i++)
    if (got[i] != want)
    {
      printf("texturinggen: flat Output: pixel %d is %d, not %d\n", i,
             got[i], want);
      return result_TEST_FAILED;
    }

  return result_OK;
}

static const int fill_low[TEXTURING_NPARAMS]  = { 40 };
static const int fill_high[TEXTURING_NPARAMS] = { 300 };

result_t texturinggen_test(const char *resources)
{
  result_t       rc;
  unsigned char *got;
  unsigned char *want;
  int            k;

  NOT_USED(resources);

  got  = malloc(TEXTURING_NPIXELS);
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

  rc = texturinggen_flat(got, NULL, 128);
  if (rc == result_OK)
    rc = texturinggen_flat(got, fill_low, 40);
  if (rc == result_OK)
    rc = texturinggen_flat(got, fill_high, 255); /* clamped */

exit:
  free(want);
  free(got);

  return rc == result_OK ? result_TEST_PASSED : rc;
}
