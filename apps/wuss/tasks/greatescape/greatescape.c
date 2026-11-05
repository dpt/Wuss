/* wuss/tasks/greatescape/greatescape.c -- The Great Escape task */

#if defined(WUSS_APP) && defined(WUSS_GREATESCAPE)

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "TheGreatEscape/TheGreatEscape.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/zxtask.h"

/* A window running the ZX Spectrum The Great Escape recreation (built from
 * WUSS_GREATESCAPE_DIR). Keys are the game's own, chosen from its menu; the
 * arrow keys and '.' drive the emulated Kempston joystick. Silent. The game
 * loops inside tge_menu and tge_main, which only return between frames or on
 * a quit; zxtask runs it on its own thread. */

static void *greatescape_game_create(zxspectrum_t *zx)
{
  return tge_create(zx);
}

static void greatescape_run(void *game, zxtask_t *task)
{
  int proceed;

  tge_setup(game);

  /* the menu returns > 0 to start the game, < 0 once sleep signals a quit */
  do
    proceed = tge_menu(game);
  while (proceed == 0);
  if (proceed < 0)
    return;

  tge_setup2(game);
  while (!zxtask_quitting(task))
    tge_main(game);
}

static void greatescape_destroy(void *game)
{
  tge_destroy(game);
}

static result_t greatescape_create(wuss_t *wuss, void **out)
{
  static const zxtask_ops_t ops =
  {
    "The Great Escape",
    "greatescape",
    Z80_CLOCK_48K, /* the game's timings are for a 48K Spectrum */
    greatescape_game_create,
    greatescape_run,
    NULL, /* sleep's quit unwinds the game by itself */
    greatescape_destroy
  };

  return zxtask_create(wuss, &ops, out);
}

result_t greatescape_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    "The Great Escape",
    "Escape the prison camp",
    greatescape_create
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP && WUSS_GREATESCAPE */
