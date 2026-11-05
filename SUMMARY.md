# Wuss 0.24.0

Released 2026-11-05. Changes since 0.23.0; see `CHANGELOG.md` for the full
list.

## Breaking

- The Patterns task is gone. Its blends are now in Gradient, under
  Menu > Shape > Two colour.
- The Tiling task is gone. Open an image in Viewing and pick Tile instead.
- Wuss now requires DPTLib 0.8.0.
- `BUILD_APPS` requires `WUSS_COMPONENTS`.

## New tasks

- **Pinboard:** owns the backdrop, with a menu to change its colour, pattern
  and image, and holds files pinned by drop or message.
- **Jottings:** Markdown notes, saved per user, which can be pinned to the
  backdrop. It has a full caret with word and line movement.
- **Tagging:** a centred cloud or columns of tags, sorted by name or count,
  with click to toggle a highlight.

## Window manager

- **Themes:** `resources/themes` sets the font, palette, titlebar height and
  icon pool, chosen in Configuring.
- **Backdrop:** `wuss_WINDOW_BACKDROP` makes a window the single backdrop, and
  `wuss_MESSAGE_PINBOARD_PIN` asks for a file to be pinned.
- **Drags:** `wuss_drag_start_bitmap()` and `wuss_drag_start_object()` carry an
  image, and `wuss_set_pointer_bounds()` locks the pointer to a drag's bounds.
- **Hourglass:** `wuss_hourglass_on()` and `wuss_hourglass_off()` show an
  hourglass, which the demo uses during slow work.
- **Gadgets:** `wuss_tagcloud` and `wuss_rubberband_t` are new, and Gridview
  uses the latter for rubber band selection. Sliders take detents.
- **New calls:** `wuss_window_scroll_by()`. `wuss_set_config()` now changes the
  titlebar height and body colours.
- **Windows:** dragged windows snap to the safe area's edges, and Shift-click
  on toggle-size fills only the safe area. A window with a resize icon but no
  scroll bars lets its content run down to the outline.

## Platforms

- **Browser:** the CRT effect works, drawn with WebGL, and now comes from
  DPTLib's `DPTLibCRT`.
- **SDL:** Alt-Return and `--fullscreen` go full screen at the display's
  resolution, and Retina displays are supported.
- **RISC OS:** a selector changes screen mode between 1 and 32 bpp, applied
  live.

## Dithering and palettes

- Palette > Computed gains Wu, and its Optimal row is now called Octree.
- Palette > Fixed gains Current, with a warning when dithering to the screen's
  palette.
- Double-click a swatch in the Target palette window to edit that colour, and
  the image re-dithers live.
- Linear light applies to the ordered methods.
- A top level Display submenu sets screen dithering, in Resizing too.
- Palette selects and edits swatches.
- The colour picker dithers its swatches with patterns.

## Other tasks

- Viewing scrolls with the arrow keys, Page Up/Down and Home/End. Space and
  Shift-Space step through the PNGs in the directory.
- Task Manager gains a window of allocation statistics, counts of apps, tasks
  and windows, and a scrolling list of task names.
- Painting gains a Stabilise setting, up to 50 pixels. The pencil is always
  one pixel wide.
- Filing gains a selection row in the menu, with Delete.
- Effecting has an icon for each effect, and its Level slider has detents at
  0% and 100%.
- Reading renders with DPTLib's `mdtext`.
- Shortcuts lists the keys of every task, more compactly.
- AppsFiler hides the System folder. Configuring, Pooling and Jottings move to
  Utilities.
- Task names are capitalised.

## Fixes

- Dithering keeps the palette when the method changes, so edits survive.
- A handler that destroys another task during a broadcast no longer leaves the
  broadcast using the freed task.
- Saved snapshots use a real clip, so Chars no longer saves a blank PNG.
- Effecting and Chars take the focus, so F3 works in them.
- Saturn and Fern resize.
- Fixed palette leaks in the icon pool, SDL present and Doughnut.
- Menu row text stays legible on an inverted palette.
- The icon bar scrolls only when the pointer is directly over the bar.
- A slider's active area matches its groove.
- WussTest builds in every subsystem variant, and the demo and core build
  without the icon bar.
- RISC OS: mouse bounds reset on a mode change, and Jottings and Pinboard link.
- Compositing's images are restored.

## Internals

- `BUILD_APPS` turns on DPTLib's `DPTLIB_ALLOC_COUNT`, sending all allocation
  by Wuss, DPTLib and the demo through it.
- Menus allocate fewer blocks.
- Divisions are gone from Gradient, Painting's ants and spray, Saturn and
  Checker, for targets without a hardware divide.
- Marching ants redraw only around their outline, and toggle-size and toolbar
  moves blit rather than repaint.
- Tasks share more code: `common/confdlg`, `taskapp_instance_t` and Save in
  taskapp.
