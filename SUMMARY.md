# Wuss 0.26.0

Unreleased. Changes since 0.25.0; see `CHANGELOG.md` for the full list.

## Breaking

- `wuss_MESSAGE_DATA_SAVE_ACK` carries a `wuss_data_save_ack_t`, not a bare
  path.
- Wuss needs DPTLib 0.10.0.

## New tasks

- **Texturing:** makes seamlessly tiling procedural textures from a node
  graph. Generators (Clouds, Marble, Wood, Cells, Stone, Weave, Veins,
  Checks, Gradient, Fill) feed Tone, Blend, Colourise and Effects nodes, and
  a Picture node takes a dropped image. There are 23 colour schemes, smooth
  and stepped, and presets after the RISC OS desktop textures. Mutate,
  Randomise and Reseed vary the result.

## Window manager

- **Node view:** a gadget that edits a DPTLib node graph.
- **RAM transfer:** tasks hand documents over as bitmaps, with no file.
  The image tasks send and take them.
- **Save As drops:** a Save As icon dropped on another task sends it the
  document.
- **Snapping:** options to stop windows snapping to the screen edges and
  the safe area, set in Configuring's new Windowing section.
- New calls: `wuss_gridview_set_min_width()` and
  `wuss_window_get_title_width()`.

## Other tasks

- Effecting's effects stack becomes reusable. Delete removes every selected
  effect, and the Edit row names the effect.
- Tagging has an icon.

## Fixes

- Scroll bar wells stay at least five pixels long.
- Windows that fit their content lose needless horizontal scroll bars.
- Gridview windows opened near the screen's right edge keep all their
  columns.
