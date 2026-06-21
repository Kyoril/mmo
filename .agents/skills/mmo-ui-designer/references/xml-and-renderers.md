# XML And Renderers

## Source Of Truth

- Layout schema: `data/client/Interface/Ui.xsd`
- Loader: `src/shared/frame_ui/layout_xml_loader.cpp`
- Frame and properties: `src/shared/frame_ui/frame.{h,cpp}`
- Renderer registration and Lua bindings: `src/shared/frame_ui/frame_mgr.cpp`
- Renderers: `src/shared/frame_ui/*_renderer.cpp`
- Load order: `data/client/Interface/GameUI/GameUI.toc` and `GlueUI/GlueUI.toc`

## Layout Rules

- Frame names are global. Inherited child names are copied into each instance but are reached reliably through `GetChild(index)` unless a unique global name is declared.
- Templates are ordinary top-level frames referenced with `inherits`. The loader copies the template before applying instance properties and area data.
- Anchor offsets use native UI coordinates. Multiple anchors determine size; an explicit `Size` can provide the remaining dimension.
- `relativeTo` resolves by global frame name at load time. Ensure the target appears earlier in load order.
- Component `<Area>` supports insets, not frame anchors. Use nested frames when artwork requires independently positioned geometry.
- XML text property values are localized automatically. Lua strings require `Localize("KEY")`.

## Imagery Contract

`ImagerySection` defines reusable visual components. `StateImagery` selects sections and their draw order. A renderer decides the active state name.

- `DefaultRenderer`: normally `Enabled` or `Disabled`.
- `ButtonRenderer`: `Normal`, `Hovered`, `Pushed`, or `Disabled`. It does not render checked state.
- `CheckboxRenderer`: unchecked states plus `NormalChecked`, `HoveredChecked`, `PushedChecked`, and `DisabledChecked`. Use it for checkboxes, radio rows, and selected tabs.

Never assume declaring a state makes it reachable. Read the renderer and verify the corresponding frame property or Lua method is used.

For mutually exclusive tabs, use `CheckboxRenderer`, set `Checkable=true`, define the checked state family, call `SetChecked(i == selectedIndex)` during every refresh, and refresh even when the active tab is clicked because the base button toggles itself before firing `OnClicked`.

## Common Failures

- A selected state is declared but never rendered because the renderer ignores or cannot reach it.
- A frame is clipped because left and right anchors leave negative or zero width.
- A localized label overflows because the design was checked only with English.
- Multiple translucent border textures are stacked, allowing the lower background to show through decorative outer rings. Prefer one self-contained border per visual boundary. When an extra outline is intentional, render it in a slightly larger backing frame before the opaque content frame so the content covers its transparent center.
- Empty repeated controls are disabled instead of hidden, leaving placeholder chrome.
- A visual list index is used for gameplay after filtering; use an entry ID for actions.
