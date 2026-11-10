# [Wuss](https://github.com/dpt/Wuss) > painting

Painting is a basic painting task in the `wuss` demo. It is also a worked example
of a task that draws into its own bitmap, captures the mouse during a stroke
and saves and loads PNG files.

The sources are in `apps/wuss/tasks/painting/`:

- `painting-doc.c`: the canvas, the drawing operations and undo. It has no
  wuss dependency, so `paintingdoc_test` runs headless.
- `painting.c`: the windows, the toolbox and input.
- `painting-new.c`: the New dialogue.

## Using it

Run `wuss -t Painting` (or pick Painting from the demo's icon bar) and click the
icon. The New dialogue opens above the icon bar. Menu > New... on the icon
bar opens the same dialogue.

- **Width** and **Height** are 1 to 2048 pixels, starting at 640 x 480. A
  value out of range is clamped when you click Create.
- **Background** is a row of swatches: the desktop palette, then
  transparent, shown as a checkerboard. It starts on the lightest colour.
  The pen starts in black or white, whichever shows on it.
- **Create** opens two windows: the canvas and a toolbox. ADJUST-click Create
  keeps the dialogue open, so you can open several canvases in a row.
  **Cancel** closes the dialogue.

Closing the canvas closes the toolbox and ends that instance. Each Create
makes an instance of its own.

- **SELECT** paints with the foreground colour, **ADJUST** with the
  background colour. Clicking a swatch in the option bar sets the foreground
  (SELECT) or the background (ADJUST). SELECT also selects the swatch.
- Each canvas has its own palette of up to 32 colours, which starts as the
  desktop's. Past 16 the swatches wrap to a second row. Menu > Palette has
  Add foreground (which selects the colour instead if the palette already
  holds it), Edit colour (the colour picker on the selected swatch),
  Remove colour, Desktop palette (start again from the desktop's) and Save
  palette, which writes a `.hex` file. Drop a `.hex` file on the canvas to
  load it as the palette.
- Tools: brush, wobbly brush, eraser (wipes to transparent), line, rect,
  filled rect, flood fill, spray, select (rectangle), lasso, magic wand and
  colour picker. Press `B` `K` `E` `L` `R` `X` `F` `P` `S` `O` `W` or `I` to
  pick one. Other keys pass on to the demo's
  hotkeys. The toolbox groups its buttons: selection tools at the top, then
  painting tools, then shapes, each outline beside its filled form. The
  slider sets the width, 1 to 32 pixels. For spray it sets the
  radius (3 pixels per unit).
- The wobbly brush draws an inked line whose two edges wander in and out
  on their own, swelling slowly and jittering finely, in the manner of
  Ellis Nadler's drawings. Each stroke wobbles differently.
- The paintbrush's **Dry** slider makes each stroke run out of paint. At 0%
  (the default) it never does. Above that, the stroke stays solid for the
  first half of its paint, then misses more and more pixels, like a dry
  brush, until it paints nothing. Higher settings run dry sooner: at 100% a
  stroke lasts about 40 pixels. Each new stroke starts with a full brush.
  The paint pot after the slider drains as a stroke uses its paint, shows
  empty once the stroke has run dry and fills again when the next stroke
  starts.
- The colour picker takes the foreground (SELECT) or the background
  (ADJUST) colour from the canvas pixel clicked.
- Spray scatters dots in a disc round the pointer. Dots build up while the
  button is held still.
- Select and lasso make a selection, shown with marching ants. The new
  selection replaces the old one. Hold Shift to add the new area to the
  selection instead, or Alt to take it away. A click without a drag drops
  the selection (with Shift or Alt it does nothing). Every drawing tool
  only changes selected pixels.
- The magic wand selects the pixels joined to the one clicked whose every
  channel is close to its colour. The slider sets how close: 1 takes exact
  matches only, and each step above adds 8 to the tolerance. Shift and Alt
  add and take away, as for select and lasso.
- Menu > Selection has Select all, Clear selection and Invert. `Esc` also
  clears the selection. Menu > Edit has Fill (`F`), which paints the selected
  pixels with the foreground colour, and Clear (`Delete`), which paints them
  with the background colour, each as one undo step. Clear selection, Fill, Clear, Undo and Redo are shaded when they do
  not apply, and update while the menu stays open. With nothing selected `F`
  picks the Fill tool instead.
- Line and rect show a rubber band while the button is down. They draw into
  the canvas when it is released.
- The pencil draws pixel-perfect lines one pixel wide: where a stroke turns
  a corner, leaving an L of three pixels, it puts the corner pixel back, so
  freehand diagonals stay one pixel thin. Untick **Pixel-perfect** on the
  size bar to keep every pixel.
- `F8` or Menu > Undo steps back, and `F9` or Menu > Redo steps forward
  again. There are 20 levels. A new edit drops the steps left to redo.
- Menu > Transform flips the whole canvas horizontally or vertically, or
  rotates it right, left or by 180°. It drops the selection. Rotating right
  or left swaps the canvas's width and height; Undo puts the old size back.
  Crop to selection cuts the canvas down to the selection's bounds. Canvas
  size opens a dialogue that grows or shrinks the canvas about its centre,
  filling any new area with the chosen background.
- Menu > Symmetry mirrors drawing about the canvas's centre: Horizontal
  copies it left to right, Vertical top to bottom, and both make four copies.
  Tiled shows the canvas with a copy on every side, and drawing that runs
  over an edge carries on from the far side, for seamless tiles. Both apply
  to the freehand tools, spray, line, shapes, paths and fill, each drawing as
  one undo step. The gradient and the selection tools ignore them, and a fill
  stops at the canvas's edge.
- The view zooms up to 3200%. From 600% up a grey grid outlines each
  canvas pixel.
- Menu > Save (`F3`) opens the Save As dialogue.
- Drop a PNG on the canvas window or on the Painting icon to load it.

## The canvas

The canvas is a `bgrx8888` bitmap of the size and colour the New dialogue
asks for. The window shows up to 400 x 300 of it and scrolls. Drawing is
opaque and hard-edged. There is no alpha channel because `screen_t` has no
span table for the alpha formats, so drawing straight onto one asserts.

Every operation in `painting-doc.h` takes canvas pixels and returns the box it
may have changed. The task passes that box to `wuss_window_invalidate`.

The selection is a `geom/region.h` region (`paintingdoc_select_*`). The lasso
fills its polygon with the even-odd rule, the wand uses `region_from_flood`
and Invert uses `region_invert`. Adding and taking away use `region_union`
and `region_subtract`.
`paintingdoc_selection_outline` gives the boundary boxes that the ants draw.

Undo keeps RLE-compressed copies of the whole canvas. A stroke takes one
`paintingdoc_checkpoint` when the button goes down. `paintingdoc_rect` and
`paintingdoc_fill` take their own. Symmetry repeats an operation with mirrored
or shifted points inside `paintingdoc_hold`, so only the first copy takes its
checkpoint.

## Loading

`paintingdoc_load` replaces the canvas with a PNG or JPEG and resizes it to
the image. Undo is dropped, as a saved step cannot apply to a canvas of another size. An image
with an alpha channel is composited over white. A paletted image is converted
to `bgrx8888`, which loses any palette transparency. On failure the canvas is
left as it was.

A dropped file that is not an image is tried as a `.hex` palette with
`palettes_load_hex`. The contents decide, not the name, as RISC OS has no
extensions.

## Open points

- There is no dirty tracking: closing the canvas never asks to save.
- The pointer is the same over the canvas and the toolbox. A per-window
  pointer shape would let the canvas show a crosshair.
