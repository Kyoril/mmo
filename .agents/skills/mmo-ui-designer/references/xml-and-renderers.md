# XML And Renderers

## Source Of Truth

- Loader (authoritative vocabulary): `src/shared/frame_ui/layout_xml_loader.cpp`
- Frame, properties, anchors: `src/shared/frame_ui/frame.{h,cpp}`
- Factories, renderers, Lua bindings, enums: `src/shared/frame_ui/frame_mgr.cpp`
- Renderers: `src/shared/frame_ui/{default,button,checkbox,combo_box,textfield}_renderer.cpp`
- Components: `border_component.cpp`, `image_component.cpp`, `text_component.cpp`
- Schema (documentation only, not authoritative): `data/client/Interface/Ui.xsd`
- Load order: `data/client/Interface/GameUI/GameUI.toc`, `GlueUI/GlueUI.toc`

The loader, not the XSD, decides what is legal. The XSD lags behind. A clean parse proves only well-formedness — it does not prove that names resolve, anchors give non-zero size, textures exist, or Lua handlers are bound. Confirm against the loader and in-game.

## Frame Types (factories)

Registered in `frame_mgr.cpp` (`RegisterFrameFactory`). The `type` attribute must be one of:

`Frame`, `Button`, `Thumb`, `ScrollBar`, `TextField`, `ProgressBar`, `ScrollingMessageFrame`, `ComboBox`, `LineFrame`.

`type` defaults to `Frame`. When `inherits` is set, the type is taken from the template and any `type` attribute is ignored.

## `<Frame>` Attributes

| Attribute | Meaning |
|---|---|
| `name` | Global identifier. Frame names are global across the entire UI — a duplicate name silently fails to create the frame and its whole subtree is skipped. |
| `type` | Frame type (see above). |
| `parent` | Global name of the parent frame. If omitted, the enclosing `<Frame>` on the stack becomes the parent. Top-level frames usually set `parent="GameParent"` (or `GlueParent` in GlueUI). |
| `inherits` | Template frame to clone before applying this frame's overrides. Template must already be loaded (earlier in load order). |
| `renderer` | Renderer name (see below). Overrides the type's default. |
| `setAllPoints` | `true` anchors all four edges to the parent — fills the parent exactly. |
| `id` | Integer id, readable from Lua/handlers; handy for repeated frames. |
| `frameLevel` | Integer draw/stacking order within the parent. |

## Element Vocabulary

Direct children of `<Frame>`, in this order:

1. `<Property name= value= />` — must come **before** `<Area>`, `<Visual>`, and `<Scripts>`. A `<Property>` placed after them is rejected by the loader.
2. `<Area>` — layout. Contains `<Anchor>`, `<Size><AbsDimension x= y=/></Size>`, `<Position>`, or `<Inset>`.
3. `<Visual>` — `<ImagerySection>` and `<StateImagery>` (see Imagery Contract).
4. `<Scripts>` / `<Events>` — inline handlers.
5. `<Animations>` — `<Animation>` / `<Track>` / `<Keyframe>`.
6. Nested `<Frame>` children.

### Anchors and sizing

`<Anchor point= relativeTo= relativePoint= offset= />`

- `point` / `relativePoint`: `TOP`, `RIGHT`, `BOTTOM`, `LEFT`, `H_CENTER`, `V_CENTER`.
- `relativeTo`: global name of the frame to anchor against; resolved at load time, so the target must appear earlier in load order. Omit to anchor to the parent.
- Two opposing anchors (e.g. `LEFT` + `RIGHT`) determine a dimension. A single anchor plus `<Size>` supplies the rest. Offsets are in native UI coordinates (see [frame-ui-coordinate-spaces](../../../../memory/frame-ui-coordinate-spaces.md) — UI space, not screen pixels).
- Two anchors on the same axis that produce zero or negative extent clip the frame to nothing. This is the most common "my frame disappeared" cause.

### Built-in Frame properties

Registered in `frame.cpp` and settable via `<Property>` or `SetProperty`: `Text`, `Visible`, `Enabled`, `Focusable`, `Clickable`, `DragEnabled`, `DropEnabled`, `ClippedByParent`, `Font`, `Color`. Type-specific properties exist too (e.g. `Progress` on `ProgressBar`). Booleans accept `true`/`false`/`True`/`False`. Properties are free-form strings, so a typo'd property name is silently stored and never read — copy names from a working layout.

## Imagery Contract

`<ImagerySection name="...">` is a reusable bag of components. `<StateImagery name="...">` groups `<Layer>` → `<Section section="..."/>` references and is selected by the renderer. The renderer alone decides which `StateImagery` name is drawn; declaring a state does nothing unless the active renderer asks for it by that exact name.

### Components

- `<TextComponent color= horzAlign= vertAlign= />` — draws the frame's `Text`. `horzAlign`: `LEFT`/`CENTER`/`RIGHT`; `vertAlign`: `TOP`/`CENTER`/`BOTTOM`. `color` is 8-digit hex **AARRGGBB** (e.g. `FFFFFFFF`).
- `<ImageComponent texture= tiling= tint= />` — single image. `tiling`: `NONE`/`HORZ`/`VERT`/`BOTH`. `tint` is AARRGGBB. Wrap in `<Area><Inset .../></Area>` to inset it.
- `<BorderComponent texture= borderSize= tint= />` — nine-slice. `borderSize` is the immutable corner thickness in source pixels; alternatively give per-side `topSize`/`leftSize`/`rightSize`/`bottomSize`. The corner size must match the texture's actual non-stretching border, or corners smear.

### Data-bound imagery (`$Property`)

Several component attributes accept a `$PropertyName` value that live-binds the visual to a frame property instead of a constant. This is the clean way to make imagery react to state without swapping textures:

- `TextComponent`: `color`, `horzAlign`, `vertAlign` — e.g. `color="$BarColor"`.
- `ImageComponent`: `tint` — e.g. `tint="$FillColor"`.
- `BorderComponent` tint is **literal only** (no `$` binding).

Set the bound property from Lua (`SetProperty("BarColor", "FFFF0000")`) and the component repaints. Prefer this over hiding/showing duplicate sections when only a color needs to change.

`<Inset all= />` or `<Inset left= top= right= bottom= />` positions a component inside its section. Component `<Area>` supports insets only — not frame anchors. When artwork needs independent positioning, use a nested `<Frame>`.

## Renderers And Their State Names

A renderer maps live frame state to a `StateImagery` name and falls back to a default if that name is missing. Use the exact names below.

| Renderer | States it requests | Fallback if missing |
|---|---|---|
| `DefaultRenderer` | `Enabled`, `Disabled` | `Enabled` |
| `ButtonRenderer` | `Normal`, `Hovered`, `Pushed`, `Disabled` | `Normal` |
| `CheckboxRenderer` | the four Button states, plus a `Checked` suffix when checked: `NormalChecked`, `HoveredChecked`, `PushedChecked`, `DisabledChecked` | `Normal` / `NormalChecked` |
| `TextFieldRenderer` | see `textfield_renderer.cpp` | — |
| `ComboBoxRenderer` | see `combo_box_renderer.cpp` | — |

`ButtonRenderer` never draws a checked state — it cannot show selection. For tabs and any mutually-exclusive selected look you **must** use `CheckboxRenderer`. `ProgressBar`, `ScrollBar`, etc. have their own renderers that draw additional named states (e.g. a `Progress`/`Overlay` layer) on top — read the type's renderer to see which `StateImagery` names it consumes before adding states.

### Selected tabs / toggles (the reliable recipe)

1. `renderer="CheckboxRenderer"`, `<Property name="Checkable" value="true" />`.
2. Define both families: `Normal`/`Hovered`/`Pushed`/`Disabled` and their `*Checked` counterparts.
3. On every refresh, call `SetChecked(i == selectedIndex)` for all tabs — not just the clicked one.
4. Refresh even when the already-active tab is clicked: the base `Button` toggles its own checked state before firing `OnClicked`, so a refresh is required to keep visual and logical state in sync.

## Inline Scripts

`<Scripts>` accepts these handler elements; each returns a function:

`OnLoad`, `OnUpdate`, `OnShow`, `OnHide`, `OnEnter`, `OnLeave`, `OnClick`, `OnMouseDown`, `OnDrag`, `OnDrop`, `OnTabPressed`, `OnEnterPressed`, `OnSpacePressed`, `OnEscapePressed`.

```xml
<Scripts>
    <OnLoad>return function(this) MyFrame_OnLoad(this) end</OnLoad>
    <OnUpdate>return function(this, elapsed) MyFrame_OnUpdate(this, elapsed) end</OnUpdate>
</Scripts>
```

Keep `OnUpdate` cheap — it runs every frame. For repeated controls created/initialized in `OnLoad`, prefer the runtime setters (`SetClickedHandler`, `SetOnEnterHandler`, …) over per-instance XML.

## Animations

Frames can declare keyframed animations directly in XML. `<Animations>` is a direct child of `<Frame>` (a sibling of `<Area>`/`<Visual>`/`<Scripts>`, not nested inside them). Implementation: `frame_animation.{h,cpp}`; loader: `ElementAnimation*` in `layout_xml_loader.cpp`. Real example: `data/client/Interface/GlueUI/AccountLogin.xml` (+ `.lua`).

```xml
<Animations>
    <Animation name="FadeIn" duration="0.5" loop="false">
        <Track property="Opacity" target="AccountLoginContent">
            <Keyframe time="0"   value="0" />
            <Keyframe time="0.5" value="1" />
        </Track>
    </Animation>
</Animations>
```

- **`<Animation>`**: `name` (required, used from Lua), `duration` in **seconds** (default `1.0`), `loop` (default `false`). A looping animation wraps its time and fires its loop callback each cycle; a non-looping one clamps to the final values, stops, and fires its finish callback.
- **`<Track property= target= >`**: one numeric property tweened over the animation. `property` must be exactly one of: `Opacity`, `PositionX`, `PositionY`, `Width`, `Height`, `ColorR`, `ColorG`, `ColorB`, `ColorA`. An unknown name silently falls back to `Opacity` (with a warning in the log). `target` is **optional**: omit it to animate the owning frame itself, or set it to a **child frame's name** to animate that child (resolved via `FindChild`; if the name doesn't resolve, the track silently does nothing). An animation may hold multiple tracks.
- **`<Keyframe time= value= />`**: `time` in seconds (relative to the animation start), `value` a float. Keyframes are auto-sorted by time; values interpolate **linearly** between them and are **held flat** before the first and after the last keyframe. A single keyframe yields a constant value.

Value ranges by property: `Opacity` and the `Color*` channels are `0..1`; `PositionX/Y`, `Width`, `Height` are native UI coordinates. `Color*` tracks drive the frame's `Color` property, so they only show if the imagery actually consumes that color (e.g. a `TextComponent`/`ImageComponent` bound with `$Color`). Animating `PositionX/Y` sets an absolute position and will fight existing anchors — prefer it on frames positioned by a single anchor or none.

**Animations do not auto-play.** Trigger them from Lua on a frame whose lifetime covers the effect:

```lua
AccountLogin:PlayAnimation("FadeIn")          -- start by name
-- chain follow-up work off completion:
local anim = AccountLogin:GetAnimation("FadeOut")
if anim and not anim:IsPlaying() then
    anim:SetOnFinish(function(a)
        a:SetOnFinish(nil)                    -- clear one-shot callback
        AccountLogin:Hide()
        RealmList_Show()
    end)
    AccountLogin:PlayAnimation("FadeOut")
end
```

Relevant Lua: `Frame` — `PlayAnimation`, `StopAnimation`, `HasAnimation`, `IsAnimationPlaying`, `GetAnimation`; `FrameAnimation` — `IsPlaying`, `GetCurrentTime`, `GetDuration`, `IsLooping`, `SetOnStart`/`SetOnFinish`/`SetOnStop`/`SetOnLoop` (each takes a function receiving the animation; pass `nil` to clear). Guard with `IsPlaying()` before replaying, and gate follow-up logic (hide/swap frames) on the finish callback rather than a fixed timer. When a frame with animations is cloned (templated/repeated frames), track `target` child names are remapped to the clone's children automatically.

## Common Failures

- A selected/checked state is declared under a `ButtonRenderer`, which cannot render it — use `CheckboxRenderer`.
- Two anchors leave zero/negative width or height, so the frame is invisible.
- A duplicate global `name` silently drops the frame and its whole subtree (watch the client log for the create error).
- `relativeTo` targets a frame defined later in load order, so it resolves to nothing.
- A localized label overflows because layout was checked only in English. Verify German and French against the bounded area.
- Stacked translucent borders let a lower background bleed through. Prefer one self-contained border per boundary; if an outer ring is intentional, draw it in a slightly larger backing frame behind an opaque content frame.
- Empty repeated controls are left disabled instead of hidden, leaving dead chrome.
- A visual list index is used for a gameplay action after filtering/reordering — key actions off a stable entry id, not the row index.
- A mistyped `<Property>` name or component attribute is accepted silently and simply does nothing; diff against a known-good layout when imagery "doesn't apply."
