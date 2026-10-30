/* wuss/options.c -- command-line options for the Wuss demo */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RISC OS's GCCSDK newlib has getopt() but not the GNU getopt_long()
 * extension, so options_parse() keeps a hand-rolled fallback there. */
#ifndef __riscos
#include <getopt.h>
#endif

#include "tasks/tasks.h"

#include "options.h"

/* ----------------------------------------------------------------------- */

static const char wuss_usage[] =
  "usage: wuss [-r|--resources DIR] [-p|--palette NAME] "
  "[-f|--font FAMILY] "
  "[-d|--depth 1|2|4|8|15|16|32] [-s|--scale N] [--res WIDTHxHEIGHT] "
  "[-t|--tasks all|NAME[,NAME...]] [--root DIR] [--crt] [--pointer] "
  "[--script FILE]\n";

/* Parses "WIDTHxHEIGHT" (e.g. "1024x768") into w and h. Returns false,
 * leaving them untouched, on anything else -- a missing 'x', a non-positive
 * dimension, or trailing junk after the height. */
static bool parse_res(const char *s, int *w, int *h)
{
  char *end;
  long  width, height;

  width = strtol(s, &end, 10);
  if (end == s || *end != 'x')
    return false;

  height = strtol(end + 1, &end, 10);
  if (*end != '\0' || width <= 0 || height <= 0)
    return false;

  *w = (int) width;
  *h = (int) height;
  return true;
}

#ifndef __riscos

/* --res, --crt and --pointer have no short form, so they are given
 * longopt-only codes past the ASCII range getopt_long uses for short
 * options. */
enum { OPT_RES = 256, OPT_CRT, OPT_POINTER, OPT_ROOT, OPT_SCRIPT };

/* Desktop: getopt_long. Accepts the short forms and the "--" long forms; the
 * historical single-dash long spellings (-resources) are no longer accepted.
 * Returns false and prints usage on an unknown option or missing argument. */
bool options_parse(int argc, char *argv[], wuss_options_t *opts)
{
  static const struct option longopts[] =
  {
    { "resources", required_argument, NULL, 'r'         },
    { "palette",   required_argument, NULL, 'p'         },
    { "font",      required_argument, NULL, 'f'         },
    { "depth",     required_argument, NULL, 'd'         },
    { "scale",     required_argument, NULL, 's'         },
    { "res",       required_argument, NULL, OPT_RES     },
    { "tasks",     required_argument, NULL, 't'         },
    { "root",      required_argument, NULL, OPT_ROOT    },
    { "crt",       no_argument,       NULL, OPT_CRT     },
    { "pointer",   no_argument,       NULL, OPT_POINTER },
    { "script",    required_argument, NULL, OPT_SCRIPT  },
    { NULL,        0,                 NULL, 0           }
  };

  int c;

  for (;;)
  {
    c = getopt_long(argc, argv, "r:p:f:d:s:t:", longopts, NULL);
    if (c == -1)
      break;

    switch (c)
    {
    case 'r': opts->resources    = optarg;       break;
    case 'p': opts->palette_name = optarg;       break;
    case 'f': opts->font_family  = optarg;       break;
    case 'd': opts->depth        = atoi(optarg); break;
    case 's': opts->scale        = atoi(optarg); break;
    case 't': opts->tasks        = optarg;       break;
    case OPT_ROOT: opts->root    = optarg;       break;
    case OPT_SCRIPT: opts->script = optarg;      break;
    case OPT_CRT:
      g_tasks.crt = true;
      break;
    case OPT_POINTER:
      g_tasks.pointer = true;
      break;
    case OPT_RES:
      if (!parse_res(optarg, &opts->res_width, &opts->res_height))
      {
        fprintf(stderr, "wuss: --res expects WIDTHxHEIGHT, got \"%s\"\n", optarg);
        return false;
      }
      break;
    default:
      fputs(wuss_usage, stderr);
      return false;
    }
  }

  return true;
}

#else /* __riscos */

/* RISC OS: no getopt_long. Hand-rolled scan of the same options, single-dash
 * long spellings only (matches the pre-getopt behaviour). */
bool options_parse(int argc, char *argv[], wuss_options_t *opts)
{
  int i;

  for (i = 1; i < argc; i++)
    if (strcmp(argv[i], "-resources") == 0 && i + 1 < argc)
      opts->resources = argv[++i];
    else if (strcmp(argv[i], "-palette") == 0 && i + 1 < argc)
      opts->palette_name = argv[++i];
    else if (strcmp(argv[i], "-font") == 0 && i + 1 < argc)
      opts->font_family = argv[++i];
    else if (strcmp(argv[i], "-depth") == 0 && i + 1 < argc)
      opts->depth = atoi(argv[++i]);
    else if (strcmp(argv[i], "-scale") == 0 && i + 1 < argc)
      opts->scale = atoi(argv[++i]);
    else if (strcmp(argv[i], "-res") == 0 && i + 1 < argc)
      parse_res(argv[++i], &opts->res_width, &opts->res_height);
    else if (strcmp(argv[i], "-tasks") == 0 && i + 1 < argc)
      opts->tasks = argv[++i];
    else if (strcmp(argv[i], "-root") == 0 && i + 1 < argc)
      opts->root = argv[++i];
    else if (strcmp(argv[i], "-script") == 0 && i + 1 < argc)
      opts->script = argv[++i];

  return true;
}

#endif /* __riscos */
