# MMO UI System Documentation

This document describes the MMO UI system as implemented in `data/client/Interface` and the `frame_ui` library (`src/shared/frame_ui`). It is written for developers and AI agents who want to build or extend UI components safely and consistently.

## Table of Contents

- [System Overview](#system-overview)
- [Runtime Entry Points](#runtime-entry-points)
- [UI File Layout](#ui-file-layout)
- [XML Layout Grammar](#xml-layout-grammar)
- [Visual System: Imagery Sections and State Imagery](#visual-system-imagery-sections-and-state-imagery)
- [Frame Types and Renderers](#frame-types-and-renderers)
- [Built-in Properties and Bindings](#built-in-properties-and-bindings)
- [Lua Event Flow and Handlers](#lua-event-flow-and-handlers)
- [Anchors, Positioning, and Scaling](#anchors-positioning-and-scaling)
- [Localization and Text Markup](#localization-and-text-markup)
- [Panel System and Layout Conventions](#panel-system-and-layout-conventions)
- [AI Agent Extension Checklist](#ai-agent-extension-checklist)
- [References](#references)

## System Overview

The UI system is a hybrid of:
- **XML layout files** (`.xml`) that define frame hierarchies, visuals, fonts, and scripts.
- **Lua scripts** (`.lua`) that implement behavior, game bindings, and event handlers.
- **FrameUI C++ runtime** (`src/shared/frame_ui`) that loads XML, binds Lua, renders, and routes input.

Key architectural features:
- **Frame hierarchy**: Every UI element is a `Frame` (or subclass) with children.
- **Visual separation**: Rendering uses named imagery sections and state imagery.
- **Property binding**: Visual components can bind to frame properties.
- **Script hooks**: XML can embed Lua via `<Scripts>` handlers or reference files.

## Runtime Entry Points

UI is loaded by the game states:
- Login flow (GlueUI): `src/mmo_client/game_states/login_state.cpp` loads `Interface/GlueUI/GlueUI.toc`.
- In-game flow (GameUI): `src/mmo_client/game_states/world_state.cpp` loads `Interface/GameUI/GameUI.toc`.

The runtime is initialized in `src/mmo_client/client.cpp`:
- `FrameManager::Initialize` sets up Lua bindings and default renderers.
- Custom renderers/frame types are registered for **Model**, **UnitModel**, **Minimap**, **World**.

## UI File Layout

Primary UI roots:
- `data/client/Interface/GlueUI`: login/character creation UIs.
- `data/client/Interface/GameUI`: in-game UI (action bars, chat, HUD).

Key file types:
- `.toc`: load order lists of XML/Lua files (one per line).
- `.xml`: UI layout definitions.
- `.lua`: scripts, event handlers, helpers.
- `.htex/.hmat`: textures and materials for UI visuals.
- `Bindings.xml`: key bindings loaded in game state.

Load order in `.toc` matters: templates and shared definitions must appear before layouts that `inherits` them.

## XML Layout Grammar

Layout files follow the `UiLayout` root and are parsed by `LayoutXmlLoader` in `src/shared/frame_ui/layout_xml_loader.cpp`. Updated schema files are in `data/client/Interface/Ui.xsd` and `data/client/Interface/GlueUI/Ui.xsd`. Some GlueUI XML files use inconsistent namespaces; for strict validation, align the `xmlns` to `http://mmo-dev.net/ui`.

High-level structure:
```xml
<UiLayout>
  <Script file="SomeFile.lua" />
  <Font name="DefaultSmall" file="Fonts/FRIZQT__.TTF" size="26" outline="0" />

  <Frame name="MyFrame" type="Frame" renderer="DefaultRenderer">
    <Property name="Text" value="HELLO" />
    <Area>
      <Anchor point="LEFT" offset="32" />
      <Anchor point="TOP" offset="32" />
      <Size><AbsDimension x="400" y="100" /></Size>
    </Area>
    <Visual>...</Visual>
    <Scripts>...</Scripts>
  </Frame>
</UiLayout>
```

### Core Elements

- `UiLayout`: file root.
- `Script file="...lua"`: Lua file to load after XML parsing.
- `Font`: registers a font name for later use by frames.
- `Frame`: a UI element (may contain nested frames).
- `Property`: name/value properties used by logic or visuals.
- `Area`: size/position/anchor definitions.
- `Visual`: imagery sections and state imagery.
- `Scripts`: inline Lua handlers.

### Frame Attributes

Supported attributes (from `LayoutXmlLoader`):
- `name` (required)
- `type` (optional): `Frame`, `Button`, `TextField`, `ScrollBar`, `Thumb`, `ProgressBar`, `ScrollingMessageFrame`, `Model`, `UnitModel`, `Minimap`, `World`, etc.
- `renderer` (optional): `DefaultRenderer`, `ButtonRenderer`, `CheckboxRenderer`, `TextFieldRenderer`, `ModelRenderer`, `WorldRenderer`.
- `parent` (optional): explicit parent by name.
- `inherits` (optional): template frame name.
- `setAllPoints` (optional): `true/false`, anchors to all parent edges.
- `id` (optional): integer ID passed into Lua (used by ActionBar, etc).

### Area and Anchors

`Area` may contain:
- `Size` with `AbsDimension x/y`
- `Position` with `AbsDimension x/y` (fallback if no anchors)
- `Anchor` with `point`, optional `relativePoint`, `relativeTo`, and `offset`

Anchor offset is a **single float**; horizontal anchors use X scale, vertical anchors use Y scale.

### Inline Scripts

`Scripts` can include:
- `OnLoad`
- `OnUpdate`
- `OnClick`
- `OnShow` / `OnHide`
- `OnEnter` / `OnLeave`
- `OnDrag` / `OnDrop`
- `OnTabPressed`, `OnEnterPressed`, `OnSpacePressed`, `OnEscapePressed`

Each script block should **return a function**:
```xml
<OnClick>
  return function(this, button)
    DoSomething(this, button)
  end
</OnClick>
```

## Visual System: Imagery Sections and State Imagery

Visuals are defined in two layers:
1. **ImagerySection**: a named collection of render components (image, border, text).
2. **StateImagery**: a named state that combines imagery sections in ordered layers.

Example (from `data/client/Interface/GameUI/ActionBarButton.xml`):
```xml
<ImagerySection name="Image">
  <ImageComponent>
    <Area><Inset all="16" /></Area>
    <PropertyValue property="Icon" />
  </ImageComponent>
</ImagerySection>

<StateImagery name="Normal">
  <Layer>
    <Section section="NormalFrame" />
    <Section section="Image" />
  </Layer>
</StateImagery>
```

### Components

- `ImageComponent` (texture/tint/tiling)
- `BorderComponent` (nine-slice border)
- `TextComponent` (color/align)

Components can bind properties:
- Prefix attributes with `$` to bind to a property (e.g., `color="$TextColor"`).
- Use `<PropertyValue property="Icon" />` inside `ImageComponent` to bind image source.

## Frame Types and Renderers

Registered frame types:
- Core: `Frame`, `Button`, `TextField`, `ScrollBar`, `Thumb`, `ProgressBar`, `ScrollingMessageFrame`
- Client UI: `Model`, `UnitModel`, `Minimap`, `World`

Registered renderers and state names:
- `DefaultRenderer`: uses `Enabled` / `Disabled`
- `ButtonRenderer`: `Normal` / `Hovered` / `Pushed` / `Disabled`
- `CheckboxRenderer`: same as Button + `Checked` suffix (e.g., `NormalChecked`)
- `TextFieldRenderer`: `Enabled` / `Disabled` and optional `Caret`
- `ProgressBar` (custom): `Enabled` / `Disabled` + `Progress` and optional `Overlay` / `OverlayDisabled`

Renderer logic lives in `src/shared/frame_ui/*_renderer.cpp`.

## Built-in Properties and Bindings

Base `Frame` properties (available in XML and Lua):
- `Text`, `Font`, `Visible`, `Enabled`, `ClippedByParent`
- `Color` (ARGB hex string)
- `Focusable`, `Clickable`
- `DragEnabled`, `DropEnabled`

Button properties:
- `Checkable`, `Checked`

TextField properties:
- `Masked`, `AcceptsTab`
- `EnabledTextColor`, `DisabledTextColor`

ProgressBar properties:
- `Progress` (0.0-1.0), `ProgressColor`

Property rules:
- `Property name="Text"` is localized during XML load.
- Visual components may bind to properties via `$PropertyName` or `PropertyValue`.

## Lua Event Flow and Handlers

UI events are delivered via:
- `Frame:RegisterEvent("EVENT_NAME", fn)` from Lua.
- `FrameManager::TriggerLuaEvent` from C++ (see `login_state.cpp`, `world_state.cpp`).

Examples of engine-triggered events:
- `PLAYER_ENTER_WORLD`
- `PLAYER_HEALTH_CHANGED`
- `PLAYER_POWER_CHANGED`
- `PLAYER_TARGET_CHANGED`
- `ACTION_BAR_CHANGED`
- `SPELL_LEARNED`
- `ITEM_RECEIVED`
- `CHAT_MSG_SAY`
- `AUTH_SUCCESS`, `AUTH_FAILED`, `REALM_LIST`, `CHAR_LIST`

Handler signatures:
- `OnClick(this, buttonName)`
- `OnUpdate(this, elapsedSeconds)`
- `OnDrag(this, buttonName, position)` and `OnDrop(...)`
- `OnEnter/OnLeave(this)`
- `OnEnterPressed/OnTabPressed/OnSpacePressed/OnEscapePressed(this)`

## Anchors, Positioning, and Scaling

Anchor rules (from `anchor_point.cpp`):
- Only one `offset` value is supported; it applies to X for horizontal anchors and Y for vertical anchors.
- Opposite anchors (left/right, top/bottom) define size.
- `setAllPoints="true"` anchors all sides to parent.

The system scales UI based on `FrameManager::SetNativeResolution` and current window size.

## Localization and Text Markup

Localization:
- `Localize("STRING_ID")` is bound to Lua.
- `Property name="Text"` is auto-localized during XML parsing.

Inline text markup:
- Color tags: `|cAARRGGBB` and reset `|r`
- Hyperlinks: `|caarrggbb|Htype:payload|h[displaytext]|h|r`

See `docs/hyperlink_usage_example.md` for examples.

## Panel System and Layout Conventions

The panel system (left/center/full) is implemented in `data/client/Interface/GameUI/GameParent.lua`. Use `ShowUIPanel`, `HideUIPanel`, and `CloseAllWindows` to manage panels.

Common conventions:
- Use templates from `GameTemplates.xml` and `GlueTemplates.xml`.
- Prefer `inherits` for repeated elements (buttons, labels).
- Register events in `OnLoad`.
- Keep UI logic in Lua; keep layout in XML.

## AI Agent Extension Checklist

When adding a new UI component:
1. Decide whether it belongs to GlueUI or GameUI and update the appropriate `.toc`.
2. Create a new `.xml` layout and `.lua` script (or extend an existing one).
3. Use `inherits` from templates (`GameTemplates.xml`, `GlueTemplates.xml`).
4. Add `Scripts` handlers that return functions.
5. Register events in `OnLoad` and handle updates in Lua.
6. Bind visuals to properties with `$PropertyName` and `<PropertyValue>`.
7. Confirm frame names are unique and consistent with Lua global usage.
8. If needed, add/extend Lua API in `docs/lua_api.md`.

## References

- UI entry points: `src/mmo_client/game_states/login_state.cpp`, `src/mmo_client/game_states/world_state.cpp`
- Layout loader: `src/shared/frame_ui/layout_xml_loader.cpp`
- Renderers: `src/shared/frame_ui/*_renderer.cpp`
- Templates: `data/client/Interface/GameUI/GameTemplates.xml`, `data/client/Interface/GlueUI/GlueTemplates.xml`
- Action bar example: `data/client/Interface/GameUI/ActionBar.xml`, `data/client/Interface/GameUI/ActionBar.lua`
- Tutorials: `docs/ui_tutorials.md`
