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
  rc = texturing_chain(graph, &ctx, kind, params, tone);
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

/* An unconnected Output gives flat mid-grey. */
static result_t texturinggen_unlinked(unsigned char *got)
{
  result_t         rc;
  texturing_ctx_t  ctx;
  nodegraph_t     *graph;
  nodegraph_node_t out;
  int              i;

  ctx.seed   = SEED;
  ctx.pixels = got;

  rc = nodegraph_create(&graph);
  if (rc != result_OK)
    return rc;

  memset(got, 0, TEXTURING_NPIXELS);
  rc = texturing_node_add(graph, &ctx, TEXTURING_NODE_OUTPUT, NULL, 0, 0,
                          &out);
  if (rc == result_OK)
    rc = nodegraph_evaluate(graph);
  nodegraph_destroy(graph);
  if (rc != result_OK)
    return rc;

  for (i = 0; i < TEXTURING_NPIXELS; i++)
    if (got[i] != 128)
    {
      printf("texturinggen: unlinked Output: pixel %d is %d\n", i, got[i]);
      return result_TEST_FAILED;
    }

  return result_OK;
}

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

  rc = texturinggen_unlinked(got);

exit:
  free(want);
  free(got);

  return rc == result_OK ? result_TEST_PASSED : rc;
}
