# [Wuss](https://github.com/dpt/Wuss)

"wuss" is a minimal window manager. It owns window creation, positioning, sizing, z-ordering, mouse/scroll event routing and dragging. Tasks draw all window contents. Each task supplies a single event-handling callback.

- It draws windows back-to-front, with an optional titlebar and 1px outline per window.
- It hit-tests and routes mouse-down/up/move and scroll events, including titlebar drag-to-move.
- It tracks a single dirty region for partial redraws. Window management (create/destroy/move/resize/bring-to-front) adds to it automatically. Tasks add to it by hand.
- Tasks supply all content drawing and mouse handling. Wuss only fills the content background before it delivers a redraw event to the task's handle callback. A task can opt out of the fill with `wuss_NO_BACKGROUND`.
- Scrolling is Wuss's own default action (see "Scrolling" below). A task can also react to `wuss_EVENT_SCROLL` for its own purposes.

## Setup

Create a window manager onto a `screen_t`, with an array of font slots, an optional system palette, optional creation config and optional allocator hooks:

```C
result_t wuss_create(screen_t               *scr,
                     const wuss_font_desc_t *fonts,
                     int                     nfonts,
                     const colour_t         *palette,
                     int                     npalette,
                     const wuss_config_t    *config,
                     const alloc_t          *alloc,
                     const char             *resources,
                     wuss_t                **wuss);
```

Some arguments can be NULL:

- `fonts` (or `nfonts` 0), for unlabelled titlebars.
- `palette`, for a built-in default palette.
- `alloc` (DPTLib's `utils/alloc.h`), for plain stdlib `malloc`/`realloc`/`free`.
- `config`, for the default titlebar height and colours.

`config->backdrop` is a `wuss_backdrop_t`: a flat colour, or an 8x8 fill pattern. Wuss paints it behind windows on every redraw. Set its `colour` to `wuss_NO_BACKGROUND` to leave the background untouched. This is the default when `config` is NULL. The caller must then repaint the background itself. A non-`screen_PATTERN_SOLID` `pattern` tiles that pattern in `colour` over `pattern_bg`. Wuss phases the pattern to a fixed screen origin, so it does not crawl between full and dirty-region redraws.

[backdrop.md](backdrop.md) describes desktop behaviour beyond this paint (a backdrop window, the Pinboard, pinned files and notes).

### Fonts

Text drawing lives in its own module, `libraries/wuss/font/`. The region core delegates text to it in the same way that it delegates furniture, icons and menus. The module owns the font slots of a `wuss_t`. It also owns the one text-rendering primitive that furniture, menus and icons all call. So code can use region handling without any font code beyond the slot storage.

`wuss_create` takes up to `wuss_MAX_FONTS` (4) slots as an array of `wuss_font_desc_t`:

```C
typedef struct wuss_font_desc
{
  bmfont_t         *font;       /* not owned; must outlive the wuss_t */
  wuss_font_class_t font_class; /* wuss_FONT_CLASS_NONE (a text face) or
                                 * wuss_FONT_CLASS_SYSTEM (chrome only) */
  const char       *name;       /* borrowed; the font's leafname sans ".png",
                                 * for a picker to match against; may be NULL */
}
wuss_font_desc_t;
```

Wuss copies the descriptors in. It does not own the fonts. Slot use:

- **slot 0** — the system font: titlebar labels, menu rows, and any icon that does not select another weight. `wuss_get_font` also reads it back, for a task that wants to draw its own content in the same face.
- **slot 1** — the bold weight, if supplied. Window and menu titles draw from it, and fall back to slot 0.
- **slot `WUSS_SYMBOL_FONT` (2)** — an optional symbol font. Wuss draws the menu tick and submenu arrow from it as glyphs. Without it, Wuss draws them as vector strokes.

An empty slot is NULL, and callers fall back to slot 0 or to strokes. If `config->titlebar_height` is not set, the titlebar takes the height of the taller of the slot-0 and slot-1 faces. If both slots are empty, it takes `WUSS_DEFAULT_TITLEBAR_HEIGHT`.

Read the slots back with `wuss_get_font` (slot 0), `wuss_get_font_n(wuss, i)`, `wuss_get_font_class_n(wuss, i)` and `wuss_get_font_name_n(wuss, i)`. With the last two, a picker such as `wuss_fontmenu` can tell a chrome font apart from a selectable text face. All text that wuss draws goes through one internal primitive, so the same optical baseline nudge applies everywhere.

A `wuss_backdrop_t` is `{ colour, pattern, pattern_bg }`. The `wuss_BACKDROP_COLOUR(c)` and `wuss_BACKDROP_PATTERN(c, p, b)` macros build one as a compound literal.

### Symbolic colours

A `wuss_colour_t` is a system-palette index. Values `0..127` are raw indices. `wuss_COLOUR_SYMBOLIC` (128) and above are *symbolic*: roles that wuss resolves to a concrete index against the live palette (and, for the chrome roles, the live config):

- `wuss_COLOUR_BLACK`, `_WHITE`, `_RED`, `_GREEN`, `_BLUE`, `_YELLOW`, `_CYAN`, `_MAGENTA`, `_GREY` — the nearest system-palette entry to the named RGB. This is the same lookup as `wuss_nearest_colour`.
- `wuss_COLOUR_TITLE_BG`, `_TITLE_FG`, `_BUTTON_HILIGHT`, `_BUTTON_SHADOW`, `_BUTTON_PRESSED`, `_ACCENT_BG`, `_ACCENT_FG`, `_BACKDROP`, `_WINDOW`, `_MENU` — echo the matching `wuss_config_t` field (`furniture.title.bg`, `bevel.light`, `bevel.dark`, `bevel.pressed`, `backdrop.colour`, `body.window`, `body.menu`, …). When `config` is NULL, `body.window` (work-area fill) defaults to `wuss_COLOUR_GREY` and `body.menu` (menu fill) to `wuss_COLOUR_WHITE`.

You can pass a symbolic value anywhere that takes a `wuss_colour_t`:

- config furniture, bevel, accent, body and backdrop
- `wuss_window_create` / `wuss_window_set_background` backgrounds
- icon specs.

Wuss resolves it once, when it stores it. So drawing never pays for it, and a read gives back a plain index. `wuss_set_palette` and `wuss_set_backdrop` resolve the values again. `wuss_NO_BACKGROUND` is not symbolic and always passes through.

Destroy with `wuss_destroy`, which also destroys any windows still open on it.

## Windows

Create a window with a content bounding box, optional title, appearance flags, a content background and a task delegate:

```C
result_t wuss_window_create(wuss_t *wuss, const box_t *content, const char *title,
                            wuss_window_flags_t flags, wuss_backdrop_t bg,
                            const wuss_task_t *task,
                            size2d_t doc, size2d_t min_doc,
                            wuss_window_t **window);
```

`bg` is a `wuss_backdrop_t` (flat colour or 8x8 pattern, as for `config->backdrop`). Wuss fills it in before each redraw event. Set its `colour` to `wuss_NO_BACKGROUND` if the task draws its own background. This avoids a redundant fill behind an opaque task. Wuss phases any pattern to the window's scroll origin, so the pattern stays locked to the content as the window scrolls. `wuss_window_set_background` changes it later, and also takes a `wuss_backdrop_t`.

`doc` is the virtual document extent. The horizontal and vertical scrollbars size their sausages from it. For a window with nothing to scroll, pass `content`'s own width and height. `doc` is also the ceiling to which a resize-drag or toggle-size grows the content area. You set it at creation. `wuss_window_set_doc` changes it later. `wuss_window_get_max_content` gives the largest content size that this screen allows. `wuss_EVENT_RELAYOUT` goes to each window after a screen resize or config change.

`min_doc` is the opposite end: the smallest content size to which a resize-drag or toggle-size shrinks the window. Pass `(0, 0)` for the built-in floor. Wuss clamps `min_doc` to two limits:

- the built-in floor, so a window never gets too small to grab
- `doc`, so it never demands a window larger than the document it shows.

You also set it once, at creation.

Furniture is additional to `content`. Wuss does not carve it out of `content`. The window's content area is always exactly the box you request. Its on-screen footprint (`wuss_window_get_visible_bounds`) is `content` expanded outward by the furniture that the flags request: a titlebar above, and/or a 1px outline around all four sides.

`wuss_task_t` holds the task's event callback:

```C
typedef struct wuss_task
{
  wuss_event_fn_t *handle;       /* NULL => task receives no events */
  void            *task_data;
}
wuss_task_t;
```

`wuss_task_start` builds one from `handle`/`task_data`.

`flags` combines these values by bitwise OR:

- `wuss_WINDOW_NONE` — default: Wuss draws every furniture region.
- `wuss_WINDOW_NO_TITLEBAR` — no titlebar, and no drag handle.
- `wuss_WINDOW_NO_OUTLINE` — no 1px border around the window.
- `wuss_WINDOW_NO_CLOSE` — no close button in the titlebar.
- `wuss_WINDOW_NO_BACK` — no back button in the titlebar (`wuss_BUTTON_SELECT` sends the window to back, `wuss_BUTTON_ADJUST` brings it to front).
- `wuss_WINDOW_NO_TOGGLE_SIZE` — no toggle-size button in the titlebar.
- `wuss_WINDOW_NO_VSCROLL` — no vertical scrollbar on the right edge.
- `wuss_WINDOW_NO_HSCROLL` — no horizontal scrollbar on the bottom edge.
- `wuss_WINDOW_NO_RESIZE` — no resize button in the bottom-right corner.
- `wuss_WINDOW_NO_RESIZE_BLIT` — a resize (drag or toggle-size) always fully redraws the window's content. It does not blit the preserved region. Use it when a task's rendering depends on window size and a partial redraw cannot patch it (e.g. a layout that spans the whole window).
- `wuss_WINDOW_NO_REDRAW` — Wuss delivers no `wuss_EVENT_REDRAW` for this window. Wuss fills its backdrop and draws its icons, and that is its whole appearance. Use it for a window that is nothing but backdrop and/or icons (a label-only dialogue, say). The task then needs no empty redraw handler. And if the window shares a task with a window that does paint, that handler needs no window-handle check.

Wuss ignores `wuss_WINDOW_NO_CLOSE`/`NO_BACK`/`NO_TOGGLE_SIZE` if `flags` includes `wuss_WINDOW_NO_TITLEBAR`. `NO_VSCROLL`/`NO_HSCROLL`/`NO_RESIZE`/`NO_RESIZE_BLIT`/`NO_REDRAW` always apply.

Wuss handles all furniture actions (back, toggle-size, resize-drag, scrollbar arrow/thumb) itself, through `wuss_mouse_click`/`wuss_mouse_move`. There are no new client events. Toggle-size fills the whole screen. With Shift held (see `wuss_set_modifiers`), it fills only the safe area, clear of the icon bar.

Other window operations: `wuss_window_close`, `wuss_window_move`, `wuss_window_resize` (keeps the content top-left), `wuss_window_restack`, `wuss_window_get_visible_bounds` (full on-screen footprint), `wuss_window_get_content_bounds`, `wuss_window_set_background`.

### Toolbars

A toolbar is an ordinary window of the same task. It attaches to one edge of a parent window, so it goes wherever the parent goes:

```C
result_t wuss_window_attach(wuss_window_t *toolbar, wuss_window_t *parent,
                            wuss_edge_t edge, wuss_align_t align, int offset);
void     wuss_window_detach(wuss_window_t *toolbar);
```

- **Placement.** The toolbar sits outside `edge` (`wuss_EDGE_LEFT`, `RIGHT`, `TOP` or `BOTTOM`). Its outline overlaps the parent's, so the two draw as a single line. Along the edge, Wuss aligns it against the parent's content area:
  - `wuss_ALIGN_TOP`/`BOTTOM` for a left or right edge
  - `wuss_ALIGN_LEFT`/`RIGHT` for a top or bottom edge
  - `0` to centre.

  `offset` then shifts it that many pixels further along. It can overhang the parent. Several toolbars on one edge stack outwards in the order of attachment, and their outlines overlap in the same way. A hidden toolbar gives up its place.
- **Size.** A toolbar keeps its own size whatever the parent's size. You can resize a toolbar, and its neighbours move to suit. The parent's maximum size and toggle-size take no account of its toolbars.
- **Overrun.** The content of a toolbar or pane is its shown icons. If it has no icons or draws its own content, the content is its document. If the content is larger than the content area, it scrolls as the icon bar does. This applies to an overrun axis without a scrollbar. While the pointer rests within 16 pixels of either end, each idle moves the content 6 pixels that way. This does not happen while a press or drag is in progress.
- **Screen edges.** A toolbar stays on screen, and overlaps the parent if necessary, until its edge of the parent goes off the screen. Then the toolbar goes too. Wuss clamps only the axis at right angles to the edge. It clamps the toolbars on one edge together.
- **Furniture.** A toolbar has no titlebar, so it has no close, back, toggle-size or resize buttons. It can have an outline and scrollbars. These give `result_BAD_ARG`:
  - other flags
  - attaching a window to a toolbar
  - attaching a toolbar to a second parent
  - attaching windows of different tasks.
- **Moving and stacking.** `wuss_window_move` on a toolbar does nothing. Move its parent instead. Toolbars stay in the parent's stack, directly in front of it. `wuss_window_restack` and `wuss_window_set_stack` on any member of the group act on the whole group.
- **Hiding and closing.** Hiding or showing the parent hides or shows its toolbars. But a toolbar that you hide on its own stays hidden. Closing the parent closes its toolbars too, and delivers no `wuss_EVENT_CLOSE` for them. `wuss_window_detach` leaves the toolbar where it is, as an ordinary window.
- **Focus.** A click on a toolbar gives the input focus to its parent, unless the toolbar has `wuss_WINDOW_FOCUSABLE`. The parent's titlebar shows as focused while any member of the group has the focus.

A click in a toolbar's content area does not bring the group to the front. The same is true for any window's content area.

### Panes

A pane is a toolbar attached inside its parent, along one edge of the parent's content area. It takes its room from the content. It does not sit outside the window:

```C
result_t wuss_window_attach_pane(wuss_window_t *pane, wuss_window_t *parent,
                                 wuss_edge_t edge);
```

- **Carving.** The parent's footprint stays the same. Its content area shrinks by the pane's thickness plus a one-pixel divider, which the parent draws as part of its furniture. A click on a divider counts as a click on the frame, as for a bare outline pixel. It goes to the titlebar if the parent has one, and otherwise to the content. `wuss_window_get_content_bounds`, redraws, the scroll origin and the scrollbar sausages all use the remaining area. The scrollbars, the resize button and any outside toolbars still span the whole box inside the furniture.
- **Size.** Wuss sets a pane's length to match the parent's content along its edge. The pane keeps its own thickness. `wuss_window_resize` on a pane changes only the thickness. If you grow or show a pane past what the parent can spare, Wuss makes it thinner, so the content keeps its minimum size.
- **Order.** Panes carve in the order of attachment. The first spans the corners. Later panes fit between those already there. Panes on the same edge stack inwards.
- **Sizing the parent.** `doc`, `min_doc`, `wuss_window_resize` and the scroll limits all refer to the carved content area. Wuss adds the panes' room on top for the resize floor, for toggle-size and for the largest size on screen. Attaching a pane to a parent too small to hold it gives `result_WUSS_TOO_SMALL`.
- **Furniture.** A pane must have `wuss_WINDOW_NO_TITLEBAR` and `wuss_WINDOW_NO_OUTLINE`. It can also have `wuss_WINDOW_HIDDEN`, `wuss_WINDOW_NO_REDRAW`, `wuss_WINDOW_NO_RESIZE_BLIT` and `wuss_WINDOW_FOCUSABLE`. Other flags give `result_BAD_ARG`. So does attaching a pane or a toolbar to a pane, or a pane to a toolbar.
- **Changes.** A pane that you hide on its own gives its room back to the content. A pane hidden along with its parent keeps its room. A pane can be shown, hidden, resized or detached. When that happens, the parent keeps its scroll offset (clamped to the new limits), repaints the whole area inside its furniture and gets a `wuss_EVENT_OPEN`.

Otherwise a pane behaves as a toolbar. It moves, stacks, hides, closes and passes on the focus with its parent. `wuss_window_detach` returns it to an ordinary window. Panes are part of the parent, so they stack directly in front of it and behind all of its toolbars. Panes are part of the core, so they work without `WUSS_FURNITURE`. There, the divider takes no room.

## Task callbacks

```C
typedef enum wuss_event_kind
{
  wuss_EVENT_REDRAW,
  wuss_EVENT_MOUSE,
  wuss_EVENT_ICON,
  wuss_EVENT_SCROLL
}
wuss_event_kind_t;

typedef struct wuss_event
{
  wuss_event_kind_t kind;
  union
  {
    struct { screen_t *scr; const box_t *content; }                                 redraw;
    struct { wuss_mouse_action_t action; point_t point; wuss_button_t button; }     mouse;
    struct { wuss_icon_t *icon; wuss_mouse_action_t action; wuss_button_t button; } icon;
    struct { point_t point; int delta; }                                            scroll;
  }
  data;
}
wuss_event_t;

typedef result_t (wuss_event_fn_t)(wuss_window_t *window, const wuss_event_t *event,
                                   void *task_data);
```

A task supplies a single `handle` callback and dispatches on `event->kind`. Only the union member that matches `kind` is valid.

For `wuss_EVENT_REDRAW`, `event->data.redraw.scr` already has `scr->clip` set to the on-screen, clipped content area. `event->data.redraw.content` gives the window's full (unclipped) content box in screen space, for context.

For `wuss_EVENT_MOUSE` and `wuss_EVENT_SCROLL`, `event->data.mouse.point` and `event->data.scroll.point` are window-local content coordinates. The content area's top-left is `(0,0)`, plus the window's current scroll offset (see "Scrolling" below). `event->data.mouse.action` is `wuss_MOUSE_DOWN`/`wuss_MOUSE_UP`/`wuss_MOUSE_MOVE`.

A titlebar click never reaches a task's handle callback. It starts a drag instead, and for `wuss_BUTTON_SELECT` it also brings the window to front. A content click never changes z-order. This is true even on a `wuss_WINDOW_NO_TITLEBAR` window with no drag handle. Only a titlebar click raises a window. So tasks can use content clicks for their own purposes, and Wuss does not reorder windows under them.

Wuss delivers `wuss_EVENT_ICON` in place of `wuss_EVENT_MOUSE` whenever the pointer is inside the bounding box of a `wuss_ICON_TYPE_ACTION` icon (see "Icons" below). `event->data.icon.icon` names the icon. `action` and `button` carry the same values as a `wuss_EVENT_MOUSE`. `wuss_ICON_TYPE_LABEL` icons, and hidden or disabled icons, never raise it. Clicks over them fall through as `wuss_EVENT_MOUSE`.

## Mouse and scroll routing

Feed mouse events in with `wuss_mouse_click` (action `wuss_MOUSE_DOWN` or `wuss_MOUSE_UP`) and `wuss_mouse_move`. Feed scroll events in with `wuss_scroll`. Each hit-tests the topmost window at `(x, y)` and delivers to its task in window-local coordinates. All three take an optional `wuss_window_t **hit` out-parameter. It names the window under the pointer (or the window being dragged). Wuss drops an event if the hit window has no handle callback, or (for scroll) if the pointer is over its titlebar.

### Drag looks

A core drag (see `wuss_drag_start`) takes over the pointer until a release or Escape ends it with `wuss_EVENT_DRAG_END`. There are three ways to start one. They differ only in what follows the pointer:

- `wuss_drag_start(wuss, window, size, hotspot)` draws a marching-ants box. Wuss repaints only its edges each frame.
- `wuss_drag_start_bitmap(wuss, window, bitmap, hotspot)` draws a bitmap at its own size, honouring its alpha. This is the equivalent of RISC OS's DragASprite.
- `wuss_drag_start_object(wuss, window, size, hotspot, draw, opaque)` calls `draw(scr, box, opaque)` to paint the image. This is the equivalent of DragAnObject. The callback runs last in `wuss_redraw_dirty`, with the clip already narrowed to the box. It must only paint.

The drag borrows the bitmap and `opaque` until it ends. A solid image repaints its old and new boxes once a frame, and only when it has moved. Windows underneath repaint through the normal dirty path. There is no save-under.

When `wuss_config_t::mouse.outline_drags` is set (the Configuring task's "Solid drags" option, unticked), solid drags show ants of the same size. A DRAGGABLE icon drags its image and a gridview drags the item's glyph. Each falls back to ants when there is no image.

### Grabbing

`wuss_drag_start_grab(wuss, window)` starts a core drag that draws nothing. It scrolls `window` instead, and keeps the content under the pointer as the pointer moves. Call it from the handler of a content `wuss_MOUSE_DOWN`.

- Wuss measures the offset from the start of the grab. So if the end of the document stops the content, the content catches the pointer up on the way back.
- The grab stays within the document extent. An axis with no scrollbar does not move. This is the same as for `wuss_window_scroll_by`.
- The grab also sets pointer bounds (see `wuss_set_pointer_bounds`) that stop the pointer where the content stops. So a host that applies them holds the pointer still when there is nothing further to drag.
- A release ends the grab with `wuss_EVENT_DRAG_END`. Escape does too, after it restores the starting offset.

Resizing and Viewing use it for their document areas.

## Scrolling

Each window has a scroll offset, `(0, 0)` by default. It is the point in the task's virtual content space that appears at the content area's top-left. `wuss_window_set_scroll(window, x, y)` moves it, and invalidates the content area so the next redraw uses the new offset. `wuss_window_get_scroll` reads it back. `wuss_scroll` applies this offset itself as Wuss's default scroll action, clamped to `doc` (set at window creation). It then also delivers `wuss_EVENT_SCROLL` to the task, if the task has a handle:

- Window-local `x`/`y` in mouse and scroll events are in virtual content space. That is, the scroll offset is already added. `wuss_window_invalidate`'s `local_box` uses the same space.
- A redraw event's `content` is still the on-screen (unscrolled) content box. A task reads the offset itself with `wuss_window_get_scroll`, to find which part of its content to paint there.

### How the coordinate spaces relate

Everything that a redraw callback gets is in _screen space_, except `scroll`, which is a _virtual content space_ offset.

```
screen (0,0) --- wuss->scr, origin top-left
  +-----------------------------------------------+
  |  win->visible  (whole on-screen footprint)    |
  |  +-----------------------------------------+  |
  |  | titlebar + 1px outline  (carved off)    |  |
  |  +--------------------------------+--------+  |  wuss__content_box() =
  |  | content box = redraw.bounds    | vscroll|  |    visible
  |  | (bounds.x0,y0) .               | (carve)|  |    - outline
  |  |   . . +-------------+         .|        |  |    - titlebar
  |  |   .   | redraw.     |  this   .|        |  |    - furniture carve
  |  |   .   | content     |  piece  .|        |  |
  |  |   .   | (one clip   |  only   .|        |  |
  |  |   .   |  piece)     |         .|        |  |
  |  |   .   +-------------+         .|        |  |
  |  |   . . . . . . . . . . . . . . .|        |  |
  |  +--------------------------------+--------+  |
  |  |         hscroll  (carve)                |  |
  |  +-----------------------------------------+  |
  +-----------------------------------------------+

virtual content space:  size win->doc, its own origin (0,0).
  win->scroll = which point of doc sits at bounds.x0,y0.
  doc extends past the content box (right + bottom); that
  overhang is exactly what scroll ranges over.
```

- `win->visible` — the window's whole on-screen box (screen space). Wuss sets it at create, move, resize and toggle. `wuss_window_get_visible_bounds`.
- content box / `redraw.bounds` — `visible` with the outline, titlebar and any scrollbar/resize carve removed (`wuss__content_box`). `wuss_window_get_content_bounds`.
- `clipped` (internal) — `bounds` intersected with the dirty rect, before Wuss subtracts the windows above.
- `redraw.content` — `clipped` minus the areas that higher windows cover, split into pieces that do not overlap. The callback fires once per piece, with `scr->clip` already set to it. Touch only pixels inside it.
- `win->scroll` / `redraw.scroll` — document-space offset. The task sets it with `wuss_window_set_scroll`. Wuss clamps it to `[0, doc - viewport]`.
- `win->doc` — virtual content extent, fixed at window create. It sets the scrollbar sausage size and the scroll clamp.

Conversions (as used in `mouse-move.c`, `scroll.c`, `scroll-step.c`):

```
screen -> document: doc.x = screen.x - bounds.x0 + scroll.x     (same for y)
document -> screen: screen.x = bounds.x0 - scroll.x + doc.x
scroll clamp:       max = doc - (bounds.x1 - bounds.x0);  clamp(scroll, 0, max)
```

In a redraw callback, start drawing at `bounds.x0 - scroll.x`, `bounds.y0 - scroll.y`. Then paint the whole content normally. The framebuffer clip (`scr->clip` = `redraw.content`) discards anything outside the piece. See `apps/wuss/tasks/reading/reading.c`.

## Redrawing

- `wuss_redraw` repaints every window, back-to-front, unconditionally. First it paints the configured backdrop colour (see Setup) behind them, if there is one.
- Within a window, Wuss paints in a fixed order:
  1. the window background colour
  2. its icons (see "Icons" below)
  3. the task's `wuss_EVENT_REDRAW` handler.

  So a task always draws over the background and any icons, never under them. A `wuss_WINDOW_NO_REDRAW` window skips the last step. It gets only the background and icons.
- `wuss_invalidate` / `wuss_window_invalidate` mark a screen-space or window-local region dirty. Window management calls them automatically for its own changes. But when a task's content changes on its own (e.g. an animation), the task must call one of them itself. It passes the union of the old and new areas that need repainting.
- `wuss_redraw_dirty` repaints only the accumulated dirty region, then clears it. If a backdrop is configured, it first paints the backdrop colour into each dirty region. Without a configured backdrop, Wuss repaints only windows, not the background between or behind them. So if a caller's invalidation can expose background (e.g. after a window move), the caller must first clear that region itself.
- `wuss_get_dirty_count`/`wuss_get_dirty(wuss, index, out)` fetch the currently accumulated dirty regions without redrawing. Wuss coalesces the regions as they accumulate, up to a fixed cap. Past the cap, it merges the pair of regions whose union adds the least area, with `box_list_add`.
- Wuss clips a window move or resize, piece by piece, against the windows above it in the z-order. It blits any pixels that a move can keep directly, and does not queue them as dirty. Only the pieces that really change go into the dirty region. This is an internal optimisation. It has no effect on a task's own redraw handling.

## Icons

As on RISC OS, a window can carry **icons**: rectangular UI elements that Wuss draws and hit-tests inside the content area. v1 has two types:

- `wuss_ICON_TYPE_LABEL` — static text, optionally bordered. The border is `spec.u.label.border`: `wuss_ICON_BORDER_NONE` / `_RIDGE` / `_GROOVE` / `_ACTION` / `_DIVIDER` / `_PLAIN`. `_GROOVE` is a RISC OS-style read-only display field. `_PLAIN` is a 1px fg outline, as on a writable. Clicks fall through to the task as `wuss_EVENT_MOUSE`.
- `wuss_ICON_TYPE_ACTION` — a bevelled rectangle with a centred label and pressed-state feedback. While the button is held, the bevel inverts and the label shifts one pixel down-right. Clicks and hovers arrive as `wuss_EVENT_ICON`.

The enum leaves room for sprite and editable-text types later.

Icons are dynamic, and their window owns them:

- `wuss_icon_create(window, spec, &icon)` — returns an opaque `wuss_icon_t *`. The spec gives:
  - the bounding box and type
  - the text (copied, with `NULL` treated as `""`)
  - foreground and background palette indices
  - a per-type payload in the `u` union (`u.label.border`, `u.pattern.tile`, `u.bitmap.image`/`u.bitmap.set`, `u.radio.group`, `u.menu_entry.swatch`)
  - flags.

  A `wuss_ICON_TYPE_ACTION` must pass a real `bg`. Passing `wuss_NO_BACKGROUND` gives `result_WUSS_BAD_ICON`. An unknown type also gives `result_WUSS_BAD_ICON`. An out-of-range `fg`/`bg` gives `result_WUSS_BAD_COLOUR`.
- `wuss_icon_delete(window, icon)` — NULL-safe.
- `wuss_icon_set_text(window, icon, text)`, `wuss_icon_set_hidden(window, icon, hidden)`, `wuss_icon_set_selected(window, icon, selected)`.
- Getters: `wuss_icon_get_bbox`, `wuss_icon_get_type`, `wuss_icon_get_text` (never `NULL`), `wuss_icon_get_selected`.

Flags:

- `wuss_ICON_FLAGS_HIDDEN` — Wuss does not draw or hit-test the icon.
- `wuss_ICON_FLAGS_DISABLED` — Wuss draws the icon greyed. Clicks fall through as `wuss_EVENT_MOUSE`.

An icon's bounding box is in **virtual content space**. This is the same space as `wuss_EVENT_MOUSE`'s `point` and `wuss_window_invalidate`'s box. So icons scroll with the content. The on-screen box is `content-top-left - scroll + bbox`.

Wuss draws icons in creation order, so later icons paint on top. Hit-testing scans in reverse, so the topmost icon at a point wins. When the pointer leaves a pressed button, its pressed state clears. v1 does not press the button again when the drag comes back in. It does not track which mouse button is held.

The bevel's light (top/left) and dark (bottom/right) edge shades come from `config->bevel.light` / `config->bevel.dark` at `wuss_create` time. Wuss checks them like the other furniture colours. When `config` is `NULL`, both default to the titlebar fill colour. A held button also fills its face with `config->bevel.pressed`, in place of the icon's own background. Pass `wuss_NO_BACKGROUND` (or `NULL` config) to make it follow `bevel.dark`.

### Icon pool

`wuss_iconpool_load(wuss, dir)` scans a directory for `*.png`. It loads each one, RLE-compresses it in place (`bitmap_compress`) and records its leafname without `.png`. It sorts the entries by name (`strcmp`) and indexes them in that order, so an index is the same on all platforms. But address entries by name all the same. `wuss_create` loads `resources/wuss/iconpool` at startup:

- `wuss_iconpool_lookup(wuss, name)` → 0-based index, or `-1`.
- `wuss_iconpool_bitmap(wuss, index)` → the compressed `bitmap_t *` (owned by the window manager), or `NULL` out of range.
- `wuss_iconpool_count(wuss)`.
- `wuss_iconpool_frame(wuss, index, frame)` → one frame of the entry, or `NULL` out of range (so past the last frame).

**Sprite sheets.** A leafname that ends `_<w>x<h>` (decimal, no leading zero, both at least 1), e.g. `duck_24x24.png`, is a sprite sheet: a uniform grid of `w`×`h` cells.

- It loads as one entry, named without the suffix (`duck`), and split into frames. Wuss RLE-compresses each frame.
- Frames are indexed row-major from 0 at the top left. Transparent cells still count.
- There is no mirroring or animation data. A left-facing set is another row.
- Wuss drops partial cells at the right and bottom edges, with a warning. It skips a sheet smaller than one cell.
- A plain PNG is a 1×1 grid.
- Everything that does not ask for a frame (`wuss_iconpool_bitmap`, name lookups, icon specs) gets frame 0.
- Leaves load in `strcmp` order. So when two share a name (`duck.png` and `duck_24x24.png`), the first wins. Wuss skips the other with a warning.

A `wuss_ICON_TYPE_BITMAP` spec with `u.bitmap.image == NULL` and `u.bitmap.set = wuss_ICONPOOL_SET(idx)` draws the loaded entry at `idx`. The `wuss_ICONPOOL_SET` macro adds one, so a zero-initialised spec means "no entry". Or set `u.bitmap.name` (and `pressed_name` for the held-down image) to an entry's name, and let `wuss_icon_create` resolve it. An unknown name logs a warning, and the icon draws the red placeholder box. `image` wins over `set`, which wins over `name`. An out-of-range index gives `result_WUSS_BAD_INDEX`.

A second call to `wuss_iconpool_load` replaces the set. `wuss_destroy` frees it. A missing directory is not an error. It gives a zero-length set. Wuss copies `dir` internally, so you can safely pass a `path_join_filename` result.

**Bevelled and latching bitmap icons.** `u.bitmap.border` (`wuss_ICON_BORDER_RIDGE` or `_GROOVE`) draws a bevel inside the bounding box, with the image centred in it. The default, `_NONE`, keeps the image at the top-left. With `wuss_ICON_FLAGS_INTERACTIVE`, a non-zero `u.bitmap.group` makes the icon latch like a grouped radio. A click selects it, and clears every other selected bitmap icon in the same group on that window. A latched icon swaps its bevel (RIDGE reads raised at rest, sunk when latched) and fills with its `bg` colour. Group zero is a plain button. Paint's toolbox uses this.

**Private pools.** A task can also keep a private pool. When Wuss resolves the names of the task's icons, it searches that pool before the shared one. The private pool is opt-in. Call one of these:

- `wuss_task_iconpool_load(task, dir)`
- `wuss_task_iconpool_load_resource(task, resources)`, which loads `resources/wuss/tasks/<task name>/iconpool`. The name comes from `wuss_task_desc_t::name`, so the task needs one.

Both replace the task's pool, and load in the same way as the shared pool.

- `wuss_task_iconpool_find(task, name)` returns the bitmap from the private pool, falls back to the shared pool, or returns `NULL`.
- `wuss_task_iconpool_frame(task, name, frame)` searches in the same way for a sheet's frames.
- Wuss frees the pool with the task.
- The icon stores a resolved name as a bitmap pointer (`u.bitmap.image` / `pressed_image`), not as an index. So the same name can mean different bitmaps in different tasks.
- `set`/`pressed_set` index only the shared pool. RADIO/OPTION glyphs also come only from the shared pool.
- Reloading either pool frees the bitmaps that it held. So reload before you create icons that name them.

## Components

The `WUSS_COMPONENTS` CMake option builds `libraries/wuss/component/`. It holds small reusable task helpers on top of the core. Their headers are under `include/wuss/component/`.

### Program-information dialogue

`wuss_proginfo` is the RISC OS "Info" / Toolbox ProgInfo dialogue in miniature. It is a small fixed window of Name / Purpose / Author / Version rows. A task fills it in once, and then usually hangs it off its MENU-button pop-up.

```C
typedef struct wuss_proginfo_desc
{
  const char *name;    /* row "Name"    -- required */
  const char *purpose; /* row "Purpose" */
  const char *author;  /* row "Author"  */
  const char *version; /* row "Version" */
}
wuss_proginfo_desc_t;

result_t       wuss_proginfo_create(wuss_proginfo_t **out, wuss_task_t *task,
                                    const wuss_proginfo_desc_t *desc);
void           wuss_proginfo_destroy(wuss_proginfo_t *doomed);
wuss_window_t *wuss_proginfo_window(const wuss_proginfo_t *pi);
```

`wuss_proginfo_create` builds one hidden window on `task`. It sizes the window from that task's font metrics and the text in `desc`. There is one label row per non-NULL field: the name right-justified in a left column, and the value left-justified beside it. The call borrows each `desc` field and copies it.

The window has `wuss_WINDOW_NO_CLOSE` and `wuss_WINDOW_NO_REDRAW`. It is labels on a flat backdrop, so it needs no redraw handler. It stays on `task` until `wuss_proginfo_destroy` closes it. So `task` must **not** be an autoclose task, because the hidden window would stop its window list from ever becoming empty. `task` must also outlive the handle. As for any borrowed menu-item window, close any menu chain that refers to `wuss_proginfo_window()` before you destroy the dialogue.

Point a menu item's `window` field at `wuss_proginfo_window()`. Wuss then shows the dialogue where a submenu would open.

## Opening files

Double-clicking a file in a Filing window opens it the RISC OS way, through a `wuss_MESSAGE_DATA_OPEN` message (`wuss/message.h`):

1. Filing sends a recorded broadcast whose payload is a `wuss_data_open_t`: the file's `filetype_t` and its full path.
2. Each running task gets it in turn, as a task-view `wuss_EVENT_MESSAGE` (`window` is NULL). A task that opens files of that type calls `wuss_acknowledge` and loads the file. The first acknowledgement stops delivery.
3. If no task acknowledges it, the message bounces back to Filing as `wuss_EVENT_MESSAGE_BOUNCED`. Filing calls its run function (`filing_set_run_fn`), the equivalent of `Alias$@RunType_XXX`, to start the app for the type. It then broadcasts the message once more.
4. If there is no app for the type, or the second broadcast bounces too, Filing reports "No application to open files of type &XXX".

A double ADJUST click opens the file and then closes the Filing window, as it does for a directory.

The demo app's tasks claim types through `taskapp_desc_t::opens`, a 0-terminated list of RISC OS filetype numbers. taskapp acknowledges a message as soon as the type matches. It then opens a new instance and passes it the path through `data_load`. The app itself reports a load that fails, so Filing never reports a second error. The run function in `apps/wuss/tasks/tasks.c` maps PNG (&B60) and JPEG (&C85) to Viewing, and text (&FFF) and Markdown to Reading.

The reverse trip goes through `wuss_MESSAGE_FILER_OPEN_DIR`. ADJUST on a document window's close icon broadcasts it, with the directory that holds the document as a NUL-terminated path. Filing opens a window on that directory (or brings the existing one to the front). The document window then closes. With Shift held, it stays open. A document that has no file yet (an untitled Painting) only closes. The demo's document tasks do this through `task_close_adjust` in `apps/wuss/tasks/common/common.h`.

Markdown has no allocated RISC OS filetype. `filetype_RISCOS_MARKDOWN` (&1D0) is a placeholder from the user range until an allocation exists.

On RISC OS, `io/dirlist` still takes a file's type from its extension, not from the filing system. So a file without an extension is `&FFD` and finds no app.

## Glossary

Terms as this document and the API use them. Several are RISC OS conventions, which Wuss follows.

- **ADJUST** — the secondary mouse button, `wuss_BUTTON_ADJUST`. By convention it gives the variant of an action. ADJUST on the back button brings a window to front, and does not send it back. ADJUST on a scroll arrow steps against the direction of the arrow.
- **Backdrop** — the desktop background colour that Wuss paints behind all windows. `config->backdrop` sets it at `wuss_create` time. `wuss_NO_BACKGROUND` leaves the area behind windows untouched, and the caller must repaint it.
- **Button flags** — `wuss_button_t` values are flags (SELECT 4, MENU 2, ADJUST 1). Wuss ORs them together, so it can report a chord. Test a reported button with `&`, never for equality.
- **Chord** — two or more mouse buttons held together, e.g. SELECT+ADJUST. Wuss's own furniture handling resolves an ambiguous chord in SELECT's favour.
- **Content area** — the part of a window that belongs to its task. Its bounds are exactly what the caller passed to `wuss_window_create`, less any room its panes take. Wuss adds furniture outside it. Read it back with `wuss_window_get_content_bounds`.
- **Dirty region** — the accumulated set of screen-space boxes that need repaint. Wuss coalesces them as they accumulate. `wuss_redraw_dirty` repaints and clears it.
- **Font slot** — one of up to `wuss_MAX_FONTS` `wuss_font_desc_t` entries passed to `wuss_create`. The `libraries/wuss/font/` module owns them. Slot 0 is the system font, slot 1 the bold weight, slot 2 (`WUSS_SYMBOL_FONT`) an optional glyph font for menu decoration. Read back with `wuss_get_font` / `wuss_get_font_n`. See "Fonts".
- **Document extent** — `doc`, the size of a task's virtual content space, fixed at window creation. It sets how far a window can scroll and the scrollbar sausages' proportions. It also sets the size to which a resize-drag or toggle-size can grow the content area.
- **Minimum extent** — `min_doc`, the smallest content size that a resize-drag or toggle-size leaves a window at, fixed at window creation. `(0, 0)` means the built-in floor.
- **Furniture** — everything that Wuss draws around a window's content: outline, titlebar and its buttons, scrollbars, resize button. Wuss draws it outside the content area, and never carves it out of the content area. Wuss handles furniture clicks itself. They never reach the task.
- **Furniture button** — a clickable furniture region in the titlebar or window corner: close, back, toggle-size, resize. (Earlier revisions called it an "icon". That name now means the work-area element below.)
- **Handle callback** — a task's single `wuss_event_fn_t`. It receives every event kind and dispatches on `event->kind`. A window whose task has no handle receives no events at all.
- **Icon** — a rectangular UI element that Wuss draws and hit-tests inside a window's content area: a static `wuss_ICON_TYPE_LABEL`, or a clickable bevelled `wuss_ICON_TYPE_ACTION`. `wuss_icon_create` creates it, and its window owns it. Its bounding box is in virtual content space, so it scrolls with the content. Its screen position is `content-top-left - scroll + bbox`. See "Icons".
- **Invalidate** — mark a region dirty for the next `wuss_redraw_dirty`. Window management does this for its own changes. A task must do it for its own content changes.
- **MENU** — the middle mouse button, `wuss_BUTTON_MENU`. Wuss routes it like any other button. A task responds by opening a pop-up menu with `wuss_menu_open` (`wuss/menu.h`).
- **Outline** — the 1px border around a window. `wuss_WINDOW_NO_OUTLINE` turns it off.
- **Pane** — a toolbar attached inside a parent window with `wuss_window_attach_pane`, along one edge of its content area. The content area shrinks to make room. See "Panes".
- **Sausage** — the draggable thumb in a scrollbar well. Its size is in proportion to how much of the document extent the content area shows.
- **Screen space** — coordinates in the underlying `screen_t`, origin at its top-left. Visible and content bounds are in screen space.
- **SELECT** — the primary mouse button, `wuss_BUTTON_SELECT`. It does the plain action, and raises a window when used on its titlebar.
- **Task** — the client of a window: an event callback plus an opaque `task_data` pointer, held in a `wuss_task_t`. Wuss owns the window. The task owns what it draws inside it.
- **Titlebar** — the strip above the content area. It carries the window's label and its buttons, and is the drag handle for moving the window. `wuss_WINDOW_NO_TITLEBAR` turns it off.
- **Toolbar** — a window attached to an edge of a parent window with `wuss_window_attach`. It moves, stacks, hides and closes with its parent. See "Toolbars".
- **Toggle size** — the titlebar button that switches a window between its normal size and a maximised size, and back.
- **Virtual content space** — the task's own full coordinate space, of size `doc`. Mouse and scroll events arrive in it, that is, with the scroll offset already added.
- **Visible bounds** — a window's whole on-screen footprint, content plus furniture. `wuss_window_get_visible_bounds`.
- **Well** — the track along which a scrollbar's sausage slides, between the two arrow buttons.
- **Window-local coordinates** — coordinates relative to the content area's top-left. `wuss_window_invalidate` takes its box in virtual content space, that is, with the scroll offset already added.
- **Z-order** — the front-to-back stacking order of windows. Wuss keeps it as three stacks:
  - `wuss_STACK_TOP` (error dialogues, menus)
  - `wuss_STACK_MIDDLE` (regular windows, the default)
  - `wuss_STACK_BACK` (the icon bar).

  Every window in an earlier stack is in front of every window in a later one. A window joins a stack at creation (`wuss_WINDOW_STACK_TOP` / `wuss_WINDOW_STACK_BACK`), or moves with `wuss_window_set_stack`. `wuss_window_restack`, or a SELECT click on a titlebar, reorders a window only within its own stack. Content clicks never change it. If the pointer stays within two rows of the bottom of the screen for 400ms, a covered icon bar pops to the back of `wuss_STACK_TOP`. It returns to `wuss_STACK_BACK` when the pointer leaves it.
