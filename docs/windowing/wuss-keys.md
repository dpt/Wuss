# [Wuss](https://github.com/dpt/Wuss) > key reference

Survey of every key the wuss demo (`apps/wuss`) and its libraries react to, split into **system** keys (handled by wuss or the launcher whatever is focused) and **application** keys (handled by one task when its window has the focus). Ends with proposed standard keys (RISC OS style first, macOS where the guide is silent).

## How a key travels

1. The frontend (`frontend-sdl.c`, `frontend-riscos.c`) turns a key press into a Unicode code point or a `wuss_KEY_*` constant plus modifiers (Shift, Ctrl, Alt). On macOS Cmd is folded into Ctrl, so "Ctrl" below means Cmd there.
2. `main.c` offers it to `tasks_system_key`, which handles the window and app chords, then checks for Ctrl-Shift-3.
3. `wuss_key` (`core/key.c`) offers it in order to:
   1. an active window drag (Escape cancels it);
   2. an open menu chain (Escape dismisses it);
   3. the caret's writable icon (editing keys);
   4. the focused window's task, as `wuss_EVENT_KEY`.
4. A task that does not want the key returns `result_WUSS_KEY_UNCLAIMED`. `main.c` then passes the key to `tasks_launcher_key`, which handles the zoom keys and tries the Wuss menu's shortcut labels.

A task's menu shortcuts only fire while one of its windows has the focus (`wuss_menu_dispatch_shortcut`). Labels match as follows: `^X` needs Ctrl; a bare label forbids Ctrl; Alt never matches; Shift is required by a `WUSS_MENU_SHIFT` prefix and forbidden for SPACE, DEL and F1..F12.

## System keys

| Key                     | Action                              | Where                  |
| ----------------------- | ----------------------------------- | ---------------------- |
| F7 / Ctrl-\`            | Next window of the app              | `tasks_system_key`     |
| Shift-F7 / Ctrl-Shift-\` | Previous window of the app         | `tasks_system_key`     |
| Ctrl-Shift-F            | Toggle window size                  | `tasks_system_key`     |
| Ctrl-F2                 | Close window                        | `tasks_system_key`     |
| F10 / Ctrl-Tab          | Next app                            | `tasks_system_key`     |
| Shift-F10 / Ctrl-Shift-Tab | Previous app                     | `tasks_system_key`     |
| Ctrl-F10                | App to front                        | `tasks_system_key`     |
| F11 / Ctrl-H            | Hide app                            | `tasks_system_key`     |
| Shift-F11 / Ctrl-Shift-H | Hide other apps                    | `tasks_system_key`     |
| Ctrl-F11                | Show all apps                       | `tasks_system_key`     |
| Ctrl-F9 / Ctrl-Q        | Quit app, with unsaved-data prompts | `tasks_system_key`     |
| Ctrl-Shift-Esc          | Kill app, with no prompts           | `tasks_system_key`     |
| Ctrl-Shift-Q            | Quit Wuss (not on Emscripten)       | `tasks_system_key`     |
| Ctrl-Shift-3            | Save screenshot (not on Emscripten) | `apps/wuss/main.c`     |
| Escape                  | Cancel a window drag or resize      | `core/key.c`           |
| Escape                  | Dismiss the open menu chain         | `menu/menu.c`          |
| Return                  | Press the dialogue's default button | `component/dialogue.c` |
| Escape                  | Press the dialogue's Cancel button  | `component/dialogue.c` |
| Ctrl-= / Ctrl-Plus      | Zoom in                             | `tasks_launcher_key`   |
| Ctrl--                  | Zoom out                            | `tasks_launcher_key`   |
| Ctrl-0                  | Actual size                         | `tasks_launcher_key`   |
| Shift-F1                | Debug > Redraw                      | `tasks_launcher_key`   |
| Ctrl-Shift-F1           | Debug > Garbage                     | `tasks_launcher_key`   |
| Shift-F3                | Debug > Pixel Stress                | `tasks_launcher_key`   |

Notes:

- A task cannot claim the `tasks_system_key` chords: they run ahead of `wuss_key`, so they also work while a menu is open or a writable holds the caret.
- The `tasks_launcher_key` keys are only reached when the focused task leaves the key unclaimed.
- The frontend's window-close request (SDL quit) also quits Wuss.

### Text editing (writable icons)

Active whenever a writable icon holds the caret. Alt-anything is ignored.

| Key                      | Action                                 |
| ------------------------ | -------------------------------------- |
| Left / Right             | Move one character                     |
| Shift-Left / Shift-Right | Move one word                          |
| Home / End               | Start / end of text                    |
| Ctrl-Left / Ctrl-Right   | Start / end of text                    |
| Tab / Shift-Tab          | Next / previous writable in the window |
| Backspace                | Delete before caret                    |
| Delete                   | Delete after caret                     |
| Ctrl-U                   | Clear the field                        |
| Ctrl-A                   | Select all                             |
| Ctrl-Z                   | Clear selection                        |
| Ctrl-C / Ctrl-X          | Copy / cut selection                   |
| Ctrl-V / Insert          | Paste at caret                         |
| Any printable            | Insert at caret                        |

### Text editing (Jottings)

Active while a note is being edited. A note is not a writable icon: `jotting_key` handles these itself. On macOS, Cmd acts as Ctrl. Lines to move by are wrapped lines, and lines to clear are the source's.

| Key                                  | Action                     |
| ------------------------------------ | -------------------------- |
| Left / Right                         | Move one character         |
| Up / Down                            | Move one line              |
| Page Up / Page Down                  | Move one window of lines   |
| Shift or Alt with Left / Right       | Move one word              |
| Home / End                           | Start / end of line        |
| Ctrl-Left / Ctrl-Right               | Start / end of line        |
| Ctrl-Home / Ctrl-End                 | Start / end of note        |
| Ctrl-Up / Ctrl-Down                  | Start / end of note        |
| Backspace / Delete                   | Delete one character       |
| Shift or Alt with Backspace / Delete | Delete one word            |
| Ctrl-Backspace                       | Clear to start of line     |
| Ctrl-Delete / Ctrl-K                 | Clear to end of line       |
| Ctrl-U                               | Delete the line            |
| Escape                               | Stop editing               |
| Any printable                        | Insert at caret            |

## Application keys

Menu shortcuts (fire when the task has the focus):

| Key           | Action          | Tasks                                                                                                                                                                 |
| ------------- | --------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F3            | Save / Save as  | ball chars checker clock compositing curving dithering doughnut effecting fern fireworks gradient greebling oscillating orbing painting resizing saturn sofa          |
| Ctrl-R        | Randomise       | checker doughnut fern gradient greebling orbing oscillating saturn sofa                                                                                               |
| R             | Reset           | checker curving doughnut fern gradient orbing oscillating saturn sofa                                                                                                 |
| Space         | Pause           | ball compositing doughnut fern fireworks oscillating saturn sofa                                                                                                      |
| A C G D       | Add ball, Clear, Gravity, Damping | ball                                                                                                                                                |
| D S H         | Digital, Seconds, 12-hour | clock                                                                                                                                                       |
| H             | Hull            | curving                                                                                                                                                               |
| C W M         | Clear, Walls, Markers | fireworks                                                                                                                                                       |
| R             | Random palettes | greebling                                                                                                                                                             |
| N             | New game        | minesweeping sudoku                                                                                                                                                   |
| 1-4           | Choose light    | orbing                                                                                                                                                                |
| C M           | Light colour, Mutate | orbing                                                                                                                                                           |
| F8            | Undo            | painting                                                                                                                                                              |
| F9            | Redo            | painting                                                                                                                                                              |
| Ctrl-X C V    | Cut, Copy, Paste (pixels) | painting                                                                                                                                                    |
| Ctrl-A Z I    | Select all, Clear selection, Invert | painting                                                                                                                                          |
| F Delete      | Fill, Clear selection (foreground, background) | painting                                                                                                                               |
| Shift-H V     | Flip horizontally, vertically | painting                                                                                                                                                |
| Shift-R L     | Rotate right, left | painting                                                                                                                                                           |
| R             | Restart         | sudoku                                                                                                                                                                |
| F2 F3         | List, Cloud     | tagging                                                                                                                                                               |
| F4 F5         | Scaling off, on | tagging                                                                                                                                                               |
| F6            | Sort by name    | tagging                                                                                                                                                               |
| Ctrl-F3       | Selected first  | tagging                                                                                                                                                               |
| Ctrl-R K N    | Rename, Delete, New tag | tagging                                                                                                                                                       |
| Ctrl-Shift-Up | Open parent     | filing                                                                                                                                                                |
| Ctrl-P        | Pause           | zxtask                                                                                                                                                                |
| Ctrl-] Ctrl-[ | Faster, Slower  | zxtask                                                                                                                                                                |
| Ctrl-M        | Monochromatic   | zxtask                                                                                                                                                                |

One label never fires. System F7 (next window) runs ahead of Tagging's F7 (Sort by count). Painting's F fills the selection while there is one; with nothing selected the row is shaded and F picks the Fill tool.

Direct keys:

| Task                                | Key                 | Action                                                  |
| ----------------------------------- | ------------------- | ------------------------------------------------------- |
| appsfiler filing                    | Arrows              | Move cursor                                             |
| appsfiler filing                    | Return              | Open item                                               |
| ball chars clock fireworks          | menu shortcuts only | as above                                                |
| checker                             | Left / Right        | Previous / next pattern                                 |
| checker                             | Up / Down           | Wider / narrower bands                                  |
| compositing                         | Left / Right        | Previous / next operator                                |
| curving                             | Left / Right        | Previous / next curve kind                              |
| curving                             | Up / Down           | Scroll (wheel equivalent)                               |
| doughnut                            | Arrows              | Rotate (a / b)                                          |
| doughnut                            | `[` `]`             | Major density down / up                                 |
| doughnut                            | `,` `.`             | Minor density down / up                                 |
| doughnut                            | `-` `=` `+`         | Tube density down / up                                  |
| effecting                           | Delete              | Delete effect (list window)                             |
| fern                                | Left / Right        | Previous / next seed                                    |
| greebling                           | Left / Right        | Previous / next palette (Right = ADJUST)                |
| greebling                           | Space               | Reseed (SELECT)                                         |
| gradient                            | S                   | Next shape                                              |
| gradient                            | D                   | Next dither matrix                                      |
| minesweeping                        | Arrows              | Move cursor                                             |
| minesweeping                        | Return              | Uncover (SELECT)                                        |
| minesweeping                        | Space               | Flag (ADJUST)                                           |
| oscillating                         | Left / Right        | Previous / next frequency pair                          |
| orbing                              | Arrows              | Nudge light (Shift = bigger step)                       |
| orbing                              | O                   | Toggle the current light                                |
| painting                            | B H K E             | Pencil, Paintbrush, Wobbly brush, Eraser                |
| painting                            | L R X               | Line, Rectangle, Filled rectangle                       |
| painting                            | C D N M             | Ellipse, Filled ellipse, Path, Filled path              |
| painting                            | F G P               | Fill, Gradient, Spray                                   |
| painting                            | S O W               | Select, Lasso, Magic wand                               |
| painting                            | I V                 | Pick colour, Move                                       |
| painting                            | `[` `]`             | Size down / up                                          |
| painting                            | `+` `=` / `-`       | Zoom in / out                                           |
| painting                            | `1`                 | Actual size                                             |
| painting                            | Escape              | Drop selection                                          |
| painting                            | Arrows              | Nudge selection (Shift = 8 pixels)                      |
| painting                            | Space + drag        | Pan the view                                            |
| saturn                              | Left / Right        | Previous / next seed                                    |
| sofa                                | Left / Right        | Previous / next model                                   |
| sofa                                | Up / Down           | Step rotation (paused only)                             |
| sudoku                              | Arrows              | Move cursor                                             |
| sudoku                              | 1-9                 | Enter digit                                             |
| sudoku                              | 0 Space Backspace Delete | Clear cell                                         |
| viewing                             | Space / Shift-Space | Next / previous image                                   |
| viewing                             | `+` `=` / `-`       | Zoom in / out (not while tiled)                         |
| viewing                             | `1`                 | Actual size                                             |
| viewing                             | Arrows              | Scroll                                                  |
| viewing                             | Page Up / Page Down | Scroll by page                                          |
| viewing                             | Home / End          | Top left / bottom                                       |
| zxtask                              | Arrows              | Kempston joystick                                       |
| zxtask                              | `.`                 | Kempston fire                                           |
| zxtask                              | Other ASCII         | Spectrum keyboard (Shift = Caps Shift)                  |
| dialogues                           | Return / Escape     | OK / Cancel                                             |

`zxtask` is the Spectrum games, Chase H.Q. and The Great Escape.

Tasks with no key handling (all keys unclaimed): palette configuring icons display reading pinboard pooling taskmanager. `keys` is the key-test task and records everything but the function keys.

## Collisions to avoid

Rules for the proposals below:

1. The RISC OS 3 Style Guide (pp. 68 to 73) wins wherever it defines a key.
2. macOS chords fill the gaps where the guide is silent. Ctrl means Cmd on macOS.
3. Never use Alt in a shortcut. The guide reserves it for international characters. Use Ctrl-Shift instead.
4. Never use F12, Shift-F12, Ctrl-F12 or Ctrl-Shift-F12. RISC OS owns them.
5. Shift modifies a function key into a related action. Ctrl gives an unrelated one.

Taken today by existing tasks: F3 (Save) F8 (painting Undo) F9 (painting Redo) Ctrl-R P M U; Escape; Return. Ctrl-P and Ctrl-M (zxtask) are safe: the guide uses the Print key and does not define Ctrl-P or Ctrl-M.

## Standard keys: RISC OS style

Adopt all of these as defined by the guide. Keys marked "new" have no code today.

### Function keys

| Key        | Action                                   |
| ---------- | ---------------------------------------- |
| F1         | Help                                     |
| F2         | Load named document                      |
| Shift-F2   | Insert named document                    |
| Ctrl-F2    | Close window (`wuss_window_try_close`)   |
| F3         | Save document                            |
| F4         | Find / search and replace                |
| F5         | Go to                                    |
| F6         | Sort                                     |
| F8         | Undo                                     |
| F9         | Redo                                     |
| F7 F10 F11 | Free for apps and the system (see below) |

Done: F3 is the Save shortcut on every task with a Save row (Ctrl-S is gone), painting Undo is F8 and painting Redo is F9. The other keys need a function no task has yet: Help (F1), Load (F2), Insert (Shift-F2), Find (F4), Go to (F5) and Sort (F6). Wire each when a task gains the function: give its menu row the shortcut label and the shortcut dispatcher does the rest.

### Ctrl keys

Ctrl-D, Ctrl-T, Ctrl-B and Ctrl-I are text-editor keys. The guide applies them only to applications that offer those functions. Other tasks may reuse them (see Duplicate and Get info below).

| Key    | Action                          |
| ------ | ------------------------------- |
| Ctrl-C | Copy selection to the clipboard |
| Ctrl-X | Cut selection to the clipboard  |
| Ctrl-V | Paste at the caret              |
| Ctrl-A | Select all                      |
| Ctrl-Z | Clear selection                 |
| Ctrl-U | Delete line                     |
| Ctrl-K | Delete (for example a file)     |
| Ctrl-D | Insert date                     |
| Ctrl-T | Insert time                     |
| Ctrl-B | Bold                            |
| Ctrl-I | Italic                          |

### Named keys

| Key             | Action                                                       |
| --------------- | ------------------------------------------------------------ |
| Escape          | Cancel operation                                             |
| Return          | New line in an editor; default action in a dialogue          |
| Print           | Print document                                               |
| Tab / Shift-Tab | Tab stop in an editor; next / previous field in a dialogue   |
| Insert          | Paste at the caret                                           |
| Backspace       | Delete left of the caret; with no caret, cut the selection   |
| Delete          | As Backspace                                                 |
| Copy            | Delete right of the caret; with no caret, copy the selection |
| Home            | Start of document                                            |
| Page Up / Down  | Scroll a page                                                |
| Left / Right    | Move one character                                           |
| Up / Down       | Move one line; in a dialogue, previous / next field          |

The guide also requires keys for word, page, line and document movement, but the arrow glyphs are lost in the PDF text. Wuss already uses Shift-Left / Right for a word and Ctrl-Left / Right for the ends of text. Check the printed guide before relying on the rest. On a PC keyboard the Copy key becomes End and Backspace / Delete never cut.

## Standard keys: macOS fill-ins

Used only where the guide is silent.

| Key             | Action                 | Notes                                                                                                     |
| --------------- | ---------------------- | --------------------------------------------------------------------------------------------------------- |
| Ctrl-N          | New window             | Task decides what "new" means (document or view).                                                         |
| Ctrl-G          | Find next              | Repeats the F4 find.                                                                                      |
| Ctrl-Shift-G    | Find previous          |                                                                                                           |
| Ctrl-E          | Use selection for find |                                                                                                           |
| Ctrl-=          | Increase size          | Zoom in, larger text or brush. Ctrl-Plus also works. Unclaimed, it zooms the whole desktop (replaces F2). |
| Ctrl--          | Decrease size          | As above (replaces Shift-F2).                                                                             |
| Ctrl-0          | Actual size            | Done.                                                                                                     |
| Ctrl-,          | Choices                |                                                                                                           |
| Ctrl-[ / Ctrl-] | Back / forward         |                                                                                                           |
| Ctrl-Shift-Up   | Parent directory       | Filer only. Done, as the Open parent row.                                                                 |
| Ctrl-Shift-N    | New directory          | Filer only.                                                                                               |
| Ctrl-D          | Duplicate              | Text editors use Ctrl-D for Insert date instead.                                                          |
| Ctrl-I          | Get info               | Text editors use Ctrl-I for Italic instead.                                                               |
| Ctrl-Shift-F    | Toggle maximise        | Done.                                                                                                     |
| Ctrl-Shift-3    | Save framebuffer PNG   | Done: writes wuss-N.png to the current directory. Not on Emscripten.                                      |
| Ctrl-Shift-Esc  | Kill the focused task  | No unsaved-data prompt. Done.                                                                             |

Deferred, because no task has a function for them yet: Ctrl-N, Ctrl-G, Ctrl-Shift-G, Ctrl-E, Ctrl-, (opening Configuring only installs its icon), Ctrl-[ and Ctrl-], Ctrl-Shift-N (the filer has no make directory), Ctrl-D and Ctrl-I (the filer has Info only on the icon bar menu).

Dropped, because the guide already covers them: Ctrl-O (F2), Ctrl-F (F4), Ctrl-L (F5), Ctrl-Shift-S (F3 opens the save dialogue), Ctrl-Shift-Z and Ctrl-Y (F9), Ctrl-W (Ctrl-F2), Ctrl-/ (F1) and Ctrl-Backspace for trash (Ctrl-K).

## System shortcuts: window and task management

The guide is silent on these, so macOS chords lead. Each has a function-key fallback for Emscripten, where browsers reserve Ctrl-Q and Ctrl-Tab. The fallbacks use only F7, F10 and F11 plus their Shift and Ctrl forms, which the guide leaves free.

| Action                                           | Primary        | Fallback  | Notes                                                                                   |
| ------------------------------------------------ | -------------- | --------- | --------------------------------------------------------------------------------------- |
| Close window                                     | Ctrl-F2        | none      | RISC OS standard. Same path as the close icon (`wuss_window_try_close`).                |
| Quit app (close all windows of the focused task) | Ctrl-Q         | Ctrl-F9   | Goes through `wuss_window_try_close` for each window so unsaved-data prompts still run. |
| Quit Wuss                                        | Ctrl-Shift-Q   | none      | Replaces F4. Not on Emscripten.                                                         |
| Hide app (all its windows)                       | Ctrl-H         | F11       | Uses the hidden state (see below).                                                      |
| Hide others                                      | Ctrl-Shift-H   | Shift-F11 | Hides every task but the focused one.                                                   |
| Show all / restore                               | none           | Ctrl-F11  | Restores every hidden task; focus returns to the last one hidden.                       |
| Next window in app                               | Ctrl-\`        | F7        | Cycles the focused task's windows back to front.                                        |
| Previous window in app                           | Ctrl-Shift-\`  | Shift-F7  |                                                                                         |
| Next app                                         | Ctrl-Tab       | F10       | Raises the front window of the next task in recency order.                              |
| Previous app                                     | Ctrl-Shift-Tab | Shift-F10 |                                                                                         |
| Bring app to front                               | none           | Ctrl-F10  | Same effect as `wuss_task_raise_children`. No free macOS chord.                         |

## Changes to existing keys

- Debug and desktop keys moved off the RISC OS standard function keys:

  | Action               | Old      | New           |
  | -------------------- | -------- | ------------- |
  | Debug > Redraw       | F1       | Shift-F1      |
  | Debug > Garbage      | Shift-F1 | Ctrl-Shift-F1 |
  | Debug > Pixel Stress | F3       | Shift-F3      |
  | Zoom in              | F2       | Ctrl-=        |
  | Zoom out             | Shift-F2 | Ctrl--        |
  | Quit Wuss            | F4       | Ctrl-Shift-Q  |

- painting: Undo moved from Ctrl-Z to F8. Ctrl-Z is now Clear selection; Escape also drops the selection. Redo is on F9.
- Wuss menu shortcut labels use the guide's symbols: `^` for Ctrl and `WUSS_MENU_SHIFT` for Shift. Function key labels show plain, such as `F8`.

## Dispatch changes

- Done: `tasks_system_key` is a system layer in `main.c` ahead of `wuss_key`, matching only the window and task chords above plus Ctrl-F2, Ctrl-Shift-F and Ctrl-Shift-Q. An app cannot swallow them. The zoom and debug keys stay in `tasks_launcher_key`, so a task may claim them first.
- Standard editing and file keys (F1 to F9, Ctrl-C and friends) go to the focused task first. If it returns `result_WUSS_KEY_UNCLAIMED`, the launcher may handle them (Ctrl-= and Ctrl-- zoom the desktop).
- Done: writable icons handle Ctrl-A, Ctrl-C, Ctrl-X, Ctrl-V, Ctrl-Z and Insert, and Backspace and Delete remove the selection. The Copy key (End on RISC OS) is not wired, and Shift-arrows still move by word, so a selection comes only from Ctrl-A.
- **Caret owns Ctrl-D, Ctrl-I, Ctrl-K, Ctrl-T and Ctrl-B.** While a writable holds the caret (for example an inline rename in the filer), it swallows these item-level keys and the task never sees them, so Ctrl-D or Ctrl-I cannot trigger Duplicate or Get info mid-rename. Other Ctrl-letter keys such as Ctrl-R still reach the task, so a seed field does not block them. System-layer chords (Ctrl-F2, Ctrl-Q and so on) run before the caret and still work. Done in `icon/writable.c`.
- Do not route the system layer through menu shortcut labels. They need a focused window.
- The RISC OS frontend must pass F12 and its Shift and Ctrl forms to the OS untouched.

## Missing library support

- **Clipboard**: done. `wuss_clipboard_set`, `wuss_clipboard_get` and `wuss_clipboard_generation` hold the text, and the SDL frontend syncs it with the host clipboard around each key press. The RISC OS frontend has no host bridge.
- **Selection**: done for writables, as an anchor beside the caret index. There is no mouse drag, double-click or Shift-arrow selection yet.
- **Hide and restore**: done. `wuss_task_set_hidden` hides an app's windows (never the back stack, which holds the icon bar). `wuss_task_hide_app`, `wuss_task_hide_others` and `wuss_task_show_all` sit behind Ctrl-H, Ctrl-Shift-H and Ctrl-F11, and the F11 forms. Show all focuses the frontmost window rather than the last one hidden.
- **Cycle and raise**: done as `wuss_task_front`, `wuss_task_cycle_windows` and `wuss_cycle_tasks`, with no recency list. Next app raises the rearmost other app and previous app sends the front app behind the rest, so repeated presses walk the whole z-order. Only visible focusable regular windows take part, and an app is a task together with its child tasks.
- **Quit app**: done as `wuss_task_close_windows`.

Build order: every step is done except the deferred macOS fill-ins and the remaining function keys.
