/* wuss/core/test/hourglass-test.c -- wuss hourglass tests */

#include <stdio.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "wuss/wuss.h"

#include "wuss/test/all-tests.h"

#include "../../test/harness.h"

/* ----------------------------------------------------------------------- */

#define FAIL(what) \
  do { printf("  failed: %s\n", what); goto Failure; } while (0)

typedef struct
{
  int calls;
  int busy;
}
hourglass_log_t;

static void hourglass_handler(int busy, void *opaque)
{
  hourglass_log_t *log;

  log = opaque;
  log->calls++;
  log->busy = busy;
}

/* ----------------------------------------------------------------------- */

result_t wuss_hourglass_test(const char *resources)
{
  result_t        rc;
  wuss_fixture_t  fixture;
  wuss_t         *wuss;
  hourglass_log_t log = { 0, 0 };

  NOT_USED(resources);

  rc = wuss_fixture_open(&fixture, 64, 64);
  if (rc != result_OK)
    goto Failure;
  wuss = fixture.wuss;

  printf("test: no handler, and an unmatched off, are harmless\n");

  wuss_hourglass_on(wuss);
  wuss_hourglass_off(wuss);
  wuss_hourglass_off(wuss);

  printf("test: nested calls reach the handler once each way\n");

  wuss_set_hourglass_handler(wuss, hourglass_handler, &log);

  wuss_hourglass_on(wuss);
  if (log.calls != 1 || !log.busy)
    FAIL("first on");
  wuss_hourglass_on(wuss);
  wuss_hourglass_off(wuss);
  if (log.calls != 1 || !log.busy)
    FAIL("inner pair");
  wuss_hourglass_off(wuss);
  if (log.calls != 2 || log.busy)
    FAIL("last off");
  wuss_hourglass_off(wuss);
  if (log.calls != 2)
    FAIL("unmatched off");

  wuss_fixture_close(&fixture);

  return result_TEST_PASSED;


Failure:
  printf("wuss_hourglass_test: failed (rc=0x%X)\n", rc);
  wuss_fixture_close(&fixture);

  return result_TEST_FAILED;
}
