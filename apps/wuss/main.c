/* wuss/main.c -- Wuss - interactive minimal window manager demo */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/debug.h"
#include "base/result.h"
#include "base/utils.h"
#include "framebuf/bitmap.h"
#include "framebuf/bmfont.h"
#include "framebuf/bmfontfamily.h"
#include "framebuf/colour.h"
#include "framebuf/palettes.h"
#include "framebuf/pixelfmt.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "io/path.h"
#include "wuss/icon.h"
#include "wuss/task.h"
#include "wuss/wuss.h"

#include "frontend.h"
#include "options.h"
#include "pointer.h"
#include "script.h"
#include "tasks/tasks.h"

#include "tasks/appsfiler/appsfiler.h"
#include "tasks/taskmanager/taskmanager.h"
#include "tasks/configuring/configuring.h"
#include "tasks/display/display.h"
#include "tasks/reading/reading.h"
#include "tasks/palette/palette.h" /* palette_load_hex for the startup *.hex */
#ifdef WUSS_ICONBAR
#include "tasks/filing/filing.h" /* filing_set_root for -root */
#endif

/* ----------------------------------------------------------------------- */

/* one iteration of the event/redraw loop; a struct because Emscripten drives
 * it as a callback (emscripten_set_main_loop_arg) rather than a plain while */
struct wuss_frame_ctx
{
  wuss_t          *wuss;
  wuss_frontend_t *frontend;
  bitmap_t        *bm;
  unsigned char   *pixels;
  int              rowbytes;
  int              scr_width;
  int              scr_height;
  colour_t        *palette;
  int              npalette;
};

static struct
{
  /* run_wuss's framebuffer bitmap and the screen_t wrapping it for wuss:
   * file-scope, like g_tasks, since run_wuss runs at most once per process
   * and app_set_mode (called from the Display task, well after run_wuss's
   * own locals have gone out of scope) needs to reallocate and re-derive
   * them. */
  bitmap_t bm;
  screen_t scr;

  /* file scope so app_set_mode (called from the Display task, long after
   * run_wuss's own locals are gone) can update pixels/rowbytes/scr_width/
   * scr_height on the very instance the main loop below is reading */
  struct wuss_frame_ctx frame_ctx;

  /* --script given: script_poll feeds input ahead of the frontend's own */
  bool         scripted;

  /* wuss_set_time's clock while scripted, in place of the frontend's */
  unsigned int script_ms;
}
g;

/* Fill config with the chrome colours for the PICO-8 (default) or RISC OS
 * 16-colour Wimp palette. */
static void fill_chrome_config(wuss_config_t *config, int use_wimp16)
{
  /* fields left at 0 (mouse timings, ...) take wuss's defaults */
  memset(config, 0, sizeof(*config));

  config->titlebar_height = 0;
  config->backdrop.fill.pattern = screen_PATTERN_DOTS;

  if (use_wimp16)
  {
    config->furniture.title.bg        = palette_WIMP16_GREY_75;
    config->furniture.title.fg        = palette_WIMP16_BLACK;
    config->furniture.title.focus_bg  = palette_WIMP16_CREAM;
    config->furniture.outline         = palette_WIMP16_BLACK;
    config->furniture.back            = palette_WIMP16_GREEN;
    config->furniture.close           = palette_WIMP16_RED;
    config->furniture.toggle          = palette_WIMP16_ORANGE;
    config->furniture.resize          = palette_WIMP16_LIGHT_BLUE;
    config->furniture.scroll.arrows   = palette_WIMP16_GREY_50;
    config->furniture.scroll.wells    = palette_WIMP16_GREY_62;
    config->furniture.scroll.sausages = palette_WIMP16_GREY_87;
    config->furniture.scroll.active   = palette_WIMP16_CREAM;
    config->furniture.pressed         = palette_WIMP16_ORANGE;
    config->bevel.light               = palette_WIMP16_WHITE;
    config->bevel.dark                = palette_WIMP16_GREY_50;
    config->bevel.divider             = palette_WIMP16_GREY_75;
    config->button.bg                 = palette_WIMP16_GREY_87;
    config->button.fg                 = palette_WIMP16_BLACK;
    config->button.pressed            = palette_WIMP16_GREY_62;
    config->accent.colour             = palette_WIMP16_ORANGE;
    config->backdrop.fill.colour           = palette_WIMP16_GREY_37;
    config->backdrop.fill.pattern_bg       = palette_WIMP16_GREY_50;
    config->body.window               = palette_WIMP16_GREY_87;
    config->body.menu                 = palette_WIMP16_WHITE;
    config->slider.track              = palette_WIMP16_WHITE;
    config->slider.value              = palette_WIMP16_GREY_50;
    config->slider.surround           = wuss_NO_BACKGROUND;
  }
  else
  {
    config->furniture.title.bg        = palette_PICO8_DARK_BLUE;
    config->furniture.title.fg        = palette_PICO8_WHITE;
    config->furniture.title.focus_bg  = palette_PICO8_BLUE;
    config->furniture.outline         = palette_PICO8_BLACK;
    config->furniture.back            = palette_PICO8_GREEN;
    config->furniture.close           = palette_PICO8_RED;
    config->furniture.toggle          = palette_PICO8_ORANGE;
    config->furniture.resize          = palette_PICO8_LAVENDER;
    config->furniture.scroll.arrows   = palette_PICO8_BLUE;
    config->furniture.scroll.wells    = palette_PICO8_DARK_BLUE;
    config->furniture.scroll.sausages = palette_PICO8_LIGHT_GREY;
    config->furniture.scroll.active   = palette_PICO8_WHITE;
    config->furniture.pressed         = palette_PICO8_ORANGE;
    config->bevel.light               = palette_PICO8_WHITE;
    config->bevel.dark                = palette_PICO8_DARK_GREY;
    config->bevel.divider             = palette_PICO8_LAVENDER;
    config->button.bg                 = palette_PICO8_LIGHT_GREY;
    config->button.fg                 = palette_PICO8_BLACK;
    config->button.pressed            = palette_PICO8_LAVENDER;
    config->accent.colour             = palette_PICO8_ORANGE;
    config->backdrop.fill.colour           = palette_PICO8_LAVENDER;
    config->backdrop.fill.pattern_bg       = palette_PICO8_LIGHT_GREY;
    config->body.window               = palette_PICO8_LIGHT_GREY;
    config->body.menu                 = palette_PICO8_WHITE;
    config->slider.track              = palette_PICO8_WHITE;
    config->slider.value              = palette_PICO8_DARK_GREY;
    config->slider.surround           = wuss_NO_BACKGROUND;
  }
}

/* Redraw the whole screen one pixel at a time: each wuss_redraw_dirty call is
 * flushed before the next pixel is invalidated, so no two pixels are ever
 * coalesced into one redraw. A task whose drawing routine assumes it always
 * gets a multi-pixel/aligned clip (rather than trusting scr->clip) will
 * visibly misdraw here even though it looks fine under larger dirty regions. */
static void pixel_stress(wuss_t *wuss, int scr_width, int scr_height)
{
  int x, y;

  for (y = 0; y < scr_height; y++)
  {
    for (x = 0; x < scr_width; x++)
    {
      box_t px;

      px.x0 = x;     px.y0 = y;
      px.x1 = x + 1; px.y1 = y + 1;

      wuss_invalidate(wuss, &px);
      wuss_redraw_dirty(wuss);
    }
  }
}

/* ----------------------------------------------------------------------- */

#ifndef __EMSCRIPTEN__
/* Save the framebuffer as wuss-N.png in the current directory, N being the
 * first number not already taken. */
static void save_screenshot(void)
{
  char  name[32];
  FILE *f;
  int   n;

  for (n = 1; n < 10000; n++)
  {
    snprintf(name, sizeof(name), "wuss-%d.png", n);
    f = fopen(name, "rb");
    if (f == NULL)
      break;

    fclose(f);
  }

  if (script_save_png(&g.bm, name) != result_OK)
    logf_error("screenshot save failed: %s", name);
}
#endif

/* Next input for this frame: the script's until it ends its part of the
 * frame, then the frontend's. */
static bool frame_poll(struct wuss_frame_ctx *c,
                       bool                  *scripting,
                       wuss_input_t          *ev)
{
  if (*scripting && script_poll(c->wuss, c->bm, ev))
    return true;

  *scripting = false;
  return wuss_frontend_poll(c->frontend, ev);
}

static void wuss_frame(void *arg)
{
  struct wuss_frame_ctx *c = arg;
  bool                 scripting;
  wuss_input_t         ev;
  bool                 move_pending;
  point_t              move_pos;
  wuss_key_modifiers_t move_mods;
  bool                 garbage;
  bool                 stress;
  bool                 redraw_all;
  box_t                old_pointer;
  bool                 had_pointer;
  box_t                new_pointer;
  bool                 has_pointer;

  /* before anything below can draw: see pointer.h */
  had_pointer = pointer_undraw(c->bm, &old_pointer);

  /* a script runs on a synthetic 50Hz clock so its double-clicks (and their
   * absence across a "wait") don't depend on how fast frames go headless */
  if (g.scripted)
    g.script_ms += 20;
  wuss_set_time(c->wuss, g.scripted ? g.script_ms
                                    : wuss_frontend_ms(c->frontend));

  /* A move is only delivered once the pointer has stopped for a poll, or
   * another event needs it in place first. A frontend that samples the
   * pointer (RISC OS) reports a fresh move on every poll while it travels,
   * and handling each one -- a window drag blits and queues dirty slivers
   * per move -- keeps this loop spinning, so the dirty list overflows and
   * nothing is redrawn until the pointer stops. */
  move_pending = false;
  move_pos     = POINT(0, 0);
  move_mods    = wuss_KEY_MOD_NONE;

  scripting = g.scripted;
  while (frame_poll(c, &scripting, &ev))
  {
    if (ev.kind == wuss_INPUT_MOUSE_MOVE ||
        ev.kind == wuss_INPUT_MOUSE_DOWN ||
        ev.kind == wuss_INPUT_MOUSE_UP ||
        ev.kind == wuss_INPUT_WHEEL)
      ev.pos = pointer_clamp(c->wuss, ev.pos, g.scripted);

    if (ev.kind == wuss_INPUT_MOUSE_MOVE)
    {
      pointer_moved(ev.pos);
      move_pos     = ev.pos;
      move_mods    = ev.mods;
      move_pending = true;
      continue;
    }

    if (move_pending)
    {
      wuss_set_modifiers(c->wuss, move_mods);
      wuss_mouse_move(c->wuss, move_pos, NULL);
      move_pending = false;
    }

    switch (ev.kind)
    {
    case wuss_INPUT_MOUSE_DOWN:
    case wuss_INPUT_MOUSE_UP:
    case wuss_INPUT_WHEEL:
      pointer_moved(ev.pos);
      break;
    case wuss_INPUT_MOUSE_LEAVE:
      pointer_left();
      break;
    default:
      break;
    }

    switch (ev.kind)
    {
    case wuss_INPUT_QUIT:
      g_tasks.quit = true;
      break;

    case wuss_INPUT_MOUSE_DOWN:
      wuss_set_modifiers(c->wuss, ev.mods);
      wuss_mouse_click(c->wuss, ev.pos, ev.button, wuss_MOUSE_DOWN, NULL);
      break;

    case wuss_INPUT_MOUSE_UP:
      wuss_set_modifiers(c->wuss, ev.mods);
      wuss_mouse_click(c->wuss, ev.pos, ev.button, wuss_MOUSE_UP, NULL);
      break;

    case wuss_INPUT_WHEEL:
      wuss_scroll(c->wuss, ev.pos, ev.wheel, NULL);
      break;

    case wuss_INPUT_KEY:
      {
        int          claimed;
        unsigned int gen;

        /* system chords, then the focused window, then the launcher menu's
         * shortcuts */
        if (tasks_system_key(ev.key, ev.mods))
          break;

#ifndef __EMSCRIPTEN__
        /* Ctrl-Shift-3: save the screen as wuss-N.png */
        if ((ev.mods & wuss_KEY_MOD_CTRL) && (ev.mods & wuss_KEY_MOD_SHIFT) &&
            (ev.key == '3' || ev.key == '#'))
        {
          save_screenshot();
          break;
        }
#endif

        /* paste reads the host clipboard; copy/cut publish to it */
        if ((ev.mods & wuss_KEY_MOD_CTRL) && (ev.key == 'v' || ev.key == 'V'))
          wuss_frontend_clipboard_pull(c->frontend, c->wuss);
        else if (ev.key == wuss_KEY_INSERT)
          wuss_frontend_clipboard_pull(c->frontend, c->wuss);

        gen = wuss_clipboard_generation(c->wuss);
        wuss_key(c->wuss, ev.key, ev.mods, &claimed);
        if (wuss_clipboard_generation(c->wuss) != gen)
          wuss_frontend_clipboard_set(c->frontend, wuss_clipboard_get(c->wuss));

        if (!claimed)
          (void) tasks_launcher_key(ev.key, ev.mods);
      }
      break;

    case wuss_INPUT_KEY_UP:
      wuss_key_up(c->wuss, ev.key, ev.mods);
      break;

    default:
      break;
    }
  }

  if (move_pending)
  {
    wuss_set_modifiers(c->wuss, move_mods);
    wuss_mouse_move(c->wuss, move_pos, NULL);
  }

  wuss_idle(c->wuss);
  tasks_reap_apps();

  wuss_frontend_capture_mouse(c->frontend,
                              wuss_is_dragging(c->wuss) || wuss_is_capturing(c->wuss));

  /* the launcher's Debug picks only set flags; act on them once per frame */
  garbage    = g_tasks.debug_garbage;
  stress     = g_tasks.debug_pixel_stress;
  redraw_all = g_tasks.debug_redraw_all;
  g_tasks.debug_garbage      = false;
  g_tasks.debug_pixel_stress = false;
  g_tasks.debug_redraw_all   = false;

  if (garbage)
  {
    /* corrupt the whole framebuffer and present it, then leave it alone --
     * wuss only repaints what it knows is dirty, so the junk stays put
     * until something else invalidates the screen */
    unsigned char *p;
    size_t         n;
    size_t         i;

    p = c->pixels;
    n = (size_t) c->rowbytes * c->scr_height;
    for (i = 0; i < n; i++)
      p[i] = (unsigned char) rand();

    (void) pointer_draw(c->bm, &new_pointer);
    wuss_frontend_present(c->frontend, c->bm, NULL);
  }
  else if (stress)
  {
    pixel_stress(c->wuss, c->scr_width, c->scr_height);
    (void) pointer_draw(c->bm, &new_pointer);
    wuss_frontend_present(c->frontend, c->bm, NULL);
  }
  else
  {
    box_t dirty, region, touched;
    int   ndirty, i, have_touched, have_any;

    ndirty = wuss_get_dirty_count(c->wuss);
    dirty  = (box_t) BOX_INIT;
    for (i = 0; i < ndirty; i++)
    {
      wuss_get_dirty(c->wuss, i, &region);
      box_union(&dirty, &region, &dirty);
    }

    /* Pixels a fast blit path (window move/resize, scroll) slid to a new
     * position without a repaint: wuss_redraw_dirty won't touch them, but
     * present() still has to re-upload them -- fold them into the same
     * bounding box before wuss_clear_touched drops them. */
    have_touched = wuss_get_touched_extent(c->wuss, &touched);
    have_any     = (ndirty > 0) || have_touched;
    if (have_touched)
      box_union(&dirty, &touched, &dirty);

    wuss_redraw_dirty(c->wuss);
    wuss_clear_touched(c->wuss);

    /* where the pointer was and now is need presenting too */
    has_pointer = pointer_draw(c->bm, &new_pointer);
    if (had_pointer)
      box_union(&dirty, &old_pointer, &dirty);
    if (has_pointer)
      box_union(&dirty, &new_pointer, &dirty);
    have_any = have_any || had_pointer || has_pointer;

    /* a Debug > Redraw earlier this frame repainted the whole pixel
     * buffer; any dirty/touched region collected afterwards (e.g. a mouse
     * move) is narrower than that and must not shrink the present rect
     * below full-screen, or the frontend only re-uploads the narrow rect
     * and leaves the rest of the previous frame's pixels on screen. */
    wuss_frontend_present(c->frontend, c->bm,
                          (have_any && !redraw_all) ? &dirty : NULL);
  }

#ifdef __EMSCRIPTEN__
  if (g_tasks.quit)
    emscripten_cancel_main_loop();
#endif
}

/* fonts[]/descs[] below hold [0] regular (system font), [1] bold, [2] symbol
 * (menu tick / submenu arrow); kept <= wuss_MAX_FONTS so wuss_create's own
 * nfonts check is never reached with arrays it can no longer fit in */
#define WUSS_MAIN_NFONTS 3
#if WUSS_MAIN_NFONTS > wuss_MAX_FONTS
#error WUSS_MAIN_NFONTS exceeds wuss_MAX_FONTS
#endif

/* click windows to bring to front, drag titlebars to move; the redraw-all
 * input redraws the whole screen, the pixel-stress input does it one pixel at
 * a time to catch tasks that misbehave under a 1x1 clip; the palette task's
 * picker menu swaps the system palette live (wuss_set_palette, picked up by
 * menu_handle's wuss_EVENT_PALETTE case); the quit input or closing the
 * window exits */
static result_t run_wuss(const char *resources,
                         const char *palette_name,
                         const char *font_family,
                         int         depth,
                         int         scale,
                         int         scr_width,
                         int         scr_height,
                         const char *tasks)
{
  static const char symbols_name[] = "Symbols Regular";

  result_t            rc;
  bmfontfamily_t     *family;
  const bmfontface_t *regular;
  const bmfontface_t *bold;
  const char         *filename;
  char                font_path[512];
  char                names_buf[2][128];
  const char         *names[WUSS_MAIN_NFONTS];
  bmfont_t           *fonts[WUSS_MAIN_NFONTS];
  int                 nfonts;
  int                 i;
  void               *pixels;
  int                 rowbytes;
  pixelfmt_t          fmt;
  colour_t            palette[wuss_SYSTEM_PALETTE_LENGTH]; /* fixed-size UI */
  colour_t            scr_palette[256]; /* palette[] padded out to whatever
                                         * count the chosen depth's bitmap
                                         * needs (p8 reads all 256) */
  wuss_t          *wuss;
  wuss_frontend_t *frontend;
  bool             use_wimp16;
  int              palette_index;
  int              scr_nentries;

  /* everything the Failure path frees, so an early goto frees nothing */
  nfonts    = 0;
  family    = NULL;
  frontend  = NULL;
  wuss      = NULL;

  {
    /* palette_name (from -palette, default "PICO-8") names a *.hex file under
     * resources/palettes, extension stripped, e.g. "RISC-OS". Chrome was
     * never derived from *.hex content (see fill_chrome_config), so it stays
     * keyed by whether the startup file is "RISC-OS" specifically, not by
     * whatever the picker menu later loads. */
    use_wimp16 = (strcmp(palette_name, "RISC-OS") == 0);
    palette_index = use_wimp16 ? 1 : 0;

    rc = palette_load_hex(resources, palette_name, palette);
    if (rc != result_OK)
    {
      logf_error("wuss: palette_load_hex(\"%s\") failed, rc=0x%X (%s)",
                palette_name, rc, wuss_result_string(rc));
      goto Failure;
    }
  }

  logf_info("wuss: resources root = \"%s\"", resources);

  {
    /* [0] regular and [1] bold come from the font_family family (-f); bold
     * is the next heavier face than regular (regular itself if there is
     * none) */
    rc = bmfontfamily_scan(pathf("%s/resources/bmfonts/%s", resources,
                                 font_family),
                           &family);
    if (rc != result_OK)
      goto Failure;
    regular = bmfontfamily_find(family, bmfontfamily_WEIGHT_REGULAR,
                                bmfontfamily_SLANT_UPRIGHT);
    if (regular == NULL)
    {
      logf_error("wuss: font family \"%s\" has no regular upright face",
                 font_family);
      rc = result_NOT_FOUND;
      goto Failure;
    }
    bold = bmfontfamily_heavier(family, regular);

    snprintf(names_buf[0], sizeof(names_buf[0]), "%s %s", font_family,
             regular->style);
    snprintf(names_buf[1], sizeof(names_buf[1]), "%s %s", font_family,
             bold->style);
    names[0] = names_buf[0];
    names[1] = names_buf[1];
    names[2] = symbols_name;

    nfonts = 0;
    for (i = 0; i < WUSS_MAIN_NFONTS; i++)
    {
      if (i < 2)
      {
        filename = (i == 0) ? regular->path : bold->path;
      }
      else
      {
        rc = bmfontfamily_label_path(pathf("%s/resources/bmfonts", resources),
                                     names[i], font_path, sizeof(font_path));
        if (rc != result_OK)
          goto Failure;
        filename = font_path;
      }
      logf_info("wuss: loading font \"%s\"", filename);
      rc = bmfont_create(filename, &fonts[i]);
      if (rc != result_OK)
      {
        logf_error("wuss: bmfont_create(\"%s\") failed, rc=0x%X (%s)", filename,
                   rc, wuss_result_string(rc));
        goto Failure;
      }
      nfonts++;
    }
  }

  rc = wuss_frontend_open(scr_width, scr_height, palette, NELEMS(palette),
                          depth, scale, &pixels, &rowbytes, &fmt, &frontend);
  logf_info("wuss: wuss_frontend_open -> rc=0x%X (%s)", rc, wuss_result_string(rc));
  if (rc != result_OK)
    goto Failure;

  /* bitmap_set_palette reads every entry a paletted format's bitmap_init
   * needs (up to 256 for p8); pad scr_palette with palette's 16 UI colours,
   * then the web-safe 216 (so a p8 screen has real range beyond the UI
   * colours for nearest-match), then black for what's left. Also done on
   * a live palette change; see task_handle_event. 1/2bpp get a grey ramp
   * instead. Unpaletted formats ignore it, so fill the lot. */
  scr_nentries = pixelfmt_paletted_nentries(fmt);
  if (scr_nentries <= 0)
    scr_nentries = NELEMS(scr_palette);
  tasks_build_screen_palette(scr_palette, scr_nentries,
                             palette, NELEMS(palette));
  wuss_frontend_set_palette(frontend, scr_palette, scr_nentries);

  rc = bitmap_init(&g.bm, SIZE2D(scr_width, scr_height), fmt, rowbytes,
                   scr_palette, pixels);
  logf_info("wuss: bitmap_init -> rc=0x%X (%s)", rc, wuss_result_string(rc));
  if (rc != result_OK)
    goto Failure;

  bitmap_clear(&g.bm, palette[palette_PICO8_WHITE]);

  screen_for_bitmap(&g.scr, &g.bm);

  {
    wuss_config_t    config;
    wuss_font_desc_t descs[WUSS_MAIN_NFONTS]; /* slot classes/names for the
                                * picker menus -- [2] Symbols is chrome-only,
                                * never a text font choice */

    fill_chrome_config(&config, use_wimp16);

    for (i = 0; i < nfonts; i++)
    {
      descs[i].font       = fonts[i];
      descs[i].font_class = (i == 2) ? wuss_FONT_CLASS_SYSTEM
                                     : wuss_FONT_CLASS_NONE;
      descs[i].name       = names[i];
    }

    rc = wuss_create(&g.scr, descs, nfonts, palette, NELEMS(palette), &config,
                     NULL, resources, &wuss);
    logf_info("wuss: wuss_create -> rc=0x%X (%s)", rc, wuss_result_string(rc));
    if (rc != result_OK)
      goto Failure;

    /* the desktop logo and the pointer arrow live in the icon pool, which
     * wuss_create has just loaded */
    config.backdrop.image = wuss_iconpool_bitmap(wuss,
                              wuss_iconpool_lookup(wuss, "wuss"));
    if (config.backdrop.image == NULL)
      logf_error("wuss: icon 'wuss' unavailable -- backdrop drawn without it");
    else
      (void) wuss_set_backdrop(wuss, &config.backdrop);

    pointer_load(wuss);
  }

  g_tasks.wuss           = wuss;
  g_tasks.frontend       = frontend;
  g_tasks.bm             = &g.bm;

  /* --crt: the frontend opens plain; switch over now so the Display
   * window's option agrees */
  if (g_tasks.crt)
    tasks_set_crt(true);

  /* --pointer likewise */
  if (g_tasks.pointer)
    tasks_set_pointer(true);

  {
    wuss_task_desc_t desc;

    desc.handle    = task_handle_event;
    desc.task_data = NULL;
    desc.name      = "menu";
    rc = wuss_task_create(wuss, &desc, &g_tasks.menu_task);
    logf_info("wuss: wuss_task_create(menu) -> rc=0x%X (%s)", rc,
              wuss_result_string(rc));
    if (rc != result_OK)
      goto Failure;
  }

  g_tasks.quit = false;

#ifdef WUSS_ICONBAR
  /* Filing's icon goes on the bar ahead of AppsFiler's; installing it is
   * idempotent so the -tasks list below does not add a second */
  tasks_spawn("Filing");

  rc = appsfiler_create(wuss);
  if (rc != result_OK)
    logf_error("wuss: appsfiler_create -> rc=0x%X (%s)", rc,
               wuss_result_string(rc));

  rc = taskmanager_create(wuss);
  if (rc != result_OK)
    logf_error("wuss: taskmanager_create -> rc=0x%X (%s)", rc,
               wuss_result_string(rc));

  /* always present, like the Task Manager; idempotent as above */
  tasks_spawn("Display,Palette");
#endif

  tasks_spawn(tasks);

#ifdef __EMSCRIPTEN__
  (void) configuring_open(wuss);
  (void) display_open(wuss);
  /* show the About text on load */
  (void) reading_open_file(wuss, pathf("%s/resources/text/About_Wuss.md",
                                       wuss_get_resources(wuss)));
#endif

  wuss_redraw(wuss);
  wuss_frontend_present(frontend, &g.bm, NULL);

  g.frame_ctx.wuss       = wuss;
  g.frame_ctx.frontend   = frontend;
  g.frame_ctx.bm         = &g.bm;
  g.frame_ctx.pixels     = pixels;
  g.frame_ctx.rowbytes   = rowbytes;
  g.frame_ctx.scr_width  = scr_width;
  g.frame_ctx.scr_height = scr_height;
  g.frame_ctx.palette    = palette;
  g.frame_ctx.npalette   = NELEMS(palette);

#ifdef __EMSCRIPTEN__
  /* the browser owns the loop; simulate_infinite_loop=1 means this call
   * never returns -- fine, the page dies on navigation. fps=0 asks for
   * requestAnimationFrame pacing. */
  emscripten_set_main_loop_arg(wuss_frame, &g.frame_ctx, 0, 1);
#else
  while (!g_tasks.quit)
    wuss_frame(&g.frame_ctx);
#endif

  rc = result_OK;

  /* Shared by the normal exit and every early failure: each resource is
   * either NULL/unset (see above) or live. */
Failure:

  if (rc != result_OK)
    printf("run_wuss: failed (rc=0x%X: %s)\n", rc, wuss_result_string(rc));

  /* ponytail: wuss_destroy() force-closes every still-open window and frees
   * every registered task node, but not the per-instance task_data block a
   * spawn_* calloc'd, so any task window left open at quit leaks that block.
   * Harmless at process exit. */
  wuss_destroy(wuss); /* also sweeps g.menu_task and closes any open chain,
                       * including the shared proginfo singleton's window */

  pointer_free();

  for (i = 0; i < nfonts; i++)
    bmfont_destroy(fonts[i]);
  bmfontfamily_destroy(family);

  wuss_frontend_close(frontend);

  return (rc == result_OK) ? result_TEST_PASSED : result_TEST_FAILED;
}

/* ----------------------------------------------------------------------- */

result_t app_set_mode(size2d_t size, int depth)
{
  result_t        rc;
  void           *pixels;
  int             rowbytes;
  pixelfmt_t      fmt;
  const colour_t *palette;
  int             npalette;
  colour_t        scr_palette[256];
  int             scr_nentries;

  rc = wuss_frontend_resize(g_tasks.frontend, size.w, size.h,
                            (depth == app_DEPTH_GREY256) ? 8 : depth,
                            &pixels, &rowbytes, &fmt);
  if (rc != result_OK)
    return rc;

  g_tasks.grey = (depth == app_DEPTH_GREY256);

  /* drop the old palette buffer: it's sized for the old format, and
   * bitmap_init would otherwise copy out of it (overrunning if the new
   * format has more entries) and leak it. Rebuilt below. */
  bitmap_set_palette(&g.bm, NULL);
  rc = bitmap_init(&g.bm, size, fmt, rowbytes, NULL, pixels);
  if (rc != result_OK)
    return rc;

  palette      = wuss_get_palette(g_tasks.wuss, &npalette);
  scr_nentries = pixelfmt_paletted_nentries(g.bm.format);
  if (scr_nentries > 0)
  {
    tasks_build_screen_palette(scr_palette, scr_nentries, palette, npalette);
    bitmap_set_palette(&g.bm, scr_palette);
    wuss_frontend_set_palette(g_tasks.frontend, scr_palette, scr_nentries);
  }

  screen_for_bitmap(&g.scr, &g.bm);

  rc = wuss_resize(g_tasks.wuss, &g.scr);
  if (rc != result_OK)
    return rc;

  g.frame_ctx.pixels     = pixels;
  g.frame_ctx.rowbytes   = rowbytes;
  g.frame_ctx.scr_width  = size.w;
  g.frame_ctx.scr_height = size.h;

  return result_OK;
}

result_t tasks_set_screen_palette(const colour_t *palette, int npalette)
{
  bitmap_set_palette(&g.bm, palette);
  wuss_frontend_set_palette(g_tasks.frontend, palette, npalette);

  /* same screen, new physical palette: wuss_resize rematches the fills and
   * icons to it and invalidates the whole screen */
  return wuss_resize(g_tasks.wuss, &g.scr);
}

int app_get_depth(void)
{
  switch (g.bm.format)
  {
  case pixelfmt_rgbx5551: return 15;
  case pixelfmt_rgb565:   return 16;
  case pixelfmt_p8:       return g_tasks.grey ? app_DEPTH_GREY256 : 8;
  default:                return 1 << pixelfmt_log2bpp(g.bm.format);
  }
}

/* ----------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
  /* pathf splices the root and each branch with the platform separator, so
   * the "here" root differs: "." on Unix, but on RISC OS the
   * currently-selected directory is "@" ("." there would give "..resources"). */
#ifdef __riscos
  const char *default_resources = "@";
#else
  const char *default_resources = ".";
#endif
  wuss_options_t opts;
  result_t       rc;

  opts.resources    = default_resources;
  opts.palette_name = "PICO-8";
  opts.font_family  = "DPT-Digits";
  opts.depth        = 4;
  opts.scale        = 0; /* 0 = let the frontend pick its default */
  opts.res_width    = 640;
  opts.res_height   = 480;
#ifdef __EMSCRIPTEN__
  opts.tasks        = "Filing,Configuring,Display,Reading"; /* browser has no command line */
#else
  opts.tasks        = "Filing,Display";
#endif
  opts.root       = NULL; /* NULL: leave filing_set_root's own default */
  opts.script       = NULL;

  if (!options_parse(argc, argv, &opts))
    return EXIT_FAILURE;

#ifdef WUSS_ICONBAR
  filing_set_root(opts.root);
  filing_set_run_fn(tasks_run_type);
#endif

  if (opts.script != NULL)
  {
    if (script_open(opts.script) != result_OK)
      return EXIT_FAILURE;
    g.scripted = true;
  }

  rc = run_wuss(opts.resources, opts.palette_name, opts.font_family,
               opts.depth, opts.scale, opts.res_width, opts.res_height, opts.tasks);

  if (g.scripted && script_close() != result_OK)
    rc = result_TEST_FAILED;

  return rc == result_TEST_PASSED ? EXIT_SUCCESS : EXIT_FAILURE;
}
