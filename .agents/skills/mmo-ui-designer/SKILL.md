---
name: mmo-ui-designer
description: Inspect, design, implement, and validate this MMO project's native game UI built from XML layouts, Lua behavior, Frame UI C++ renderers and bindings, localized strings, and HTEX assets. Use when creating or modifying files under data/client/Interface, diagnosing frame state or anchoring problems, wiring mmo_client or frame_ui Lua APIs, adding UI textures, or polishing in-game layout and interaction UX.
---

# MMO UI Designer

Use live engine code and existing layouts as the source of truth. Treat XML, Lua, localization, renderer behavior, and texture assets as one UI feature rather than independent files.

## Workflow

1. Read the target XML and Lua completely, then find its entry in the relevant `.toc` file.
2. Inspect two or more nearby interfaces that use the same control or interaction pattern.
3. Trace uncertain XML behavior through `src/shared/frame_ui`, especially the renderer, layout loader, component, and Lua binding implementations.
4. Trace game-specific Lua functions and bound data types through `src/mmo_client/game_script.cpp` and the subsystem that owns the behavior.
5. Preview every candidate HTEX rather than inferring appearance from its filename.
6. Implement the smallest coherent XML/Lua/renderer change. Preserve established visual language unless the task explicitly requests a new direction.
7. Add every new localization key to all locale files.
8. Validate XML structure, texture paths, Lua API names, localization coverage, and the relevant C++ target.

Read [xml-and-renderers.md](references/xml-and-renderers.md) for frame inheritance, anchors, imagery, renderer states, and common traps. Read [lua-and-localization.md](references/lua-and-localization.md) for event wiring and API discovery. Read [htex-workflow.md](references/htex-workflow.md) whenever textures are involved.

## UX Review

- Make selection, hover, pressed, disabled, empty, and loading states visually distinct.
- Hide unavailable repeated content instead of leaving disabled placeholder controls unless empty slots convey gameplay meaning.
- Keep labels within explicit bounded areas; verify long localized strings, especially German and French.
- Prefer one clear navigation cue over duplicate headings.
- Keep navigation geometry stable across states; hide pagination only when its surrounding chrome also collapses cleanly.
- Preserve readable hierarchy at the engine's native resolution and actual UI scale.
- Verify interaction handlers after filtering or reordering data; use stable IDs for actions, not visual indices.

## Validation

Run from the repository root:

```powershell
[xml](Get-Content -Raw data/client/Interface/GameUI/Target.xml) | Out-Null
rg 'NEW_LOCALIZATION_KEY' data/client/Locales --glob Localization.txt
cmake --build build --config Release -t mmo_client
```

The XML schema is useful documentation but is not fully authoritative; the live loader accepts features the schema may not list. A successful parse does not prove anchors, inherited names, asset paths, or Lua handlers are valid. Inspect client logs and test the interface in game whenever possible.
