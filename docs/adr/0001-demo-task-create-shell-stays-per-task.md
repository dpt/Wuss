# 1. Demo task create sequences stay per task

Status: accepted

## Context

The 33 demo tasks under `apps/wuss/tasks/` each open with a similar
sequence in `X_create`. Each task allocates a block, fills a
`wuss_task_desc_t`, calls `wuss_task_create`, sets autoclose, creates a window
and unwinds on failure.
An architecture review proposed deepening this into one shared module (for
example a `task_open` helper that owns the task block and the failure
cleanup).

## Decision

Keep the sequence in each task. Do not add a shared create shell.

## Reasons

The similarity is superficial. The differences are the part that matters:

- Cleanup before the delegate exists is task-specific. `ball.c` frees only the
  block. `chars.c` also frees its font array. `compositing.c` releases four
  bitmaps. A helper cannot own that cleanup, so the caller would keep it and
  the helper would hide nothing.
- Autoclose placement is deliberate. Most tasks set it straight after create,
  `dithering.c` sets it once the window exists and `effecting.c` omits it so
  that QUIT arrives at `wuss_destroy`.
- Window creation varies. Only 16 of the 33 use `task_window_create`. The rest
  call `wuss_window_create_at` with their own flags, backdrop, minimum size and
  placement, sometimes after loading resources that size the window.
- Failure after the window exists unwinds task-owned objects (saveas,
  gridview, dialogues) before `wuss_task_destroy(delegate)`.

The only code common to every task is filling a `wuss_task_desc_t` and calling
`wuss_task_create`, about four lines. A helper over that is shallow. Its
interface is nearly as large as its implementation. It would also push the
order of autoclose and `task->delegate` into 33 call sites.

The `X_handle` event switches are likewise not worth sharing. Tasks handle
different event sets, and the compiler checks each switch.

## Consequences

New tasks copy a nearby task's `X_create`. If a future change makes the
cleanup uniform (for example every task freeing its block from `X_destroy`
before the delegate exists), revisit this record.
