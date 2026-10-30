/* wuss/gadget/tagcloud.c -- a cloud or list of tags on a caller's window */

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/result.h"
#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "framebuf/colour.h"
#include "framebuf/screen.h"
#include "geom/box.h"
#include "geom/point.h"
#include "geom/size.h"

#include "wuss/window.h"

#include "wuss/gadget/tagcloud.h"

#include "../core/types.h"
#include "../core/palette.h"
#include "../font/font.h"
#include "../icon.h"

/* ----------------------------------------------------------------------- */

/* drawn between neighbouring tags on a cloud line. The RISC OS original
 * uses a middle dot, but the bitmap fonts have no glyph for it */
#define TAGCLOUD_SEPARATOR " | "

/* count bands scaling shades tags into */
#define TAGCLOUD_BANDS 5

typedef struct tagcloud_entry
{
  char  *name;
  int    length;
  int    count;
  int    highlighted;
  int    index;       /* as passed to set_tags */
  int    group;       /* sort key ahead of the name or count */
  int    separator;   /* a separator follows on its cloud line */
  box_t  box;         /* document coordinates, set by layout */
}
tagcloud_entry_t;

struct wuss_tagcloud
{
  alloc_t                    alloc;   /* copied hooks; wuss_t not retained */
  wuss_window_t             *window;
  wuss_tagcloud_toggle_fn_t *toggle;
  void                      *opaque;
  void                      *block;   /* entries, order and names */
  tagcloud_entry_t          *entries; /* by index */
  tagcloud_entry_t         **order;   /* sorted */
  int                        ntags;
  int                        min, max; /* count range */
  wuss_tagcloud_options_t    options;
  int                        hover;   /* tag under the pointer, -1 for none */
};

/* ----------------------------------------------------------------------- */

static int tagcloud_compare_name(const void *va, const void *vb)
{
  const tagcloud_entry_t *a;
  const tagcloud_entry_t *b;
  int                     d;

  a = *(const tagcloud_entry_t *const *) va;
  b = *(const tagcloud_entry_t *const *) vb;
  d = a->group - b->group;
  if (d == 0)
    d = strcasecmp(a->name, b->name);
  if (d == 0)
    d = a->index - b->index;
  return d;
}

static int tagcloud_compare_count(const void *va, const void *vb)
{
  const tagcloud_entry_t *a;
  const tagcloud_entry_t *b;
  int                     d;

  a = *(const tagcloud_entry_t *const *) va;
  b = *(const tagcloud_entry_t *const *) vb;
  d = a->group - b->group;
  if (d == 0)
    d = b->count - a->count;
  if (d == 0)
    d = a->index - b->index;
  return d;
}

static void tagcloud_sort(wuss_tagcloud_t *tc)
{
  int i;

  for (i = 0; i < tc->ntags; i++)
    tc->entries[i].group = tc->options.selected_first &&
                           !tc->entries[i].highlighted;

  qsort(tc->order, (size_t) tc->ntags, sizeof(*tc->order),
        tc->options.sort == wuss_TAGCLOUD_SORT_NAME ? tagcloud_compare_name
                                                    : tagcloud_compare_count);
}

/* ----------------------------------------------------------------------- */

static bmfont_t *tagcloud_font(const wuss_t *wuss, const tagcloud_entry_t *e)
{
  return e->highlighted ? wuss__bold_font(wuss) : wuss->fonts.fonts[0];
}

static int tagcloud_entry_width(const wuss_t           *wuss,
                                const tagcloud_entry_t *e)
{
  bmfont_t      *font;
  bmfont_width_t w;

  font = tagcloud_font(wuss, e);
  if (font == NULL)
    return 0;

  wuss__text_measure(font, e->name, e->length, INT_MAX, NULL, &w);
  return (int) w;
}

/* Pack the tags into lines no wider than "avail", each line centred.
 * Returns the number of lines. */
static int tagcloud_layout_cloud(wuss_tagcloud_t *tc,
                                 int              avail,
                                 int              pad,
                                 int              line_h,
                                 int              leading)
{
  wuss_t           *wuss;
  tagcloud_entry_t *e;
  int               sep_w, x, y, w, i, start, j, shift, lines;

  wuss  = tc->window->task->wuss;
  sep_w = (int) wuss__text_width(wuss->fonts.fonts[0], TAGCLOUD_SEPARATOR);
  x     = 0;
  y     = pad;
  start = 0;
  lines = 0;

  for (i = 0; i <= tc->ntags; i++)
  {
    e = i < tc->ntags ? tc->order[i] : NULL;
    w = e != NULL ? tagcloud_entry_width(wuss, e) : 0;

    /* finish the line when this tag will not fit, or at the end */
    if (i > start && (e == NULL || x + sep_w + w > avail))
    {
      shift = pad + (avail - x) / 2;
      for (j = start; j < i; j++)
      {
        tc->order[j]->box.x0 += shift;
        tc->order[j]->box.x1 += shift;
        tc->order[j]->separator = j + 1 < i;
      }
      lines++;
      y    += leading;
      x     = 0;
      start = i;
    }

    if (e == NULL)
      break;

    if (i > start)
      x += sep_w;
    e->box = (box_t) BOX_POS_SIZE(x, y, w, line_h);
    x     += w;
  }

  return lines;
}

/* Lay the tags out in columns as wide as the widest tag. Returns the number
 * of rows. */
static int tagcloud_layout_list(wuss_tagcloud_t *tc,
                                int              avail,
                                int              pad,
                                int              line_h,
                                int              leading)
{
  wuss_t           *wuss;
  tagcloud_entry_t *e;
  int               col_w, cols, i;

  wuss  = tc->window->task->wuss;
  col_w = 0;
  for (i = 0; i < tc->ntags; i++)
    col_w = MAX(col_w, tagcloud_entry_width(wuss, &tc->entries[i]));
  col_w += pad;
  cols   = MAX(1, (avail + pad) / col_w);

  for (i = 0; i < tc->ntags; i++)
  {
    e            = tc->order[i];
    e->box       = (box_t) BOX_POS_SIZE(pad + (i % cols) * col_w,
                                        pad + (i / cols) * leading,
                                        tagcloud_entry_width(wuss, e),
                                        line_h);
    e->separator = 0;
  }

  return (tc->ntags + cols - 1) / cols;
}

/* Sort and lay out the tags to the window's visible width, set the document
 * extent to match and redraw. */
static void tagcloud_reflow(wuss_tagcloud_t *tc)
{
  wuss_t  *wuss;
  box_t    content;
  size2d_t max;
  int      pad, line_h, leading, avail, widest, lines, i;

  wuss    = tc->window->task->wuss;
  pad     = wuss_STD_GAP * 2;
  line_h  = wuss__fontset_height(&wuss->fonts, 0);
  leading = line_h + wuss_STD_GAP;

  wuss_window_get_content_bounds(tc->window, &content);
  wuss_window_get_max_content(tc->window, &max);
  avail = MAX(content.x1 - content.x0 - 2 * pad, 1);

  widest = 1;
  for (i = 0; i < tc->ntags; i++)
    widest = MAX(widest, tagcloud_entry_width(wuss, &tc->entries[i]));

  tagcloud_sort(tc);
  if (tc->options.display == wuss_TAGCLOUD_DISPLAY_CLOUD)
    lines = tagcloud_layout_cloud(tc, avail, pad, line_h, leading);
  else
    lines = tagcloud_layout_list(tc, avail, pad, line_h, leading);

  /* cannot fail: both sizes are positive */
  (void) wuss_window_set_min_size(tc->window,
                                  SIZE2D(widest + 2 * pad, line_h + 2 * pad));
  (void) wuss_window_set_doc(tc->window,
                             SIZE2D(MAX(max.w, widest + 2 * pad),
                                    MAX(lines, 1) * leading - wuss_STD_GAP +
                                    2 * pad));
  wuss_window_invalidate_visible(tc->window);
}

/* ----------------------------------------------------------------------- */

static colour_t tagcloud_colour(const wuss_tagcloud_t  *tc,
                                const tagcloud_entry_t *e)
{
  const wuss_t *wuss;
  int           step, band;

  wuss = tc->window->task->wuss;
  if (e->highlighted)
    return wuss->palette[wuss->accent];
  if (!tc->options.scaling)
    return wuss->palette[wuss->button.fg];

  step = MAX(1, (tc->max - tc->min) / TAGCLOUD_BANDS);
  band = CLAMP((e->count - tc->min) / step, 0, TAGCLOUD_BANDS - 1);

  /* faintest band two-sixths of the way from the background to the text */
  return colour_mix(wuss->palette[wuss->body.window],
                    wuss->palette[wuss->button.fg],
                    (unsigned int) (2 + band), TAGCLOUD_BANDS + 1);
}

static void tagcloud_draw_entry(wuss_tagcloud_t        *tc,
                                const wuss_event_t     *event,
                                const tagcloud_entry_t *e)
{
  wuss_t   *wuss;
  screen_t *scr;
  bmfont_t *font;
  box_t     screen_box, clip;
  screen_t  clipped;
  colour_t  fg, bg;
  point_t   pos;
  int       ascent;

  wuss = tc->window->task->wuss;
  scr  = event->data.redraw.scr;
  font = tagcloud_font(wuss, e);
  if (font == NULL)
    return;

  wuss__icon_box_to_screen(event->data.redraw.bounds,
                           event->data.redraw.scroll, &e->box, &screen_box);
  if (e->separator)
    screen_box.x1 += (int) wuss__text_width(wuss->fonts.fonts[0],
                                            TAGCLOUD_SEPARATOR);
  if (box_intersection(&scr->clip, &screen_box, &clip))
    return;

  clipped      = *scr;
  clipped.clip = clip;

  fg = tagcloud_colour(tc, e);
  bg = wuss->palette[wuss->body.window];
  bmfont_get_info(font, NULL, NULL, &ascent, NULL);
  pos.x = screen_box.x0;
  pos.y = screen_box.y0 + ascent;
  wuss__text_draw(font, &clipped, e->name, e->length, fg, bg, &pos, &pos);

  if (e->index == tc->hover)
    screen_fill_hline(&clipped, screen_box.x0, screen_box.y0 + ascent + 1,
                      e->box.x1 - e->box.x0, fg);

  if (e->separator)
    wuss__text_draw_str(wuss->fonts.fonts[0], &clipped, TAGCLOUD_SEPARATOR,
                        wuss->palette[wuss->button.fg], bg, &pos, NULL);
}

static void tagcloud_set_hover(wuss_tagcloud_t *tc, int index)
{
  if (index == tc->hover)
    return;

  if (tc->hover >= 0)
    wuss_window_invalidate(tc->window, &tc->entries[tc->hover].box);
  tc->hover = index;
  if (index >= 0)
    wuss_window_invalidate(tc->window, &tc->entries[index].box);
}

/* ----------------------------------------------------------------------- */

result_t wuss_tagcloud_create(wuss_tagcloud_t          **out,
                              wuss_window_t             *window,
                              wuss_tagcloud_toggle_fn_t *toggle,
                              void                      *opaque)
{
  wuss_t          *wuss;
  wuss_tagcloud_t *tc;

  if (out == NULL || window == NULL)
    return result_NULL_ARG;

  wuss = window->task->wuss;

  tc = wuss->alloc.malloc(sizeof(*tc));
  if (tc == NULL)
    return result_OOM;

  tc->alloc                  = wuss->alloc;
  tc->window                 = window;
  tc->toggle                 = toggle;
  tc->opaque                 = opaque;
  tc->block                  = NULL;
  tc->entries                = NULL;
  tc->order                  = NULL;
  tc->ntags                  = 0;
  tc->min                    = 0;
  tc->max                    = 0;
  tc->options.display        = wuss_TAGCLOUD_DISPLAY_CLOUD;
  tc->options.sort           = wuss_TAGCLOUD_SORT_NAME;
  tc->options.selected_first = 0;
  tc->options.scaling        = 1;
  tc->hover                  = -1;

  tagcloud_reflow(tc);

  *out = tc;
  return result_OK;
}

void wuss_tagcloud_destroy(wuss_tagcloud_t *doomed)
{
  if (doomed == NULL)
    return;

  doomed->alloc.free(doomed->block);
  doomed->alloc.free(doomed);
}

result_t wuss_tagcloud_set_tags(wuss_tagcloud_t           *tc,
                                const wuss_tagcloud_tag_t *tags,
                                int                        ntags)
{
  size_t             size;
  void              *block;
  tagcloud_entry_t  *entries;
  tagcloud_entry_t **order;
  char              *names;
  int                i;

  if (ntags < 0)
    return result_BAD_ARG;

  /* one block: the entries, then the sort order, then the names */
  size = (size_t) ntags * (sizeof(*entries) + sizeof(*order));
  for (i = 0; i < ntags; i++)
    size += strlen(tags[i].name) + 1;

  block = tc->alloc.malloc(MAX(size, 1));
  if (block == NULL)
    return result_OOM;

  entries = block;
  order   = (tagcloud_entry_t **) (entries + ntags);
  names   = (char *) (order + ntags);
  for (i = 0; i < ntags; i++)
  {
    entries[i].name        = names;
    entries[i].length      = (int) strlen(tags[i].name);
    entries[i].count       = tags[i].count;
    entries[i].highlighted = tags[i].highlighted != 0;
    entries[i].index       = i;
    memcpy(names, tags[i].name, (size_t) entries[i].length + 1);
    names                 += entries[i].length + 1;
    order[i]               = &entries[i];
  }

  tc->alloc.free(tc->block);
  tc->block   = block;
  tc->entries = entries;
  tc->order   = order;
  tc->ntags   = ntags;
  tc->hover   = -1;

  tc->min = INT_MAX;
  tc->max = 0;
  for (i = 0; i < ntags; i++)
  {
    tc->min = MIN(tc->min, tags[i].count);
    tc->max = MAX(tc->max, tags[i].count);
  }

  tagcloud_reflow(tc);
  return result_OK;
}

void wuss_tagcloud_set_highlight(wuss_tagcloud_t *tc,
                                 int              index,
                                 int              highlighted)
{
  if (index < 0 || index >= tc->ntags)
    return;

  tc->entries[index].highlighted = highlighted != 0;
  tagcloud_reflow(tc);
}

void wuss_tagcloud_set_options(wuss_tagcloud_t               *tc,
                               const wuss_tagcloud_options_t *options)
{
  tc->options = *options;
  tagcloud_reflow(tc);
}

/* ponytail: linear scan of every tag's box; bucket by line if tag counts
 * reach the thousands */
int wuss_tagcloud_hit_test(const wuss_tagcloud_t *tc, point_t point)
{
  int i;

  for (i = 0; i < tc->ntags; i++)
    if (box_contains_point(&tc->entries[i].box, point.x, point.y))
      return i;

  return -1;
}

int wuss_tagcloud_handle_event(wuss_tagcloud_t    *tc,
                               const wuss_event_t *event)
{
  wuss_t *wuss;
  int     index, i;

  switch (event->kind)
  {
  case wuss_EVENT_OPEN:
  case wuss_EVENT_PALETTE:
  case wuss_EVENT_RELAYOUT:
    tagcloud_reflow(tc);
    return 0; /* other listeners may care too */

  case wuss_EVENT_REDRAW:
    wuss = tc->window->task->wuss;
    wuss__fill_colour(wuss, event->data.redraw.scr,
                      event->data.redraw.content,
                      wuss->palette[wuss->body.window],
                      POINT(event->data.redraw.bounds->x0 -
                            event->data.redraw.scroll.x,
                            event->data.redraw.bounds->y0 -
                            event->data.redraw.scroll.y));
    for (i = 0; i < tc->ntags; i++)
      tagcloud_draw_entry(tc, event, &tc->entries[i]);
    return 1;

  case wuss_EVENT_MOUSE:
    index = wuss_tagcloud_hit_test(tc, event->data.mouse.point);
    if (event->data.mouse.action == wuss_MOUSE_MOVE)
    {
      tagcloud_set_hover(tc, index);
      return 0;
    }

    if (event->data.mouse.action != wuss_MOUSE_DOWN || index < 0 ||
        !(event->data.mouse.button & WUSS_BUTTON_CLICK))
      return 0;

    wuss_tagcloud_set_highlight(tc, index, !tc->entries[index].highlighted);
    if (tc->toggle != NULL)
      tc->toggle(tc, index, tc->entries[index].highlighted, tc->opaque);
    return 1;

  case wuss_EVENT_POINTER_EXIT:
    tagcloud_set_hover(tc, -1);
    return 0;

  default:
    return 0;
  }
}
