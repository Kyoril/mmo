# GameUI Agent Playbook

This guide is for AI agents making or reviewing in-game UI changes in `GameUI`.

After reading it, the agent should be able to add or modify a working in-game UI feature without guessing about load order, frame lifetime, Lua bindings, or panel behavior.

This document is intentionally narrower than the general UI docs. It covers the runtime and conventions that matter for reliable changes in the in-game HUD, windows, menus, tooltips, and dynamic widgets.

## Scope

Use this playbook for the in-game client UI loaded from `Interface/GameUI/GameUI.toc`.

Do not use it for login, realm, character creation, or character select work. Those belong to `GlueUI` and have a separate load graph and screen flow.

## Mental Model

The in-game UI is a three-layer stack:

1. `GameUI.toc` defines load order.
2. XML files build the frame tree, visuals, templates, and inline script hooks.
3. Lua files provide behavior and register for engine events.

The important runtime rule is the order:

1. The world game state loads `GameUI.toc`.
2. Each XML file is parsed into frames immediately.
3. `<Script file="...lua" />` entries inside that XML are deferred until the XML finishes parsing.
4. Inline `<Scripts>` blocks are compiled after those deferred Lua files are executed.
5. After the whole TOC is loaded, `OnLoad` runs recursively from the top frame down.

What this means in practice:

- `inherits`, `parent`, and `relativeTo` targets must already exist when the XML parser reaches them.
- TOC order matters. Template and shared-parent files must be listed before dependents.
- Inline handlers can safely call Lua functions defined in the XML's referenced script file, because the file runs before the inline handler is compiled.
- `OnLoad` is the place to wire events and initialize runtime-only state.

## What Exists Today

The existing GameUI files already show the main patterns you should follow:

- `GameParent` is the in-game root and owns high-level panels, error text, and cross-screen events.
- `GameTemplates` defines reusable fonts, buttons, text styles, fields, scrollbars, borders, and panel templates.
- `ActionBar` shows the indexed-button pattern: static XML children with numeric `id` values and Lua loops over `_G["ActionButton"..i]`.
- `PlayerFrame` and `TargetFrame` show the unit-frame pattern: XML for structure, Lua event handlers for refresh, custom `UnitModel` children for portraits.
- `GameMenu` and `ContextMenu` show the runtime clone pattern: clone a template, add it as a child, then anchor and configure it from Lua.
- `StaticDialog` shows modal behavior, dynamic sizing, and input capture for edit boxes.
- `Minimap` shows use of custom client-only frame types and lifecycle hooks like `OnShow`, `OnHide`, and `OnUpdate`.

When you add something new, start by finding the closest existing pattern and copy that shape instead of inventing a new one.

## Framework Rules That Matter

### Named frames become globals

Every named frame is registered in the frame manager and exposed to Lua as a global. Existing code also uses `_G["Name"]` heavily for indexed access.

Consequences:

- Frame names must be globally unique across the loaded UI, not just unique inside one XML file.
- Renaming a frame usually requires a matching Lua rename.
- Case-only renames are risky. The manager lookup is case-insensitive, but Lua globals are created with the exact frame name.

### Supported frame types are explicit

Do not assume WoW FrameXML parity. Use only frame types registered by the engine.

Core types used in GameUI:

- `Frame`
- `Button`
- `TextField`
- `Thumb`
- `ScrollBar`
- `ProgressBar`
- `ScrollingMessageFrame`

Client-specific types used in GameUI:

- `World`
- `Model`
- `UnitModel`
- `Minimap`
- `Cooldown`

If you need a new frame type or renderer, that is a C++ task, not just an XML/Lua task.

### Supported inline script hooks are explicit

The XML loader supports these inline handlers:

- `OnLoad`
- `OnUpdate`
- `OnClick`
- `OnMouseDown`
- `OnShow`
- `OnHide`
- `OnEnter`
- `OnLeave`
- `OnDrag`
- `OnDrop`
- `OnTabPressed`
- `OnEnterPressed`
- `OnSpacePressed`
- `OnEscapePressed`

Write them as blocks that return a function.

Do not build new work around `<Events>`. The parser logs that event tags are not supported. Runtime events should be registered from Lua with `RegisterEvent`.

### Renderer state names are not arbitrary

The renderer decides which state imagery names matter.

Common expectations:

- `DefaultRenderer`: `Enabled`, `Disabled`
- `ButtonRenderer`: `Normal`, `Hovered`, `Pushed`, `Disabled`
- `CheckboxRenderer`: button-style states plus checked variants
- `TextFieldRenderer`: `Enabled`, `Disabled`, optional `Caret`
- `ProgressBar`: usually `Enabled`, `Disabled`, `Progress`, optional overlay states

If a renderer expects a state you do not define, the frame may render partially or not as intended.

### Property binding is first-class

Use properties to connect XML visuals to Lua-updated state.

Common patterns:

- `$PropertyName` inside component attributes such as text color or alignment.
- `<PropertyValue property="Icon" />` for image source binding.
- Lua calling `SetProperty`, `SetText`, `SetProgress`, `SetOpacity`, or `SetButtonState` to update the rendered result.

Prefer property-driven visuals over rewriting layout from Lua.

### Cloning is the standard dynamic-list mechanism

Dynamic menus and repeated rows commonly work like this:

1. Define a template frame in XML.
2. Clone it from Lua with `Template:Clone()`.
3. Add it to a container with `AddChild`.
4. Anchor it from Lua.
5. Hide or remove old children before rebuilding.

Use this for data-driven repeated content such as context menus, list rows, or generated buttons.

## Reliable Workflow For GameUI Changes

### 1. Start from the closest existing example

Use the existing screen that behaves most like your target:

- Panel window: copy the shape used by `CharacterWindow`, `SpellBook`, `FriendsFrame`, or `GuildFrame`.
- Runtime-generated menu: copy `GameMenu` or `ContextMenu`.
- Unit HUD element: copy `PlayerFrame`, `TargetFrame`, or `PartyFrame`.
- Tooltip-driven hover behavior: copy `ActionBar` or `GameTooltip`.
- Modal dialog: copy `StaticDialog`.

If you cannot name the existing analog first, you probably are not ready to edit.

### 2. Decide whether the change is static, dynamic, or engine-backed

Use XML only when the structure is fixed and the behavior is minimal.

Use XML plus Lua when:

- the frame responds to game events
- text, icons, or progress change at runtime
- buttons need handlers
- a list is rebuilt from data

Escalate to C++ only when:

- you need a new frame type
- you need a renderer feature the XML system cannot express
- you need a new Lua API binding
- the needed event is never fired into Lua

### 3. Respect TOC dependency order

When adding a new file pair:

- place templates before users of those templates
- place shared parent/layout files before children that anchor to them
- place the XML before later XML files that `inherits` or `relativeTo` its named frames

If the feature is just a change to an existing screen, prefer editing the existing XML and Lua file rather than adding a new top-level file.

### 4. Keep layout in XML and behavior in Lua

Existing GameUI code is consistent about this split:

- XML defines frame tree, anchors, visuals, fonts, and inline hookup shims.
- Lua owns event registration, dynamic content, show/hide decisions, and gameplay calls.

If you find yourself creating large numbers of children manually in Lua for a static layout, you are probably bypassing a template that should exist in XML.

### 5. Register game events in `OnLoad`

The common pattern is:

1. XML `OnLoad` calls `Feature_OnLoad(this)`.
2. Lua `Feature_OnLoad` registers runtime events with `self:RegisterEvent(...)`.
3. Event handlers update visuals or rebuild child content.

Do not guess event names. Search the client code for `TriggerLuaEvent(` and reuse the exact string already emitted by the engine.

### 6. Use the panel system for real windows

If the frame behaves like a normal in-game window, wire it into `UIPanelWindows` and use `ShowUIPanel` and `HideUIPanel`.

Do not call `Show()` directly for windows that should participate in left/center/full layout behavior. Existing code relies on the panel manager to prevent overlapping and to move panels between slots.

Use plain `Show()` and `Hide()` only for lightweight elements that are not managed panels, such as tooltips, overlays, error text, or generated sub-elements.

### 7. Use `id` and contiguous names when indexing repeated static children

The codebase frequently combines these:

- static XML names like `ActionButton1`, `ActionButton2`, ...
- numeric `id` values on each child
- Lua loops that access `_G["Name"..i]`

If you add more repeated static children, keep that pattern intact. It is simple and heavily used.

### 8. Use `userData` for per-frame transient Lua state

The base frame exposes a `userData` field to Lua. Existing code uses it for timers and temporary state.

Use it for local runtime state attached to a frame instance. Do not use it as a substitute for stable layout structure or global configuration.

## Change Recipes

### Recipe: add a new side panel

1. Start from an existing side-panel template or panel screen.
2. Define the frame structure in XML and anchor it under `GameParent`.
3. Add an entry to `UIPanelWindows` with the right `area` and `pushable` value.
4. Hook the close button through the shared side-panel behavior or your own `OnLoad`.
5. Open and close it through `ShowUIPanel` and `HideUIPanel`.
6. Register game events in Lua and refresh the content there.

### Recipe: add a dynamic list or menu

1. Define one item template in XML.
2. Define a stable container frame in XML.
3. In Lua, clear old children before rebuilding.
4. Clone the template for each item, then `AddChild` and anchor it.
5. Set text, icon, enabled state, and click handler from Lua.
6. Recalculate container or parent height if the list is variable-length.

`ContextMenu` and `GameMenu` are the best references for this pattern.

### Recipe: add a new data-driven HUD element

1. Add the frame structure in XML near the closest related HUD block.
2. Reuse an existing template or visual style where possible.
3. Add a Lua `OnLoad` that registers the exact engine events which affect the data.
4. Write a dedicated `Update` function and call it from every relevant event handler.
5. If the element contains a model, cooldown, scrollbar, or minimap behavior, start from an existing XML block using that type.

`PlayerFrame`, `TargetFrame`, `CastBar`, and `AuraBar` are good references.

### Recipe: add a new engine-backed capability

Only do this when XML and current Lua bindings are insufficient.

The usual search points are:

- `FrameManager::Initialize` for base UI bindings
- `ClientUiRuntime::Initialize` for client-only frame types and widget bindings
- `game_script` bindings for gameplay Lua APIs
- client systems and world/login states for `TriggerLuaEvent` emitters

If the required function, event, or frame type is missing there, add it in C++ first, then consume it from Lua/XML.

## Failure Modes To Avoid

These are the mistakes most likely to break a GameUI change:

- Adding a frame name that collides with an existing global.
- Referring to a template, parent, or anchor target that is not loaded yet.
- Using `Show()` for a managed window instead of `ShowUIPanel`.
- Guessing a WoW API function that does not exist in this engine.
- Guessing an event name instead of tracing `TriggerLuaEvent`.
- Adding a renderer state name that the renderer never uses.
- Building a dynamic list without clearing old children.
- Writing runtime behavior directly into large XML inline scripts instead of a real Lua function.
- Editing `GlueUI` patterns into `GameUI` or vice versa.

## Verification Checklist

Do not claim the UI change is reliable until these checks are satisfied:

1. The file load graph is valid.
   Templates load before inheritors, and named anchor targets exist before use.
2. The frame names are unique.
   Lua references, `_G` lookups, and XML names all agree.
3. The chosen renderer has the states it expects.
4. `OnLoad` runs and event registration happens.
5. The change reacts to the exact game events that should drive it.
6. Managed windows open and close through the panel system.
7. Dynamic content rebuilds cleanly without duplicate children or stale handlers.
8. Hover, click, drag, drop, tooltip, and focus behaviors still work for the affected controls.
9. If the change required new engine support, the Lua binding or event emission exists before the Lua code depends on it.

## Search Strategy For Future Agents

Before you invent anything, search these concepts in the codebase:

- `GameUI.toc` for load order.
- The feature name in existing `GameUI` XML and Lua files.
- `TriggerLuaEvent(` for event producers.
- `luabind::def(` for gameplay Lua functions.
- `RegisterFrameFactory` and `RegisterFrameRenderer` for supported widget types.
- `Clone()` and `AddChild(` for dynamic UI patterns.
- `UIPanelWindows` and `ShowUIPanel` for window-management behavior.

This is faster and safer than guessing based on external FrameXML knowledge.

## Related Docs

- [UI Documentation](ui_documentation.md) for the broad UI architecture.
- [UI Tutorials](ui_tutorials.md) for small worked examples.
- [Lua API](lua_api.md) for the gameplay-side Lua API surface.

Use this playbook first when the task is "change the in-game UI safely". Use the broader docs when you need background.
