# Wuss 0.25.0

Released 2026-11-12. Changes since 0.24.0; see `CHANGELOG.md` for the full
list.

## Breaking

- `wuss_pinboard_actor_click_t` and `wuss_pinboard_actor_drop_t` move the
  owner and id into an `actor` member.
- `wuss_iconpool_frames` and `wuss_task_iconpool_frames` are gone.
- The fixed size macros are gone: sizes come from `wuss_std_height()` and
  `wuss_std_frame_insets()`.
- Wuss needs DPTLib 0.9.0, and libjpeg through it.

## New tasks

- **Duckling:** up to seven ducklings and a Mama duck roam the backdrop as
  Pinboard actors. They chase the pointer, sleep and swim in up to four
  ponds, keeping out of one another's way.

## Painting

- **Layers:** up to 16 per canvas, each with visibility, opacity and a lock,
  managed in a layers bar.
- **Editing:** cut, copy and paste pixels, redo (F9), and fill or clear the
  selection.
- **Transforms:** flip, rotate, crop, trim, scale and resize the canvas, or
  flip and rotate the selection. Arrow keys nudge it.
- **Palette:** up to 32 colours per canvas, saved as a `.hex` file. ADJUST on
  a colour chip edits it in the colour picker.
- **Drawing:** symmetry, tiled mode, pencil sizes and nozzles, a
  pixel-perfect pencil, nice-size circles and a Dry paintbrush.
- **View:** zoom up to 64x with a pixel grid and a user grid. Tab hides and
  restores the toolbars, and Space-drag pans.
- The menu follows the menu standard.

## Window manager

- **Scale view:** a component for setting a percentage zoom, on the Display
  menu of Dithering, Effecting, Painting and Viewing. ADJUST applies it and
  keeps the dialogue open. Those tasks show the scale in their window
  titles.
- **Fonts:** desktop sizings derive from the current font, and dialogues
  re-lay themselves out when it changes.
- **Pinboard actors:** other tasks can show bitmaps on the backdrop, drawn by
  depth then nearest last.
- **Gridview:** small icons and full info display modes. Filing uses them and
  gains the rest of the RISC OS Filer's menu, shaded where unimplemented.
- **Icon pools** load sprite sheets.
- **Drags:** `wuss_drag_start_grab()` scrolls a window to follow the pointer.
- **Icon bar:** pops to the front when the pointer is held at the bottom of
  the screen.
- **Toolbars:** overrun toolbars and panes scroll near their ends.
- Disabled bitmap icons draw faded, and buttons leave two spaces either side
  of their label.
- New calls: `wuss_menu_dismiss()` and `wuss_drag_past_threshold()`.

## Other tasks

- Image tasks open JPEGs, and Filing and Pinboard pick icons by file type.
- Resizing gains Lanczos3 (the default) and five more resampling methods.
- Dithering's menu follows the menu standard: Animate strength becomes Pause
  (Space) and Show palette moves to the Palette submenu.
- Pinboard gains Display > Dithering and Selection > Reveal.
- Task Manager splits into Apps and Stats windows, groups child tasks under
  their parents and records lowest and highest counts.
- Orbing's controls move into a left-hand pane.
- Compositing swaps its animation for an Alpha slider and gains Display >
  Dithering.

## Fixes

- Text and frame captions draw transparently over patterned grounds.
- Panes grow back once there is room, and toggle-size picks grow or revert
  from the window's size.
- Image tasks keep a paletted PNG's transparency.
- Use after free fixes at quit, in menu pick flashes and the shared program
  info dialogue.
- Opening Display no longer crashes.
