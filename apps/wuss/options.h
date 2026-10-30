/* wuss/options.h -- command-line options for the Wuss demo */

#ifndef WUSS_OPTIONS_H
#define WUSS_OPTIONS_H

#ifdef WUSS_APP

#include <stdbool.h>

/* Parsed command-line options. Members are use-ordered to match run_wuss's
 * parameter list. */
typedef struct wuss_options
{
  const char *resources;    /* -r/--resources: fixture root */
  const char *palette_name; /* -p/--palette: startup *.hex leafname */
  const char *font_family;  /* -f/--font: system font family directory name */
  int         depth;        /* -d/--depth: framebuffer bpp (1, 2, 4, 8, 15, 16 or 32) */
  int         scale;        /* -s/--scale: initial window zoom, 0 = default */
  int         res_width;    /* --res WIDTHxHEIGHT: screen size in pixels */
  int         res_height;
  const char *tasks;        /* -t/--tasks: comma-separated launcher task
                             * names to auto-open at startup, or "all";
                             * default "Filing,Display"
                             * ("Filing,Configure,Reading" under
                             * Emscripten); giving -t replaces it */
  const char *root;         /* --root: directory Filing's icon bar icon and
                             * System menu row open; default "." (or "/"
                             * under Emscripten, see filing_set_root) */
  const char *script;       /* --script: input script to run; see script.h */
}
wuss_options_t;

/* Parse argv into opts, which holds the defaults on entry. --crt and
 * --pointer set g_tasks.crt and g_tasks.pointer directly. Returns false,
 * having printed why, on an unknown option or a bad argument. */
bool options_parse(int argc, char *argv[], wuss_options_t *opts);

#endif /* WUSS_APP */

#endif /* WUSS_OPTIONS_H */
