# AGENTS.md

This file provides guidance to coding agents (Claude Code, Codex, etc.) when working with code in this repository.

## Overview

Wuss is a minimal window manager in C99: window creation, z-ordering, mouse routing, dirty-region redraw and client-delegated content. It is layered as core, furniture, icons, menus, gadgets and components under `libraries/wuss/`, plus the interactive `wuss` demo and its tasks under `apps/wuss/`. Targets desktop (Linux/macOS/Windows), WebAssembly and RISC OS (via GCCSDK).

Wuss was split out of [DPTLib](https://github.com/dpt/DPTLib) and depends on it for everything below the window manager (bitmaps, bmfont, geometry, regions, streams, `result_t`, debug macros).

- CMake fetches DPTLib with `FetchContent`, pinned by `GIT_TAG` in `CMakeLists.txt` — read that line for the current form (tag or commit hash) rather than assuming.
- For work on both repos at once, configure with `-DFETCHCONTENT_SOURCE_DIR_DPTLIB=../DPTLib` so the local checkout is used instead.
- A change that needs a new DPTLib feature lands in DPTLib first. Bump the pin to the new DPTLib commit or tag as part of the same work, and say so: the local-checkout override hides a stale pin, so fetched builds stay broken until it moves. If the pin changes form, correct the line above in the same change.

The bitmap fonts in `resources/bmfonts` are DPTLib's: configuring creates a symlink (a copy where links fail) to the fetched DPTLib's `resources/bmfonts`. It is git-ignored.

## Build

Build directories are per-config; there is **no** plain `build/` — never invoke `./build/WussTest` or `cmake --build build`.

- `build-asan/`  — **default**. Debug, tests (`BUILD_TESTS=YES`), ASan + UBSan on (`USE_ASAN=YES`). Use this unless told otherwise.
- `build-sdl/`   — Release, ASan on, tests and apps on (`BUILD_APPS=ON`). Needed for the interactive `wuss` app (target `wuss`) and anything under `apps/wuss/tasks/`.
- `build-wuss-min/` — Release, tests with `WUSS_FURNITURE=OFF`. Checks the subsystem options still build when trimmed. (`wuss-menu` fails its tick check in this config; this predates the split.)
- RISC OS: GCCSDK cross build via `cmake/riscos.cmake`; no build dir is kept. Leave alone unless working on RISC OS.
- `build-emscripten/` — WebAssembly build of the demo. If missing, configure with `emcmake cmake -B build-emscripten -G Ninja -DBUILD_APPS=ON -DBUILD_TESTS=OFF`, build with `cmake --build build-emscripten --target wuss`, then serve the dir and open `wuss.html` (e.g. `python3 -m http.server -d build-emscripten`). Uses Emscripten's bundled SDL3 and libpng ports (`-sUSE_SDL=3`, `-sUSE_LIBPNG=1`); the browser owns the main loop via `emscripten_set_main_loop_arg`; `resources/` is baked into `wuss.data` and mounted at MEMFS `/resources`. `.github/workflows/pages.yml` publishes this build.

CMake options: `BUILD_TESTS`, `BUILD_APPS` (the `wuss` demo; needs `WUSS_FURNITURE` and `WUSS_ICONS`, and SDL3 off RISC OS), `USE_ASAN` (ASan + UBSan; DPTLib reads the same cache variable). Subsystems, all default ON: `WUSS_FURNITURE`, `WUSS_ICONBAR`, `WUSS_ICONS`, `WUSS_MENUS` (implies icons), `WUSS_GADGETS` (implies menus), `WUSS_COMPONENTS` (implies gadgets). DPTLib's own options (`USE_FORTIFY`, `DPTLIB_IMAGES_READ_ONLY`) pass through. DPTLib's tests are never built from here: its test block is guarded by `PROJECT_IS_TOP_LEVEL`.

(Re)configure a dir only if its `CMakeCache.txt` is missing or you are changing options:
```
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=YES -DUSE_ASAN=YES -DFETCHCONTENT_SOURCE_DIR_DPTLIB=../DPTLib
```

Build:
```
cmake --build build-asan --target WussTest
cmake --build build-sdl --target wuss    # interactive app
```

The library target is `WussLib` (so it does not clash, case-insensitively, with the `wuss` executable). Requires libpng through DPTLib.

## Testing

Run from the **repo root** so `-resources .` resolves the fixture files:
```
./build-asan/WussTest-debug -resources .
```

Run a subset by naming tests (names come from the `tests[]` table in `apps/test/main.c`, e.g. `wuss`, `wuss-menu`, `wuss-pointer`):
```
./build-asan/WussTest-debug -resources . wuss wuss-menu
```

The binary name carries a per-config suffix: `WussTest-debug` in Debug dirs (`build-asan/`), plain `WussTest` in Release dirs (`build-sdl/`, `build-wuss-min/`). The CMake target is `WussTest` either way.

Success prints `++ Tests completed in Ns: N of N tests passed.`

To drive the real demo headless, write a script (`move`, `click`, `key`, `wait N`, `dump`, `png FILE`, `quit`; see `apps/wuss/script.h`) and run `SDL_VIDEODRIVER=dummy build-sdl/wuss --script FILE` from the repo root. `png` dumps the framebuffer, not the desktop.

### Adding a new test

1. Write `libraries/wuss/<area>/test/<module>-test.c` (or beside a task under `apps/wuss/tasks/`) exposing a `result_t <module>_test(const char *resources)`.
2. Declare it as `extern testfn_t <module>_test;` in `include/wuss/test/all-tests.h`.
3. Add `{ "<module>", <module>_test }` to the `tests[]` table in `apps/test/main.c`.
4. Add the new `.c`/`.h` files to `TEST_SOURCES` in `CMakeLists.txt`.

## Architecture

**Layout.** Public headers are in `include/wuss/` (Doxygen-documented); implementation in `libraries/wuss/<layer>/`, often one function per file, plus a private `impl.h` for opaque structs and internal-only declarations. New source/header files must be added by hand to the relevant `set(WUSS_..._SOURCES ...)` list in `CMakeLists.txt` — there is no globbing.

**Internal naming.** Functions declared in a private `impl.h` and not part of the public header use a double-underscore prefix, e.g. `wuss__titlebar_height_for`; internal-only enum constants still use the module's normal single-underscore style (e.g. `wuss_WINDOW_STATE_TOGGLED`), matching public enums. Per-instance internal state that isn't part of the public appearance API (e.g. `struct wuss_window`'s toggled/maximised state) is kept as a bitflags enum with `wuss__window_*` accessor helpers rather than loose `int`/`bool` fields, leaving room to add flags without growing the struct.

**Error handling.** Functions return DPTLib's `result_t`. Wuss codes are `result_WUSS_*` in `include/wuss/wuss.h`, based at `result_BASE_WUSS` (reserved in DPTLib's `base/result.h`). Use `wuss_result_string(rc)` rather than `result_string(rc)` so those codes get readable text; it falls back to DPTLib's for everything else.

**Debug/logging.** DPTLib's `base/debug.h` provides `logf_info/warning/error/fatal/abort`, plus `check(err)` (log-and-`goto failure`) and `sentinel` (unreachable-code marker), both of which are compiled out entirely when `NDEBUG` is set — don't rely on their side effects in release builds.

**Docs.** `docs/windowing/` (start at `wuss.md`), `docs/adr/` for decisions, `CONTEXT.md` for the domain glossary.

**RISC OS.** `TARGET_RISCOS` (set by the toolchain file) makes DPTLib fetch and build zlib and libpng from source, links OSLib from `$GCCSDK_INSTALL_ENV` and applies `riscos_set_flags()`. The demo uses `apps/wuss/frontend-riscos.c` instead of SDL.

## Code style

- C99, `-Wall -Wextra -pedantic`. Allman brace style, 2-space indentation, tabs converted to spaces (see `.astylerc`/`.editorconfig` — astyle is the formatter of record, but the user runs it; never run astyle yourself — hand-format edits).
- Declare variables at the top of each scope (pre-C99 style is followed throughout, even though the standard is C99), ordered by first use. Exception: a `result_t` status local (`rc`, `err`, `crc`, ...) is always declared first in its block, ahead of the use-ordered locals.
- File header comment format: `/* filename.c -- one-line description */`.
- Section breaks within files use `/* ----- ... ----- */` rule comments.
- Public API docs use Doxygen (`\file`, `\param`, `\return`).
- Edit `.c`/`.h` files with the Edit tool. Never rewrite them with a script — no `sed -i`, no `perl -pi`, no `python3`/`awk` heredoc doing string replacement, in this repo or in DPTLib: such splices corrupt the Allman/2-space layout and can't be verified without re-reading. To inspect exact bytes or indentation, Read the file; don't shell out to `cat -A`/`cat -v`.
- After editing any `.c`/`.h` function prototype or definition, run `python3 tools/fmt/wrap_protos.py <file>`; after editing a header's Doxygen, run `python3 tools/fmt/wrap_doxygen.py <file>` (skip vendored headers).
- Before committing any `.c`/`.h` changes, run `python3 tools/fmt/realign_decls.py <file>` (declaration-block alignment) and `python3 tools/fmt/balance_braces.py <file>` (if one branch of an `if`/`else` chain is braced, brace them all) alongside `wrap_protos.py`/`wrap_doxygen.py` on the same files.

## Reuse DPTLib

Before hand-rolling, grep DPTLib's `include/` (the fetched copy, or `../DPTLib/include`) for an existing helper. Do not re-implement these:

- Growable arrays: `array_grow` / `array_shrink` / `array_delete_element(s)` (`utils/array.h`), never a bare `realloc` doubling block. Also `datastruct/vector.h`, `list.h`, `hash.h`, `bitvec.h` before writing new containers.
- `MIN` / `MAX` / `CLAMP` / `SWAP` / `NELEMS` / `NOT_USED` / `DIVIDE_ROUNDING_UP` (`base/utils.h`).
- Sorted lookup: `utils/bsearch.h`. Maths and fixed point: `utils/maths.h`, `utils/fxp.h`.
- Rectangles: `box_t` helpers in `geom/box.h` (`box_intersection`, `box_is_empty`, `box_list_add`, `BOX_INIT`...), `geom/line.h` for line clipping.
- Pixel sets and polygons: `geom/region.h` (sparse per-row runs; box and even-odd polygon fill, contains, outline). Do not use dense per-pixel masks or write scanline fills.
- Bitmap compression: `bitmap_compress` / `bitmap_decompress` (`framebuf/bitmap.h`); drawing primitives in `framebuf/screen.h`.
- File paths: build them with `pathf` (`io/path.h`) from `/`-separated pieces, never a raw `snprintf("%s/%s")`; RISC OS needs `.` separators and no extension. `pathf` returns one shared static buffer, so copy the result before the next call. Use `path_leaf`, `path_leaf_valid`, `path_leaf_strip_ext` and `path_is_full` rather than `strrchr(path, '/')`. When joining a directory and a leaf that may contain `.` (a listing's entries), `pathf` would mangle it: join with the host separator instead (see `filing_join` in `filing.c`).
- Errors: `result_t` codes and `check` / `sentinel` (`base/result.h`, `base/debug.h`).

A new helper with no dependence on wuss belongs in DPTLib, in the matching library area with a test, not in Wuss, a demo or a task.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/): `<type>[optional scope]: <description>`, e.g. `fix(menu): keep ticks live`. Common types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `ci`, `build`. Add a `!` before the colon (or a `BREAKING CHANGE:` footer) for breaking changes.

When the user says "commit": stage the relevant files and `git commit` with the
message passed via repeated `-m` flags (subject, then body). Do **not** write a
`COMMIT_MSG` / `COMMIT_MSG_TMP` file. Never `git push` unless explicitly asked —
pushing prompts for an SSH key passphrase and will hang.
