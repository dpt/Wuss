/* wuss/theme.h -- desktop themes: font, metrics, icons and palette */

#ifndef WUSS_THEME_H
#define WUSS_THEME_H

#ifdef WUSS_APP

#include "base/result.h"
#include "wuss/wuss.h"

/* Most themes the selector lists */
#define THEME_MAX 32

/* A theme, as read from "resources/themes/<name>.theme": lines of "key
 * value", '#' starting a comment. Keys:
 *
 *   font     system font family under resources/bmfonts
 *   palette  *.hex leafname under resources/wuss-tasks/palettes
 *   titlebar titlebar height in pixels, 0 to derive it from the font
 *   icons    icon pool directory under resources
 *
 * A missing key keeps the Default theme's value. */
typedef struct theme
{
  char name[32];
  char font[64];
  char palette[64];
  int  titlebar;
  char icons[64];
}
theme_t;

/* Read theme "name" from resources into out. */
result_t theme_load(const char *resources, const char *name, theme_t *out);

/* Fill config with theme's chrome colours and titlebar height. Chrome is
 * keyed by the palette: "RISC-OS" takes the Wimp colours, anything else the
 * PICO-8 ones. Fields left at 0 take wuss's defaults. */
void theme_fill_config(const theme_t *theme, wuss_config_t *config);

/* Collect the theme names under resources into names, sorted. */
result_t theme_list(const char *resources,
                    char        names[THEME_MAX][32],
                    int        *count);

/* The theme last set by theme_set_current, "" before then. */
const char *theme_current(void);

void theme_set_current(const char *name);

#endif /* WUSS_APP */

#endif /* WUSS_THEME_H */
