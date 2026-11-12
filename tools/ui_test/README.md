# UI test app (`ui_test`)

Renders the frame UI (GlueUI, GameUI or any TOC) outside the game client, with the client's own
frame_ui code, fonts, textures and Lua — so what you see is what the client shows. It reloads the
whole UI from disk within ~100 ms (Release) whenever a `.xml`, `.lua`, `.toc` or `Localization.txt`
file changes, and writes a screenshot plus a machine-readable `status.json` after every load.
It is meant for fast UI iteration, by people and by AI agents. It does not connect to a server and
is not a Lua debugger.

```powershell
cmake --build build --config Release -t ui_test        # needs MMO_BUILD_CLIENT (Windows)
```

## Quick start

```powershell
# One screenshot, then exit (exit code 1 if anything logged an error)
powershell -File tools/ui_test/ui_test.ps1 shot -Scenario friends -Name friends.png

# Live: hot reload on every save; latest.png + status.json are rewritten after each reload
powershell -File tools/ui_test/ui_test.ps1 start -Scenario friends
powershell -File tools/ui_test/ui_test.ps1 cmd "lua FriendsFrame:IsVisible()" "hover 240 300" "screenshot hover.png"
powershell -File tools/ui_test/ui_test.ps1 status
powershell -File tools/ui_test/ui_test.ps1 stop
```

Output goes to `artifacts/ui_test/`: `latest.png`, `status.json`, `ui_test.log`, named screenshots.
In the window, F5 reloads and F12 writes `screenshot.png`; mouse and keyboard work as in the game.

## The loop for an agent

1. `ui_test.ps1 start -Scenario <scenario>` once.
2. Edit the XML/Lua. Wait until `status.json`'s `reload` counter increases (≈0.5 s after the save).
3. Look at `latest.png`. Read `errors`, `warnings`, `unmockedCalls`, `undefinedGlobals` in `status.json`.
4. Repeat. Use `cmd` for anything a file save cannot do (hover, click, open a window, fire an event).

## How the game API is provided: mocks

The client binds ~350 Lua functions in C++ (`GetUnit`, `GetFriendInfo`, `GetNumQuestLogEntries`, ...).
The test app has none of that code; instead every `*.lua` in `mocks/` is run, in name order,
before the TOC is loaded:

| File | Purpose |
|---|---|
| `00_core.lua` | helpers: `UiTest.StubApi`, `UiTest.data`, `UiTest.Returns` |
| `01_api_stubs.lua` | **generated**: a stub for every client API function. Stubs return nil (`GetNum*` → 0, `Is*`/`Has*`/`Can*` → false) and are listed under `unmockedCalls` when called |
| `10_system.lua`, `20_units.lua`, `30_inventory.lua`, `40_friends.lua` | hand-written mocks with fixture data |

To make a window show data, mock the functions listed under `unmockedCalls` in a new or existing
`NN_<area>.lua`, keeping the client's signature and conventions (index bases, return shapes — look
the function up with `python tools/ui_test/api_coverage.py --grep <Name>`, which prints its C++
source location). Keep fixture data in `UiTest.data.<area>` so scenarios can change it.

`GetUnit(name)` returns `UiTest.data.units[name]`, built by `UiTest.MakeUnit{...}`. Unit methods
derive from field names: `level = 12` answers `unit:GetLevel()`, `alive = true` answers `unit:IsAlive()`.

```powershell
python tools/ui_test/api_coverage.py --used          # API the UI references that has no hand-written mock
python tools/ui_test/api_coverage.py --write-stubs   # regenerate 01_api_stubs.lua after client API changes
```

## Scenarios

A scenario is a Lua file run after the UI loaded — the state you want to look at. `scenarios/world.lua`
raises the events the client raises on entering the world; others build on it:

```lua
dofile(UiTest.scenarioDir .. "world.lua")
ShowUIPanel(FriendsFrame)
UiTest.FireEvent("FRIEND_LIST_UPDATE")
```

Pass one with `-Scenario <name>` (looked up in `scenarios/`) or a path. Scenario files are watched too.

## Lua API of the app

| | |
|---|---|
| `UiTest.FireEvent(name, ...)` | raises a UI event like the client does (`TriggerLuaEvent`) |
| `UiTest.Screenshot(file)` | screenshot after the current frame (relative to the output folder) |
| `UiTest.GetScreenSize()` | width, height |
| `UiTest.toc`, `UiTest.locale`, `UiTest.scenarioDir` | |
| `print(...)` | goes to `ui_test.log`, and to the result of the command that caused it |

## Commands (`ui_test.ps1 cmd ...`)

Any file `artifacts/ui_test/commands/*.cmd` is executed line by line; the results appear in a
`.result` file next to it. `ui_test.ps1 cmd` does this for you and prints the result.

| Command | |
|---|---|
| `lua <code>` | runs Lua; expressions print their values (`lua GetNumFriends()` → `= 5`) |
| `event <NAME> [args]` | `UiTest.FireEvent` with numbers/strings/booleans |
| `screenshot [file]` | waits for the next settled frame |
| `hover <x> <y>` / `click <x> <y> [right]` | mouse input in window pixels |
| `key <ESCAPE\|ENTER\|TAB\|A...\|code>` / `type <text>` | keyboard input |
| `wait <frames>` | lets animations and OnUpdate handlers run |
| `reload`, `toc <file>`, `scenario <name\|none>`, `resize <w> <h>`, `quit` | |

## Options

`ui_test.exe --help` lists them: `--game` (default) / `--glue` / `--toc`, `--scenario`, `--size WxH`,
`--locale`, `--screenshot <png>` (one-shot), `--settle <frames>`, `--no-watch`, `--backdrop RRGGBB`,
`--background <texture>`, `--data`, `--tools`, `--out`.

## Limits and gotchas

- **Same checkout for code and data.** The UI data must match the frame_ui code the app was built
  from. Worktrees usually have empty data submodules, so the app then finds the data of another
  checkout (walking up the folders) and warns about it in `status.json`; if that checkout has
  uncommitted frame_ui changes (e.g. new XML features), layouts can render wrong or not at all.
  Build `ui_test` in the checkout whose UI you edit, or pass `--data`.
- No 3D: model, unit model, minimap and world frames are placeholders (`ModelFrame` etc.
  keep their Lua methods but draw only their XML imagery). Cooldown sweeps are not drawn.
- Textures are cached for the app's lifetime; restart it after replacing a `.htex`.
- Screenshots are taken `--settle` frames (default 10) after a load; fades that take longer (zone
  text) may be caught mid-way. Add `wait <frames>` before `screenshot` when it matters.
