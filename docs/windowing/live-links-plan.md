# Live links (pipes) between Wuss tasks

Status: proposal.

## Context

Data moves between tasks only as files. Save As (`libraries/wuss/component/saveas.c`) runs the RISC OS protocol: DATA_SAVE, then DATA_SAVE_ACK with a path, then the source writes the file, then DATA_LOAD, then LOAD_ACK. However, **only the Filer answers DATA_SAVE** (`apps/wuss/tasks/filing/filing.c`). A Save As icon dropped on another app's window bounces silently. Every transfer is also a one-shot copy: after editing the source again, the user must save and drag again.

Goal:

1. One-shot app-to-app transfer.
2. A persistent **link** ("pipe"): the source re-sends its document each time it changes.

Linking to a Filer path gives "save with hot reload" for free: the file stays current.

All tasks share one process and one message bus (`include/wuss/message.h`), so a link needs no OS pipe. It reuses the existing protocol, repeated.

## Design

### Stage 1: app-to-app (scrap transfer) — done

- `taskapp` answers DATA_SAVE on an instance window, or on the app's icon bar icon, for any app with `data_load` that takes the offered type (`desc->opens`). It replies with a `wuss_data_save_ack_t` naming a scrap file from DPTLib's `path_scrap` (`$TMPDIR/Wuss/<leafname>`; `<Wimp$ScrapDir>.Wuss.<leafname>` on RISC OS), keeping the saver's leafname so the receiver titles the document sensibly.
- The ack sets `unsafe`, so Save As writes there but keeps its own name, as RISC OS does for `<Wimp$Scrap>`. The Filer sends `unsafe = 0`.
- Save As addresses the drop with a `wuss_msg_target_t` (window plus icon), so a drop on an icon bar icon reaches the icon's task.
- DATA_LOAD then arrives through the existing `taskapp_window_data_load` (`apps/wuss/tasks/common/taskapp.c`), or the icon's load path. `app_load` deletes the scrap file afterwards, loaded or not.
- This allows Painting to send to Dithering, Effecting, Resizing and Viewing with no per-app code.

### Stage 2: the link

- **Gesture:** Shift while dropping the Save As icon asks for a link. A plain drop stays a one-shot copy. If Save As cannot see modifiers at drop time, use a "Link" option icon in the dialogue instead.
- **Source side:** a link is `{target window, path, filetype}`, held by the source instance (`taskapp_instance_t`). It is created when the receiver's LOAD_ACK arrives for a linked save.
- **Change hook:** add `taskapp_changed(instance)`. Each source app calls it after an edit (a Painting stroke ends, an effect parameter changes). It records the `wuss` time. On `wuss_EVENT_IDLE`, if a change is pending and 250 ms have passed (the debounce), taskapp calls `desc.save(path)` and sends a recorded DATA_LOAD to the target again.
- **Receiver side:** no change. A repeat DATA_LOAD simply reloads (`painting_load`, Viewing and so on). Protecting a receiver's own unsaved edits from a reload is out of scope for v1.
- **Teardown:** when the target closes, the recorded DATA_LOAD bounces (`wuss_EVENT_MESSAGE_BOUNCED`) and the source drops the link. When the source instance closes, its links are freed. There is one link per target window; linking again replaces it.
- **Hot reload to file:** Shift-drop on the Filer gives a link whose target is the Filer window. Each push rewrites the file and the Filer's DATA_LOAD handler rescans. Tasks that opened that file independently get no update; a file watcher (mtime poll on idle) would cover them later.
- **Indicator:** a source menu row, "Links ▸ Break all", reached next to `taskapp_save_row`, shows that links exist and removes them.

### Not in v1

- In-memory (RAM) transfer. Scrap files are simple and need no new message. Add a RAM message later if encoding a PNG per push proves too slow.
- Large fan-out or loop detection. A loop (A to B to A) cannot ping-pong: only local edits call `taskapp_changed`, and loads never do.
- File watching.

## Files

- `apps/wuss/tasks/common/taskapp.{c,h}`: DATA_SAVE reply, link list, `taskapp_changed`, idle push and bounce teardown.
- `libraries/wuss/component/saveas.{c,h}`: carry the link request (Shift or option) to the callback; report the final target window and path on LOAD_ACK.
- `apps/wuss/tasks/painting/painting.c` (first adopter): call `taskapp_changed` after edits; make sure `painting_load` does not call it.
- `docs/windowing/drag-save-plan.md`: mark task-to-task transfer done and point here.
- `docs/windowing/wuss-keys.md` and `apps/wuss/tasks/shortcuts/shortcuts.c`: Shift-drop, if that is the chosen gesture.

## Reuse

- Recorded send, bounce and automatic LOAD_ACK: `libraries/wuss/core/message.c`.
- `pathf` and `path_leaf` (`io/path.h`); `array_grow` for the link list.
- `wuss_EVENT_IDLE` and the `wuss_set_time` clock for the debounce.

## Verification

- A test (component or taskapp) with two fake tasks: drop with link; signal a change and idle past the debounce; assert a second DATA_LOAD arrives; close the target; assert the bounce clears the link.
- `./build-asan/WussTest-debug -resources .`
- Headless demo script (`build-sdl/wuss --tasks Painting --script FILE`): open Painting and Viewing, Shift-drag the Save As icon onto Viewing, paint a stroke, `wait 30`, `png`, and check Viewing shows the stroke.
