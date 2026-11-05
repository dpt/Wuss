/* wuss/tasks/chasehq/chasehq.c -- Chase H.Q. task */

#if defined(WUSS_APP) && defined(WUSS_CHASEHQ)

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "ChaseHQ/ChaseHQ.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/zxtask.h"

/* A window running the ZX Spectrum Chase H.Q. port (built from
 * WUSS_CHASEHQ_DIR). Keys are the game's own (K/L steer, A/Z accelerate/brake,
 * N gear, Space turbo); the arrow keys and '.' drive the emulated Kempston
 * joystick. Silent. The game blocks inside chq_start for its whole life;
 * zxtask runs it on its own thread. */

static void *chasehq_game_create(zxspectrum_t *zx)
{
  return chq_create(zx);
}

static void chasehq_run(void *game, zxtask_t *task)
{
  NOT_USED(task);

  chq_start(game, 1 /* 128K */);
}

static void chasehq_stop(void *game)
{
  chq_stop(game);
}

static void chasehq_destroy(void *game)
{
  chq_destroy(game);
}

static result_t chasehq_create(wuss_t *wuss, void **out)
{
  static const zxtask_ops_t ops =
  {
    "Chase H.Q.",
    "chasehq",
    Z80_CLOCK_128K,
    chasehq_game_create,
    chasehq_run,
    chasehq_stop,
    chasehq_destroy
  };

  return zxtask_create(wuss, &ops, out);
}

result_t chasehq_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    "Chase H.Q.",
    "Chase and smash bad guys",
    chasehq_create
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP && WUSS_CHASEHQ */
