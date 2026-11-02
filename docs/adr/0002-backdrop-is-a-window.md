# 2. The backdrop is a window owned by a task

Status: accepted

## Context

The desktop backdrop is paint only: a `wuss_backdrop_t` fill and image. A
click on empty desktop is discarded in `libraries/wuss/core/mouse-click.c`
(`if (win == NULL) return`). We want a backdrop with a menu, pinned files, a
look that can change at run time and room for notes (see
[backdrop.md](../windowing/backdrop.md)).

Two models were considered:

- **Core desktop hook.** A task registers as the desktop owner. Clicks,
  menus and drops on empty desktop go to it with `window == NULL`, as icon
  bar clicks already do.
- **Backdrop window.** A task creates one full-screen window that stays at
  the bottom of the back stack. Everything reaches it as it reaches any
  window.

## Decision

The backdrop is a window. Core gains one flag, `wuss_WINDOW_BACKDROP`, which
pins the window to the screen, puts it at the bottom of `wuss_STACK_BACK`
and keeps it there. A Pinboard task owns it. Notes are a separate Post-Its
task with ordinary windows.

## Reasons

- A window already gets everything the backdrop needs: hit-testing, menus,
  `wuss_EVENT_DRAG_END`, recorded DataLoad sends addressed by window, redraw
  with clipping and icons. A hook would need a window-less copy of each.
- The icon bar is already the one window-less special case in
  `mouse-click.c`. A second one would add branches to the click, drag,
  message and redraw paths.
- The z-order stacks (`wuss_stack_t`) and `wuss_WINDOW_PINNED` already do
  most of the work. The new flag only joins them and keeps the window at the
  bottom.
- With no backdrop window, wuss behaves as it does now. The feature costs
  nothing for a client that does not use it.

## Consequences

- Restacking and the window cycling in `core/task/arrange.c` must skip the
  backdrop window.
- Only one backdrop window may exist. Creating a second one fails.
- Post-it notes do not depend on the Pinboard and can be built first or
  separately. Their "Pin to backdrop" item reaches the Pinboard only through
  the `wuss_MESSAGE_PINBOARD_PIN` broadcast, which bounces when no Pinboard
  is running.
