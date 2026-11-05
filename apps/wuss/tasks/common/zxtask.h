/* wuss/tasks/common/zxtask.h -- shared host for ZX Spectrum game tasks */

#ifndef TASKS_ZXTASK_H
#define TASKS_ZXTASK_H

#if defined(WUSS_APP) && (defined(WUSS_CHASEHQ) || defined(WUSS_GREATESCAPE))

#include "base/result.h"
#include "wuss/wuss.h"

#include "ZXSpectrum/Spectrum.h"

/* The wuss side of a ZX Spectrum game port: a virtual Spectrum, a window
 * with a border in the game's border colour, a menu with Speed (pause and
 * rate), Monochromatic and Double size, real-time pacing, the arrows and '.'
 * as a Kempston joystick, Shift as CAPS SHIFT and a thread for the game
 * loop. Each port supplies a zxtask_ops_t. */

typedef struct zxtask zxtask_t;

typedef struct zxtask_ops
{
  const char *title;     /* window and menu name */
  const char *name;      /* wuss task name */
  double      clock_hz;  /* T-states per second, for pacing */

  /* make the game on zx. returns NULL on failure */
  void *(*create)(zxspectrum_t *zx);

  /* game thread: run the game until it ends or a sleep reports a quit */
  void (*run)(void *game, zxtask_t *task);

  /* optional: nudge the game thread out of its loop, after the quit flag is
   * set */
  void (*stop)(void *game);

  void (*destroy)(void *game);
}
zxtask_ops_t;

/* Open a task running a game. Suits a taskapp_create_fn_t. */
result_t zxtask_create(wuss_t *wuss, const zxtask_ops_t *ops, void **out);

/* returns non-zero once the task is quitting */
int zxtask_quitting(zxtask_t *task);

#endif /* WUSS_APP && (WUSS_CHASEHQ || WUSS_GREATESCAPE) */

#endif /* TASKS_ZXTASK_H */
