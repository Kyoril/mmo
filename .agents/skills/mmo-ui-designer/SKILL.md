---
name: mmo-ui-designer
description: Inspect, design, implement, and validate this MMO project's native game UI built from XML layouts, Lua behavior, Frame UI C++ renderers and bindings, localized strings, and HTEX assets. Use when creating or modifying files under data/client/Interface, diagnosing frame state or anchoring problems, wiring mmo_client or frame_ui Lua APIs, adding UI textures, or polishing in-game layout and interaction UX.
---

# MMO UI Designer

This project's UI is a custom retained-mode system (`src/shared/frame_ui`), not HTML/CSS or an off-the-shelf engine. A UI feature is one unit spanning XML layout, Lua behavior, C++ renderer rules, localized strings, and HTEX art. Treat live engine code and existing layouts as the source of truth — copy proven patterns instead of inventing them.

## Workflow

1. Read the target XML and Lua completely, then find its load entry in the relevant `.toc`.
2. Inspect two or more nearby interfaces that already use the same control or interaction pattern, and mirror them.
3. For uncertain XML behavior, trace `src/shared/frame_ui` — the loader (`layout_xml_loader.cpp`) is authoritative over the XSD; then the relevant renderer, component, and `frame.cpp`.
4. For Lua, trace bindings in `frame_mgr.cpp` (Frame UI) and `src/mmo_client/game_script.cpp` (game data). Never invent a method or guess argument order — read the binding.
5. Preview every candidate HTEX with the tool; never judge a texture by its filename.
6. Make the smallest coherent XML/Lua/renderer change. Preserve the established visual language unless a new direction is explicitly requested.
7. Add every new localization key to all locale files.
8. Validate: well-formed XML, real texture paths, bound Lua names, localization coverage in every locale, and a successful `mmo_client` build. Then check the client log and, when possible, test in game.

Read [xml-and-renderers.md](references/xml-and-renderers.md) for the element/attribute vocabulary, renderer state names, data-bound imagery, anchors, and traps. Read [lua-and-localization.md](references/lua-and-localization.md) for the bound API surface and event wiring. Read [visual-language.md](references/visual-language.md) before designing or restyling anything visible — it has the project's canonical border/color vocabulary, copy-paste panel/list/selectable-row recipes, and do/don'ts harvested from shipping layouts. Read [htex-workflow.md](references/htex-workflow.md) whenever textures are involved.

## Cheat Sheet (verify against source before relying on it)

- **Frame types** (`type=`): `Frame`, `Button`, `Thumb`, `ScrollBar`, `TextField`, `ProgressBar`, `ScrollingMessageFrame`, `ComboBox`, `LineFrame`. Defaults to `Frame`; `inherits` takes the template's type.
- **Renderers** (`renderer=`) and the `StateImagery` names each one draws:
  - `DefaultRenderer` → `Enabled`, `Disabled`
  - `ButtonRenderer` → `Normal`, `Hovered`, `Pushed`, `Disabled` (cannot show checked/selected)
  - `CheckboxRenderer` → those four plus `NormalChecked`/`HoveredChecked`/`PushedChecked`/`DisabledChecked` — use for tabs, toggles, selected rows.
- **Frame attributes**: `name` (global — duplicates silently drop the frame), `type`, `parent`, `inherits`, `renderer`, `setAllPoints`, `id`, `frameLevel`.
- **Anchor points**: `TOP RIGHT BOTTOM LEFT H_CENTER V_CENTER`. Two opposing anchors set a dimension; zero/negative extent makes the frame vanish.
- **Components**: `TextComponent`, `ImageComponent` (`tiling` = `NONE/HORZ/VERT/BOTH`), `BorderComponent` (nine-slice, `borderSize` = source corner px). Colors are 8-digit hex **AARRGGBB**.
- **Data-bound imagery**: `TextComponent` `color`/`horzAlign`/`vertAlign` and `ImageComponent` `tint` accept `$PropertyName` to live-bind to a frame property — prefer this over swapping sections just to recolor. (`BorderComponent` tint is literal only.)
- **`<Property>` ordering**: must precede `<Area>`, `<Visual>`, `<Scripts>`. Unknown property/attribute names are accepted silently and do nothing — diff against a working layout when imagery won't apply.
- **Animations**: declare `<Animations>`→`<Animation name= duration= loop=>`→`<Track property= target=>`→`<Keyframe time= value=>` in XML (animatable: `Opacity`, `PositionX/Y`, `Width`, `Height`, `ColorR/G/B/A`). They do **not** auto-play — start with `frame:PlayAnimation("Name")` from Lua and chain follow-ups off `GetAnimation():SetOnFinish(...)`. Full syntax in the renderers reference.

A minimal, idiomatic frame skeleton is in [reference-skeleton.xml](references/reference-skeleton.xml).

## UX Review

- Make selection, hover, pressed, disabled, empty, and loading states visually distinct.
- Hide unavailable repeated content rather than leaving disabled placeholder chrome — unless an empty slot itself conveys gameplay meaning.
- Keep labels inside explicit bounded areas; verify long localized strings, especially German and French.
- Prefer one clear navigation cue over duplicate headings.
- Keep navigation geometry stable across states; collapse pagination only when its surrounding chrome collapses cleanly too.
- Preserve readable hierarchy at the engine's native resolution and actual UI scale.
- Re-verify interaction handlers after filtering or reordering data; key actions off stable entry ids, not visual row indices.

## Validation

Run from the repository root:

```powershell
[xml](Get-Content -Raw data/client/Interface/GameUI/Target.xml) | Out-Null   # well-formedness only
rg 'NEW_LOCALIZATION_KEY' data/client/Locales --glob Localization.txt        # must hit every locale
cmake --build build --config Release -t mmo_client
```

A clean parse proves only well-formedness — not that names resolve, anchors give non-zero size, textures exist, or handlers are bound. The XSD is helpful documentation but trails the loader, which accepts features the schema omits. Inspect the client log for create/anchor/template errors and test the interface in game whenever possible.
