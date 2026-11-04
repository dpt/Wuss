# [Wuss](https://github.com/dpt/Wuss) > Backdrop

This document specifies how the desktop backdrop behaves: what it paints, what
it does when clicked, how files are pinned to it, how its look is changed and
where notes live. It is implemented by the core `wuss_WINDOW_BACKDROP` flag
and the Pinboard and Jottings demo tasks. Where the implementation falls
short of the original specification, the text says so. See
[ADR 0002](../adr/0002-backdrop-is-a-window.md) for why the backdrop is a
window.

## Layers

From the back to the front:

1. **Backdrop paint.** The existing `wuss_backdrop_t` (a fill plus an
   optional centred image), set by `wuss_config_t::backdrop` or
   `wuss_set_backdrop`. It does not change. Wuss paints it wherever no window
   covers the screen. With no backdrop window, it is the whole desktop and a
   click on it does nothing.
2. **Backdrop window.** At most one. A full-screen window at the bottom of
   `wuss_STACK_BACK`, owned by a task (normally the Pinboard). It has no
   titlebar and no outline. It cannot be moved, resized, closed or brought to
   the front, and a click on it never raises it. A screen resize recomputes
   its box, as for the icon bar.
3. Ordinary windows in the back, middle and top stacks, and the icon bar.

Clicks, menus, drops and redraws reach the backdrop window through the
ordinary window event paths. Core needs no backdrop-specific routing.

### Core support

One public window flag, `wuss_WINDOW_BACKDROP`:

- It implies `wuss_WINDOW_STACK_BACK`, `wuss_WINDOW_PINNED`,
  `wuss_WINDOW_NO_TITLEBAR` and `wuss_WINDOW_NO_OUTLINE`. The window's box is
  always the screen.
- The window stays at the bottom of the back stack. `wuss_window_restack`
  and a click leave it there. The window cycling in `core/task/arrange.c`
  (`back_window`) skips it.
- Creating a second backdrop window fails with
  `result_WUSS_BACKDROP_EXISTS`. The first is left alone.
- The flag is needed because tasks must not set `wuss_WINDOW_PINNED`
  themselves.

## Pinboard task

The Pinboard is a demo task under `apps/wuss/tasks/pinboard/`. It owns the
backdrop window and is the only thing that gives the backdrop behaviour. With
the Pinboard not running, the desktop is backdrop paint only.

### Menu

MENU over the backdrop window (empty backdrop or a pinned icon) opens the
Pinboard menu. It follows [the menu standard](menu-standard.md).

```
Pinboard
  Backdrop      >  Colour   >  (shared colourmenu)
                   Pattern  >  (pattern list)
                   Image    >  None / Centre / Tile / Scale
                   ---
                   Clear
  Selection     >  Open
                   Remove
  ---
  Select all
  Clear selection
  Tidy
```

- `Selection` is greyed when nothing is selected. A MENU press over an
  unselected icon selects that icon only for the life of the menu, as the
  Filing task does.
- `Tidy` snaps every pinned icon to the grid, packed from the top left.

### Changing the backdrop

- `Colour` and `Pattern` build a new `wuss_backdrop_t` and call
  `wuss_set_backdrop`. Other tasks are not notified.
- `Image > Centre` uses `wuss_backdrop_t::image`. `Tile` and `Scale` are
  more than the paint offers, so the Pinboard draws them itself in its
  window's redraw. The backdrop window then has a fill of
  `wuss_NO_BACKGROUND` behind the image.
- To set the image, select a pinned PNG and pick `Image > Centre`, `Tile` or
  `Scale`, or drop a PNG on the backdrop with SHIFT held. A plain drop pins
  the file (see below). `Image > None` removes the image. Those three rows
  are shaded while there is no image and no PNG pin is selected.
- The menu rows are updated in place after each pick, so an ADJUST pick
  that leaves the menu open shows the new state at once.
- `Clear` returns colour, pattern and image to the theme's backdrop.

### Selection and dragging

- SELECT on an icon selects it alone. ADJUST toggles it. Each pin keeps its
  own selection flag.
- SELECT on empty backdrop clears the selection.
- A drag on empty backdrop starts with `wuss_drag_start`, and the Pinboard
  draws the rubber-band box itself. At the end, the icons inside it become
  selected. SHIFT is read with `wuss_get_modifiers`. With SELECT the
  selection is replaced. With ADJUST they are added.
- A drag on a selected icon drags the selection as a solid drag
  (`wuss_drag_start_bitmap`). A drop on the backdrop window moves the icons.
  A drop on another window transfers the file (see below).
- The backdrop window takes no keys, so there are no keyboard shortcuts.
  Use the menu's `Selection > Remove` and `Select all`.

## Pinned files

- **Pin.** The Filing task's drag-out sends `wuss_MESSAGE_DATA_LOAD(path)`
  straight to the drop target, without DataSave first. When the target is
  the backdrop window, the Pinboard stores the path, places an icon at the
  drop point and replies `wuss_MESSAGE_DATA_LOAD_ACK`. The file is
  referenced, not copied.
- **DataSave.** A DataSave (for example from a save dialogue) has no file on
  disc yet. The Pinboard does not acknowledge it, so it bounces. Saving to
  the backdrop is not supported.
- **Duplicates.** Pinning a path that is already pinned moves its icon to
  the drop point. It does not make a second icon.
- **Icon.** The file-type sprite from the icon pool, with the leafname
  beneath it. The position snaps to a grid. A drop on an existing icon is
  placed in the nearest free grid cell.
- **Open.** A double-click (or `Selection > Open`) broadcasts
  `wuss_MESSAGE_DATA_OPEN` with a `wuss_data_open_t`, as the Filing task
  does. A directory broadcasts `wuss_MESSAGE_FILER_OPEN_DIR` instead. If no
  running app claims the file, the Pinboard starts the app for its type and
  sends it once more, as Filing does.
- **Drag out.** Dropping a pinned icon on another window sends that window
  `wuss_MESSAGE_DATA_LOAD(path)`, as the Filing task's drag-out does.
- **Remove.** `Selection > Remove` unpins the icon. The file is never
  deleted.
- **Missing files.** A pinned path that no longer exists remains pinned.
  Opening it bounces, as it would from the Filing task.
- **Pin message.** Any task may pin a path without a drag by broadcasting
  the core message `wuss_MESSAGE_PINBOARD_PIN`, whose payload is the path
  as a NUL-terminated string (like `wuss_MESSAGE_FILER_OPEN_DIR`). The
  Pinboard acknowledges it and places the icon in the first free grid cell
  from the top left. With no Pinboard running, the message bounces to the
  sender.

## Jottings notes

Notes belong to a separate Jottings task under `apps/wuss/tasks/jottings/`.
The Jottings task does not depend on the Pinboard. The two tasks meet only
through messages.

- A note is an ordinary window in the middle stack, with a titlebar, a close
  box and a resize box. Its default fill is yellow. `Colour` in its menu
  opens the shared colourmenu.
- The note text is Markdown, rendered with DPTLib's `text/mdtext`, which the
  Reading task also uses.
- Menu: `New note`, `Colour >`, `Edit` (a toggle between editing the source
  and showing the rendered text), `Pin to backdrop` and `Delete`. A
  double-click on a rendered note also starts editing. ESC stops it.
- Editing happens at a movable caret. A click places it, and a double-click
  on a rendered note places it near that point in the source. The arrow keys,
  Home, End, Page Up and Page Down move it. As in writable icons, Shift moves
  by word and Ctrl to the end of the line (Alt also moves by word, and Cmd on
  macOS acts as Ctrl). Ctrl+Home and Ctrl+End, or Ctrl+Up and Ctrl+Down, go to
  the ends of the note. BACKSPACE and DELETE take the same modifiers. Ctrl+K
  clears to the end of the line and Ctrl+U removes the line. Deleting by line
  works on the source's lines, and moving by line on the wrapped lines. There
  is no selection or clipboard.
- **Notes directory.** A permanent per-user directory, created on first
  use by DPTLib's `path_user_data("Wuss/Notes")`. It survives between
  sessions:

  | Platform | Directory |
  |---|---|
  | macOS | `~/Library/Application Support/Wuss/Notes` |
  | Linux | `$XDG_DATA_HOME/wuss/notes` (default `~/.local/share/wuss/notes`) |
  | Windows | `%APPDATA%\Wuss\Notes` |
  | RISC OS | `<Choices$Write>.Wuss/Notes` (see below) |
  | WebAssembly | `$HOME/.local/share/wuss/notes` on MEMFS, so notes last only for the page |

  On RISC OS, `/` in a leafname is the extension separator, so the
  directory is one leaf, `Wuss/Notes`, and note files have no `.md`
  extension.
- **Pin to backdrop.** Saves the note's Markdown source to a file in the
  notes directory, named from the note's title (`.md`, Markdown file type).
  Then it broadcasts `wuss_MESSAGE_PINBOARD_PIN` with that path. The note
  window stays open. A later pin of the same note rewrites its own file, and
  the Pinboard treats it as a duplicate, so no second icon appears.
- **Name clashes.** Because the directory is permanent, a file with the same
  name may be left from an earlier session. A note never overwrites a file
  it did not write itself. It adds `-2`, `-3` and so on to the leafname
  until the name is free.
- If the pin message bounces (no Pinboard), the task reports "No pinboard is
  running" through the error component. The file is kept.
- **Opening a pinned note.** A double-click on the pinned icon broadcasts
  `wuss_MESSAGE_DATA_OPEN`. The Jottings task acknowledges it for any file
  in the notes directory, including notes from earlier sessions, and opens
  it in a new note window. A note that is already open gets a second
  window. Markdown files elsewhere are left to the Reading task. This works
  through the `claims` hook in `taskapp_desc_t` and the start-up order:
  Jottings starts before Reading, so it sees each `wuss_MESSAGE_DATA_OPEN`
  first.
- **Delete.** `Delete` closes the note. If the note has a file, the task
  asks first and then deletes the file as well. A pinned icon for a deleted
  file stays, as for any missing file.
- When it starts, the task opens one welcome note that explains the desktop
  to new users. It opens again each time the task is started.

## Interaction summary

| Pointer over | SELECT | ADJUST | MENU | Drop |
|---|---|---|---|---|
| Backdrop, no Pinboard | nothing | nothing | nothing | bounces |
| Empty backdrop | clear selection; drag: rubber band | drag: rubber band, adding | Pinboard menu | pin; SHIFT: set image |
| Pinned icon | select; double: open; drag: move or transfer | toggle | Pinboard menu, Selection live | pin in nearest free cell |

## Persistence

Only note files persist. A note's text is kept once it has been pinned,
because pinning writes it to the permanent notes directory. Pinned icons and
their positions, unpinned note text, note window positions and a changed
backdrop all last only for the session. Keeping those needs a pin file and a
choices system, and is out of scope here. A note file from an earlier session
can be pinned again from the Filing task.

## Open questions

- Should a `wuss_EVENT_PALETTE` change reset a backdrop colour that the
  Pinboard chose, or should the Pinboard map it again to the nearest colour?
- Is a drop on the icon bar's area ever routed to the backdrop window? It
  should not be, because the icon bar is hit-tested first.
- Should the Jottings task reopen every note in the notes directory when it
  starts, instead of the welcome note?
