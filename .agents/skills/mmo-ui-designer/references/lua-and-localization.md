# Lua And Localization

## API Discovery

Do not invent Lua methods or guess argument order. The bindings are the contract:

- Frame UI classes, methods, enums, globals: `src/shared/frame_ui/frame_mgr.cpp` (the `luabind::class_<...>` / `luabind::def(...)` block).
- Game data and gameplay functions: `src/mmo_client/game_script.cpp` (~130 bindings).

```powershell
rg -n 'luabind::def|def_lambda|class_<' src/shared/frame_ui/frame_mgr.cpp
rg -n 'luabind::def|def_lambda|class_<' src/mmo_client/game_script.cpp
```

Read the bound C++ function whenever argument indexing, lifetime, return values, or side effects matter — the Lua name rarely tells the whole story.

## Frame UI Bound Surface (frame_mgr.cpp)

Verify against source before relying on a method; this is the high-traffic subset.

**Free functions / globals**

- `Localize("KEY")` — returns the localized string for a key (prints the key if missing).
- `getglobal("FrameName")` — fetches a globally named frame (same as `_G["FrameName"]`).
- `gameData` — the loaded proto project (items, spells, …). `loginConnector`, `realmConnector` — network connectors.
- Enum tables: `AnchorPoint.{NONE,TOP,RIGHT,BOTTOM,LEFT,H_CENTER,V_CENTER}`, `ButtonState.{NORMAL,HOVERED,PUSHED}`.

**`Frame`** — `Show`/`Hide`/`SetEnabled`/`Enable`/`Disable`, `IsVisible`, `IsHovered`, `SetText`/`GetText`/`GetTextWidth`/`GetTextHeight`, `SetProperty`/`GetProperty`, `RegisterEvent`, `SetAnchor`/`ClearAnchor`/`ClearAnchors`, `SetSize`/`SetWidth`/`SetHeight`/`GetWidth`/`GetHeight`/`GetX`/`GetY`, `GetParent`/`AddChild`/`GetChild`/`GetChildCount`/`RemoveAllChildren`/`Clone`, `SetOpacity`/`GetOpacity`, `SetFrameLevel`/`GetFrameLevel`/`BringToFront`/`SendToBack`, `CaptureInput`/`HasInputCaptured`/`ReleaseInput`, `PlayAnimation`/`StopAnimation`/`HasAnimation`/`IsAnimationPlaying`/`GetAnimation`, and handler setters `SetOnEnterHandler`, `SetOnLeaveHandler`, `SetOnDragHandler`, `SetOnDropHandler`, `SetOnMouseDownHandler`, `SetOnMouseUpHandler`, `SetOnMouseMoveHandler`, `SetOnMouseWheelHandler`. Compare two frames with `==` (bound `__eq`), not by name string.

**`Button : Frame`** — `IsChecked`/`SetChecked`, `IsCheckable`/`SetCheckable`, `SetClickedHandler`, `SetButtonState`/`GetButtonState`.

**`TextField : Frame`** — `SetTextMasked`, `SetAcceptsTab`/`AcceptsTab`, `SetTextAreaOffset`/`GetTextAreaOffset`.

**`ProgressBar : Frame`** — `SetProgress`/`GetProgress` (0..1).

**`ScrollBar : Frame`** — `SetMinimum`/`SetMaximum`/`SetValue`/`GetValue`/`GetMinimum`/`GetMaximum`/`GetStep`/`SetStep`/`SetOnValueChangedHandler`.

**`ScrollingMessageFrame : Frame`** — `AddMessage`, `Clear`, `ScrollUp`/`ScrollDown`/`ScrollToTop`/`ScrollToBottom`, `IsAtTop`/`IsAtBottom`.

**`ComboBox : Frame`** — `AddItem`/`ClearItems`/`GetItemCount`/`GetItemText`/`GetItemUserData`, `GetSelectedIndex`/`SetSelectedIndex`/`GetSelectedText`/`GetSelectedUserData`, `Open`/`Close`/`Toggle`/`IsOpen`, `SetPopupFrame`/`SetMaxDropHeight`/`GetMaxDropHeight`/`SetItemColor`, `SetOnSelectionChanged`/`SetOnClickedHandler`/`SetOnDismissHandler`.

**`FrameAnimation`** — `GetName`/`GetDuration`/`IsLooping`/`IsPlaying`/`GetCurrentTime`, `SetOnStart`/`SetOnFinish`/`SetOnStop`/`SetOnLoop`.

## Wiring

- `<Script file="Name.lua" />` loads Lua relative to the XML file's directory.
- Inline `<Scripts>` handlers each `return` a function (see the renderers reference for the handler element list). Existing layouts are the canonical syntax examples.
- Use the runtime `Set*Handler` setters for repeated controls built in `OnLoad`; reach globally-named repeats with `getglobal("Name"..i)` or `_G["Name"..i]`.
- Register game events on a frame whose lifetime spans the interface, then refresh visible state from the handler. Don't poll in `OnUpdate` what an event already pushes.
- Cache child lookups in `OnLoad` instead of calling `getglobal` / `GetChild` every frame.

## Localization

- Strings live in `data/client/Locales/Locale_*/Localization.txt`.
- Add every new key to **all** locales — use a clear placeholder where a real translation isn't available. A missing key renders as the raw key in-game.
- XML `Text` (and any text-valued property) localizes automatically — put a KEY there, never a literal sentence.
- In Lua, wrap user-facing text in `Localize("KEY")`.
- For ranks, pages, counts, names, etc., store a format key and feed it through `string.format(Localize("KEY"), ...)` rather than concatenating sentence fragments (word order differs per language).
- Search for an existing key before adding a near-duplicate.

```powershell
rg 'KEY_NAME' data/client/Locales --glob Localization.txt
```
