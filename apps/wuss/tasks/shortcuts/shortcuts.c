/* wuss/tasks/shortcuts/shortcuts.c -- keyboard shortcut list task */

#ifdef WUSS_APP

#include <stdlib.h>
#include <string.h>

#ifdef FORTIFY
#include "fortify/fortify.h"
#endif

#include "base/utils.h"
#include "framebuf/bmfont.h"
#include "wuss/gadget/gridview.h"
#include "wuss/icon.h"
#include "wuss/task.h"
#include "wuss/window.h"

#include "../apps.h"
#include "../common/taskapp.h"
#include "../common/common.h"

#define SHORTCUTS_LABEL_CHARS 20 /* label column width */
#define SHORTCUTS_COLUMNS      2 /* keys, then action */
#define SHORTCUTS_ROWS        24 /* window opens at most this many tall */

/* shows every system-wide keyboard shortcut as a two-column grid: the keys,
 * then what they do */
typedef struct shortcuts_task
{
  taskapp_instance_t base;
  wuss_gridview_t   *gv;
}
shortcuts_task_t;

/* First the chords handled ahead of any window's task: tasks_system_key, the
 * main loop's key case and tasks_launcher_key in apps/wuss, the core's drag and
 * menu cancels, and the writable field keys in wuss__writable_key. Then, by
 * task, each task's menu shortcuts and direct keys. A system chord wins over a
 * task's key, so a shortcut it shadows is left out. Keep in step with those. A
 * row with no action is a group heading. */
static const struct
{
  const char *keys;
  const char *action;
}
shortcuts[] =
{
  { "Windows",        NULL                  },
  { "F7",             "Next window"         },
  { "Shift-F7",       "Previous window"     },
  { "Ctrl-`",         "Next window"         },
  { "Ctrl-Shift-`",   "Previous window"     },
  { "Ctrl-Shift-F",   "Toggle window size"  },
  { "Ctrl-F2",        "Close window"        },
  { "Apps",           NULL                  },
  { "F10",            "Next app"            },
  { "Shift-F10",      "Previous app"        },
  { "Ctrl-Tab",       "Next app"            },
  { "Ctrl-Shift-Tab", "Previous app"        },
  { "Ctrl-F10",       "App to front"        },
  { "F11",            "Hide app"            },
  { "Shift-F11",      "Hide other apps"     },
  { "Ctrl-H",         "Hide app"            },
  { "Ctrl-Shift-H",   "Hide other apps"     },
  { "Ctrl-F11",       "Show all apps"       },
  { "Ctrl-F9",        "Quit app"            },
  { "Ctrl-Q",         "Quit app"            },
  { "Ctrl-Shift-Esc", "Kill app"            },
  { "Wuss",           NULL                  },
#ifndef __EMSCRIPTEN__
  { "Ctrl-Shift-Q",   "Quit Wuss"           },
  { "Ctrl-Shift-3",   "Save screenshot"     },
#endif
  { "Ctrl-=",         "Zoom in"             },
  { "Ctrl--",         "Zoom out"            },
  { "Ctrl-0",         "Actual size"         },
  { "Escape",         "Cancel drag"         },
  { "Escape",         "Close menu"          },
  { "Clipboard",      NULL                  },
  { "Ctrl-A",         "Select all text"     },
  { "Ctrl-C",         "Copy text"           },
  { "Ctrl-X",         "Cut text"            },
  { "Ctrl-V",         "Paste text"          },
  { "Insert",         "Paste text"          },
  { "Field",          NULL                  },
  { "Ctrl-Z",         "Clear selection"     },
  { "Ctrl-U",         "Clear field"         },
  { "Tab",            "Next field"          },
  { "Shift-Tab",      "Previous field"      },
  { "Caret",          NULL                  },
  { "Shift-Left",     "Word left"           },
  { "Shift-Right",    "Word right"          },
  { "Ctrl-Left",      "Start of field"      },
  { "Ctrl-Right",     "End of field"        },
  { "Dialogues",      NULL                  },
  { "Return",         "OK"                  },
  { "Escape",         "Cancel"              },
  { "Debug",          NULL                  },
  { "Shift-F1",       "Redraw"              },
  { "Ctrl-Shift-F1",  "Garbage"             },
  { "Shift-F3",       "Pixel stress"        },
  { "Apps folder",    NULL                  },
  { "Arrows",         "Move cursor"         },
  { "Return",         "Open item"           },
  { "Ball",           NULL                  },
  { "Space",          "Pause"               },
  { "F3",             "Save"                },
  { "A",              "Add ball"            },
  { "C",              "Clear"               },
  { "G",              "Gravity"             },
  { "D",              "Damping"             },
  { "Chars",          NULL                  },
  { "F3",             "Save"                },
  { "Checker",        NULL                  },
  { "F3",             "Save"                },
  { "Ctrl-R",         "Randomise"           },
  { "R",              "Reset"               },
  { "Left",           "Previous pattern"    },
  { "Right",          "Next pattern"        },
  { "Up",             "Wider bands"         },
  { "Down",           "Narrower bands"      },
  { "Clock",          NULL                  },
  { "F3",             "Save"                },
  { "D",              "Digital"             },
  { "S",              "Seconds"             },
  { "H",              "12-hour"             },
  { "Compositing",    NULL                  },
  { "F3",             "Save"                },
  { "Left",           "Previous operator"   },
  { "Right",          "Next operator"       },
  { "Curving",        NULL                  },
  { "F3",             "Save"                },
  { "H",              "Hull"                },
  { "R",              "Reset points"        },
  { "Left",           "Previous curve kind" },
  { "Right",          "Next curve kind"     },
  { "Up",             "Scroll up"           },
  { "Down",           "Scroll down"         },
  { "Dithering",      NULL                  },
  { "F3",             "Save"                },
  { "Space",          "Pause"               },
  { "Doughnut",       NULL                  },
  { "Space",          "Pause"               },
  { "F3",             "Save"                },
  { "Ctrl-R",         "Randomise"           },
  { "R",              "Reset"               },
  { "Arrows",         "Rotate"              },
  { "[",              "Less major density"  },
  { "]",              "More major density"  },
  { ",",              "Less minor density"  },
  { ".",              "More minor density"  },
  { "-",              "Less tube density"   },
  { "=",              "More tube density"   },
  { "+",              "More tube density"   },
  { "Effecting",      NULL                  },
  { "F3",             "Save"                },
  { "Delete",         "Delete effect"       },
  { "Fern",           NULL                  },
  { "Space",          "Pause"               },
  { "F3",             "Save"                },
  { "Ctrl-R",         "Randomise"           },
  { "R",              "Reset"               },
  { "Left",           "Previous seed"       },
  { "Right",          "Next seed"           },
  { "Filing",         NULL                  },
  { "Arrows",         "Move cursor"         },
  { "Return",         "Open item"           },
  { "Ctrl-Shift-Up",  "Open parent"         },
  { "Fireworks",      NULL                  },
  { "Space",          "Pause"               },
  { "F3",             "Save"                },
  { "C",              "Clear"               },
  { "W",              "Walls"               },
  { "M",              "Markers"             },
  { "Gradient",       NULL                  },
  { "F3",             "Save"                },
  { "Ctrl-R",         "Randomise"           },
  { "R",              "Reset"               },
  { "S",              "Next shape"          },
  { "D",              "Next dither matrix"  },
  { "Greebling",      NULL                  },
  { "F3",             "Save"                },
  { "R",              "Random palettes"     },
  { "Ctrl-R",         "Randomise"           },
  { "Space",          "Reseed"              },
  { "Left",           "Previous palette"    },
  { "Right",          "Next palette"        },
  { "Jottings",       NULL                  },
  { "Left",           "Character left"      },
  { "Right",          "Character right"     },
  { "Up",             "Line up"             },
  { "Down",           "Line down"           },
  { "Page Up",        "Page up"             },
  { "Page Down",      "Page down"           },
  { "Shift-Left",     "Word left"           },
  { "Alt-Left",       "Word left"           },
  { "Shift-Right",    "Word right"          },
  { "Alt-Right",      "Word right"          },
  { "Home",           "Start of line"       },
  { "Ctrl-Left",      "Start of line"       },
  { "End",            "End of line"         },
  { "Ctrl-Right",     "End of line"         },
  { "Ctrl-Home",      "Start of note"       },
  { "Ctrl-Up",        "Start of note"       },
  { "Ctrl-End",       "End of note"         },
  { "Ctrl-Down",      "End of note"         },
  { "Backspace",      "Delete left"         },
  { "Delete",         "Delete right"        },
  { "Shift-Backspace", "Delete word left"   },
  { "Alt-Backspace",  "Delete word left"    },
  { "Shift-Delete",   "Delete word right"   },
  { "Alt-Delete",     "Delete word right"   },
  { "Ctrl-Backspace", "Clear to line start" },
  { "Ctrl-Delete",    "Clear to line end"   },
  { "Ctrl-K",         "Clear to line end"   },
  { "Ctrl-U",         "Delete line"         },
  { "Escape",         "Stop editing"        },
  { "Keys",           NULL                  },
  { "Any but F keys", "Add to history"      },
  { "Minesweeping",   NULL                  },
  { "N",              "New game"            },
  { "Arrows",         "Move cursor"         },
  { "Return",         "Uncover cell"        },
  { "Space",          "Flag cell"           },
  { "Orbing",         NULL                  },
  { "F3",             "Save"                },
  { "1-4",            "Choose light"        },
  { "C",              "Light colour"        },
  { "M",              "Mutate"              },
  { "Ctrl-R",         "Randomise"           },
  { "R",              "Reset"               },
  { "O",              "Toggle light"        },
  { "Arrows",         "Nudge light"         },
  { "Shift-Arrows",   "Shove light"         },
  { "Oscillating",    NULL                  },
  { "Space",          "Pause"               },
  { "F3",             "Save"                },
  { "Ctrl-R",         "Randomise"           },
  { "R",              "Reset"               },
  { "Left",           "Previous frequencies" },
  { "Right",          "Next frequencies"    },
  { "Painting",       NULL                  },
  { "F3",             "Save"                },
  { "F8",             "Undo"                },
  { "F9",             "Redo"                },
  { "Ctrl-X",         "Cut"                 },
  { "Ctrl-C",         "Copy"                },
  { "Ctrl-V",         "Paste"               },
  { "Ctrl-A",         "Select all"          },
  { "Ctrl-Z",         "Select none"         },
  { "Ctrl-I",         "Invert selection"    },
  { "F",              "Fill selection"      },
  { "Delete",         "Clear selection"     },
  { "Shift-H",        "Flip horizontally"   },
  { "Shift-V",        "Flip vertically"     },
  { "Shift-R",        "Rotate right"        },
  { "Shift-L",        "Rotate left"         },
  { "B",              "Pencil"              },
  { "H",              "Paintbrush"          },
  { "K",              "Wobbly brush"        },
  { "E",              "Eraser"              },
  { "L",              "Line"                },
  { "R",              "Rectangle"           },
  { "X",              "Filled rectangle"    },
  { "C",              "Ellipse"             },
  { "D",              "Filled ellipse"      },
  { "N",              "Path"                },
  { "M",              "Filled path"         },
  { "F",              "Fill (no selection)" },
  { "G",              "Gradient"            },
  { "P",              "Spray"               },
  { "S",              "Select"              },
  { "O",              "Lasso"               },
  { "W",              "Magic wand"          },
  { "I",              "Pick colour"         },
  { "V",              "Move"                },
  { "[",              "Smaller size"        },
  { "]",              "Larger size"         },
  { "+",              "Zoom in"             },
  { "-",              "Zoom out"            },
  { "1",              "Actual size"         },
  { "Escape",         "Drop selection"      },
  { "Arrows",         "Nudge selection"     },
  { "Shift-Arrows",   "Nudge by 8 pixels"   },
  { "Space-drag",     "Pan"                 },
  { "Tab",            "Hide/show toolbars"  },
  { "Resizing",       NULL                  },
  { "F3",             "Save"                },
  { "Saturn",         NULL                  },
  { "Space",          "Pause"               },
  { "F3",             "Save"                },
  { "Ctrl-R",         "Randomise"           },
  { "R",              "Reset"               },
  { "Left",           "Previous seed"       },
  { "Right",          "Next seed"           },
  { "Sofa",           NULL                  },
  { "Space",          "Pause"               },
  { "F3",             "Save"                },
  { "Ctrl-R",         "Randomise"           },
  { "R",              "Reset"               },
  { "Left",           "Previous model"      },
  { "Right",          "Next model"          },
  { "Up",             "Rotate when paused"  },
  { "Down",           "Rotate when paused"  },
  { "Spectrum games", NULL                  },
  { "Ctrl-P",         "Pause"               },
  { "Ctrl-]",         "Faster"              },
  { "Ctrl-[",         "Slower"              },
  { "Ctrl-M",         "Monochromatic"       },
  { "Arrows",         "Kempston joystick"   },
  { ".",              "Kempston fire"       },
  { "Shift",          "Caps Shift"          },
  { "Other keys",     "Spectrum keyboard"   },
  { "Sudoku",         NULL                  },
  { "N",              "New game"            },
  { "R",              "Restart"             },
  { "Arrows",         "Move cursor"         },
  { "1-9",            "Enter digit"         },
  { "0",              "Clear cell"          },
  { "Space",          "Clear cell"          },
  { "Backspace",      "Clear cell"          },
  { "Delete",         "Clear cell"          },
  { "Texturing",      NULL                  },
  { "F3",             "Save"                },
  { "Ctrl-R",         "Randomise"           },
  { "Space",          "Reseed"              },
  { "K",              "Next kind"           },
  { "G",              "Next grain"          },
  { "Left",           "Previous colours"    },
  { "Right",          "Next colours"        },
  { "Tagging",        NULL                  },
  { "F2",             "List"                },
  { "F3",             "Cloud"               },
  { "F4",             "Scaling off"         },
  { "F5",             "Scaling on"          },
  { "F6",             "Sort by name"        },
  { "Ctrl-F3",        "Selected first"      },
  { "Ctrl-R",         "Rename tag"          },
  { "Ctrl-K",         "Delete tag"          },
  { "Ctrl-N",         "New tag"             },
  { "Viewing",        NULL                  },
  { "Space",          "Next image"          },
  { "Shift-Space",    "Previous image"      },
  { "+",              "Zoom in"             },
  { "-",              "Zoom out"            },
  { "1",              "Actual size"         },
  { "Arrows",         "Scroll"              },
  { "Page Up",        "Page up"             },
  { "Page Down",      "Page down"           },
  { "Home",           "Top left"            },
  { "End",            "Bottom"              }
};

/* wuss_gridview_item_fn_t: even cells are keys, odd cells actions; used
 * only to measure the cell, since shortcuts_draw_cell does the drawing */
static void shortcuts_item(int                   index,
                           void                 *opaque,
                           wuss_gridview_item_t *out)
{
  NOT_USED(opaque);

  out->glyph = NULL;
  out->label = (index & 1) ? shortcuts[index / 2].action
                           : shortcuts[index / 2].keys;
}

/* the system palette's nearest entry to an RGB value */
static colour_t shortcuts_colour(const shortcuts_task_t *task,
                                 int                     r,
                                 int                     g,
                                 int                     b)
{
  return wuss_get_palette(task->base.wuss, NULL)[wuss_nearest_colour(task->base.wuss,
                                                                r, g, b)];
}

/* wuss_gridview_draw_fn_t: the cell's text left-aligned, a group heading in
 * the bold weight (font slot 1) when one was supplied. The gridview has
 * already filled the background. */
static void shortcuts_draw_cell(wuss_gridview_t    *gv,
                                const wuss_event_t *event,
                                int                 index,
                                const box_t        *screen_box,
                                void               *opaque)
{
  shortcuts_task_t *task;
  const char       *text;
  int               slot;
  int               height, ascent;
  point_t           pos;

  NOT_USED(gv);

  task = opaque;
  text = (index & 1) ? shortcuts[index / 2].action
                     : shortcuts[index / 2].keys;
  if (text == NULL)
    return;

  slot = (shortcuts[index / 2].action == NULL &&
          wuss_get_font_n(task->base.wuss, 1) != NULL) ? 1 : 0;
  bmfont_get_info(wuss_get_font_n(task->base.wuss, slot), NULL, &height, &ascent,
                  NULL);
  pos.x = screen_box->x0 + wuss_STD_GAP;
  pos.y = screen_box->y0 + (screen_box->y1 - screen_box->y0 - height) / 2 +
          ascent;
  (void) wuss_text_draw(task->base.wuss, slot, event->data.redraw.scr, text,
                        (int) strlen(text),
                        shortcuts_colour(task, 0x00, 0x00, 0x00),
                        shortcuts_colour(task, 0xFF, 0xFF, 0xFF), &pos, NULL);
}

static result_t shortcuts_init(shortcuts_task_t *task)
{
  result_t rc;

  rc = wuss_gridview_window_create(&task->gv, task->base.delegate, &task_place,
                                   "Shortcuts",
                                   (int) NELEMS(shortcuts) * 2,
                                   shortcuts_item, SHORTCUTS_LABEL_CHARS,
                                   NULL, task, 0, &task->base.window);
  if (rc != result_OK)
    return rc;

  /* one or two columns, so each pair keeps to a row when there is room */
  wuss_gridview_set_layout(task->gv, SHORTCUTS_COLUMNS, 1, 0);
  /* text only, so no glyph spacing and little between the lines */
  wuss_gridview_set_padding(task->gv, wuss_STD_GAP * 2, 1, wuss_STD_GAP);
  wuss_gridview_set_draw(task->gv, shortcuts_draw_cell, 0);
  wuss_gridview_set_background(task->gv,
                               shortcuts_colour(task, 0xFF, 0xFF, 0xFF));
  wuss_gridview_fit_window(task->gv, SHORTCUTS_COLUMNS, SHORTCUTS_ROWS,
                           &task_place);

  return result_OK;
}

static result_t shortcuts_handle(wuss_window_t      *window,
                                 const wuss_event_t *event,
                                 void               *task_data)
{
  shortcuts_task_t *st;

  NOT_USED(window);

  st = task_data;

  switch (event->kind)
  {
  case wuss_EVENT_QUIT:
    wuss_gridview_destroy(st->gv);
    return result_OK;

  case wuss_EVENT_PALETTE:
    if (st->gv != NULL)
    {
      wuss_gridview_set_background(st->gv,
                                   shortcuts_colour(st, 0xFF, 0xFF, 0xFF));
      (void) wuss_gridview_handle_event(st->gv, event);
    }
    return result_OK;

  default:
    if (st->gv != NULL)
      (void) wuss_gridview_handle_event(st->gv, event);
    return result_OK;
  }
}

result_t shortcuts_app_create(wuss_t *wuss)
{
  static const taskapp_desc_t desc =
  {
    .name          = "Shortcuts",
    .purpose       = "Keyboard shortcut list",
    .instance_size = sizeof(shortcuts_task_t),
    .init          = (taskapp_init_fn_t) shortcuts_init,
    .handle        = shortcuts_handle
  };

  return taskapp_install(wuss, &desc);
}

#endif /* WUSS_APP */
