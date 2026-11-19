# AGENTS.md

This file gives guidance to coding agents (Claude Code, Codex, etc.) that work with code in this repository.

## Overview

Wuss is a minimal window manager in C99. It handles window creation, z-ordering, mouse routing, dirty-region redraw and client-delegated content. Its layers are core, furniture, icons, menus, gadgets and components, under `libraries/wuss/`. The interactive `wuss` demo and its tasks live under `apps/wuss/`. Wuss targets desktop (Linux/macOS/Windows), WebAssembly and RISC OS (through GCCSDK).

Wuss began as part of [DPTLib](https://github.com/dpt/DPTLib). It uses DPTLib for everything below the window manager (bitmaps, bmfont, geometry, regions, streams, `result_t`, debug macros).

- CMake fetches DPTLib with `FetchContent`. `GIT_TAG` in `CMakeLists.txt` sets the pin. Read that line for the current form (tag, commit hash or branch). Do not assume it.
- To work on both repos at once, configure with `-DFETCHCONTENT_SOURCE_DIR_DPTLIB=../DPTLib`. CMake then uses the local checkout.
- A change that needs a new DPTLib feature lands in DPTLib first. Bump the pin to the new DPTLib commit or tag as part of the same work, and say so. The local-checkout override hides a stale pin. Fetched builds stay broken until the pin moves. If the pin changes form, correct the first bullet in the same change.

The bitmap fonts in `resources/bmfonts` come from DPTLib. Configuring creates a symlink to the fetched DPTLib's `resources/bmfonts`. Where links fail, it makes a copy. Git ignores the result.

## Build

Each config has its own build directory. There is **no** plain `build/`. Never run `./build/WussTest` or `cmake --build build`.

- `build-asan/` — **default**. Debug, tests (`BUILD_TESTS=YES`), ASan + UBSan on (`USE_ASAN=YES`). Use this directory unless told otherwise.
- `build-sdl/` — Release, ASan on, tests and apps on (`BUILD_APPS=ON`). Use it for the interactive `wuss` app (target `wuss`) and for anything under `apps/wuss/tasks/`.
- `build-wuss-min/` — Release, tests with `WUSS_FURNITURE=OFF`. It checks that the subsystem options still build when trimmed. If it is missing, configure with `cmake -B build-wuss-min -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=YES -DWUSS_FURNITURE=OFF -DFETCHCONTENT_SOURCE_DIR_DPTLIB=../DPTLib`.
- RISC OS: GCCSDK cross build through `cmake/riscos.cmake`. No build directory exists for it. Leave it alone unless you work on RISC OS.
- `build-emscripten/` — WebAssembly build of the demo. If it is missing, follow these steps:
  1. Configure with `emcmake cmake -B build-emscripten -G Ninja -DBUILD_APPS=ON -DBUILD_TESTS=OFF`.
  2. Build with `cmake --build build-emscripten --target wuss`.
  3. Serve the directory (e.g. `python3 -m http.server -d build-emscripten`) and open `wuss.html`.

  This build uses Emscripten's bundled SDL3, libpng and libjpeg ports (`-sUSE_SDL=3`, `-sUSE_LIBPNG=1`, `-sUSE_LIBJPEG=1`). The browser owns the main loop through `emscripten_set_main_loop_arg`. The build packs `resources/` into `wuss.data` and mounts it at MEMFS `/resources`. `.github/workflows/pages.yml` publishes this build.

CMake options:

- `BUILD_TESTS`.
- `BUILD_APPS`: the `wuss` demo. It needs `WUSS_FURNITURE` and `WUSS_COMPONENTS`, and SDL3 off RISC OS.
- `USE_ASAN`: ASan + UBSan. DPTLib reads the same cache variable.
- Subsystems, all default ON: `WUSS_FURNITURE`, `WUSS_ICONBAR`, `WUSS_ICONS`, `WUSS_MENUS` (implies icons), `WUSS_GADGETS` (implies menus), `WUSS_COMPONENTS` (implies gadgets).
- DPTLib's own options (`USE_FORTIFY`, `DPTLIB_IMAGES_READ_ONLY`) pass through.

`BUILD_APPS` turns on DPTLib's `DPTLIB_CRT` off RISC OS, with `DPTLIB_CRT_VULKAN` off. Wuss cannot recreate its window to reclaim a swapchain on Linux.

`BUILD_APPS` also turns on `DPTLIB_ALLOC_COUNT`, unless `USE_FORTIFY` is set. In all code that links DPTLib, `malloc`, `calloc`, `realloc`, `free` and `strdup` then become macros for the counting allocator. Such blocks must not cross to or from uninstrumented code (libc, SDL, the fetched game libraries).

This project never builds DPTLib's tests. `PROJECT_IS_TOP_LEVEL` guards DPTLib's test block.

Configure a directory again only if its `CMakeCache.txt` is missing or you change options:
```
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=YES -DUSE_ASAN=YES -DFETCHCONTENT_SOURCE_DIR_DPTLIB=../DPTLib
```

Build:
```
cmake --build build-asan --target WussTest
cmake --build build-sdl --target wuss    # interactive app
```

The library target is `WussLib`, so that its name does not clash (case-insensitively) with the `wuss` executable. It requires libpng and libjpeg through DPTLib.

## Testing

Run from the **repo root**, so that `-resources .` resolves the fixture files:
```
./build-asan/WussTest-debug -resources .
```

To run a subset, name the tests. The names come from the `tests[]` table in `apps/test/main.c` (e.g. `wuss`, `wuss-menu`, `wuss-pointer`):
```
./build-asan/WussTest-debug -resources . wuss wuss-menu
```

The binary name has a per-config suffix. Debug directories (`build-asan/`) make `WussTest-debug`. Release directories (`build-sdl/`, `build-wuss-min/`) make plain `WussTest`. The CMake target is `WussTest` in both cases.

On success the binary prints `++ Tests completed in Ns: N of N tests passed.`

To drive the real demo headless:

1. Write a script. The commands are `move`, `click`, `key`, `wait N`, `dump`, `png FILE` and `quit`. See `apps/wuss/script.h`.
2. From the repo root, run `SDL_VIDEODRIVER=dummy build-sdl/wuss --script FILE`.

`png` dumps the framebuffer, not the desktop.

Painting demo: run `SDL_VIDEODRIVER=dummy build-sdl/wuss --tasks Painting --script FILE`. To open a canvas, use `click select 447 450`, `wait 5`, `click select 578 411`, `wait 10`. The canvas content starts at y=121. The size bar pane is 222,90–622,120. Slider end caps ignore clicks, so click inside the trough. Crop and zoom screenshots with `sips` (no PIL).

### Adding a new test

1. Write `libraries/wuss/<area>/test/<module>-test.c` (or put it beside a task under `apps/wuss/tasks/`). It exposes a `result_t <module>_test(const char *resources)`.
2. Declare it as `extern testfn_t <module>_test;` in `include/wuss/test/all-tests.h`.
3. Add `{ "<module>", <module>_test }` to the `tests[]` table in `apps/test/main.c`.
4. Add the new `.c`/`.h` files to `TEST_SOURCES` in `CMakeLists.txt`.

## Architecture

**Layout.** Public headers are in `include/wuss/`, with Doxygen comments. The implementation is in `libraries/wuss/<layer>/`, often one function per file. Each layer can have a private `impl.h` for opaque structs and internal-only declarations. Add each new source or header file by hand to the matching `set(WUSS_..._SOURCES ...)` list in `CMakeLists.txt`. CMake does not glob.

**Internal naming.**

- A function declared in a private `impl.h`, and not in the public header, uses a double-underscore prefix, e.g. `wuss__titlebar_height_for`.
- Internal-only enum constants use the module's normal single-underscore style, as public enums do (e.g. `wuss_WINDOW_STATE_TOGGLED`).
- Some per-instance internal state is not part of the public appearance API (e.g. the toggled and maximised state of `struct wuss_window`). Keep that state in a bitflags enum with `wuss__window_*` accessor helpers. Do not use loose `int` or `bool` fields. The enum leaves room for new flags without a bigger struct.

**Error handling.** Functions return DPTLib's `result_t`. Wuss codes are `result_WUSS_*` in `include/wuss/wuss.h`. They start at `result_BASE_WUSS`, which DPTLib's `base/result.h` reserves. Use `wuss_result_string(rc)`, not `result_string(rc)`, so that Wuss codes get readable text. For all other codes it falls back to DPTLib's text.

**Debug and logging.** DPTLib's `base/debug.h` supplies `logf_info/warning/error/fatal/abort`. It also supplies `check(err)` (log, then `goto failure`) and `sentinel` (unreachable-code marker). When `NDEBUG` is set, the compiler removes `check` and `sentinel` entirely. Do not rely on their side effects in release builds.

**Docs.** `docs/windowing/` (start at `wuss.md`), `docs/adr/` for decisions, `CONTEXT.md` for the domain glossary.

**RISC OS.** The toolchain file sets `TARGET_RISCOS`. That makes DPTLib fetch and build zlib, libpng and libjpeg-turbo from source. It also links OSLib from `$GCCSDK_INSTALL_ENV` and applies `riscos_set_flags()`. The demo uses `apps/wuss/frontend-riscos.c` instead of SDL.

## Code style

- C99, `-Wall -Wextra -pedantic`. Allman brace style, 2-space indentation, tabs converted to spaces. See `.astylerc` and `.editorconfig`. astyle is the formatter of record, but the user runs it. Never run astyle yourself. Format edits by hand.
- Declare variables at the top of each scope, in order of first use. The code follows pre-C99 style throughout, although the standard is C99. Exception: always declare a `result_t` status local (`rc`, `err`, `crc`, ...) first in its block, before the use-ordered locals.
- File header comment format: `/* filename.c -- one-line description */`.
- Section breaks within files use `/* ----- ... ----- */` rule comments.
- Public API docs use Doxygen (`\file`, `\param`, `\return`).
- Edit `.c`/`.h` files with the Edit tool. Never rewrite them with a script: no `sed -i`, no `perl -pi`, no `python3`/`awk` heredoc that replaces strings. This rule applies in this repo and in DPTLib. Such splices corrupt the Allman/2-space layout. You cannot check them without reading the file again. To inspect exact bytes or indentation, Read the file. Do not use `cat -A` or `cat -v`.
- After you edit any `.c`/`.h` function prototype or definition, run `python3 tools/fmt/wrap_protos.py <file>`.
- After you edit a header's Doxygen, run `python3 tools/fmt/wrap_doxygen.py <file>`. Skip vendored headers.
- Before you commit any `.c`/`.h` changes, run these tools on the same files, together with `wrap_protos.py` and `wrap_doxygen.py`:
  - `python3 tools/fmt/realign_decls.py <file>` (aligns declaration blocks).
  - `python3 tools/fmt/balance_braces.py <file>` (if one branch of an `if`/`else` chain has braces, it braces all branches).

## Reuse DPTLib

Before you write your own code, grep DPTLib's `include/` (the fetched copy, or `../DPTLib/include`) for an existing helper. Do not write new versions of these:

- Growable arrays: `array_grow` / `array_shrink` / `array_delete_element(s)` (`utils/array.h`). Never write a bare `realloc` doubling block. Also check `datastruct/vector.h`, `list.h`, `hash.h` and `bitvec.h` before you write a new container.
- `MIN` / `MAX` / `CLAMP` / `SWAP` / `NELEMS` / `NOT_USED` / `DIVIDE_ROUNDING_UP` (`base/utils.h`).
- Sorted lookup: `utils/bsearch.h`. Maths and fixed point: `utils/maths.h`, `utils/fxp.h`.
- Rectangles: `box_t` helpers in `geom/box.h` (`box_intersection`, `box_is_empty`, `box_list_add`, `BOX_INIT`...). Line clipping: `geom/line.h`.
- Pixel sets and polygons: `geom/region.h`. It stores sparse per-row runs and does box fill, even-odd polygon fill, contains and outline. Do not use dense per-pixel masks. Do not write scanline fills.
- Bitmap compression: `bitmap_compress` / `bitmap_decompress` (`framebuf/bitmap.h`). Drawing primitives: `framebuf/screen.h`.
- File paths:
  - Build paths with `pathf` (`io/path.h`) from `/`-separated pieces. Never use a raw `snprintf("%s/%s")`. RISC OS needs `.` separators and no extension.
  - `pathf` returns one shared static buffer. Copy the result before the next call.
  - Use `path_leaf`, `path_leaf_valid`, `path_leaf_strip_ext` and `path_is_full`, not `strrchr(path, '/')`.
  - A leaf can contain `.` (for example, an entry in a directory listing). `pathf` damages such a leaf. To join a directory and such a leaf, use the host separator (see `filing_join` in `filing.c`).
- Errors: `result_t` codes and `check` / `sentinel` (`base/result.h`, `base/debug.h`).

A new helper that does not depend on wuss belongs in DPTLib. Put it in the matching library area, with a test. Do not put it in Wuss, a demo or a task.

## Keyboard shortcuts

A change can add, remove, rebind or rename a key. The key can be a system chord, a writable field key, a task's menu shortcut or a direct key. In the same change, update these two places, so that the Shortcuts app stays accurate:

- the `shortcuts[]` table in `apps/wuss/tasks/shortcuts/shortcuts.c`
- `docs/windowing/wuss-keys.md`.

A new task with keys gets its own group in the table. Leave out a task's key when a system chord shadows it. That key never reaches the task.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/): `<type>[optional scope]: <description>`, e.g. `fix(menu): keep ticks live`. Common types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `ci`, `build`. For breaking changes, add a `!` before the colon (or a `BREAKING CHANGE:` footer).

When the user says "commit": stage the relevant files and run `git commit`. Pass the message
with repeated `-m` flags (subject, then body). Do **not** write a
`COMMIT_MSG` / `COMMIT_MSG_TMP` file. Never `git push` unless the user asks for it.
A push prompts for an SSH key passphrase and hangs.

Commits are SSH-signed. Run `git commit … </dev/null` with a 30 s timeout. If it
times out, the agent does not have the key. Ask the user to `ssh-add`. Do not try again.

## Releases

Run DPTLib's `tools/release.py` from the repo root: `python3 ../DPTLib/tools/release.py <command>`. See DPTLib's AGENTS.md for the steps (`check`, `cut --test`, `merge`, `push`, `start`). Wuss differs in three ways:

- `merge` makes a `--no-ff` merge commit on `main`, as Wuss always did.
- `cut` also changes the `Unreleased.` line in `SUMMARY.md` to `Released YYYY-MM-DD.`. `check` needs the summary headed `# Wuss X.Y.Z`.
- `check` refuses a DPTLib `GIT_TAG` that is not a release tag (e.g. `develop`). Release DPTLib and bump the pin first.

After `start`, when you summarise the new version's changes, rewrite `SUMMARY.md` for that version.
