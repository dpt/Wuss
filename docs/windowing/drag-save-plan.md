# Drag and drop saving

Design record for RISC OS-style drag and drop saving in wuss. The user drags
the file icon of a Save As dialogue onto a Filer directory window. The code
implements the design below.

## Scope

- v1 is **save only**: drag the Save As icon onto a Filer directory window.
- Deferred, but the protocol leaves room for them: load from the Filer,
  task-to-task transfer and RAM transfer. All three have since landed.
- wuss messaging is internal. It has no bridge to the real RISC OS Wimp. On
  RISC OS the whole of wuss runs inside one Wimp task.

## Where things live

| Piece | Location |
|---|---|
| Messaging, drag session, time, double-click | `libraries/wuss/core/` |
| File types | `io/filetype` (new) |
| Directory listing | `io/dirlist` (new) |
| Leafname validation | `io/path` |
| Grid view | `include/wuss/gadget/gridview.h` (`WUSS_GADGETS`) |
| Save As | `include/wuss/component/saveas.h` (`WUSS_COMPONENTS`) |
| Filer | `apps/wuss/tasks/filing/filing.{c,h}`, in `g_system_tasks` beside Display and Palette |

## Design

### `io/filetype`

- A struct holding both a RISC OS filetype (`uint16`) and an extension.
- A static mapping table: png↔0xB60, txt↔0xFFF, plus directory and
  application specials. Unknown maps to 0xFFD / `""`. Extension lookup is
  case-insensitive.
- `filetype_set(path, t)` sets the filetype attribute on RISC OS and does
  nothing elsewhere. The saver calls it after writing.

### `io/dirlist`

- A dynamic list of `{leaf, is_dir, filetype}` entries.
- Sorted by name, case-insensitively. The list leaves out dotfiles.
- No cap and no silent skipping. (`io/namelist` is unsuitable: it uses a
  fixed table, skips long names silently, sorts with `strcmp` and carries no
  directory flag.)

### `io/path` additions

- Leafname validation: non-empty, contains no platform separator, and is
  neither `.` nor `..`.
- A "is this a full path" test, so callers never hard-code `/`.

### Inter-task messaging (core)

- Each message is a fixed 1 KiB block. The header holds
  `{size, action, sender, target window, my_ref, your_ref}`. Core copies
  blocks into an array queue. There is no per-message allocation.
- A payload that does not fit (e.g. an over-long path) is an explicit error.
  Core never truncates paths.
- Delivery kinds: plain send, recorded send and acknowledge. Recorded
  messages that nobody acknowledges bounce back to the sender as
  `wuss_EVENT_MESSAGE_BOUNCED`.
- A recorded message counts as acknowledged if, during its handler, the
  recipient sends any message with `your_ref == my_ref` or an explicit
  acknowledge. Core decides on the bounce when the handler returns.
- `my_ref` comes from a global counter that skips 0 on wrap.
- Addressing is point-to-point (by task) or broadcast. Broadcast goes to
  every task except the sender, in creation order. For a recorded broadcast
  the first acknowledgement stops further delivery. Broadcast has no v1
  user, but it has a test.
- **Priority.** Sending always queues. Delivery never nests inside another
  handler. Core keeps a dispatch-depth counter. A public entry point
  (`wuss_mouse_click`, `wuss_mouse_move`, `wuss_key`, `wuss_idle`) can return
  to depth 0. Each time that happens, core drains the queue to empty in FIFO
  order, including messages sent during the drain. `wuss_idle` also drains
  before and after it delivers IDLE events. So the queue is always empty when
  control returns to the frontend. There is no public pump.
- **Livelock cap.** More than 256 deliveries in one drain logs a warning and
  defers the remainder to the next entry point. ASan builds also assert.
- **Dead endpoints.** On delivery, core checks that the header's target
  window still exists (by scanning the window list). If not, it bounces the
  message. `wuss_task_destroy` purges the queue:
  - Recorded messages addressed to the destroyed task bounce to their
    senders.
  - Core drops anything addressed to a destroyed sender.
- Core reserves actions 0x0000-0xFFFF (`include/wuss/message.h`). Tasks use
  0x10000 upwards.

### Transfer protocol

Every step is a recorded send:

1. The saver sends `DataSave(point, estimated size or -1, filetype,
   leafname)` to the drop target. The header holds the target window.
2. The target replies `DataSaveAck(unsafe, full path)`. A Filer sends
   `unsafe = 0` and the saver adopts the path. A task that only loads the
   data sends a scrap path (`path_scrap`) with `unsafe = 1`. The saver then
   keeps its name, and the target deletes the file once loaded. See
   [live-links-plan.md](live-links-plan.md).
3. The saver writes the file, calls `filetype_set`, then sends
   `DataLoad(path)`.
4. The target replies `DataLoadAck`.

#### RAM transfer

All tasks share one process, so an image moves between them as a bitmap, not
as encoded bytes:

1. `DataSave` carries `ram = 1` when the Save As has a bitmap function
   (`wuss_saveas_set_bitmap_fn`).
2. A target that takes bitmaps replies `RamFetch` (no payload) in place of
   `DataSaveAck`. Any other target replies as above, so the file path is the
   fallback.
3. The saver makes a `pixelfmt_rgba8888` bitmap with `bitmap_convert` and
   sends `RamTransmit(bitmap, leafname)`.
4. The target takes the bitmap and acknowledges. Core turns that into a
   `DataLoadAck`, so the dialogue closes as before. If nothing acknowledges
   it, the message bounces and the saver frees the bitmap.

The saver keeps its leafname, as for a scrap file. In the demo, `taskapp`
sets the bitmap function for every snapshot-saved task and for tasks with
`save_bitmap`. Viewing, Dithering, Effecting and Resizing take bitmaps through
`data_load_bitmap`.

Because of the priority rule, the whole handshake completes within the
`wuss_mouse_click` that ends the drag.

### Time and double-click (core)

- The frontend calls `wuss_set_time(wuss, ms)` once per frame (SDL:
  `SDL_GetTicks`, RISC OS: monotonic time). Tests set it explicitly. The
  `wuss_mouse_click` signature does not change.
- A double-click is a normal DOWN carrying a `wuss_BUTTON_DOUBLE` flag, so
  it does not affect handlers that ignore the flag. It requires the same
  button and the same window, within the configured time and distance.
- New `wuss_config_t` fields: `double_click_ms`, `double_click_px` and
  `drag_threshold_px`, with 0 meaning the defaults (400 ms, 4 px, 4 px).
  `wuss_set_config` can change them live, and the Configuring task shows
  them.

### Drag session (core)

- `wuss_drag_start(task, box, ...)` starts a drag.
  `wuss_EVENT_DRAG_END {icon, drop window, point, cancelled}` goes to the
  originating task.
- Over an icon bar icon, `drop` is the bar's window and `drop_icon` names the
  icon. A sender addresses it with `wuss_send_recorded_to` and a
  `wuss_msg_target_t { drop, drop_icon }`. That delivers to the icon's
  owning task (window NULL). The message bounces if nothing acknowledges it
  or if the icon is destroyed first. The same target struct takes any window
  plus one of its work-area icons.
- Core draws the drag as a marching-ants box, painted last in
  `wuss_redraw_dirty`. It invalidates only the four 1 px edges, and the ants
  step every N frames.
- During a drag, core takes over input:
  - Core does not deliver the ending release as `MOUSE_UP`.
  - Menu hover tracking pauses.
  - Core defers `POINTER_ENTER`/`EXIT` and settles them at drag end.
  - `wuss_key` intercepts Escape, which cancels the drag.
  - Core clamps the pointer to the screen.

  The SDL frontend captures the mouse for the duration.
- Menu chains dismiss only on a press outside them, so releasing a drag
  outside a menu-leaf Save As does not close the chain.

### DRAGGABLE icon

- Implements the existing `wuss_ICON_TYPE_DRAGGABLE` stub.
- Draws its bitmap as `wuss_ICON_TYPE_BITMAP` does.
- Starts a core drag once the pointer moves beyond `drag_threshold_px` with
  a button held. A click without movement does nothing.

### Grid view gadget

- Lives on a caller's window without owning it, following the `stringset`
  pattern: the task forwards `REDRAW`, `MOUSE` and `OPEN` to
  `wuss_gridview_handle_event`.
- Items come from a count plus
  `item_fn(index, opaque) -> {glyph, label}`. The gadget draws the glyph over
  the label and truncates long labels with an ellipsis. (A per-cell draw hook
  waits until a caller needs one.)
- Cell size is the largest glyph combined with a fixed N-character label
  width in the current font, plus padding. So one long name cannot inflate
  the grid.
- Column count follows the visible width and reflows on `OPEN`. The gadget
  sets the window's document extent. Hit-testing is arithmetic. Gaps
  between cells miss.
- It owns the selection (a `bitvec`) with RISC OS rules: SELECT selects only
  the item, ADJUST toggles it, a click on empty space clears the selection.
  A double-click on an item calls `activate_fn(index)`.
- Intended for reuse by other tasks (e.g. a resources editor). Drag-out
  hooks in here later.

### Save As component

- API: `wuss_saveas_create(&sa, wuss, &filetype, leafname, save_fn, opaque)`
  with `save_fn(const char *path, void *opaque) -> result_t`, plus
  `wuss_saveas_set_leafname`, `wuss_saveas_set_filetype`,
  `wuss_saveas_get_path`, `wuss_saveas_open` (show the dialogue) and
  `wuss_saveas_window` (for use as a menu submenu).
- The dialogue runs on a task of its own. `wuss_saveas_create` creates that
  task and `wuss_saveas_destroy` frees it. The task forwards the dialogue
  window's events through `wuss_saveas_handle_event(sa, window, event)`, so
  callers forward nothing.
- Layout: DRAGGABLE file icon, writable leafname, Cancel and Save buttons,
  status line.
- The default leafname gets the platform extension (none on RISC OS).
- If the writable holds a full path, Return saves directly. Otherwise the
  status line reads "To save, drag the icon to a directory display".
- Transfer state lives in `wuss_saveas_t`. The component ignores a drag
  while a transfer is in progress.
- The dialogue closes on `DataLoadAck`. A bounce is silent.
- The component works both as a menu submenu leaf and standalone through
  `wuss_saveas_open`, which callers invoke from a menu selection. After a
  real save the writable holds the full path.

### Filer task

- A System menu row starts the task, which places a drive icon on the icon
  bar. A click on that icon, or a second pick of the menu row while the
  Filer runs, opens the root window.
- The root comes from `-root`, defaulting to the current directory.
  Emscripten uses `/`.
- Each window is a grid view over an `io/dirlist`, titled with its full
  path. Double-clicking a directory opens a new window, or brings an existing
  window for that path to the front. Double-clicking a file does nothing.
- As a drop target, the Filer:
  - checks the leafname (it does not acknowledge a bad one, so that one
    bounces)
  - treats an over-long path as an error
  - overwrites existing files silently
  - holds no state between `DataSaveAck` and `DataLoad`
  - rescans on `DataLoad` and when a window opens.

  It ignores the drop point. Files always land in the window's own directory.
- Glyphs are one PNG per glyph (`file_unk.png`, `file_dir.png`,
  `file_b60.png`) in `resources/wuss/iconpool/`. Each loads as its own
  `bitmap_t`, since bitmap icons have no sub-rect support. A missing glyph
  logs a warning but is not an error: the cell shows its label only.

## Testing

- Each layer lands with its own tests.
- Tests write files only into a dedicated temporary directory, which they
  delete afterwards.
- Test tasks build only under `BUILD_APPS`, so `WussTest` cannot link the
  Filer. Instead the end-to-end headless test in `wuss-test.c` uses a
  minimal fake receiver task defined in the test. It covers messaging, drag,
  DRAGGABLE and Save As under `build-asan`. We check the Filer's own glue by
  hand in `build-sdl`.

## Build order

All steps are complete. Each step has its own commit, with its tests.

1. `io/filetype`.
2. `io/dirlist` and the `io/path` leafname validation.
3. Core messaging.
4. `wuss_set_time`, the double-click flag, the config fields and the
   Configuring task UI.
5. Core drag session, plus SDL mouse capture.
6. DRAGGABLE icon.
7. Grid view gadget.
8. Save As component and the end-to-end headless test (fake receiver).
9. Filer task: System menu row, icon bar icon, grid windows, receiver,
   glyphs.
10. Pilot in the gradient task.
11. Route `snapshot_save_png` through Save As (now used by most test tasks).
12. Update `libraries/wuss/TODO.txt` (layer 5 and 6 items).

## Deferred

- Error and query boxes.
- Sprite drag visuals (after masked sprites).
- Filer copy, move, delete and rename.
- Other views.
- ADJUST-closes-parent.
- Grid drag-out and custom cell drawing.
- `saved_fn`, a public message pump and unsaved-changes tracking.
