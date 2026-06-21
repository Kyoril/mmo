# Lua And Localization

## API Discovery

Do not invent Lua methods. Search bindings first:

```powershell
rg -n 'luabind::def|luabind::def_lambda|class_<.*\("' `
  src/shared/frame_ui/frame_mgr.cpp src/mmo_client/game_script.cpp
```

Frame UI bindings include visibility, enabled state, anchors, sizing, properties, children, opacity, events, animation, and button checked state. Game bindings expose player data, spells, inventory, actions, sound, and subsystem-specific functions. Read the bound C++ function when argument indexing, lifetime, or side effects matter.

## Wiring

- XML `<Script file="..." />` loads Lua relative to the XML file.
- XML script blocks return a callback function. Existing layouts are the best syntax examples.
- Runtime handler setters such as `SetClickedHandler`, `SetOnEnterHandler`, and `SetOnDragHandler` are useful for repeated controls initialized in `OnLoad`.
- Register game events on a frame whose lifetime covers the interface. Refresh visible state from the event handler.
- `_G["FrameName" .. i]` accesses globally named repeated frames.

## Localization

- Put strings in `data/client/Locales/Locale_*/Localization.txt`.
- Add each new key to every locale, using a clear placeholder where translation is unavailable.
- XML `Text` properties localize automatically.
- Lua must call `Localize("KEY")`.
- Use localized format keys with `string.format` for ranks, pages, counts, and names.
- Search for an existing key before adding a near-duplicate.

Check all locales with:

```powershell
rg 'KEY_NAME' data/client/Locales --glob Localization.txt
```
