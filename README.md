# Wuss

version 0.6.0

[![Build status](https://github.com/dpt/Wuss/actions/workflows/ci.yml/badge.svg)](https://github.com/dpt/Wuss/actions)

© David Thomas, 2020-2026

## Overview

Wuss is a minimal window manager written in portable C99. It handles window creation, z-ordering, mouse routing, dirty-region redraw and client-delegated content. On top of that core sit optional layers: window furniture, icons, menus, gadgets and shared components.

Wuss was split out of [DPTLib](https://github.com/dpt/DPTLib), which it uses for bitmaps, fonts, geometry and the other basics. CMake fetches DPTLib automatically.

## Modules

- [`wuss/wuss.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/wuss.h) — minimal window manager {[docs](https://github.com/dpt/Wuss/blob/master/docs/windowing/wuss.md)}
  - [`wuss/window.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/window.h) — window creation, positioning, sizing and client delegation
  - [`wuss/task.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/task.h) — tasks: the clients of windows
  - [`wuss/message.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/message.h) — inter-task messaging
  - [`wuss/icon.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/icon.h) — work-area icons
  - [`wuss/icon-spec.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/icon-spec.h) — fill helpers for common icon shapes
  - [`wuss/iconbar.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/iconbar.h) — RISC OS-style icon bar
  - [`wuss/menu.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/menu.h) — pop-up menus
  - [`wuss/menu-desc.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/menu-desc.h) — build a menu tree from a descriptor string
  - [`wuss/gadget/colourset.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/gadget/colourset.h) — titled colour well with a colour menu
  - [`wuss/gadget/gridview.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/gadget/gridview.h) — glyph and label grid
  - [`wuss/gadget/stringset.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/gadget/stringset.h) — field with a pop-up menu of fixed strings
  - [`wuss/component/dialogue.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/component/dialogue.h) — interactive dialogue base class
  - [`wuss/component/saveas.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/component/saveas.h) — RISC OS-style drag-and-drop Save As dialogue
  - [`wuss/component/info.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/component/info.h) — label and value grid dialogue
  - [`wuss/component/proginfo.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/component/proginfo.h) — shared program information dialogue
  - [`wuss/component/colourmenu.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/component/colourmenu.h) — menu of the system palette colours
  - [`wuss/component/fontmenu.h`](https://github.com/dpt/Wuss/blob/master/include/wuss/component/fontmenu.h) — menu of the available bitmap fonts

The interactive `wuss` demo (`apps/wuss/`) hosts several example tasks, including [Painting](https://github.com/dpt/Wuss/blob/master/docs/windowing/painting.md). More notes are in [`docs/windowing/`](https://github.com/dpt/Wuss/tree/master/docs/windowing).

## Building

Use CMake with Ninja or Make, e.g.:

```
cmake -B build -G Ninja -DBUILD_TESTS=YES
cmake --build build --target WussTest
```

Requires libpng. The demo also needs SDL3. Useful options:

- `BUILD_TESTS` — the test binary
- `BUILD_APPS` — the interactive `wuss` demo
- `USE_ASAN` — AddressSanitizer and UBSan
- `WUSS_FURNITURE`, `WUSS_ICONBAR`, `WUSS_ICONS`, `WUSS_MENUS`, `WUSS_GADGETS`, `WUSS_COMPONENTS` — subsystems (all default ON)

To build against a local DPTLib checkout instead of the pinned commit, add `-DFETCHCONTENT_SOURCE_DIR_DPTLIB=../DPTLib`.

The bitmap fonts come from DPTLib: configuring links `resources/bmfonts` to DPTLib's copy.

RISC OS is built with a GCCSDK toolchain file. WebAssembly builds of the demo use `emcmake`; see [AGENTS.md](AGENTS.md).

## Testing

Run from the repository root:

```
./build/WussTest -resources .
```

(Debug builds name the binary `WussTest-debug`.) Name tests to run a subset, e.g. `wuss wuss-menu`. If successful you'll see:

```
++ Tests completed in 0.9909s: N of N tests passed.
```
