/* wuss/theme.c -- desktop themes: font, metrics, icons and palette */

#ifdef WUSS_APP

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "framebuf/palettes.h"
#include "io/namelist.h"
#include "io/path.h"

#include "theme.h"

/* ----------------------------------------------------------------------- */

#define THEME_EXT ".theme"

static char g_current[32];

/* ----------------------------------------------------------------------- */

/* Copy src into dst (size cap), dropping trailing whitespace. */
static void theme_copy(char *dst, size_t cap, const char *src)
{
  size_t n;

  n = strlen(src);
  while (n > 0 && isspace((unsigned char) src[n - 1]))
    n--;
  if (n >= cap)
    n = cap - 1;

  memcpy(dst, src, n);
  dst[n] = '\0';
}

result_t theme_load(const char *resources, const char *name, theme_t *out)
{
  FILE *f;
  char  line[256];
  char *key;
  char *value;

  memset(out, 0, sizeof(*out));
  theme_copy(out->name,    sizeof(out->name),    name);
  theme_copy(out->font,    sizeof(out->font),    "DPT-Digits");
  theme_copy(out->palette, sizeof(out->palette), "PICO-8");
  theme_copy(out->icons,   sizeof(out->icons),   "wuss/iconpool");

  f = fopen(pathf("%s/resources/themes/%s" THEME_EXT, resources, name), "r");
  if (f == NULL)
    return result_FILE_NOT_FOUND;

  while (fgets(line, sizeof(line), f) != NULL)
  {
    key = line;
    while (isspace((unsigned char) *key))
      key++;
    if (*key == '#' || *key == '\0')
      continue;

    value = key;
    while (*value != '\0' && !isspace((unsigned char) *value))
      value++;
    if (*value != '\0')
      *value++ = '\0';
    while (isspace((unsigned char) *value))
      value++;

    if (strcmp(key, "font") == 0)
      theme_copy(out->font, sizeof(out->font), value);
    else if (strcmp(key, "palette") == 0)
      theme_copy(out->palette, sizeof(out->palette), value);
    else if (strcmp(key, "titlebar") == 0)
      out->titlebar = atoi(value);
    else if (strcmp(key, "icons") == 0)
      theme_copy(out->icons, sizeof(out->icons), value);
  }

  fclose(f);

  return result_OK;
}

void theme_fill_config(const theme_t *theme, wuss_config_t *config)
{
  memset(config, 0, sizeof(*config));

  config->titlebar_height       = theme->titlebar;
  config->backdrop.fill.pattern = screen_PATTERN_DOTS;

  if (strcmp(theme->palette, "RISC-OS") == 0)
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
    config->backdrop.fill.colour      = palette_WIMP16_GREY_37;
    config->backdrop.fill.pattern_bg  = palette_WIMP16_GREY_50;
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
    config->backdrop.fill.colour      = palette_PICO8_LAVENDER;
    config->backdrop.fill.pattern_bg  = palette_PICO8_LIGHT_GREY;
    config->body.window               = palette_PICO8_LIGHT_GREY;
    config->body.menu                 = palette_PICO8_WHITE;
    config->slider.track              = palette_PICO8_WHITE;
    config->slider.value              = palette_PICO8_DARK_GREY;
    config->slider.surround           = wuss_NO_BACKGROUND;
  }
}

result_t theme_list(const char *resources,
                    char        names[THEME_MAX][32],
                    int        *count)
{
  return namelist_scan(pathf("%s/resources/themes", resources), THEME_EXT,
                       names[0], sizeof(names[0]), THEME_MAX, 1 /* sorted */,
                       count);
}

const char *theme_current(void)
{
  return g_current;
}

void theme_set_current(const char *name)
{
  theme_copy(g_current, sizeof(g_current), name);
}

#endif /* WUSS_APP */
