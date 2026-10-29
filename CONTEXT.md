# CONTEXT.md

Domain glossary for Wuss. Use these terms in code, docs and design discussion.

## wuss

**Drag look**: what a core drag shows under the pointer. The default is a marching-ants box (`wuss_drag_start`). A solid drag shows a bitmap (`wuss_drag_start_bitmap`, like RISC OS DragASprite) or an image painted by a client callback (`wuss_drag_start_object`, like DragAnObject). Every look shares one drag session and ends in the same `wuss_EVENT_DRAG_END`. `wuss_config_t::mouse.outline_drags` turns solid drags back into ants of the same size.

**Menu session**: the state and behaviour of one open menu interaction in wuss: the chain of open menus and submenus, the owning task, the bottom gap, the `eat_up` flag that swallows the tail of the press that opened the menu, outside-click dismissal, key handling while open, and abandonment when a window closes or the owning task is destroyed. Owned by the menu layer (`libraries/wuss/menu/`). Core reaches it only through a few entry points: press, key, window-closed, task-destroyed. State is opaque to core.

**Pane**: a toolbar attached inside its parent window, along one edge of the parent's content area, with `wuss_window_attach_pane`. The parent's footprint stays the same; its content area shrinks by the pane's thickness plus a divider. Wuss sets the pane's length to match the content along that edge; the pane keeps its own thickness. Otherwise it follows its parent as a toolbar does. Used for internal toolbars. Distinct from a **toolbar**, which sits outside the parent's edge.
