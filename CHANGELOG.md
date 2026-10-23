# Changelog

All notable changes to Wuss are recorded here. Wuss was split out of
[DPTLib](https://github.com/dpt/DPTLib); entries before the split were
carried over from DPTLib's changelog.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [0.23.0] - 2026-10-23

### Added

- `wuss_colourpicker`: a true-colour picker dialogue (RGB, CMYK, HSL, HSV or
  OKLab, old and new swatches, hex field) reporting every change live.
  Orbing's Light colour... (C) uses it.
- `wuss_window_attach()` / `wuss_window_detach()`: toolbar windows attached
  to a parent's left, right, top or bottom edge. The group moves, restacks,
  hides, shows and closes together; a click on a toolbar gives the focus to
  its parent. Painting's toolbox splits into a left-edge tool bar and a
  bottom-edge option bar holding the colours, the desktop palette and the
  size slider.
- `wuss_swatches_t` gadget: a grid of flat colour cells with hit testing,
  used by Palette and Painting.
- Window placement near the pointer (`wuss_PLACE_POINTER`) within a safe
  area (the screen inset by 8px at the left, top and right and 47px at the
  bottom). Windows too large for the area shrink, `wuss_place_t::extra`
  reserves room for attached toolbars and `wuss_window_place()` re-places an
  open window. `wuss_iconbar_window_open()` opens a dialogue where an icon
  bar menu would open. Dithering, Effecting and Resizing now open their
  window with the first image, sized to it, rather than growing a
  placeholder.
- `wuss_set_font()` replaces a font slot mid-session, re-deriving the
  titlebar height and relaying out windows. Configuring gains a Font frame
  to set the system font.
- `wuss_colourset_set_rgb()`: colour set fields show an exact 24-bit
  colour, not just the nearest palette entry.
- `wuss_dialogue_handle_pre_close()`: runs a dialogue's Cancel action (or
  hides it) and vetoes the close.
- `wuss_FONTMENU_FAMILIES`: `wuss_fontmenu` lists one item per font family.
  Reading picks families and draws Markdown bold in the family's Bold face.
- `wuss` DataOpen: Filing broadcasts `wuss_MESSAGE_DATA_OPEN` for a
  double-clicked file. A taskapp listing the type in its desc's `opens`
  claims it; otherwise Filing starts the app through its run function
  (`filing_set_run_fn()`) and broadcasts again. Open directories show the
  new `file_diro` glyph.
- `wuss` Sudoku task: a compact 9x9 game with unique-solution puzzles.
- `wuss` Shortcuts app: lists system keyboard shortcuts under bold group
  headings.
- Painting: a colour picker tool (I), a Dithering submenu, Ctrl-A select
  all, Ctrl-I invert selection and the N nozzle shortcut on its menu row.
- Dithering: Strength and Colours become slider dialogues (Colours
  logarithmic, 2 to 256). New BW, BRYW and CMYKW palettes; CMYK loses white.
  Effecting gains a Dithering submenu, shared with Viewing, Orbing and
  Painting as `dithermenu_t` (`tasks/common/dithermenu.h`).
- The SDL frontend draws a 16px border (`WUSS_SDL_BORDER`) around the
  screen.
- `docs/windowing/menu-standard.md`: order, separators, labels and reserved
  keys for task window menus. Every task's menu now follows it: Save PNG
  becomes Save on F3, lone Background rows move into a Colours submenu and
  dividers mark a change in the kind of row.
- `wuss_EVENT_CLOSE_ADJUST`: ADJUST on a close icon now tells the window's
  task, with the modifier keys held (`data.close_adjust.modifiers`), and
  leaves the window open (it used to start a move drag). The filer uses it
  as RISC OS does: it opens the parent directory's window in the current
  one's place (or brings it to the front) and closes the current one; with
  Shift held the parent cascades instead and the current one stays. The
  demo's Apps filer does the same between a category and the root, and a
  double ADJUST on a category opens it in the root's place. Frontends report
  held modifiers with the new `wuss_set_modifiers()` before each
  `wuss_mouse_click()`; the demo's SDL and RISC OS frontends do so, and
  script button names take `MOD+` prefixes (`click shift+adjust X Y`).
- `wuss_window_create_at()` opens a window where a `wuss_place_t` says: in
  the first free region (`wuss_PLACE_SPARE`) with a fallback, centred or
  against any of the nine screen anchors (`wuss_ALIGN_*`), or offset from
  another window, optionally keeping clear of the icon bar
  (`wuss_PLACE_AVOID_ICONBAR`). `wuss_window_create_placed()` is now a
  wrapper around it.
- `wuss` private icon pools: a task may load its own pool
  (`wuss_task_iconpool_load()`, `wuss_task_iconpool_load_resource()`, from
  `resources/wuss/tasks/<task name>/iconpool/`) and look names up with
  `wuss_task_iconpool_find()`. Icons named by `u.bitmap.name` / `pressed_name`
  search the task's pool first, then the shared one. Name-resolved icons now
  store bitmap pointers; `wuss_icon_spec_t` gains `u.bitmap.pressed_image`.
  Minesweeper's `mine` and `flag` icons move to its private pool.
- `wuss` Orbing task: a shaded sphere with draggable point lights (marker
  handles, a Light menu, per-light colour via `wuss_colourset`, highlight
  size/sharpness sliders, Randomise, arrow-key light nudging, a Dithering
  submenu drawing via `screen_copy_bitmap_dithered()`), laid out in Sphere/
  Light frames with the control strip lined up in columns.
- `wuss_colourset` gadget — a row of colour swatches (one active pick, an
  optional title) built on `wuss_colourmenu`; `wuss_colourset_button_width()`
  and `wuss_STD_GLYPH_SIZE` size it, and `wuss_stringset`/`wuss_colourset`
  both take an optional title width.
- `wuss_menu_open_at_pointer()` and `wuss_colourmenu`'s RGB open/pick
  helpers factor out boilerplate the per-task menu code kept repeating.
- Nearly every `wuss` demo task gains menu rows and keyboard shortcuts it
  previously lacked: Save PNG, Pause, Clear/Reset, arrow-key and Space
  controls, colour/pattern/mode submenus (Background colour, Ink/Paper,
  Rule, Shape, Model, Alignment, Digital clock mode, chars Unicode Page
  paging, Lissajous Ratio, doughnut Tube, curve Type/Hull, greeble Size
  presets/Palette, minesweeper Difficulty/Give Up/chording/best-times,
  particles Gravity/Walls/emitters, porter-duff Swap images) — see
  individual task headers for the full list. `WUSS_MENU_ITEM_SHORTCUT`
  renders a shortcut label (Shift-prefixed, `^`-prefixed for Ctrl, drawn in
  muted grey rounded keycaps) on a menu row, and task key handlers now
  dispatch through those same shortcut labels instead of a parallel
  hand-rolled switch; Ctrl/Alt-modified keys are ignored by task handlers.
  F1-F3 driver hotkeys move into a Debug launcher submenu; F4 quits via the
  launcher menu's shortcut on every frontend including RISC OS.
- `wuss` Particles task: emitters (added via menu), pointer-trailing
  particles, Gravity/Walls toggles, Space/C keys.
- `wuss` CRT post-effect for the SDL frontend, and an optional software
  mouse pointer.
- The Display task also picks colour depth (not just resolution), gains
  Cancel/Change buttons (closing the task as RISC OS does) and offers
  480x360; mode changes move windows back on-screen instead of shrinking
  them. 1bpp/2bpp modes use a grey-ramp screen palette.
- `wuss_set_config()` — swap a wuss instance's `wuss_config_t` mid-session.
- `wuss_window_set_title()`.
- `wuss` inter-task messaging: fixed-size queued messages (`wuss_send`,
  recorded sends, acknowledge) with point-to-point and broadcast
  addressing, a bounce for unacknowledged recorded messages and delivery
  deferred to dispatch depth 0. Destroying a task purges its queue.
- `wuss` double-click detection (`wuss_BUTTON_DOUBLE`, driven by a
  per-frame `wuss_set_time()` clock). Double-click time and distance and
  the drag threshold live in `wuss_config_t`; the new `wuss_get_config()`
  reads the config back. The Configure task gains a Timing frame to edit
  them live.
- `wuss` core drag session: `wuss_drag_start()` begins a hotspot-anchored
  marching-ants drag that owns all input until it ends, delivering
  `wuss_EVENT_DRAG_END` with the drop window and the button that started
  it; Escape cancels. The SDL frontend captures the mouse for the drag's
  duration. `wuss_ICON_TYPE_DRAGGABLE` is implemented: a bitmap icon that
  starts a drag once the pointer clears the drag threshold, and behaves as
  an ordinary press otherwise.
- `wuss_gridview` gadget — a reflowing glyph-and-label grid with RISC OS
  selection rules, double-click activation and ellipsis-truncated labels;
  `wuss_gridview_get_cell_size()` reports its cell size.
- `wuss_saveas` component — a RISC OS-style drag-to-save dialogue (file
  icon showing `file_<type>` from the icon set, leafname writable,
  Cancel/Save) driving the DataSave protocol against the drop window, or
  saving directly when the writable holds a full path. ADJUST keeps it
  open for repeat saves. Laid out with `geom/stack`.
  `wuss_menu_open_window()` opens a caller's window as a menu chain of its
  own, so a Save As can be shown as a submenu or standalone. Every demo
  task's Save PNG row and ^S shortcut now open a Save As rather than
  writing a fixed filename.
- `wuss` Filer task — a RISC OS-style directory viewer and drag-save
  target, opened from the icon bar or the System menu; double-click opens
  subdirectories.
- `wuss` RISC OS-style icon bar: a pinned, chromeless window along the
  bottom of the screen (`wuss_WINDOW_PINNED`) holding
  `wuss_iconbar_icon_t` icons whose clicks reach their task as
  `wuss_EVENT_ICON`.
- `wuss` windows live in one of three z-order stacks — top, middle and back
  (`wuss_WINDOW_STACK_TOP`/`_BACK` at creation, `wuss_window_set_stack()`
  later). Restacking acts within a window's own stack. Menus sit in the top
  stack and the icon bar in the back.
- `wuss_menu_disable_item_live()`, `wuss_icon_set_disabled()` and
  `wuss_menu_set_title_live()` shade rows and retitle a menu while it is
  open, e.g. after an ADJUST pick.
- `wuss --script` drives the demo from a script file of mouse, key, text
  and wait commands; `dump` prints every window's boxes, focus and hidden
  state, and `png` saves the framebuffer. Run with
  `SDL_VIDEODRIVER=dummy` for headless use.
- `wuss -f`/`--font` chooses the system font family; the bold slot comes
  from `bmfontfamily_heavier()`.
- Particles task: repeller and attractor forces (Add submenu) with a shared
  Strength dialogue whose Range slider sets the force's reach, and a
  Markers toggle (M) drawing each force's range.
- Ball task: Select grabs a ball and throws it on release; Menu > Add ball
  (A) adds one. Wall bounces lose a quarter of the speed.
- `wuss_ICON_TYPE_DISPLAY` leaves the reserved set: `wuss_icon_spec_display()`
  now builds one rather than a grooved LABEL. It draws via the label
  renderer. The icons demo gains a Display fields group.
- Gadget tier (`WUSS_GADGETS`) between menus and components for compound
  in-window widgets. First gadget: `wuss_stringset`, a display field plus a
  pop-up arrow (`gright`/`gright-p`, or an ACTION ">" fallback) opening a
  menu of fixed strings. Tasks forward ICON, MENU_SELECT and MENU_CLOSED
  events to `wuss_stringset_handle_event()`.
- Bitmap icons gain `pressed_set`, an icon-set entry drawn while the icon is
  held.
- The text task loads its samples from `.md`/`.txt` files under
  `resources/text`, sorted by leafname, with an "About Wuss" introduction as
  the default. Window auto-sizing gains an "Auto-size" menu entry.
- `wuss` opens the Configure window before `--tasks` windows so task
  windows stack in front of it.
- `wuss_ICON_TYPE_SLIDER` — horizontal/vertical slider icon: bevelled surround
  with a sunken groove inset by a fixed gap, drag/click value mapping that
  rounds to the nearest value rather than truncating, and a min > max
  reversed-fill mode. `wuss_icon_get_value()` / `wuss_icon_set_value()` read
  and write it.
- `wuss/icon-spec.h` — `wuss_icon_spec_label()` / `_slider()` / `_action()`
  fill a `wuss_icon_spec_t` for the common cases, replacing manual
  field-by-field assignment (following the `create-from-desc.c` helper
  precedent).
- `wuss_STD_GAP` / `wuss_STD_INSET` / `wuss_STD_SLIDER_HEIGHT` /
  `wuss_STD_SECONDARY_BUTTON_HEIGHT` / `wuss_STD_PRIMARY_BUTTON_HEIGHT` —
  standard sibling-gap, edge-inset and icon-height constants in
  `wuss/icon.h`, generalised out of what was local to `saturn.c`.
- The saturn task gets a Colours menu (Foreground/Background via
  `wuss_colourmenu_t`) and a Size... submenu — a hover-opened dialogue
  (`wuss_menu_item_t::window`) with a slider for the sketch size and a
  second slider for the stars-loop iteration count, Apply/Cancel/
  Adjust-Apply/Adjust-Cancel following RISC OS convention.
- The `wuss` chars task draws a dotted baseline rule and a light-blue
  advance-width rule under each glyph (useful for eyeballing advance-width
  drift), trims contiguous leading/trailing blank grid rows, and gains a
  `wuss_STD_INSET` margin.
- `wuss_EVENT_POINTER_ENTER` / `wuss_EVENT_POINTER_EXIT` — window-scoped
  events fired when the pointer crosses onto or off a window's whole
  on-screen footprint (content or furniture alike); exactly one ENTER is
  outstanding per window, always balanced by a later EXIT.
- `wuss_text_draw()` / `wuss_text_measure()` — public wrappers exposing the
  existing internal `bmfont_draw`/`bmfont_measure` pass-throughs, so a
  client task can draw/measure text in a wuss system font without including
  `framebuf/bmfont.h`.
- The minesweeper task gains a Grid Size submenu (24x24 down to 12x12,
  radio-ticked to the current size); board dimensions move from
  compile-time defines to runtime task fields.
- The image task gains a Background colour menu (`wuss_colourmenu_t`) for
  the fill behind the 9-patch border band; `wuss_MENU_ITEM_BORROWED_SUBMENU`
  marks a submenu `wuss_menu_destroy()` should not recurse into, and
  `wuss_NO_BACKDROP` replaces the `wuss_BACKDROP_COLOUR(wuss_NO_BACKGROUND)`
  idiom.
- `wuss_menu_create_from_desc()`'s `!`/`~` (tick/shade) prefixes now
  optionally pull a bool vararg at the point they're scanned instead of
  always applying, so a descriptor can encode live state (e.g. a toggle's
  current value) directly.
- `wuss_menu_tick_exclusive()` / `wuss_menu_tick_item()` /
  `wuss_menu_tick_set_live()` — data-level tick setters (for editing menu
  items before a (re)open) completing the existing live-chain setter family
  (`wuss_menu_tick_exclusive_live`, `wuss_menu_tick_item_live`, renamed from
  `wuss_menu_set_ticked`/`wuss_menu_set_item_ticked`).
- Shift-F1 on the RISC OS frontend now forces `wuss_INPUT_GARBAGE`
  (single-frame screen corruption), matching the existing SDL frontend and
  `frontend.h`'s documented F1/Shift-F1 intent.
- Six reserved `wuss_ICON_TYPE_*` constants (`DISPLAY`, `WRITABLE`,
  `NUMBER`, `STRING_SET`, `SLIDER`, `DRAGGABLE`) after `RULE`, validated
  from a spec but (bar SLIDER and WRITABLE, now implemented) not yet
  drawn/hit-tested/routed.
- `wuss/menu.h` gains `wuss_menu_should_keep_open(ev)` (the "ADJUST keeps
  the chain open, SELECT has already freed it" predicate) and
  `wuss_menu_open_ticked(task, menu, ticks[], at, out)` (sets each row's
  TICKED bit from a bool array, then opens), factoring the open/close
  boilerplate tasks hand-rolled.
- `wuss_menu_set_item_ticked()` — updates a single menu row's tick in
  place on an ADJUST pick, for a menu tracking more than one independent
  tick, without reopening or repositioning the chain.
- `wuss_window_set_doc()` — change a window's virtual document extent after
  creation (used when cycling to a differently-sized image).
- New `wuss` demo tasks: **Minesweeper** (12x12/20-mine board, flood-fill
  reveal, flags on Adjust, mine counter and elapsed timer HUD) and
  **greeble** (an edge-matched tile-placement greebling pattern built from
  205 baked artist stamps, prefab blocks scattered largest-first, a random
  4-colour palette per pattern, Adjust cycles the base palette).
- `wuss` text task gains a Sample submenu (pangrams plus the Lorem Ipsum
  default) and moves the font picker under a root MENU.
- The `wuss` SDL frontend tracks an integer device-pixel scale in
  `struct wuss_frontend`, opening at 2x; F2 / Shift-F2 step it, clamped to
  `[1, 4]`, and the window is sized from `scr_width/height * scale` so
  repeated halving can't drift it off the grid.
- A custom Emscripten shell page for the `wuss` demo (`--shell-file`),
  replacing the default boilerplate with a title, control-key legend and
  about blurb; `LINK_DEPENDS` on it so editing the shell relinks.
- An Emscripten/WebAssembly build of the interactive `wuss` demo. The
  run-loop is extracted into `wuss_frame(void *)` over a `wuss_frame_ctx`,
  driven by `emscripten_set_main_loop_arg` in the browser while desktop and
  RISC OS still spin it directly. CMake's `EMSCRIPTEN` branch uses the
  bundled `-sUSE_LIBPNG=1` port instead of `find_package(PNG)`, links
  Emscripten's SDL3 port (`-sUSE_SDL=3`), and preloads `resources/` into
  `wuss.data` mounted at MEMFS `/resources`; the target emits `wuss.html`.
  A GitHub Actions Pages workflow builds and deploys the demo on pushes to
  `master` and `develop`.
- `wuss/component/fontmenu.h` and `wuss/component/colourmenu.h` — shared
  task components (the RISC OS Toolbox analogue), gated on a new
  `WUSS_COMPONENTS` CMake option (implies `WUSS_MENUS`). `wuss_fontmenu_create()`
  builds a flat, name-sorted `wuss_menu_t` from the bitmap fonts in a
  directory (via `bmfont_enumerate()`); `wuss_fontmenu_selected()` recovers
  the picked font name from a `wuss_EVENT_MENU_SELECT`, and
  `wuss_fontmenu_set_ticked(fm, index)` mutates the component's own items to
  tick one row (index -1 clears every tick). `wuss_colourmenu_create()`
  builds a swatch-row menu, one row per system-palette entry labelled
  `#RRGGBB`, and resolves a pick back to the palette index. Both components
  route every allocation through the caller's `wuss_alloc_t` hooks (copied
  into the handle, not a borrowed `wuss_t *`, so they can safely outlive
  `wuss_destroy()`).
- `wuss_menu_item_t` / icon rows gain an optional colour chip
  (`wuss_MENU_ITEM_SWATCH` / `wuss_ICON_FLAGS_SWATCH` plus a `wuss_colour_t
  swatch` field), drawn in the left gutter where the tick sits; a chip wins
  over a selected tick.
- Font slot 2 (`WUSS_SYMBOL_FONT`) is now consulted to draw the menu
  selection tick and submenu arrow as glyphs (`WUSS_GLYPH_TICK` `'*'` /
  `WUSS_GLYPH_SUBMENU` `'>'`) instead of vector strokes, when a font is
  present in that slot.
- `wuss_create()`'s `fonts` array elements are now tagged with a
  `wuss_font_class_t` and a borrowed leafname (`wuss_font_desc_t`), letting
  a picker such as `wuss_fontmenu` and the system-decoration font (menu
  ticks/arrows) be identified by class rather than array position.
  `wuss_FONT_CLASS_SYSTEM` marks a chrome/decoration-only slot.
- `wuss_icon_plot()` — validates an icon spec exactly as `wuss_icon_create()`
  does and draws it once through the window manager's screen, retaining
  nothing; for static content a task can redraw from its own model without
  a live icon per element.
- `wuss_window_invalidate_extent()` — marks a window's whole virtual
  document extent dirty (`window->doc`), not just the visible content
  rectangle `wuss_window_invalidate_visible()` covers, so a change touching the
  whole document repaints correctly at any scroll position.
- Clicking in a scrollbar well (not on the sausage) now pages the content
  one visible extent towards the click, keeping one `WUSS_SCROLL_STEP` of
  overlap, RISC OS style.
- Symbolic `wuss_colour_t` values (`wuss/wuss.h`). A raw `wuss_colour_t` is a
  palette index, `0..127`; `wuss_COLOUR_SYMBOLIC` (128) and up name roles the
  window manager resolves to a concrete index. `wuss_COLOUR_BLACK`,
  `_WHITE`, `_RED`, `_GREEN`, `_BLUE`, `_YELLOW`, `_CYAN`, `_MAGENTA`,
  `_GREY` pick the nearest system-palette entry to the named RGB;
  `wuss_COLOUR_TITLE_BG` / `_TITLE_FG` / `_BUTTON_HILIGHT` / `_BUTTON_SHADOW`
  / `_ACCENT_BG` / `_ACCENT_FG` / `_BACKDROP` echo the matching
  `wuss_config_t` field. Accepted anywhere a `wuss_colour_t` is taken —
  config furniture/bevel/accent/backdrop, `wuss_window_create()` /
  `wuss_window_set_background()` backgrounds, icon specs — and resolved once
  when the value is stored, so draw code and `wuss_nearest_colour()` are
  unaffected. Resolutions are recomputed on `wuss_set_palette()` and
  `wuss_set_backdrop()`.
- `wuss/menu.h` (new `WUSS_MENUS` option, implies `WUSS_ICONS`) — RISC OS-style
  pop-up menus. `wuss_menu_open()` shows a caller-owned, immutable
  `wuss_menu_t` (title plus an array of `wuss_menu_item_t`) as a borderless
  window, nudged to stay on screen and opened under the pointer; wuss owns
  layout, submenu chaining on hover and whole-chain dismissal. Per-item flags
  cover ticks, disabled rows, dashed separators and submenus. A leaf pick is
  delivered to the opening task as `wuss_EVENT_MENU_SELECT` — SELECT closes
  the chain, ADJUST keeps it open. `wuss_menu_close()` / `wuss_menu_is_open()`
  manage a chain by handle. An over-tall menu gets a real vertical scrollbar
  instead of being cropped.
- `wuss_menu_create_from_desc()` / `wuss_menu_destroy()` — build a heap
  `wuss_menu_t` tree from a compact descriptor string (PrivateEye's
  `menu_create_from_desc` syntax: `,` between items, leading `|` for a dashed
  separator, `{ ... }` submenus, `!` tick, `~` shade, `>` and `%s` varargs).
  The whole tree is one owned allocation graph freed by `wuss_menu_destroy()`.
- `wuss_menu_item_t::window` — a menu row may carry a caller-owned
  `wuss_window_t` instead of a `submenu` (the two are mutually exclusive).
  Hovering the row shows that window where a submenu would open, using the same
  anchor maths; leaving the row, a click outside, a leaf SELECT elsewhere or
  `wuss_destroy()` hide it again rather than close it, so the same handle is
  reused on the next hover. Create the window with `wuss_WINDOW_HIDDEN`.
- `wuss_window_set_hidden()` and the `wuss_WINDOW_HIDDEN` create flag — a hidden
  window keeps its z-order slot but is not drawn, not hit-tested and occludes
  nothing. `wuss_window_move()` still works on it (translate only, no blit) so
  it can be parked and re-shown in position. Revealing one fires a veto-able
  `wuss_EVENT_PRE_SHOW` then `wuss_EVENT_SHOW`; `wuss_window_set_hidden()`
  returns `result_t` accordingly.
- `wuss_set_palette()` — swap the system palette mid-session. Copies the new
  palette in, refreshes the cached nearest-black/white indices, broadcasts a
  new `wuss_EVENT_PALETTE` to every registered task so it can recache
  `wuss_nearest_colour()` selections, then invalidates the whole screen.
  Length must match `wuss_create()`'s; a now-out-of-range furniture/bevel/
  backdrop index is rejected with the palette left unchanged.
- `wuss_nearest_colour()` — the system-palette index closest to an RGB value by
  squared Euclidean distance, ties to the lower index.
- `wuss_alloc_t` and the `wuss_alloc` stdlib default — pluggable malloc/realloc/
  free hooks. `wuss_create()` takes a new `const wuss_alloc_t *` argument
  (NULL selects `wuss_alloc`); every heap block a `wuss_t` owns goes through
  the hooks.
- `wuss/icon.h` — work-area icons drawn inside a window's content area, in
  virtual document space so they scroll with the content. Wuss hit-tests
  interactive icons before the content task sees a click and delivers them as
  `wuss_EVENT_ICON`; labels and hidden or disabled icons fall through as
  `wuss_EVENT_MOUSE`. Icon types: `LABEL` (with `JUSTIFY_RIGHT` / `_CENTRE`
  flags), bevelled `BUTTON` (`DEFAULT` flag draws it as the default action
  button), `RADIO` and `OPTION` latching buttons (radios with a non-zero
  `group` are mutually exclusive), `FRAME` grouping box, `BITMAP` (a
  caller-owned image, hit-tested only with the `INTERACTIVE` flag), `PATTERN`
  swatch, `MENU_ENTRY` and inert `RULE` rows.
- `wuss_icon_get_selected()` / `wuss_icon_set_selected()` — query and set a
  radio or option icon's latched state; setting a grouped radio clears the
  others in its group. No task event — the programmatic path.
- `wuss_icon_create_array()` — creates a batch of icons from a spec array with
  all-or-nothing rollback: on the first failure any icons already created by
  the call are destroyed and no handles are written.
- `wuss_window_create_placed()` — creates a window from a content size instead
  of a box, letting Wuss pack it (furniture included) into the first free
  screen region. Successive auto-placed windows tile; placement cascades when
  no region fits. The slot is released on close and on the first
  `wuss_window_move()` / `wuss_window_resize()`. An overall screen margin is
  kept around all auto-placed windows.
- `wuss_ICON_TYPE_PATTERN` — a non-interactive work-area icon whose bbox is
  filled with a `screen_fill_pattern()` pattern in fg/bg, aligned to document
  space. Clicks fall through as `wuss_EVENT_MOUSE`; disabled swatches fold fg
  into bg.
- `WUSS_FURNITURE` and `WUSS_ICONS` CMake options (both default ON) drop the
  furniture/*.c and icon/*.c files and `#ifdef`-guard every core call site,
  struct field and helper that references them. With `WUSS_FURNITURE` off every
  window is chromeless (content box == visible box); with `WUSS_ICONS` off the
  `wuss_icon_*` API is not compiled. Programmatic and wheel scrolling survive
  either off via the new core `scroll-step.c`.
- The `wuss` SDL demo accepts `-d`/`--depth 1` and `--depth 2`, selecting a
  `pixelfmt_p1` (stride `(width + 7) / 8`) or `pixelfmt_p2` (stride
  `(width + 3) / 4`) framebuffer alongside the existing 4 and 32.
- The `wuss` curve task draws the active control points' convex hull
  (Andrew's monotone chain) as a closed light-grey polyline before the
  curve and blobs.
- The `wuss` gradient task cycles its ordered-dither matrix through 2x2,
  4x4 and 8x8 on SELECT / ADJUST clicks; each matrix cell maps to a fixed
  -8..+8 offset so a larger matrix gives a finer pattern, not a noisier
  one (4x4 output unchanged). The current size is drawn top-left.
- The `wuss` text task's menu gains a Spacing submenu (Normal/Letter/Word/
  Letter+Word presets), Foreground/Background colourmenus
  (`wuss_colourmenu_t`) and a tickable No Background entry that draws
  glyphs with a transparent background instead of the picked colour.
- The `wuss` demo launcher's Launch submenu is split into Games/Tests/
  Utilities/Visuals category submenus hung directly off the top-level task
  menu.
- The saturn task's Configure dialogue gains a Default button that resets
  `task->config` to `SATURN_CONFIG_DEFAULT`, refills the sliders and
  applies it.
- `wuss_ICON_TYPE_WRITABLE` — an editable single-line text field with an
  icon-owned fixed-size buffer. A click places a red I-beam caret at the
  nearest character (one caret per `wuss`, dropped on focus loss, hide,
  delete or window close). The field takes printable Latin-1, Backspace,
  Delete, Left/Right, Home/End, Shift+Left/Right (by word), Ctrl+Left/Right
  (start/end) and Ctrl+U (clear); Tab/Shift-Tab cycle through the window's
  writables. Text scrolls to keep the caret in view and every edit raises
  `wuss_EVENT_ICON`. Adds `wuss_icon_set_caret()`, the
  `wuss_icon_spec_writable()` helper and a demo group in the icons task.
- `wuss` input focus and key events: windows opt in with
  `wuss_WINDOW_FOCUSABLE`; a Select/Adjust press on a focusable window's
  content takes the focus, and `wuss_set_focus()`/`wuss_get_focus()`
  manage it directly. Focus changes send `wuss_EVENT_GAIN_FOCUS`/
  `LOSE_FOCUS` and tint the titlebar with the new `title.focus_bg` palette
  entry. `wuss_key()` routes presses to the focused window as
  `wuss_EVENT_KEY` (code plus Shift/Ctrl/Alt modifiers); a task returns
  `result_WUSS_KEY_UNCLAIMED` to pass a key on. The SDL demo translates
  key-down and text-input events, runs its F1-F4 hotkeys only for
  unclaimed keys and adds a Keys test task.
- `wuss_STD_INSETS`, `wuss_STD_FRAME_INSET`/`_TOP_INSET` and
  `wuss_STD_FRAME_INSETS` standard padding constants,
  `wuss_SYSTEM_PALETTE_LENGTH`, `wuss_ICON_FLAGS_JUSTIFY_LEFT` (0, for
  explicitness) and `wuss_ICON_BORDER_PLAIN` (a 1px fg-colour outline).
- `wuss_icon_spec_frame()`/`_option()`/`_radio()`/`_display()` helpers;
  `_display()` is a label with a groove border (the RISC OS display-field
  look).
- `wuss_MENU_ITEM_PRE_OPEN`: a flagged submenu or window row fires
  `wuss_EVENT_PRE_SUBMENU_OPEN`/`wuss_EVENT_PRE_SHOW` on hover and opens
  only if the handler calls the matching open-now function, letting a task
  retarget one shared submenu per row. `WUSS_MENU_ITEM`/`_MENU`/`_WINDOW`
  and `WUSS_MENU_TITLE` macros build menu tables one call per item.
- The colourmenu gains an optional None row (hatched chip, resolving to
  `wuss_NO_BACKGROUND`), toggled at runtime with `wuss_colourmenu_set_none()`.
- `wuss_resize()` and `wuss_frontend_resize()` change the desktop
  resolution live, shrinking/nudging open windows back on-screen (RISC OS
  returns `result_NOT_SUPPORTED`). A new Display task picks from fixed
  resolutions, under a new top-level System menu alongside Palette.
- `wuss_backdrop_t` gains an `image` drawn centred over the backdrop fill;
  the demo app shows `resources/wuss/wuss.png`.
- Window titles are drawn with a `bmfont_draw_relief()` drop shadow in the
  bevel-dark colour.
- Scroll arrows, the resize icon, toggle-size, close and back icons light
  up while held, in a new `wuss_furniture_palette_t.pressed` colour
  (yellow by default).
- `wuss_icons_load_resource()`; `wuss_create()` now loads the icon set once
  itself instead of per task.
- A Configure task (also on the main Wuss menu): System frame options to
  swap the Menu/Adjust mouse buttons and reverse scroll-wheel direction,
  and a Backdrop frame with foreground/background colours, a fill-pattern
  grid, a result swatch and a Set backdrop action, laid out with
  `geom/stack`.
- `wuss` demo command-line options `--res WIDTHxHEIGHT` and `-t/--tasks`
  (comma-separated launcher task names, or `all`, to open at startup).
- A Doughnut task (Andy Sloane's spinning torus): Select pauses, the wheel
  zooms, an Adjust drag spins it directly, arrow keys rotate it while
  paused, plus a Background colour picker.
- The text task gains a Markdown Demo sample (headers plus inline bold/
  italic/code as colour-tagged runs).
- Minesweeper draws its HUD with DPT-Digits-Bold-Lg, uses mine/flag
  bitmaps (cells grow to 22px), caches its colours (rebuilt on
  `wuss_EVENT_PALETTE`) and is keyboard-playable: arrows move an orange
  cell cursor, Return reveals and Space flags.
- Release builds dead-strip unused code (`-ffunction-sections`/
  `-fdata-sections` with `-dead_strip` or `--gc-sections`), shrinking the
  release `wuss` binary by ~6%.
- `wuss` Paint task: a scrollable canvas with a toolbox of nine tools (brush,
  eraser, line, rectangle, filled rectangle, flood fill, spray, rectangle and
  lasso selection) on 24x24 latching icons, a size slider and swatches; undo
  (20 RLE-compressed steps, ^Z), a Selection menu (Select All, Deselect,
  Invert), a New dialogue for canvas size and background, Save PNG (^S) and
  PNG load by file drop. Its UI-free core is `paint-doc`, covered by the new
  `paintdoc` test. See `docs/windowing/paint.md`.
- `wuss` bitmap icons can be latching and bevelled (RIDGE/GROOVE border plus a
  radio-style group).
- `wuss` per-window mouse capture while a button is held; `wuss_is_capturing()`
  lets the SDL frontend keep OS capture.
- `wuss` dialogues: `wuss_dialogue_handle_key()` (RETURN activates the default
  action, ESCAPE the action set by `wuss_dialogue_set_cancel()`) and
  `wuss_dialogue_set_focusable()`. Layout rules are in
  `docs/windowing/wuss-layout.md`.
- `wuss` messaging: `wuss_msg_target_t` (window plus optional icon),
  `wuss_send_to()` and `wuss_send_recorded_to()`; icon bar icons receive
  messages for their owning task. `wuss_EVENT_DRAG_END` gains `drop_icon`.
- `wuss` DataLoad: the Filer drags files out and drops them on windows or icon
  bar icons. Viewing, Reading, Dithering, Effects and Tiling load dropped
  files through the shared `taskapp_window_data_load`; `taskapp_desc_t`
  gains `drop_only` and an optional `data_load` hook.
- `wuss` gridview: `wuss_gridview_window_create()`, `_fit_window()`,
  `_hit_test()`, `_select_all()`, `_selected_count()`, `_set_drag()`; RISC OS
  style selection. The Filer gains Select all, Clear selection and Open
  parent menu rows and starts by default.
- `wuss` Effecting stack: effects are held in a reorderable stack shown in
  its own window (without a close icon). The menu adds, edits and deletes
  elements, sets the blend level (-100% to 200%) and applies or cancels the
  result. Menu shading updates live as the stack changes.
- `wuss_gridview_select()` selects a single item by index. A MENU click over
  an unselected item (with nothing selected) selects it until the menu
  closes, so the menu acts on it.
- `wuss_task_set_hidden()` hides a task's windows and its children's, and
  reveals only those it hid; `wuss_task_has_windows()` reports whether a task
  has any. The Tasks, Filing and Apps filer icon bar menus gain a Hide/Show
  row (relabelled live with `wuss_menu_set_item_text_live()`), and grey out
  Hide and Close all when nothing is open.
- An overfull `wuss` icon bar scrolls when the pointer hovers within 40px of
  either end.
- `wuss_colourlist` (with `wuss_colourlist_init_pair()` for the
  Foreground/Background case): a shared submenu of N named colours, used by
  Doughnut, Sofa, Saturn, Fern, Oscillating, Checker and Patterns.
- Randomise and Reset menu options in Doughnut, Orbing, Oscillating, Sofa,
  Fern, Saturn, Checker and Patterns, backed by `colour_random()` and
  `colour_random_contrast()` in `framebuf/colour.h`. Doughnut gains Major and
  Minor density submenus and tube keys.
- Save PNG in the Ball and Fireworks tasks; wheel zoom in Fern and Saturn;
  Space toggles Animate in Fern and Saturn; Up/Down step rotation in Sofa
  while paused.
- `docs/`: key reference and proposed system shortcuts for `wuss`.
- `wuss` Fern task: a parameterised Barnsley fern with size, points and curl
  (-300..300), colour menus, Animate, seed stepping by click or Left/Right
  and Save PNG.
- `wuss` Display task: a 256 greys mode (`app_DEPTH_GREY256`). It now holds
  the Zoom (1x-4x), CRT effect and Software pointer options, moved from the
  Wuss menu; nothing takes effect until Change. It opens at startup on native
  builds and is a singleton.
- Icon bar apps may be singletons: `wuss_task_raise_children()` and a
  `taskapp_desc` singleton flag make SELECT raise the open instance. Display,
  Palette, Configure, Icons and Keys use it. SELECT on the Palette icon opens
  the desktop palette and ADJUST the system palette. The Filer icon bar menu
  has Info and Open root rows.
- Every task has its own icon, looked up as `app_<name>` in the icon pool
  (falling back to `file_app`); icon bar labels are dropped for tasks with
  icons.
- Ball Damping option (D). Paint `[` and `]` step the brush size.
- Double ADJUST on an Apps filer task closes the viewer it was launched
  from.
- Reading shows any text file (plain text unless named `.md`) and refuses a
  file containing a NUL byte; a dropped file that fails to load closes its new
  instance. Its window has no horizontal scroll bar, reflows to any width up
  to the screen's, and scrolls to the end of text of any length.
- Pooling task: a grid of every shared icon pool entry with its name, using
  new `wuss_iconpool_name()`.
- `wuss_error_report()`: a non-modal singleton error window in the manner of
  `Wimp_ReportError` (image, one-line message, OK and optional Cancel, later
  reports queued and raised as they arrive), showing the reporting app's icon
  via `taskapp_glyph_name()`. Reading reports an unreadable file through it.
- DCS component: a Discard/Cancel/Save dialogue on `wuss_dialogue`, with a
  Quit variant and `wuss_dcs_set_message()`.
- Standard keys, proposed from the RISC OS 3 Style Guide with macOS chords
  filling the gaps (see the keys document). A system key layer ahead of
  `wuss_key` binds Ctrl-F2 close window, Ctrl-Q quit app, Ctrl-Shift-Q quit
  Wuss, Ctrl-H / Ctrl-Shift-H hide app / others, Ctrl-F11, Ctrl-` and Ctrl-Tab
  cycle apps and windows, Ctrl-Shift-Esc kill app, Ctrl-Shift-F toggle size,
  Ctrl-Shift-3 save the screen, Ctrl-0 actual size and Ctrl-Shift-Up open
  parent. Save moves to F3; Debug keys move to Shift-F1, Ctrl-Shift-F1 and
  Shift-F3; zoom to Ctrl-= and Ctrl--. New `wuss_task_front()`,
  `wuss_task_cycle_windows()`, `wuss_cycle_tasks()`, `wuss_task_hide_app()`,
  `wuss_task_hide_others()`, `wuss_task_show_all()`,
  `wuss_task_close_windows()`, `wuss_task_get_root()`,
  `wuss_task_is_hidden()`, `wuss_window_get_task()` and
  `wuss_window_toggle_size()`. Menu shortcut labels accept UP, DOWN, LEFT and
  RIGHT, and the icon bar Hide rows show `^H`.
- Writable icons have a clipboard (`wuss_clipboard_set()`, `_get()`,
  `_generation()`) and a selection: Ctrl-A, C, X, V and Z, Insert, and typing
  or deleting replaces the selection. The SDL frontend syncs the clipboard
  with the host.
- `wuss_menu_select_is()` and `wuss_menu_select_index()` test whether an event
  picked a given menu row.
- Per-colour fills: `wuss_fill_t` and `wuss_set_colour_fill()` let any desktop
  colour be a pattern. Desktop colours are dithered against the physical
  palette, solid where the screen can show them exactly.
- Painting paintbrush tool (H) with round, square, flat, tall, slash,
  backslash and bristles nozzles (N or the Nozzle menu).
- Ball-to-ball collisions (mass follows area; the held ball is immovable).
  Greebling Ctrl-R randomises the seed and base palette.
- Emscripten builds open Configuring, Display and a Reading window showing
  `About_Wuss.md` at start-up, scroll the mouse wheel 4x faster, and add an
  Open filedrop row to the Filer icon bar menu.

### Changed

- **Breaking:** Wuss now lives in its own repository, split out of DPTLib
  with its history. It fetches DPTLib with CMake `FetchContent` (pinned to a
  commit; override with `-DFETCHCONTENT_SOURCE_DIR_DPTLIB`). The library
  target is `WussLib`, the test runner is `WussTest` and the test
  declarations are in `wuss/test/all-tests.h`. `resources/bmfonts` is linked
  from DPTLib at configure time.
- New `wuss_result_string()` gives text for `result_WUSS_*` codes, which
  DPTLib's `result_string()` no longer knows.

- The demo tasks move from `libraries/wuss/tasks/` to `apps/wuss/tasks/`,
  beside the `wuss` app that runs them. The app's icon bar tasks, Apps
  (`appsfiler`) and Tasks (`taskmanager`), join them in their own directories.
- **Breaking:** `wuss_PLACE_IGNORE_SAFE_AREA` replaces
  `wuss_PLACE_AVOID_ICONBAR`, with the inverse sense.
- **Breaking:** `wuss_gridview_fit_window()` takes a `wuss_place_t` and
  re-places the window after fitting it.
- **Breaking:** `wuss_fontmenu_menu()` takes a flags argument; pass
  `wuss_FONTMENU_NONE` for the per-face menu.
- **Breaking:** `wuss_menu_close()` and the `_live` calls no longer accept a
  stale handle; drop it on `wuss_EVENT_MENU_CLOSED` and pass NULL.
- **Breaking:** task resources move from `resources/wuss/` to
  `resources/wuss-tasks/` (task icon pools load from
  `resources/wuss-tasks/<task name>/iconpool/`), artwork sources to
  `resources/sources/` and test fixtures to `resources/test/`. Neither
  sources nor test fixtures are preloaded into the Emscripten build.
- Each demo task lives in its own directory under `libraries/wuss/tasks/`;
  shared task sources move to `tasks/common/`.
- File-scope mutable static globals are grouped into one `static struct g`
  per source file.
- Painting's toolbox shows the desktop palette and follows palette changes;
  Palette's Load menu lists only sixteen-colour palettes.
- Viewing no longer draws a ninepatch frame.
- New and updated icons, including the task manager and Sudoku.
- **Breaking:** `wuss_gridview_activate_fn_t` takes the double-click's
  `wuss_button_t` after the index, and may close the gadget's window. The
  filer uses it as RISC OS does: a double ADJUST on a directory opens it in
  the place of the viewer it was in and closes that viewer.
- **Breaking:** `wuss_gridview_window_create()` takes a `const wuss_place_t *`
  after the task (NULL keeps the old first-free-region placement). The filer
  and Apps windows use it to open a directory or category window offset
  +32,+32 from the window it was opened from.
- **Breaking:** the `wuss` icon set becomes the icon pool.
  `resources/wuss/icons/` and `resources/wuss/files/` merge into one flat
  directory, `resources/wuss/iconpool/`; `wuss_icons_*` is renamed
  `wuss_iconpool_*` and `wuss_ICON_SET` becomes `wuss_ICONPOOL_SET`.
  Entries are now sorted by name, so indices no longer depend on the
  platform's directory scan order. Also moves `resources/palettes/` to
  `resources/wuss/palettes/`.
- **Breaking:** bitmap fonts move to a hierarchical layout,
  `resources/bmfonts/<Family>/<Style>.png` (e.g. `GrongyUI/Bold.png`,
  `Tiny/Regular.png`; the `-Lg` digits become the `DPT-DigitsLg` family).
  `bmfont_enumerate()` now walks family directories and reports
  `Family Style` labels, so `wuss_fontmenu` items read e.g. `GrongyUI Bold`.
- Colour components (`wuss_colour_t` RGB fields etc.) move from `int` to
  `unsigned int`.
- Scroll sausage Adjust-drag now scrolls both axes at once; scroll arrows
  auto-repeat while held; a window's minimum size floors at what its
  furniture needs; dragged windows snap to screen edges; a menu chain
  raises to front after an Adjust pick.
- Task menu boilerplate (plain window creation, proginfo desc, key-shortcut
  prologue, clamp-to-range) is factored into shared helpers across tasks;
  task clamps use `MIN`/`MAX` instead of hand-rolled comparisons.
- SDL frame pacing now targets a deadline instead of a fixed delay.
- Several redraw-path colour/palette resolutions move from per-pixel to
  once-per-redraw or once-per-frame (doughnut greys and z-buffer, particle
  palette, saturn/lissajous plot colours, porter-duff checkerboard fill,
  doughnut p8 blit and shade palette); `screen_set_pixel`/`_p*` resolve
  colour and open-code their clip test after clipping instead of before;
  p4 unpack-to-bgrx8888 and span fill get byte-at-a-time/`memset` fast
  paths; the clock task repaints only when the shown time changes.
- Auto-placed windows are centred on screen when the packer finds no room,
  replacing the cascade fallback.
- Setting a type-scoped icon flag (`DEFAULT`, `INTERACTIVE`, `SUBMENU`/
  `SEPARATOR`/`SWATCH`) on the wrong icon type is now rejected instead of
  ignored.
- The launcher menu is regrouped into Utilities, Games, Visuals, Toys,
  Tests and System in Title Case; Info, Configure, Debug, zoom, CRT Effect
  and Software Pointer move under System. Emscripten builds drop Quit.
- A disabled menu row's shortcut keycap is drawn as an outline in the row's
  grey.
- The Configure window's "ms"/"px" units follow their fields.
- **Breaking:** reserved `wuss_ICON_TYPE_STRING_SET` removed (reimplemented
  as the `wuss_stringset` gadget); `DRAGGABLE`'s numeric value drops by one.
- `wuss_nearest_colour()` defers to `colour_to_pixel()`, matching the
  renderer's luma-weighted palette matching.
- Text task: window auto-sizing off by default; background defaults to
  none. Doughnut task background lightened to dark grey. Image task menu
  loses its unhandled Quit entry.
- Label border widths named (`WUSS_*_BORDER_WIDTH`, `WUSS_BEVEL_WIDTH`,
  `WUSS_LABEL_TEXT_GAP`/`_PAD`).
- Internal: furniture hit testing, layout, pressed-box lookup and drag kind
  driven from one element table; icon draw and validation driven from a
  type table (`wuss__icon_types[]`); shared press-and-highlight, icon font
  lookup and radio/option sprite names.
- **Breaking:** furniture window flags flip to opt-in:
  `wuss_WINDOW_NO_TITLEBAR`/`_OUTLINE`/`_CLOSE`/`_BACK`/`_TOGGLE_SIZE`/
  `_VSCROLL`/`_HSCROLL`/`_RESIZE` become `wuss_WINDOW_TITLEBAR`/`_OUTLINE`/
  `_CLOSE`/`_BACK`/`_TOGGLE_SIZE`/`_VSCROLL`/`_HSCROLL`/`_RESIZE` (set to
  show, not set to hide); `wuss_WINDOW_DEFAULT` is the OR of all eight, so a
  bare-`DEFAULT` caller needs no change, but any caller that combined
  `DEFAULT` with a single furniture flag (e.g. `wuss_WINDOW_NO_RESIZE_BLIT`
  alone) must now OR `DEFAULT` in explicitly to keep its chrome.
- **Breaking:** menu tick setters are renamed to separate data-level and
  live-chain variants: `wuss_menu_set_ticked` → `wuss_menu_tick_exclusive_live`,
  `wuss_menu_set_item_ticked` → `wuss_menu_tick_item_live`;
  `wuss_menu_open_ticked` and the new `wuss_menu_tick_set` take a bit-vector
  (`unsigned int`) instead of an `int` array.
- struct `wuss_icon`'s mutually-exclusive per-type fields (label border,
  pattern tile, bitmap image, radio group, menu-entry swatch) move into a
  per-type union, mirrored on `wuss_icon_spec_t`, then the spec is nested
  directly inside `struct wuss_icon` as `{ spec; state; }` —
  `icon->FIELD` becomes `icon->spec.FIELD`. `wuss_icon_get_window()` and the
  icon→window back-pointer are removed; the four mutators that needed it
  (delete, set_text, set_hidden, set_selected) now take the window as an
  explicit argument.
- `wuss_ICON_TYPE_BUTTON` is renamed `wuss_ICON_TYPE_ACTION` throughout the
  icon API, implementation, tests and docs.
- Fonts move out of loose `struct wuss` fields into a core-owned
  `libraries/wuss/font/` module (`struct wuss_fontset`,
  `wuss__fontset_init/_height`, the `wuss_get_font*` accessors, the
  centralised text renderer); `wuss`'s four loose font fields collapse to
  one embedded fontset.
- The `wuss` demo's SDL present path converts and uploads only dirty rows
  instead of the whole screen every frame (`wuss_frontend_present` now
  takes a dirty box); a new `wuss->touched` region list (distinct from
  `wuss->dirty`) tracks pixels written directly by `wuss__blit_pieces`/
  resize/toggle-size so the frontend still re-uploads blitted-but-not-
  repainted destinations.
- `wuss_window_move()` translates the cached furniture-layout rects by the
  move delta instead of invalidating and rebuilding the whole per-window
  layout cache on every drag tick; `wuss_window_move()` and the resize path
  both now early-return once the target box is found equal to the window's
  current one, skipping the blit/invalidate machinery for a no-op drag tick.
- Toggle-size now grows a window to fill the whole screen (capped per axis
  at the document extent, or uncapped when that's 0), nudging the top-left
  toward the origin by the minimum needed to fit, rather than pinning the
  top-left and only growing the bottom-right; restore returns the exact
  pre-toggle box.
- Adjust-clicking a scroll well now pages away from the click point
  (mirroring Select's page-towards), matching RISC OS's usual Select/Adjust
  reversal elsewhere in the furniture.
- **Breaking:** `wuss_window_invalidate_all()` is renamed
  `wuss_window_invalidate_visible()` -- "all" misread as the whole document
  when it only ever covered the visible content rectangle. Same behaviour;
  use `wuss_window_invalidate_extent()` for the full document.
- The `wuss` demo task launcher is split out of `apps/wuss/main.c` into
  `apps/wuss/tasks.c` behind `tasks.h`: the 19 `spawn_*` callbacks, every
  menu table and `task_handle_event` move over, and the shared file-scope
  context becomes `struct wuss_app_tasks`. `main.c` keeps the frame loop,
  `pixel_stress` and the chrome config.
- The `wuss` palette picker is driven off the `wuss_EVENT_PALETTE`
  broadcast rather than an app-supplied `on_select` callback:
  `palette_create()` no longer takes a palette/`on_select`/`user_data`,
  `palette_menu_select` installs the pick with `wuss_set_palette()`
  directly, and the swatch grid reads the live array back via a new
  `wuss_get_palette()` accessor each redraw. The frontend bitmap push
  moves into `main.c`'s `wuss_EVENT_PALETTE` case.
- The `wuss` palette-picker menu and the greeble palettes are now scanned
  from `resources/palettes/*.hex` at runtime instead of hardcoded PICO-8 /
  WIMP16 tables, so a new palette drops in without a rebuild.
- The `wuss` menu row highlight now inverts only the text column between
  the two gutters (padded by a measured space-width either side), not the
  full item bbox, so the tick and submenu-arrow gutters stay un-darkened.
  `WUSS_MENU_TICK_W` / `WUSS_MENU_ARROW_W` renamed
  `WUSS_MENU_GUTTER_LEFT` / `WUSS_MENU_GUTTER_RIGHT`;
  `WUSS_MENU_TEXT_PAD` dropped.
- The `wuss` demo quit key moves from `Q` (which collided with text-entry
  tasks) to `F4`; Escape still quits.
- `greeble_stamp` blits by same-slot run with `screen_fill_hline()` (1-4
  run fills per row) instead of 64 `screen_set_pixel()` calls per stamp.
- `GREEBLE_MAX_COLS` goes 32 → 48 to match `GREEBLE_MAX_ROWS`, and the
  greeble window opens at the full generator grid size (from
  `GREEBLE_MAX_COLS/ROWS` × tile pixel size) rather than a hardcoded
  160x320.
- **Breaking:** `wuss_create()`'s `fonts` argument is now
  `const wuss_font_desc_t *` (handle, `wuss_font_class_t`, borrowed
  leafname) instead of a bare `bmfont_t *const *` array; up to
  `wuss_MAX_FONTS` (4) slots are stored, slot 0 remaining the system font
  used for titlebars and any icon that does not select another.
  `wuss_get_font_n(wuss, index)` reads a given slot; `wuss_get_font` stays
  slot 0. An icon's flags gain a two-bit font-select field
  (`wuss_ICON_FLAGS_FONT_MASK`, bits 9-10) with `wuss_ICON_FONT(n)` /
  `wuss_ICON_FONT_OF(f)` helpers, and titlebars are drawn in font slot 1
  (the bold weight) when one is supplied, else slot 0.
- **Breaking:** `wuss_fontmenu_create()` takes a fourth argument,
  `const wuss_alloc_t *alloc` (NULL selects `wuss_alloc`), routing every
  block the handle keeps through those hooks instead of libc
  malloc/calloc/strdup/free, matching `wuss_colourmenu` and `wuss_create`.
- Resizing a window (`wuss_window_resize()`) no longer clamps the result to
  the visible on-screen strip when the window's top-left is off-screen —
  the requested content size is applied verbatim and the window may
  overhang the screen edge, the same latitude toggle-size and drag already
  allow. `wuss_window_create()` keeps its own screen cap.
- Toggle-size now grows a window to fill the whole screen (capped per axis
  at the window's document extent, or uncapped when that extent is 0),
  nudging the top-left toward the origin by the minimum needed to fit; it
  previously pinned the top-left and only grew the bottom-right corner, so
  a window not already near the origin could never fill the screen and a
  zero document extent toggled to nothing. Restore returns the exact
  pre-toggle box, position included.
- **Breaking:** `wuss_create()` takes a `const wuss_alloc_t *alloc` argument
  after `config`; pass NULL for the stdlib allocator.
- **Breaking:** `wuss_config_t::palette` is renamed `furniture`, and its type
  `wuss_palette_t` renamed `wuss_furniture_palette_t`. `wuss_colour_t` narrows
  from `int` to `unsigned char`.
- **Breaking:** the window/desktop background is now a `wuss_backdrop_t`
  (`{ colour, pattern, pattern_bg }`) instead of a bare `wuss_colour_t`:
  `wuss_config_t::backdrop`, and the `bg` parameter of
  `wuss_window_create()`, `wuss_window_create_placed()` and
  `wuss_window_set_background()`. A non-`screen_PATTERN_SOLID` `pattern` fills
  with `screen_fill_pattern()` — the desktop phased to the screen origin, a
  window's content phased to its scroll origin so the pattern stays locked to
  the content. `wuss_BACKDROP_COLOUR(c)` and `wuss_BACKDROP_PATTERN(c, p, b)`
  build one; the flat-colour case is `wuss_BACKDROP_COLOUR(old_value)`.
- **Breaking:** `wuss_button_t` values are now flags (`wuss_BUTTON_SELECT` 4,
  `wuss_BUTTON_MENU` 2, `wuss_BUTTON_ADJUST` 1, `wuss_BUTTON_NONE` 0) so
  chords such as Select+Adjust can be reported. Client code comparing a
  reported button for equality must now test with `&`.
- **Breaking:** `wuss_window_create()` takes a `min_doc` argument between
  `doc` and `window`, the minimum content extent a resize-drag or toggle-size
  will shrink to. It is clamped up to the built-in grab floor and down to
  `doc`. Pass `(0, 0)` for the built-in floor.
- Adjust-clicking a scroll arrow now steps against the direction the arrow
  points, so one arrow can be worked both ways without moving the pointer.
  Toggle-size stays Select-only.
- A window can no longer be resized larger than the screen.
- **Breaking:** event dispatch is reworked around a registered, opaque
  `wuss_task_t` that owns its windows and is the sole delivery target.
  - `wuss_task_start()` / `wuss_task_stop()` and the by-value task-delegate
    struct are gone. Register a task with
    `wuss_task_create(wuss, const wuss_task_desc_t *, wuss_task_t **)` —
    `wuss_task_desc_t` is `{ wuss_window_fn_t *handle; void *task_data;
    const char *name; }` — and tear it down with
    `wuss_task_destroy(wuss_task_t *)`, which closes the task's windows,
    fires one `wuss_EVENT_QUIT` and unregisters it.
  - `wuss_task_set_autoclose(wuss_task_t *, int)` opts a task into
    self-destruct: once its last window closes it fires one `wuss_EVENT_QUIT`
    and unregisters, so it stops receiving `wuss_idle()` / `wuss_set_palette()`
    broadcasts. Such tasks should free `task_data` from `wuss_EVENT_QUIT`, not
    `wuss_EVENT_CLOSE`.
  - `wuss_window_create()` and `wuss_window_create_placed()` no longer take a
    leading `wuss_t *` or a task-delegate pointer; their first argument is now
    the owning `wuss_task_t *`. The window inherits that task's handler.
  - `wuss_event_kind_t` is a single master enum
    (`REDRAW, MOUSE, ICON, SCROLL, OPEN, PRE_SHOW, SHOW, PRE_CLOSE, CLOSE,
    IDLE, QUIT, PALETTE, MENU_SELECT`). Handlers must handle unknown kinds
    (fall through / `default:`).
  - New veto-able pre-events. `wuss_window_set_hidden()` now returns
    `result_t`: revealing a hidden window fires `wuss_EVENT_PRE_SHOW` first
    and a non-OK return keeps it hidden and propagates. New
    `wuss_window_try_close()` fires `wuss_EVENT_PRE_CLOSE` (non-OK vetoes),
    then `wuss_EVENT_CLOSE`, then closes; the close icon routes through it.
    `wuss_window_close()` stays the forced, unvetoable teardown and fires no
    pre-events.
  - `wuss_set_palette()` and `wuss_idle()` now broadcast once per *registered
    task* (in registration order, `window == NULL`), not once per open
    window's task.
  - `wuss_menu_open()` takes a `wuss_task_t *` as its first argument. A picked
    leaf now delivers `wuss_EVENT_MENU_SELECT` (with `window == NULL`) to that
    task — `data.menu_select` carries `{ const struct wuss_menu *menu; int
    index; wuss_button_t button; }`. `wuss_menu_select_fn_t` is removed.
- The greeble prefab tables (`greeble_prefab_cells` / `greeble_prefab`) are
  regenerated from the current sheet; `GREEBLE_NPREFAB` 103 -> 137.
- The `wuss` image task resizes the window frame on load (via
  `wuss_window_resize()` before `wuss_window_set_doc()`, which alone only
  moved the scroll extent) and insets an 8px solid border inside the
  ninepatch frame.
- The `wuss` demo's global task-list variable is renamed `g` -> `g_tasks`,
  avoiding a shadow of the `g`/`b` loop locals in
  `tasks_build_screen_palette`; the saturn task now spawns at startup
  instead of only via the launcher.
- `wuss__clip_to_visible()` and `wuss__subtract_boxes()`'s near-identical
  ping-pong carve loops are factored into a shared `carve_by_cuts()` driven
  by a `get_cut` callback, with z-order and plain-array adapters for the two
  call sites.
- `wuss_window_move()` and `wuss_window_resize()`'s duplicated "filter clean
  pieces against `wuss->dirty[]`" loops are factored into a shared
  `wuss__filter_settled()`.
- **Breaking:** the colourmenu, fontmenu and proginfo components are
  process-wide singletons. `wuss_colourmenu_create/destroy`,
  `wuss_fontmenu_create/destroy` (and the `wuss_fontmenu_t` handle) and
  `wuss_proginfo_create/destroy` are replaced by
  `wuss_colourmenu_menu(wuss)`, `wuss_fontmenu_menu(dir, title, wuss)` and
  `wuss_proginfo_window(task)`, rebuilt only when their inputs change;
  `wuss_colourmenu_set_none/_set_title` and `wuss_proginfo_set_desc`
  retarget the shared instance.
- **Breaking:** `wuss_icon_spec_action()` drops its fg/bg parameters and
  `wuss_icon_spec_label()`/`_slider()`/`_slider_row()` drop fg (always
  black); `wuss_icon_spec_label()` takes icon flags instead of a
  `justify_right` bool; `wuss_icon_spec_slider_row()`'s caller now supplies
  the value text buffer.
- `wuss_ICON_TYPE_WRITABLE` moves out of the reserved types, swapping its
  numeric value with `DISPLAY`. Constructing a reserved type (`DISPLAY`,
  `NUMBER`, `STRING_SET`, `DRAGGABLE`) now asserts in debug builds.
- Demo task menus are stored per task instance rather than as file-scope
  statics, so two instances of one task no longer share menu state.
- `wuss__menu`'s `borrowed` and `flash.keep_open` and
  `wuss__furniture_layout`'s `valid`/`has_titlebar` fold into `flags`
  bitmasks.
- Scrolling dirties only the moved axis's scrollbar well instead of all the
  furniture; scroll arrows get their own divider row/column rather than
  losing one to it; the sausage may shrink to 1px.
- Frame captions are drawn above a shifted border rather than overpainting
  a gap in it.
- The `wuss` demo opens no tasks at startup by default (see `-t`). The
  Swatches task is removed (superseded by Configure), Blank is renamed
  Patterns (a timed ordered-dither blend between random colours, with a
  Speed submenu and full furniture), the Image task drops its unwired
  menu rows, the Palette task moves its palettes to a Load submenu and
  menu categories are reordered.
- The `wuss` Image task is renamed Viewing and shows dropped images only; the
  Text task is renamed Reading and moves to a new Text category. Their old
  image/sample lists and Load menus are removed. The default startup task list
  follows the new names.
- Task glyphs and the demo logo/pointer come from the icon pool instead of
  per-task `bitmap_load_png` calls.
- The Chars task draws cell numbers in Tiny Regular and its grid lines in one
  pass per redraw.
- Xcode schemes set the working directory for the test and wuss targets.
- **Breaking:** `wuss_gridview_window_create()` also takes the default
  furniture to omit.
- Demo tasks move to `libraries/wuss/tasks`. Renamed: Paint to Painting,
  Greeble to Greebling, Minesweeper to Minesweeping, Effects to Effecting,
  Filer to Filing, Porter-Duff to Compositing, Lissajous to Oscillating,
  Configure to Configuring, Particles to Fireworks and Curve to Curving.
- `wuss_PLACE_SPARE` starts its search from the corner the alignment names
  (right and/or bottom). Task windows open in the first free region clear of
  the icon bar, falling back to centred; the Viewing window opens centred.
- A gridview window cannot shrink below one cell wide enough for a
  ten-character label and one row tall.
- `resources/images` moves to `resources/wuss/images`.
- `wuss` core: `core/impl.h` splits into per-concern headers; furniture and
  icon click handling move into their layers (furniture driven from the
  element table); the menu session is opaque to core. Behaviour is unchanged.
- The `wuss` tests split out of `wuss-test` into per-layer tests that each
  run alone on a fresh fixture. The `WUSS_FURNITURE=OFF`, `WUSS_ICONS=OFF`
  and `WUSS_MENUS=OFF` builds link and pass again.
- Gradient opens centred at its document size, and dithering can be turned
  off. Painting's New dialogue is titled New Painting with a single
  Dimensions row.
- ADJUST on the toggle size icon toggles as SELECT does.
- Internal: shared helpers replace repeated code in the menu, event delivery,
  text-width, popup-field, screen-to-document and invalidation paths
  (`wuss__menu_install`, `wuss__deliver_kind`, `wuss__text_width`,
  `wuss__screen_to_doc`, `WUSS_BUTTON_CLICK` and others); behaviour is
  unchanged.

### Fixed

- Closing a colour picker, Save As, DCS or error dialogue with Ctrl-Q or
  Ctrl-F9 left its component holding a freed window (a crash in Orbing at
  shutdown).
- Colour-keyed icon pixels were drawn on 32bpp screens.
- Colour fills in 1bpp and 2bpp modes were phased against the screen, not
  their window, so they misaligned with the background.
- The colour picker's title bar could be pushed off screen, and its value
  column left pixels behind.
- Viewing's Stage tick did not update on Adjust-click.
- Toolbar outlines sat 1px out from their parent's.
- AppsFiler windows sat well above the icon bar after shrinking to fit.
- `wuss_gridview_set_count()` re-measures the cell from the items, so a filer
  window widens its cells when a rescan brings in a longer name (e.g. after
  saving a new file) and narrows them when one goes.
- `wuss_destroy()` closes each task's windows before freeing the task, so
  abandoning a later task's menu chain cannot hand the focus back to a window
  whose task is already gone (a heap use-after-free when quitting `wuss` with
  Paint's New dialogue open).
- `wuss` windows are now placed around their actual (furniture-included)
  positions rather than their content box.
- A menu title is padded by a space either side; a menu row can be both
  ticked and swatched at once; the colourmenu ticks the current colour;
  descriptor-built menu items now initialise their shortcut field; a gap
  is fixed between font runs in menu shortcut labels; shared gutters are
  used consistently between shortcut-label drawing and menu sizing.
  Porter-Duff's paused rule now starts stepping from the ramp midpoint.
  A task's menu chain is abandoned when autoclose reaps the task.
  `WUSS_FURNITURE=OFF` builds again. The chars font menu ticks the current
  font, and the fontmenu directory is copied before enumeration (matching
  the earlier `bmfont_enumerate` fix). The missing-glyph box is drawn one
  column further left. The image task's Background menu drops a stray
  None entry.
- Escape now dismisses an open menu. Closing a window still in the open
  menu chain abandons the chain rather than leaving it pointing at freed
  memory. Auto-placement ignores open menus.
- A core drag keeps the pointer even over a parent menu's titlebar. The
  drag ants are no longer clipped to the last window redrawn, leave no
  trail and repaint only their edges each frame.
- Dialogue and Configure windows take the input focus, so their writable
  fields accept typing. The SDL frontend no longer swallows the click that
  focuses its window.
- The backdrop image is clipped to each piece it fills instead of painting
  over windows.
- `wuss_get_config()` builds with `WUSS_FURNITURE=OFF`.
- Titlebar text is centred by the font's ascent rather than a fixed offset.
  Window title truncation no longer splits a UTF-8 codepoint.
- Toggle-size no longer queues overlapping dirty rectangles and repaints
  the furniture strip it vacates when shrinking.
- `wuss_stringset` kept its menu handle after the chain closed, so
  `wuss_stringset_destroy()` used it after free.
- Left/right-justified text on bordered LABEL/DISPLAY icons overlapped the
  border; now inset by the border width plus clear space.
- Text task default colours gave white text under the Wimp16 palette; now
  picked by nearest palette entry. The Sample menu ticks the loaded sample
  and keeps the first samples by name regardless of readdir order.
- Furniture pressed state survived window close/hide, drawing the next
  window's close icon pressed.
- `wuss_destroy()`'s QUIT sweep could free its saved next node when a
  handler closed another task; all tasks are now marked reaping first.
- `wuss_icon_delete()` swapped the last icon into the freed slot, changing
  draw and hit-test order; the rest now shift down.
- `run_wuss` leaked the backdrop, and on early failure the fonts, frontend
  and wuss instance.
- Several furniture task windows (swatches, text, palette) passed
  `wuss_WINDOW_NO_RESIZE_BLIT` alone as their window flags, zeroing every
  furniture bit under the new opt-in flag polarity instead of just opting
  out of blit-based resize; now OR'd with `wuss_WINDOW_DEFAULT`.
- The saturn Size dialogue's Cancel/Apply buttons now act on Select release
  (`wuss_MOUSE_UP`) instead of press, so a press dragged off the button no
  longer commits and an Adjust click leaves the dialogue open per RISC OS
  convention; the slider groove now derives from the surround box rather
  than the bevel box.
- Icon text was vertically centred using ascender+descender (`font_height`),
  pulling label/button/radio-option/menu-entry text visually high since
  most glyphs never touch the descender band; centred by `font_ascent`
  instead, factored into a shared `icon_text_baseline_y()` helper.
- The chars grid's document extent wasn't updated on a font switch, so the
  scroll range stayed sized to the original font — a larger font put grid
  cells out of scroll reach, a smaller one left dead scroll space.
- A borrowed-window menu item (`wuss_menu_item_t::window`) could open
  off-screen near the parent menu's right edge, since `wuss_window_move`
  deliberately doesn't clamp; extracted `wuss_window_create()`'s on-screen
  nudge into a shared helper and applied it after the anchor move.
- The RADIO/OPTION icon glyph square was always font-height, truncating a
  larger state-sprite blit and ignoring its real width for the label
  offset; now sized from the state bitmap when present. Its box now also
  grows to fit the larger of its two state bitmaps so a select-time
  invalidate covers the whole overhanging sprite, and the box is now
  cleared before redraw when the icon has no explicit background
  (previously a shrinking glyph swap left stale pixels).
- A submenu dragged back over its own parent's titlebar was closed
  mid-drag, because the IDLE-tick "pointer over parent titlebar" check
  didn't verify the parent was actually frontmost there; now requires
  `wuss__window_at` to resolve to the parent itself. Separately, a menu
  chain's submenu now only opens while the pointer is in the row's arrow
  gutter (not anywhere on the row) and closes on re-entry; and the parent
  menu's title bar being hovered now closes an open child chain (previously
  `wuss_mouse_move`'s early-return over furniture meant nothing heard about
  it).
- The screen palette wasn't repadded on a live palette change, only at
  startup: picking a palette entry while running p8 fed wuss's bare
  16-entry UI palette straight to `bitmap_set_palette()`, which reads 256
  entries for p8 — a heap-buffer-overflow. The pad-to-screen-size logic is
  now shared between startup and the live-change handler.
- `wuss_window_invalidate(window, NULL)` built its whole-visible-rect
  placeholder already in screen space then ran it through the doc-space-
  to-screen translation a second time, double-applying scroll once a
  window was shrunk below its document and scrolled, sliding queued dirty
  rects off to one side; the placeholder is now built in doc space.
- A client-supplied `wuss_window_invalidate()` box was never clipped
  against the window's own furniture (only against occluding windows above
  it), so an edge-hugging client could dirty the titlebar/scrollbars/
  outline around its content; now intersected with the content box first.
- The ball task's edge-clamp was off-by-one on the right/bottom (content
  being a half-open box), poking the invalidation 1px into the furniture
  strip.
- A resize drag's scroll-reclamp fast-path blit read its source without
  checking `wuss->dirty[]` (unlike move/set-scroll), letting an earlier
  unpainted dirty region in the same frame get slid forward as settled
  content; a follow-up guarded the same path against
  `WUSS_MAX_INVALIDATE_PIECES` overflow (falling back to a full repaint
  instead of silently dropping survivor pieces), and a duplicate
  furniture-repaint call on the same reclamp path was removed.
- `wuss__blit_pieces` translated clean source pieces by the scroll delta
  and clipped only against occluding windows, never against the content
  box itself — a fast scroll (especially after a shrink) could blit and
  then invalidate screen area outside the window, painted with stale/
  garbage content on the next redraw; now intersected against the content
  box first.
- A scroll-well click's page-and-hold also armed a sausage drag, turning a
  held button into a live scroll; then, once guarded off by region, that
  guard also blocked a *direct* sausage hit from arming a drag at all —
  now gated on whether the click actually paged, not on which region it
  hit.
- Interactive resize-drag could be dragged past the screen's own
  width/height when the window's top-left was off-screen (negative x0/y0),
  since the clamp measured room left from the window's current position
  rather than the screen's total size.
- Minesweeper repainted the whole HUD strip and every board cell on any
  click, even a single flag toggle; reveal now accumulates just the
  board-cell range its flood fill touched and redraw skips the HUD/
  off-dirty cells accordingly, cutting the flicker.
- curve/porter-duff/gradient task content (a type label, a checkerboard, a
  composited bitmap) was pinned to the window corner instead of tracking
  the document scroll offset, so it stayed visually fixed while the rest
  of the content scrolled underneath it.
- `wuss_window_move()`'s fast path no longer slides pixels still queued in
  `wuss->dirty[]` from an earlier invalidation this frame onto the
  window's new position: pending dirty regions are stripped from the
  clean blit-source set before the slide. Emscripten's browser event
  queue batches several `MOUSE_MOTION` events per frame, so edge-drags hit
  this every time; native SDL rarely batched enough to expose it.
- `wuss_mouse_move()` clamps the incoming point to the screen before the
  drag-move / resize / scrollbar paths, so a pointer report from outside
  the frame can no longer carry a window off the desktop.
- The greeble menu's file-scope struct is shared by every greeble window;
  its single row's tick now follows the window it opened over (synced from
  task state at open time, re-ticked in place on an ADJUST pick) instead
  of showing whichever window toggled last.
- A MENU press that lands on another window is no longer spent closing the
  open menu chain — the chain still closes but the press falls through to
  that window's task so it can open its own menu, matching RISC OS and the
  bare-backdrop path.
- `wuss__menu_spawn` measures the menu title as well as the widest item
  label and sizes the window to the wider of the two, so a title longer
  than every item (e.g. a one-item menu) is no longer clipped by the
  titlebar.
- `spawn_image` copies the ninepatch path into its own buffer before
  `image_create()`, which calls `path_join_filename()` again and clobbers
  the shared static buffer; `image_click` now swallows a failed reload
  (warn, keep the current image) instead of propagating `rc`.
- Dropped dead `< 0` range checks on unsigned `wuss_colour_t` /
  `screen_pattern_t` fields across `wuss` create and icon-from-spec
  (`-Wtype-limits`), removed a bogus `const` on `pattern_runs_t`
  parameters that the RISC OS cross-compiler rejected, and marked
  `bitmap` PNG-save locals `volatile` against `-Wclobbered`.
- `wuss_destroy()` now delivers `wuss_EVENT_QUIT` to each still-registered
  task before freeing it, instead of freeing the task block directly. Per
  the `task_data`-ownership contract a task's client-owned allocations are
  freed only from its `QUIT` handler, so a still-registered task (e.g. a
  menu-spawned demo window not yet closed) previously leaked `task_data`
  on whole-manager teardown.
- `wuss_task_destroy()` now closes a live menu chain it owns before freeing
  the task, matching `wuss_destroy()`'s whole-manager teardown; previously
  a later row pick or in-flight pick-flash completion could call into the
  freed task through a dangling chain-owner pointer.
- A click outside a menu (or another `wuss_menu_open()`/`wuss_menu_close()`)
  landing during a pick's highlight flash no longer silently drops the
  `wuss_EVENT_MENU_SELECT` the flash was standing in for.
- A borrowed window opened by hovering a menu row (`wuss_menu_item_t::window`)
  no longer sticks at the submenu anchor position when its
  `wuss_EVENT_PRE_SHOW` vetoes the reveal — its prior position is restored.
- A submenu now opens only while the pointer is in the row's arrow gutter,
  not anywhere on the row, and closes on any re-entry rather than staying
  open for every subsequent mouse move over that row.
- An ADJUST pick that keeps a menu chain open now re-ticks the picked row
  in place; previously the tick was only applied at `wuss_menu_open()` time
  so an ADJUST-kept-open menu's tick state went stale.
- A window's furniture (titlebar, scrollbars, outline) is now clipped to
  its unoccluded pieces when redrawing its own dirty region; previously
  only the content redraw was occlusion-clipped, so a partly-covered
  window repainted furniture pixels straight over the window on top.
- `wuss_create()`'s font-slot arrays and loop bound in the interactive demo
  are now sized from one named, compile-time-checked constant
  (`WUSS_MAIN_NFONTS`, checked against `wuss_MAX_FONTS`) instead of three
  independent hardcoded literals that could silently drift apart.
- Opening a menu from a task's mouse-down handler no longer picks the menu's
  first row on the matching mouse-up: that release is now swallowed.
- A menu chain is now closed before its `on_select` callback runs, so a
  callback that opens another menu no longer fights the one being torn down.
- Menu text is drawn in the nearest-black palette entry rather than assuming
  a fixed index, so menus stay legible under any system palette.
- The mouse wheel no longer scrolls a window on an axis it declared
  non-scrollable.
- A MENU-button click on window furniture now has no effect, instead of being
  routed to the content task.
- A window's scroll offset is re-clamped after a resize reveals content past
  the document extent.
- The scrollbar well keeps a 2px gap at each end.
- Dragging a scrollbar well with Select no longer raises the window; only a
  resize-icon grab restacks it.
- `wuss_window_move()` no longer repaints already-blitted pixels when a drag
  past an occluded corner slides one clean piece of the window onto ground
  another clean piece just vacated.
- Several scroll-redraw glitches fixed: stale pixels when scroll events arrive
  faster than redraws, content blitted over a mid-content occluder, blit
  sub-pieces clobbering each other's source region, and repaint sets not
  clipped to the visible area. The hovered icon is re-resolved after a scroll.
- A work-area button held on mouse-down is now released if the click opens a
  window that covers the button's owner, instead of staying stuck pressed.
- Resize-corner drag preserves where within the resize icon the mouse-down
  landed, so the window's corner no longer jumps to the raw pointer position
  on the first move.
- The `NO_RESIZE_BLIT` branch of `wuss_window_resize()` invalidated the
  dirty region but never dropped the cached furniture layout, so
  `wuss__furniture_draw` kept painting titlebar/carve/outline rects at the
  previous frame's width. It now calls `wuss__chrome_invalidate_layout`,
  matching the blit branch's `wuss__chrome_repaint`.
- `wuss_destroy()`'s task sweep delivered `wuss_EVENT_QUIT` and freed each
  task node without first setting `wuss_TASK__REAPING`. A QUIT handler that
  closed its last autoclose window hit `wuss_window_close`'s self-destruct
  path, which delivered a second QUIT and freed the node — which the sweep
  then freed again. `wuss_TASK__REAPING` is now set before QUIT in the
  sweep, and `wuss_task_destroy()` is a no-op when the task is already
  being reaped.
- `wuss_window_set_hidden()` marked a window hidden but left
  `wuss->furniture.dragging`, `wuss->pressed_icon` and `wuss->hover_icon`
  pointing into it, so hiding a window mid-drag from an event handler meant
  the next `wuss_mouse_move()` still drove `wuss_window_move()` on an
  off-screen window every pointer report. It now clears the same pointer
  state `wuss_window_close()` does.
- `scroll_strip()` always reserved `size + WUSS_DIVIDER_PX` at the far end
  for the resize corner, but `scroll_strip_hit()` only reserves it when
  something owns that corner. With a horizontal scrollbar and `NO_VSCROLL`
  and `NO_RESIZE` the drawn strip stopped short while the hit box ran to
  the visible edge: the rightmost band showed stale pixels yet hit-tested
  as `HSCROLL_WELL` and started a scrollbar drag over empty chrome.
  `scroll_strip()` gets the same far-end rule.
- `wuss__furniture_hit_test()`'s `nearest_edge_region()` fallback claimed
  `wuss_FURNITURE_RESIZE` for a whole bottom or right carve band whenever a
  resize icon was present, and a scroll well for an edge with no scrollbar
  strip. A click on the bare carve band left of the corner icon then
  started a resize far from the visible handle. It now hands off to a
  scroll well only when that scrollbar's strip runs the full edge;
  every other edge pixel resolves to `TITLE` or `CONTENT`.
- The saturn task's stars/ring energy-gate exclusion thresholds were literal
  constants tuned for `size==256`; since `p`'s range grows linearly with
  size, a larger window shrank the exclusion disc relative to half and let
  stars/ring points land inside the planet body. Scaled by
  `size/SATURN_SIZE_DEFAULT`.
- `wuss__slider_row_snap()`'s `CLAMP(v, min, max)` assumed `min <= max`, so a
  reversed (fill-backwards) slider's `CLAMP` always returned `max` regardless
  of drag position; clamps against `MIN(min,max)`/`MAX(min,max)` instead.
- `wuss_colourmenu_create()` handed out `wuss_colour_t` swatches up to
  `wuss->npalette` with no upper bound, so a palette with more than 128
  entries produced swatches aliasing the symbolic/chrome-role colour
  namespace; capped at `wuss_COLOUR_SYMBOLIC`.
- A disabled menu row's `wuss_MOUSE_MOVE` handling was skipped entirely, so
  hovering off an open submenu onto a disabled sibling never ran the
  close-on-move-away logic, leaving the stale submenu open; the disabled
  check now only suppresses opening a submenu, not the move-away close.
- `wuss_dialogue_handle_icon()` lacked the `NULL`/action-kind guard every
  other component's icon handler has, risking a crash for a task
  multiplexing `ICON` dispatch across an optionally-created dialogue.
- `wuss__order_pieces()` built its full overlap graph even for a single
  piece, which by construction cannot clobber itself; added an `n <= 1` fast
  path, the common case on every plain move/resize/scroll blit.
- `scroll_sausage()`'s `CLAMP(sausage_px, WUSS_MIN_SAUSAGE, track_px)`
  silently dropped below `WUSS_MIN_SAUSAGE` whenever the well was shorter
  than the minimum sausage; the upper bound is now clamped against
  `WUSS_MIN_SAUSAGE` too so the floor always wins.
- `wuss_proginfo_set_desc()` dereferenced a borrowed `desc` with no `NULL`
  check; guarded, matching the idiom used elsewhere in the component
  helpers.
- PRE_SHOW handlers in the demo app and saturn called
  `wuss_menu_open_window_now()` with a `NULL` handle for plain windows.
- A task's stale main-window pointer was used after close while its
  proginfo dialogue stayed open (use-after-free via `wuss_idle`).
- The chars/text font menus cached the fontmenu singleton's submenu and
  could open or re-tick it after a rebuild freed it.
- A menu chain owner's handle went stale: a normal pick now also delivers
  `MENU_CLOSED`, and `wuss_destroy()` abandons a task's chain before QUIT.
- A Menu-button release on a menu row selected it.
- Demo task mouse handlers acted on clicks in any window, e.g. a Menu
  click on the Info dialogue opened the task menu.
- A Menu click over an icon never reached the window's mouse handler.
- The Image task's ninepatch path dropped its leafname.
- `wuss__icon_from_spec` rejected `wuss_NO_BACKGROUND` swatches.
- Hidden windows forced repaints of visible content (toggle-size occlusion
  check, close invalidation, icon invalidation).
- Occlusion carving silently dropped pieces on overflow; paint callers now
  fall back to the whole box and blit-source callers to nothing
  (`overpaint_safe` on `carve_by_cuts`/`wuss__subtract_boxes`/
  `wuss__clip_to_visible`).
- The SDL frontend didn't present the initial full redraw, and a
  REDRAW_ALL input could be narrowed by later events in the same batch,
  leaving stale texture rows.
- The title shadow fill covered the titlebar divider rule.
- `wuss_icon_spec_slider_row()` left the value text pointing at a dead
  stack buffer.
- The proginfo dialogue's widest right-justified label sat 1px left of the
  rest.
- Scroll/resize press highlighting dragged the window by a stale offset.
- The colourmenu None chip's hatch was phased against the screen rather
  than the chip, tearing after a move.
- Minesweeper's Grid Size tick didn't update on an Adjust pick, and its
  redraw ignored the scroll offset.
- The doughnut render was sized from the dirty box rather than the window,
  and recomputed theta trig in its inner loop.
- The `wuss` and `bmfont` enumerate tests hardcoded fixture counts.
- `wuss` core drag takes the pointer from a captured press.
- Ten `wuss` tasks swallowed every keypress; unused keys now return
  `result_WUSS_KEY_UNCLAIMED` so driver hotkeys work.
- Pointer moves are coalesced per frame, fixing window-move trails and dirty
  list overflow on polling frontends (RISC OS).
- A toggled window is trimmed to the document extent after a task reflows.
- RISC OS: the Filer and Paint use native path separators, and
  `convolve` no longer uses `lroundf`, which the RISC OS libm lacks.
- Save As closes after a drag-save (`DataLoadAck` is now sent).
- A gridview keeps a MENU-click selection until the menu pick has been
  handled. The Effecting Save PNG row opens as a plain window submenu.
- Fireball particle style probability was unset, dividing by zero when
  building the chance table (trapped in the Emscripten build at Minesweeping
  launch).
- The Apps filer passes on keys it does not use, so app shortcuts such as F2
  reach the fallback.
- Saturn skips the window resize when the size is unchanged.
- Minesweeping Give Up shades live after it or a reset.
- Two UBSan signed-arithmetic reports: gridview redraw with an empty clip
  and bitmap resize with negative origins.
- Reading no longer truncates long text, no longer asserts when centring or
  right-aligning text with blank lines, and keeps its scroll bar in step with
  the text.
- Icon bar Hide/Show rows resync after the hide keys, so clicks no longer
  toggle the wrong way.
- Effects closes its effect stack window with the main window.
