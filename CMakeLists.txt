# CMakeLists.txt
#
# Wuss
#
# Copyright (c) David Thomas, 2020-2026
#
# vim: sw=4 ts=8 et

cmake_minimum_required(VERSION 3.23.2)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

project(Wuss VERSION 0.6.0 DESCRIPTION "Minimal window manager" LANGUAGES C)

# The values set in the toolchain file aren't available until this point.
if(TARGET_RISCOS)
    riscos_set_flags()
endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif(CCACHE_FOUND)

# USE_ASAN, USE_COVERAGE and USE_FORTIFY are shared with DPTLib, which reads
# the same cache variables and propagates the flags to its dependants.
option(USE_ASAN "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(WUSS_FURNITURE "Build the Wuss window-furniture subsystem" ON)
option(WUSS_ICONBAR "Build the Wuss RISC OS-style icon bar" ON)
option(WUSS_ICONS "Build the Wuss in-content icon subsystem" ON)
option(WUSS_MENUS "Build the Wuss pop-up menu helper (implies WUSS_ICONS)" ON)
option(WUSS_GADGETS "Build the Wuss compound in-window gadgets (implies WUSS_MENUS)" ON)
option(WUSS_COMPONENTS "Build the Wuss shared task components (implies WUSS_GADGETS)" ON)
option(BUILD_TESTS "Build test program" OFF)
option(BUILD_APPS "Build the Wuss interactive demo" OFF)

# DPTLib
#
# For local work on both repos, configure with
# -DFETCHCONTENT_SOURCE_DIR_DPTLIB=../DPTLib

include(FetchContent)
FetchContent_Declare(dptlib
                     GIT_REPOSITORY https://github.com/dpt/DPTLib.git
                     GIT_TAG 811bc301ca8546ef4f4f0b06d55a4ca8bdd5ea4b) # split-wuss
FetchContent_MakeAvailable(dptlib)

# The demo and the tests load bitmap fonts from resources/bmfonts, which are
# DPTLib's: link them into this tree so -resources . and the emscripten
# preload both find them. ponytail: a symlink in the source tree; give wuss a
# second resource root if this ever bites (e.g. RISC OS packaging)
if(POLICY CMP0205)
    cmake_policy(SET CMP0205 NEW) # COPY_ON_ERROR copies the whole directory
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/bmfonts")
    file(CREATE_LINK "${dptlib_SOURCE_DIR}/resources/bmfonts"
         "${CMAKE_CURRENT_SOURCE_DIR}/resources/bmfonts"
         SYMBOLIC COPY_ON_ERROR)
endif()

add_library(WussLib)

set_target_properties(WussLib PROPERTIES
    VERSION 0.6.0
    DESCRIPTION "Wuss window manager library"
    C_STANDARD 99
    PREFIX ""  # remove 'lib' prefix
    ARCHIVE_OUTPUT_NAME_DEBUG WussLib-debug
    ARCHIVE_OUTPUT_NAME_RELEASE WussLib
    ARCHIVE_OUTPUT_NAME_RELWITHDEBINFO WussLib-relwithdebinfo
    ARCHIVE_OUTPUT_NAME_MINSIZEREL WussLib-minsizerel)

target_include_directories(WussLib PUBLIC include)

set(PUBLIC_HEADERS
    include/wuss/message.h
    include/wuss/task.h
    include/wuss/window.h
    include/wuss/wuss.h)

if(WUSS_ICONBAR)
    list(APPEND PUBLIC_HEADERS include/wuss/iconbar.h)
endif()
if(WUSS_ICONS)
    list(APPEND PUBLIC_HEADERS include/wuss/icon.h)
    list(APPEND PUBLIC_HEADERS include/wuss/icon-spec.h)
endif()
if(WUSS_MENUS)
    list(APPEND PUBLIC_HEADERS include/wuss/menu.h)
    list(APPEND PUBLIC_HEADERS include/wuss/menu-desc.h)
endif()
if(WUSS_GADGETS OR WUSS_COMPONENTS)
    list(APPEND PUBLIC_HEADERS include/wuss/gadget/stringset.h)
    list(APPEND PUBLIC_HEADERS include/wuss/gadget/gridview.h)
    list(APPEND PUBLIC_HEADERS include/wuss/gadget/swatches.h)
endif()
if(WUSS_COMPONENTS)
    list(APPEND PUBLIC_HEADERS include/wuss/component/fontmenu.h)
    list(APPEND PUBLIC_HEADERS include/wuss/component/colourmenu.h)
    list(APPEND PUBLIC_HEADERS include/wuss/component/info.h)
    list(APPEND PUBLIC_HEADERS include/wuss/component/proginfo.h)
    list(APPEND PUBLIC_HEADERS include/wuss/component/dialogue.h)
    list(APPEND PUBLIC_HEADERS include/wuss/component/saveas.h)
    list(APPEND PUBLIC_HEADERS include/wuss/component/dcs.h)
    list(APPEND PUBLIC_HEADERS include/wuss/component/error.h)
    list(APPEND PUBLIC_HEADERS include/wuss/component/colourpicker.h)
    list(APPEND PUBLIC_HEADERS include/wuss/gadget/colourset.h)
endif()

# The public headers must be set as properties of the library, not as
# target_sources. The quoting is essential.
set_target_properties(WussLib PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}")

# Sources
#

set(WUSS_CORE_SOURCES
    libraries/wuss/core/backdrop.c
    libraries/wuss/core/capture.c
    libraries/wuss/core/copy-text.c
    libraries/wuss/core/create.c
    libraries/wuss/core/clipboard.c
    libraries/wuss/core/destroy.c
    libraries/wuss/core/drag.c
    libraries/wuss/core/get-focus.c
    libraries/wuss/core/get-palette.c
    libraries/wuss/core/get-pointer.c
    libraries/wuss/core/get-resources.c
    libraries/wuss/core/get-screen-size.c
    libraries/wuss/core/idle.c
    libraries/wuss/core/impl.h
    libraries/wuss/core/alloc.h
    libraries/wuss/core/dispatch.h
    libraries/wuss/core/geometry.h
    libraries/wuss/core/input.h
    libraries/wuss/core/invalidate.h
    libraries/wuss/core/layers.h
    libraries/wuss/core/palette.h
    libraries/wuss/core/types.h
    libraries/wuss/core/windows.h
    libraries/wuss/core/invalidate.c
    libraries/wuss/core/is-dragging.c
    libraries/wuss/core/key.c
    libraries/wuss/core/message.c
    libraries/wuss/core/double-click.c
    libraries/wuss/core/mouse-click.c
    libraries/wuss/core/mouse-move.c
    libraries/wuss/core/nearest-colour.c
    libraries/wuss/core/rebuild-palettecache.c
    libraries/wuss/core/redraw.c
    libraries/wuss/core/resize.c
    libraries/wuss/core/scroll.c
    libraries/wuss/core/scroll-step.c
    libraries/wuss/core/get-config.c
    libraries/wuss/core/set-backdrop.c
    libraries/wuss/core/set-colour-fill.c
    libraries/wuss/core/set-config.c
    libraries/wuss/core/set-focus.c
    libraries/wuss/core/set-font.c
    libraries/wuss/core/set-palette.c
    libraries/wuss/core/set-time.c
    libraries/wuss/core/relayout.c
    libraries/wuss/core/result-string.c
    libraries/wuss/font/font.h
    libraries/wuss/font/registry.c
    libraries/wuss/font/text.c
    libraries/wuss/core/task/create.c
    libraries/wuss/core/task/arrange.c
    libraries/wuss/core/task/close-windows.c
    libraries/wuss/core/task/destroy.c
    libraries/wuss/core/task/raise-children.c
    libraries/wuss/core/task/set-autoclose.c
    libraries/wuss/core/task/set-hidden.c
    libraries/wuss/core/task/set-parent.c
    libraries/wuss/core/task/deliver.c
    libraries/wuss/core/window/at.c
    libraries/wuss/core/window/attach.c
    libraries/wuss/core/window/create.c
    libraries/wuss/core/window/create-placed.c
    libraries/wuss/core/window/close.c
    libraries/wuss/core/window/get-content-bounds.c
    libraries/wuss/core/window/get-max-content.c
    libraries/wuss/core/window/get-scroll.c
    libraries/wuss/core/window/get-task.c
    libraries/wuss/core/window/get-title.c
    libraries/wuss/core/window/get-visible-bounds.c
    libraries/wuss/core/window/invalidate.c
    libraries/wuss/core/window/move.c
    libraries/wuss/core/window/resize.c
    libraries/wuss/core/window/restack.c
    libraries/wuss/core/window/set-stack.c
    libraries/wuss/core/window/z-order.c
    libraries/wuss/core/window/set-background.c
    libraries/wuss/core/window/set-doc.c
    libraries/wuss/core/window/set-min-size.c
    libraries/wuss/core/window/set-hidden.c
    libraries/wuss/core/window/set-scroll.c
    libraries/wuss/core/window/set-title.c
    libraries/wuss/core/window/toggle-size.c
    libraries/wuss/core/window/try-close.c)

set(WUSS_FURNITURE_SOURCES
    libraries/wuss/furniture/back-box.c
    libraries/wuss/furniture/click.c
    libraries/wuss/furniture/close-box.c
    libraries/wuss/furniture/content-box.c
    libraries/wuss/furniture/drag-resize.c
    libraries/wuss/furniture/draw.c
    libraries/wuss/furniture/elements.c
    libraries/wuss/furniture/hit-test.c
    libraries/wuss/furniture/press-drag.c
    libraries/wuss/furniture/press-icons.c
    libraries/wuss/furniture/press-scroll.c
    libraries/wuss/furniture/invalidate.c
    libraries/wuss/furniture/layout.c
    libraries/wuss/furniture/ops.c
    libraries/wuss/furniture/resize-box.c
    libraries/wuss/furniture/scroll-action.c
    libraries/wuss/furniture/scroll-box.c
    libraries/wuss/furniture/titlebar-box.c
    libraries/wuss/furniture/toggle-action.c
    libraries/wuss/furniture/toggle-box.c
    libraries/wuss/furniture.h)

set(WUSS_ICONBAR_SOURCES
    libraries/wuss/iconbar/create.c
    libraries/wuss/iconbar/destroy.c
    libraries/wuss/iconbar/draw.c
    libraries/wuss/iconbar/hit-test.c
    libraries/wuss/iconbar/menu-open.c
    libraries/wuss/iconbar/task-destroyed.c
    libraries/wuss/iconbar.h)

set(WUSS_ICON_SOURCES
    libraries/wuss/icon/create.c
    libraries/wuss/icon/create-array.c
    libraries/wuss/icon/delete.c
    libraries/wuss/icon/draw.c
    libraries/wuss/icon/free.c
    libraries/wuss/icon/from-spec.c
    libraries/wuss/helpers/icon-spec.c
    libraries/wuss/icon/get-bbox.c
    libraries/wuss/icon/get-selected.c
    libraries/wuss/icon/get-text.c
    libraries/wuss/icon/get-type.c
    libraries/wuss/icon/get-value.c
    libraries/wuss/icon/click.c
    libraries/wuss/icon/hit-test.c
    libraries/wuss/icon/invalidate.c
    libraries/wuss/icon/plot.c
    libraries/wuss/icon/iconpool.c
    libraries/wuss/icon/screen-box.c
    libraries/wuss/icon/set-disabled.c
    libraries/wuss/icon/set-flag.c
    libraries/wuss/icon/set-hidden.c
    libraries/wuss/icon/set-hover.c
    libraries/wuss/icon/set-selected.c
    libraries/wuss/icon/set-text.c
    libraries/wuss/icon/set-value.c
    libraries/wuss/icon/slider-box.c
    libraries/wuss/icon/writable.c
    libraries/wuss/icon.h)

set(WUSS_MENU_SOURCES
    libraries/wuss/menu/menu.c
    libraries/wuss/menu/shortcut.c
    libraries/wuss/helpers/create-from-desc.c
    libraries/wuss/menu.h)

set(WUSS_GADGET_SOURCES
    libraries/wuss/gadget/stringset.c
    libraries/wuss/gadget/popup.c
    libraries/wuss/gadget/gridview.c
    libraries/wuss/gadget/swatches.c)

set(WUSS_COMPONENT_SOURCES
    libraries/wuss/component/fontmenu.c
    libraries/wuss/component/colourmenu.c
    libraries/wuss/component/info.c
    libraries/wuss/component/proginfo.c
    libraries/wuss/component/dialogue.c
    libraries/wuss/component/saveas.c
    libraries/wuss/component/dcs.c
    libraries/wuss/component/error.c
    libraries/wuss/component/colourpicker.c
    libraries/wuss/gadget/colourset.c) # a gadget, but built on colourmenu

set(WUSS_SOURCES ${WUSS_CORE_SOURCES})
if(WUSS_FURNITURE)
    list(APPEND WUSS_SOURCES ${WUSS_FURNITURE_SOURCES})
endif()
if(WUSS_ICONBAR)
    list(APPEND WUSS_SOURCES ${WUSS_ICONBAR_SOURCES})
endif()
if(WUSS_COMPONENTS)
    set(WUSS_GADGETS ON) # the shared components build on the gadgets
endif()
if(WUSS_GADGETS)
    set(WUSS_MENUS ON) # the gadgets build on the menu helper
endif()
if(WUSS_MENUS)
    set(WUSS_ICONS ON) # the menu helper needs the icon subsystem
endif()
if(WUSS_ICONS)
    list(APPEND WUSS_SOURCES ${WUSS_ICON_SOURCES})
endif()
if(WUSS_MENUS)
    list(APPEND WUSS_SOURCES ${WUSS_MENU_SOURCES})
endif()
if(WUSS_GADGETS)
    list(APPEND WUSS_SOURCES ${WUSS_GADGET_SOURCES})
endif()
if(WUSS_COMPONENTS)
    list(APPEND WUSS_SOURCES ${WUSS_COMPONENT_SOURCES})
endif()

set(ALL_SOURCES
    ${PUBLIC_HEADERS}
    ${WUSS_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ALL_SOURCES})

target_sources(WussLib PRIVATE ${ALL_SOURCES})

if(MSVC)
    target_compile_options(WussLib PRIVATE /W4)
    target_compile_definitions(WussLib PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
else()
    target_compile_options(WussLib PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(WussLib PRIVATE -ffunction-sections -fdata-sections)
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(WussLib PRIVATE DPTLIB_DEBUG)
endif()

target_link_libraries(WussLib PUBLIC DPTLib)

if(WUSS_FURNITURE)
    target_compile_definitions(WussLib PUBLIC WUSS_FURNITURE)
endif()
if(WUSS_ICONBAR)
    target_compile_definitions(WussLib PUBLIC WUSS_ICONBAR)
endif()
if(WUSS_ICONS)
    target_compile_definitions(WussLib PUBLIC WUSS_ICONS)
endif()
if(WUSS_MENUS)
    target_compile_definitions(WussLib PUBLIC WUSS_MENUS)
endif()
if(WUSS_GADGETS)
    target_compile_definitions(WussLib PUBLIC WUSS_GADGETS)
endif()
if(WUSS_COMPONENTS)
    target_compile_definitions(WussLib PUBLIC WUSS_COMPONENTS)
endif()


# Self-test executable
#

if(BUILD_TESTS)
    set(TEST_SOURCES
        apps/test/main.c
        libraries/wuss/test/gridview-bench-test.c
        libraries/wuss/test/painting-doc-test.c
        libraries/wuss/test/harness.c
        libraries/wuss/test/harness.h
        libraries/wuss/menu/test/menu-test.c
        libraries/wuss/menu/test/menu-desc-test.c
        libraries/wuss/icon/test/iconpool-test.c
        libraries/wuss/icon/test/icon-test.c
        libraries/wuss/icon/test/icon-draw-test.c
        libraries/wuss/icon/test/writable-test.c
        libraries/wuss/icon/test/slider-test.c
        libraries/wuss/icon/test/hover-test.c
        libraries/wuss/icon/test/click-test.c
        libraries/wuss/icon/test/set-text-test.c
        libraries/wuss/iconbar/test/iconbar-test.c
        libraries/wuss/gadget/test/gadget-test.c
        libraries/wuss/component/test/component-test.c
        libraries/wuss/component/test/dcs-test.c
        libraries/wuss/component/test/error-test.c
        libraries/wuss/component/test/colourpicker-test.c
        libraries/wuss/core/test/pointer-test.c
        libraries/wuss/core/test/message-test.c
        libraries/wuss/core/test/drag-test.c
        libraries/wuss/core/test/task-test.c
        libraries/wuss/core/test/content-test.c
        libraries/wuss/core/test/stack-test.c
        libraries/wuss/core/test/lifecycle-test.c
        libraries/wuss/core/test/invalidate-test.c
        libraries/wuss/core/test/placement-test.c
        libraries/wuss/core/test/focus-test.c
        libraries/wuss/core/test/hidden-test.c
        libraries/wuss/core/test/move-test.c
        libraries/wuss/core/test/resize-test.c
        libraries/wuss/core/test/scroll-test.c
        libraries/wuss/core/test/toggle-test.c
        libraries/wuss/core/test/toolbar-test.c
        libraries/wuss/furniture/test/furniture-test.c
        apps/wuss/tasks/painting/painting-doc.c
        apps/wuss/tasks/painting/painting-doc.h
        libraries/wuss/test/wuss-test.c)

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TEST_SOURCES})

    add_executable(WussTest ${TEST_SOURCES})

    set_target_properties(WussTest PROPERTIES
        XCODE_GENERATE_SCHEME ON
        XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DESCRIPTION "Wuss test program"
        C_STANDARD 99
        OUTPUT_NAME_DEBUG WussTest-debug
        OUTPUT_NAME_RELEASE WussTest
        OUTPUT_NAME_RELWITHDEBINFO WussTest-relwithdebinfo
        OUTPUT_NAME_MINSIZEREL WussTest-minsizerel)

    target_link_libraries(WussTest WussLib)
    if(NOT MSVC)
        target_link_libraries(WussTest m)
    endif()
endif()


# Standalone apps
#

if(BUILD_APPS)
    # The Wuss interactive demo: main.c drives wuss, tasks.c wires the task
    # launcher and its menus; a per-platform backend (frontend-*.c) opens the
    # surface and reports input.
    # RISC OS uses native VDU/OS_Mouse calls; everything else uses SDL.
    if(NOT (WUSS_FURNITURE AND WUSS_ICONS))
        message(FATAL_ERROR "BUILD_APPS requires WUSS_FURNITURE and WUSS_ICONS")
    endif()

    set(WUSS_APP_SOURCES
        apps/wuss/tasks/appsfiler/appsfiler.c
        apps/wuss/tasks/appsfiler/appsfiler.h
        apps/wuss/main.c
        apps/wuss/script.c
        apps/wuss/script.h
        apps/wuss/tasks/taskmanager/taskmanager.c
        apps/wuss/tasks/taskmanager/taskmanager.h
        apps/wuss/tasks/tasks.c
        apps/wuss/tasks/ball/ball.c
        apps/wuss/tasks/chasehq/chasehq.c
        apps/wuss/tasks/greatescape/greatescape.c
        apps/wuss/tasks/chars/chars.c
        apps/wuss/tasks/checker/checker.c
        apps/wuss/tasks/clock/clock.c
        apps/wuss/tasks/configuring/configuring.c
        apps/wuss/tasks/curving/curving.c
        apps/wuss/tasks/dithering/dithering.c
        apps/wuss/tasks/display/display.c
        apps/wuss/tasks/doughnut/doughnut.c
        apps/wuss/tasks/effecting/effecting.c
        apps/wuss/tasks/effecting/effecting-edit.c
        apps/wuss/tasks/filing/filing.c
        apps/wuss/tasks/common/fireball.c
        apps/wuss/tasks/gradient/gradient.c
        apps/wuss/tasks/greebling/greebling.c
        apps/wuss/tasks/icons/icons.c
        apps/wuss/tasks/viewing/viewing.c
        apps/wuss/tasks/keys/keys.c
        apps/wuss/tasks/oscillating/oscillating.c
        apps/wuss/tasks/minesweeping/minesweeping.c
        apps/wuss/tasks/painting/painting-doc.c
        apps/wuss/tasks/painting/painting-doc.h
        apps/wuss/tasks/painting/painting-new.c
        apps/wuss/tasks/painting/painting-new.h
        apps/wuss/tasks/fern/fern.c
        apps/wuss/tasks/fern/fern.h
        apps/wuss/tasks/painting/painting.c
        apps/wuss/tasks/painting/painting.h
        apps/wuss/tasks/palette/palette.c
        apps/wuss/tasks/fireworks/fireworks.c
        apps/wuss/tasks/patterns/patterns.c
        apps/wuss/tasks/pooling/pooling.c
        apps/wuss/tasks/shortcuts/shortcuts.c
        apps/wuss/tasks/compositing/compositing.c
        apps/wuss/tasks/saturn/saturn.c
        apps/wuss/tasks/common/snapshot.c
        apps/wuss/tasks/sofa/sofa.c
        apps/wuss/tasks/sudoku/sudoku.c
        apps/wuss/tasks/orbing/orbing.c
        apps/wuss/tasks/reading/reading.c
        apps/wuss/tasks/resizing/resizing.c
        apps/wuss/tasks/resizing/resizing.h
        apps/wuss/tasks/tiling/tiling.c
        apps/wuss/tasks/common/taskapp.c
        apps/wuss/tasks/common/taskapp.h
        apps/wuss/tasks/common/zxtask.c)

    if(TARGET_RISCOS)
        list(APPEND WUSS_APP_SOURCES apps/wuss/frontend-riscos.c)
    else()
        list(APPEND WUSS_APP_SOURCES
            apps/wuss/crt.c
            apps/wuss/crt.h
            apps/wuss/frontend-sdl.c)
    endif()

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${WUSS_APP_SOURCES})

    add_executable(wuss ${WUSS_APP_SOURCES})

    set_target_properties(wuss PROPERTIES
        XCODE_GENERATE_SCHEME ON
        XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DESCRIPTION "Wuss interactive demo"
        C_STANDARD 99
        OUTPUT_NAME_DEBUG wuss-debug
        OUTPUT_NAME_RELEASE wuss
        OUTPUT_NAME_RELWITHDEBINFO wuss-relwithdebinfo
        OUTPUT_NAME_MINSIZEREL wuss-minsizerel)

    target_include_directories(wuss PRIVATE apps/wuss libraries/wuss)
    target_compile_definitions(wuss PRIVATE WUSS_APP)
    # OS window title, naming the build type (e.g. "Wuss (Debug)")
    target_compile_definitions(wuss PRIVATE
        "WUSS_WINDOW_TITLE=\"$<IF:$<BOOL:$<CONFIG>>,Wuss ($<CONFIG>),Wuss>\"")
    target_link_libraries(wuss WussLib)

    # The Particles task uses the Explosion engine. For local work on both
    # repos, configure with -DFETCHCONTENT_SOURCE_DIR_EXPLOSION=../Explosion
    FetchContent_Declare(explosion
                         GIT_REPOSITORY https://github.com/dpt/Explosion.git
                         GIT_TAG e86fdffa80c83c51dfd1ab3ee3922e01ad8ef5de) # develop
    FetchContent_MakeAvailable(explosion)
    target_link_libraries(wuss explosion)

    if(NOT MSVC)
        target_compile_options(wuss PRIVATE -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(wuss PRIVATE -Wl,-dead_strip)
        elseif(NOT EMSCRIPTEN AND NOT TARGET_RISCOS)
            # Not on RISC OS: it strips startup code the SCL stubs need,
            # giving "Branch through zero" before main
            target_link_options(wuss PRIVATE -Wl,--gc-sections)
        endif()
    endif()

    if(TARGET_RISCOS)
        # native VDU/OS_Mouse frontend, no SDL
    elseif(EMSCRIPTEN)
        # Emscripten ships its own SDL3 port; the browser owns the main loop
        # (see emscripten_set_main_loop_arg in apps/wuss/main.c). resources/ is
        # baked into wuss.data, minus resources/test (the unit-test fixtures) and
        # resources/sources (editable artwork sources), and
        # mounted at MEMFS /resources so the runtime file loads and the
        # font picker's opendir() resolve.
        target_compile_definitions(wuss PRIVATE USE_SDL)
        target_compile_options(wuss PRIVATE "-sUSE_SDL=3")
        target_link_options(wuss PRIVATE
            "-sUSE_SDL=3"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sEXIT_RUNTIME=0"
            "--preload-file"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources@/resources"
            "SHELL:--exclude-file */resources/test"
            "SHELL:--exclude-file */resources/sources"
            "--shell-file"
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/wuss/shell.html")
        set_target_properties(wuss PROPERTIES
            SUFFIX ".html"
            LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/apps/wuss/shell.html")
    else()
        find_package(SDL3 REQUIRED)
        target_compile_definitions(wuss PRIVATE USE_SDL)
        target_link_libraries(wuss SDL3::SDL3)
    endif()

    # The Chase H.Q. and Great Escape tasks run ZX Spectrum game ports on a
    # pthread, so desktop only. FetchContent only fetches the sources (no
    # CMakeLists.txt in SOURCE_SUBDIR); the libraries are built here. For local
    # work on the ports, configure with e.g.
    # -DFETCHCONTENT_SOURCE_DIR_CHASEHQ=../ChaseHQ
    if(NOT TARGET_RISCOS AND NOT EMSCRIPTEN AND NOT MSVC)
        FetchContent_Declare(chasehq
                             GIT_REPOSITORY https://github.com/dpt/ChaseHQ.git
                             GIT_TAG 05ce4bdda77843a9ed27141c3f8257164ec61bed # develop
                             SOURCE_SUBDIR none)
        FetchContent_Declare(greatescape
                             GIT_REPOSITORY https://github.com/dpt/The-Great-Escape-in-C.git
                             GIT_TAG 8a3f5c572716851999018c4d13b7a29f7f91dd17 # master
                             SOURCE_SUBDIR none)
        FetchContent_MakeAvailable(chasehq greatescape)
        find_package(Threads REQUIRED)

        # https://github.com/dpt/ChaseHQ
        set(CHQ_DIR "${chasehq_SOURCE_DIR}/C")
        set(CHQ "${CHQ_DIR}/libraries")
        add_library(chasehq STATIC
            ${CHQ}/ChaseHQ/Data/CommonData.c
            ${CHQ}/ChaseHQ/Data/LoadingScreen.c
            ${CHQ}/ChaseHQ/Data/SoundSamples.c
            ${CHQ}/ChaseHQ/Data/Stage1Data.c
            ${CHQ}/ChaseHQ/Data/Stage2Data.c
            ${CHQ}/ChaseHQ/Data/Stage3Data.c
            ${CHQ}/ChaseHQ/Data/Stage4Data.c
            ${CHQ}/ChaseHQ/Data/Stage5Data.c
            ${CHQ}/ChaseHQ/Data/Stages.c
            ${CHQ}/ChaseHQ/Engine/Bank3.c
            ${CHQ}/ChaseHQ/Engine/Bank7.c
            ${CHQ}/ChaseHQ/Engine/Create.c
            ${CHQ}/ChaseHQ/Engine/Main.c
            ${CHQ}/ZXSpectrum/Kempston.c
            ${CHQ}/ZXSpectrum/Keyboard.c
            ${CHQ}/ZXSpectrum/Screen.c
            ${CHQ}/ZXSpectrum/Spectrum.c
            ${CHQ}/ZXSpectrum/Z80.c)
        # The Great Escape ships its own ZXSpectrum/*.h, so the include dir is
        # handed only to the file that needs it, not to all of wuss
        target_include_directories(chasehq
            PRIVATE "${CHQ_DIR}/include" "${CHQ}")
        set_source_files_properties(apps/wuss/tasks/chasehq/chasehq.c
            PROPERTIES INCLUDE_DIRECTORIES "${CHQ_DIR}/include")
        # third-party code: its warnings are policed by its own build
        target_compile_options(chasehq PRIVATE -w)
        target_link_libraries(chasehq PUBLIC Threads::Threads)
        target_link_libraries(wuss chasehq)
        target_compile_definitions(wuss PRIVATE WUSS_CHASEHQ)

        # https://github.com/dpt/The-Great-Escape-in-C
        set(TGE_DIR "${greatescape_SOURCE_DIR}")
        set(TGE "${TGE_DIR}/libraries")
        add_library(greatescape STATIC
            ${TGE}/TheGreatEscape/Data/ExteriorTiles.c
            ${TGE}/TheGreatEscape/Data/Font.c
            ${TGE}/TheGreatEscape/Data/InteriorObjectDefs.c
            ${TGE}/TheGreatEscape/Data/InteriorTiles.c
            ${TGE}/TheGreatEscape/Data/ItemBitmaps.c
            ${TGE}/TheGreatEscape/Data/Map.c
            ${TGE}/TheGreatEscape/Data/Masks.c
            ${TGE}/TheGreatEscape/Data/Music.c
            ${TGE}/TheGreatEscape/Data/RoomDefs.c
            ${TGE}/TheGreatEscape/Data/SpriteBitmaps.c
            ${TGE}/TheGreatEscape/Data/Sprites.c
            ${TGE}/TheGreatEscape/Data/StaticGraphics.c
            ${TGE}/TheGreatEscape/Data/StaticTiles.c
            ${TGE}/TheGreatEscape/Data/SuperTiles.c
            ${TGE}/TheGreatEscape/Engine/Create.c
            ${TGE}/TheGreatEscape/Engine/Debug.c
            ${TGE}/TheGreatEscape/Engine/Events.c
            ${TGE}/TheGreatEscape/Engine/Input.c
            ${TGE}/TheGreatEscape/Engine/Main.c
            ${TGE}/TheGreatEscape/Engine/Menu.c
            ${TGE}/TheGreatEscape/Engine/Messages.c
            ${TGE}/TheGreatEscape/Engine/Screen.c
            ${TGE}/TheGreatEscape/Engine/Text.c
            ${TGE}/TheGreatEscape/Engine/Utils.c
            ${TGE}/TheGreatEscape/Engine/Zoombox.c
            ${TGE}/ZXSpectrum/Kempston.c
            ${TGE}/ZXSpectrum/Keyboard.c
            ${TGE}/ZXSpectrum/Screen.c
            ${TGE}/ZXSpectrum/Spectrum.c)
        target_include_directories(greatescape
            PRIVATE "${TGE_DIR}/include"
                    "${TGE}/TheGreatEscape/include"
                    "${TGE}/ZXSpectrum/include")
        # Chase H.Q. links its own ZX Spectrum layer under the same names, so
        # this copy, and the task file calling it, is renamed
        set(TGE_ZX_RENAMES)
        foreach(sym zxkempston_assign zxkeyset_assign zxkeyset_clear
                    zxkeyset_clearchar zxkeyset_for_port zxkeyset_setchar
                    zxscreen_convert zxscreen_convert16
                    zxspectrum_claim_screen zxspectrum_create
                    zxspectrum_destroy zxspectrum_release_screen)
            list(APPEND TGE_ZX_RENAMES "${sym}=tge_${sym}")
        endforeach()
        target_compile_definitions(greatescape PRIVATE ${TGE_ZX_RENAMES})
        # its ZX layer converts the screen to 4bpp only on RISC OS, keyed on
        # __riscos, which affects nothing else in Spectrum.c. ponytail:
        # replace with a TGE build option if it ever gains one
        set_source_files_properties(${TGE}/ZXSpectrum/Spectrum.c
            PROPERTIES COMPILE_DEFINITIONS "__riscos")
        set_source_files_properties(apps/wuss/tasks/greatescape/greatescape.c
            PROPERTIES
                INCLUDE_DIRECTORIES "${TGE_DIR}/include"
                COMPILE_DEFINITIONS "${TGE_ZX_RENAMES}")
        # third-party code: its warnings are policed by its own build
        target_compile_options(greatescape PRIVATE -w)
        target_link_libraries(greatescape PUBLIC Threads::Threads)
        target_link_libraries(wuss greatescape)
        target_compile_definitions(wuss PRIVATE WUSS_GREATESCAPE)
    endif()

    if(NOT MSVC)
        target_link_libraries(wuss m)
    endif()

    install(TARGETS wuss RUNTIME
        DESTINATION ${CMAKE_INSTALL_PREFIX})
endif()
