# [Wuss](https://github.com/dpt/Wuss) > Menu standard

Every wuss app's window menu follows this arrangement, so the same thing sits
in the same place from one app to the next. It follows the RISC OS Style
Guide and PrivateEye's menus where they apply.

The icon bar menu is not covered here. `taskapp` builds it (Info, New or New...,
Hide, Quit). Help and Choices... will join it when wuss has help text and a
choices system.

A window menu is laid out in two steps. First build the logical layout from
the top-level entries below. Then reduce it to the physical layout the user
sees.

## Logical layout

A window menu is titled with the app's name. Its top-level entries come in
this order. Leave out any entry the app has nothing for.

1. **`File ⇒`** — document-wide operations: Info, Save, Print.
2. **`Edit ⇒`** — Undo, Redo, Cut, Copy, Paste, Delete, `Select all`,
   `Clear selection`, Find.
3. **`View ⇒`** — how the document is shown, not what it holds: new view,
   view options, grids, scale and zoom, `Display ⇒` with its
   `Dithering ⇒` (`dithermenu_t`'s `display`).
4. **`<Object> ⇒`** — operations on one type of document object, named for
   that type: `Text` in a text editor. An app with several object types has
   one entry for each, such as `Text`, `Style` and `Page`, or `Selection`,
   `Shapes` and `Layer`. An app's own settings (`Model ⇒`, `Gravity`) and
   generators (`Randomise`, `Reset`) go under the object they act on.
5. **`Utils ⇒`** — whatever is left.

If the app has a single object type (a ball game, say) and its entry holds
only a handful of rows, put those rows straight in the main menu, between
dividers, in place of the `<Object> ⇒` entry.

## Physical layout

The logical layout is then reduced:

- A submenu of one or two rows is replaced by those rows. `File ⇒` holding
  only `Save ⇒` becomes `Save ⇒`; `Edit ⇒` holding only Undo and Redo becomes
  `Undo` and `Redo`.

## Dividers

A divider marks a change in the kind of row, as in Ovation Pro. It is not
there to break up length.

- Rows put in the main menu by reduction, or a single object's rows, form a
  group. Divide each group from its neighbours, never inside it.
- Submenu rows (`Save ⇒`, `Edit ⇒`, `View ⇒`, `<Object> ⇒`) sit together
  with no dividers between them, even when reduction put them there.
- A single object's rows that are mostly submenus join the submenu rows
  with no divider. Dithering's `Palette ⇒`, `Method ⇒`, `Strength ⇒` and
  `Pause` follow `Save ⇒` and `Display ⇒` undivided.
- A menu of three rows or fewer has no dividers, except as below.
- In a submenu, divide commands from a list of choices (Ovation Pro's
  `Edit style, Remove` above its list of styles), however short the
  submenu.

For example, Sofa. Logically it has `File ⇒` (Save), `View ⇒` (Colours) and
one object, the simulation (Pause, Model, Auto-cycle, Randomise, Reset).
Reduced, it becomes:

```
Sofa
  Save         ⇒ F3
  Colours      ⇒
  ----------------------
  Pause          Space
  Model        ⇒
  Auto-cycle
  Randomise      ^R
  Reset          R
```

## Save

`Save ⇒` opens the `wuss_saveas` box as its submenu (set the row's `window`
to `wuss_saveas_window`). `F3` opens the same box on its own through
`wuss_saveas_open`. Every app with Save binds `F3`.

The row is `Save`, not `Save PNG`. Once an app can save in a second format,
`Save ⇒` becomes a submenu of formats, each opening its save box, and `F3`
opens the box for the format used last (PNG to start with). Until then
`Save ⇒` goes straight to the PNG box.

## Labels

- Sentence case: `Select all`, `New game`, `Grid size`. Proper nouns keep
  their capitals.
- `...` only on a row that opens a dialogue box. A row with a submenu has no
  `...`.
- `Colours ⇒` for colour choices, including a lone background colour. There
  is no top-level `Background ⇒`.
- Running and stopping is one `Pause` row, ticked while paused. There is no
  `Animate`.
- Selection rows are `Select all` and `Clear selection`.

## Keys

These keys are reserved. Don't use them for anything else in a window.

| Row             | Key     |
|-----------------|---------|
| Save ⇒          | `F3`    |
| Pause           | `Space` |
| Undo            | `F8`    |
| Redo            | `F9`    |
| Select all      | `^A`    |
| Clear selection | `^Z`    |
| Randomise       | `^R`    |
| Reset           | `R`     |
| New game        | `N`     |

Use `WUSS_MENU_ITEM_SHORTCUT` so the key is shown against the row.
