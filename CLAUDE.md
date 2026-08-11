# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Custom MMO game engine and server ecosystem built in C++17. Three independent server tiers (login, realm, world), a native 3D game client (D3D11 on Windows / Metal on macOS), an ImGui-based editor, and a Lua scripting layer. WoW-inspired architecture with no third-party engine dependency.

## Implemented Gameplay Features

The following systems are fully implemented and should not be suggested as future work:

- **Quest system** — including quest tracker UI and objective tracking
- **Talent system** — fully playable in-game
- **Action bar** — with cooldown display
- **Minimap**
- **Chat** — significantly enhanced
- **Loot UI** — including group loot distribution
- **Inventory system** — refactored with command pattern and strong types
- **Spell visualizations & animations**
- **Terrain hole system**
- **Foliage & particle systems**
- **Unit stat system** — creatures use stat formulas instead of hardcoded values
- **Asset browser** — visual thumbnails, text search, drag & drop from browser into viewport

## Agentic Workflow

- All agent implementation work happens on a `feature/<topic>` branch (use a worktree
  for parallel sessions). Direct commits to `develop` are reserved for trivial
  data/docs tweaks the user explicitly requests.
- Merging to `develop` goes through the local quality gate: `/gate` runs
  `tools/gate/verify.ps1` (Debug build + unit tests + E2E) and then a code review of
  the branch diff; `/ship` performs the merge and refuses without a green, full,
  HEAD-matching `tools/gate/last_report.json`.
- Never push to origin unless the user explicitly asks.
- Scheduled reports land in `tools/gate/reports/` (nightly gate on develop, weekly
  content audit). At session start, if the newest nightly report there is red,
  surface it to the user before starting new work.
- A nightly report with `"skipped": true` / `"passed": null` means the run was skipped
  because the repo was busy (dirty tree or non-develop branch checked out) — treat
  that as "did not run," not as red. Note also that `.claude/settings.local.json`
  (htex MCP config) does not follow git worktrees, so parallel worktree sessions run
  without it.

## Network Protocol Changes

Any change to what goes on the wire — an opcode added, removed, renumbered or reordered, a
packet payload changed, framing or the game cipher touched — requires bumping
`mmo::auth::ProtocolVersion` and/or `mmo::game::ProtocolVersion`, then running:

```bash
python tools/protocol_version_check.py --update
```

Without the bump, incompatible peers are not rejected — they authenticate and then misparse
each other. `tools/protocol_version_check.py` runs as the first step of the gate and fails
on changes it can see; it cannot see a payload change inside a handler, so that case still
needs judgement. Record the bump in [docs/protocol_versions.md](docs/protocol_versions.md).

## Build Commands

### Prerequisites
- CMake 3.12+
- OpenSSL (system)
- MySQL/MariaDB client libraries
- **Linux:** `gcc-13 g++-13 libmysqlclient-dev uuid-dev`
- **Windows:** dependencies installed system-wide (no vcpkg)

### Configure and Build (Linux / servers only)
```bash
git submodule update --init
mkdir build && cd build
cmake ../
make
```

### Configure (Windows — full stack)
```powershell
cmake -S . -B build `
  -DMMO_BUILD_CLIENT=ON `
  -DMMO_BUILD_EDITOR=ON `
  -DMMO_BUILD_TOOLS=ON `
  -DMMO_WITH_DEV_COMMANDS=ON
```

The primary workflow on Windows is **Visual Studio**: open the generated solution from `build/`, then build the whole solution or press F5 to build and run the active target.

For command-line builds:
```powershell
cmake --build build --config Release          # build everything
cmake --build build -t <targetname>           # build a specific target, e.g. world_server
```

### Key CMake Options
| Option | Default | Purpose |
|---|---|---|
| `MMO_BUILD_CLIENT` | ON (Win), OFF (other) | Build the game client |
| `MMO_BUILD_EDITOR` | OFF | Build ImGui editor (requires ASSIMP) |
| `MMO_BUILD_TOOLS` | OFF | Build hpak_tool, nav_builder, update_compiler |
| `MMO_BUILD_TESTS` | ON | Build Catch2 unit tests |
| `MMO_WITH_DEV_COMMANDS` | OFF | Enable GM dev commands in servers |
| `MMO_UNITY_BUILD` | OFF | Batch .cpp files to speed up distribution builds |
| `MMO_DISABLE_ITERATOR_DEBUG` | ON | Disable MSVC iterator debugging (faster debug builds) |

Binaries go to `bin/`, libraries to `lib/`.

### Run Tests

Tests live in `src/tests/<library>_tests/`, one executable per library. Build them all with
the `all_tests` target and run them with CTest:

```bash
cmake --build build --config Debug -t all_tests
cd build && ctest -C Debug --output-on-failure
```

Individual suites are still ordinary executables in `bin/` (`bin/Debug/` on Windows), so
`./bin/math_tests "[aabb_tree]"` works for narrowing down a single failure.

**Adding a suite** costs one folder. Create `src/tests/<library>_tests/`, add a
`CMakeLists.txt` whose only content is `mmo_add_test(<library>_tests <libs...>)`, and add
one `add_subdirectory` line to `src/tests/CMakeLists.txt`. The macro globs the directory
recursively, links Catch2's shared main, and registers the suite with CTest and with
`all_tests` — the gate and CI pick it up with no further edits. Suites needing a graphics
device go inside the `MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR` guard; everything else must
keep building on the headless Linux server build.

### End-to-End Gameplay Tests (Windows)

The E2E harness verifies gameplay changes against a real, isolated server stack with a
headless Lua-scripted client — see [e2e/README.md](e2e/README.md) for the scenario API.

```powershell
$env:MMO_E2E_MYSQL_PASSWORD = "<mysql password>"
cmake --build build -t e2e_client login_server realm_server world_server --config Debug
powershell -File tools/e2e/e2e_run.ps1        # up -> all scenarios -> down; exit 0 = green
```

Scenarios live in `e2e/scenarios/*.lua`; results (JSONL transcripts + `summary.json`)
land in `e2e/runtime/logs/`. The test stack uses its own ports and throwaway databases,
so it coexists with a running dev stack. Requires `MMO_WITH_DEV_COMMANDS=ON`.

## Architecture

### Server Tiers

**Login Server** (`src/login_server/`) — Authentication gateway and realm registry.  
Flow: Client authenticates via SRP6a → receives realm list → selects realm.  
Ports: auth 3724, realm connector 6279, REST 8090.  
DB: `mmo_login`

**Realm Server** (`src/realm_server/`) — Character management and world routing.  
Flow: Authenticated client → character list → enters world → routed to world node.  
Ports: game 8130, world connector 6280, REST 8092.  
DB: `mmo_realm_01`

**World Server** (`src/world_server/`) — Authoritative game simulation (movement, combat, spells, AI, quests, inventory).  
The world server connects *upstream* to the realm server on startup and must be pre-registered via REST.  
DB: `mmo_world_01`

All three servers follow the same program pattern: `main.cpp` → `Program::run()` → `asio::io_service` event loop with a separate DB thread.

### Server Registration Flow
1. Create realm at login server: `POST http://localhost:8090/create-realm`
2. Create world node at realm server: `POST http://localhost:8092/create-world`
3. Configure SHA1 password hashes in server config files.

REST API collections for all servers are in `bruno/mmo-dev/`.

### Shared Library Layer (`src/shared/`)

The bulk of the engine logic lives here as static libraries consumed by all executables:

| Layer | Key libraries |
|---|---|
| Foundation | `base/` (typedefs, signals, timers, SHA1, BigNumber), `log/`, `math/`, `binary_io/` |
| Networking | `network/` (ASIO templates), `auth_protocol/`, `game_protocol/` (with `GameCrypt`), `http/`, `web_services/` |
| Data | `proto_data/` (50+ .proto files), `mysql_wrapper/`, `client_data/`, `simple_file_format/` |
| Game logic | `game/` (items, spells, auras, maps), `game_server/` (server-side objects, AI, inventory), `game_client/` (client-side objects) |
| Rendering (client/editor only) | `graphics/` + `graphics_d3d11/` / `graphics_metal/`, `scene_graph/`, `terrain/`, `deferred_shading/`, `frame_ui/` |
| Audio | `audio/` + `fmod_audio/` (Win) / `null_audio/` |
| Navigation | `nav_mesh/`, `nav_build/` (Recast), `paging/` |
| Custom formats | `hpak/` (archive), `tex/` (texture), `simple_file_format/` (config) |

### Key Abstractions

**`FieldMap<T>`** (`src/shared/game/field_map.h`) — All game objects (units, players, items) expose their replicated properties through an indexed field map. This is the network serialization layer — only changed fields are sent on update.

**`signal<>` / `scoped_connection`** (`src/shared/base/signal.h`) — Observer pattern used everywhere for event handling. `scoped_connection` auto-disconnects on destruction, making lifetime management safe.

**`AbstractConnection<Protocol>`** (`src/shared/network/connection.h`) — Header-only ASIO template. Packet handling is via `IConnectionListener::connectionPacketReceived()` returning `PacketParseResult` (Pass / Block / Disconnect).

**`NonCopyable`** (`src/shared/base/non_copyable.h`) — Base class for all resource-managing objects.

**Game object naming convention**: server-side objects end in `_s` (e.g., `GameUnitS`, `GamePlayerS`); client-side end in `_c` (e.g., `GameUnitC`, `GamePlayerC`).

### Lua Scripting

Server scripts live in `data/scripts/`. Register creature event hooks with `RegisterCreatureScript(entryId, scriptTable)`. The script table provides callbacks: `OnGossipHello`, `OnGossipSelect`, `OnQuestAccept`, `OnQuestComplete`, etc.

## Database Migrations

Modifying the databases for the login server or realm server are automated. To create a new database migration you create a new .sql file with mysql syntax to do the db migration. The filename needs to be in the format "YYYYMMDD_OrderNumber_ChangeDescriptionShort", where OrderNumber is just used to ensure execution order in case of multiple migrations at the same date.

The file name is added to the databases "history" table with the filename (without .sql) as the migration names already applied.

When you create a database migration, and you want to also include the changes in the "*_db_full.sql", you need to add the INSERT call in the history table there as well, otherwise the server will try to apply the update you already applied a second time on server launch, which might fail (due to tables already existing, or being in a wrong state).


## Localization

UI strings are localized. Localized strings are in formatted text files located in data/client/Locales/<locale_code>/Localization.txt.

UI lua files and xml files should not contain hardcoded strings, but string constants which then will be used as a key in Localization.txt.

If you add or modify localized strings, add them in all locales (at least as a placeholder).

Text property values in XML are automatically localized. In lua, you should use the `Localize("KEY")` method. If a key is not available in the current locales Localization.txt file, the KEY is printed as is.


### Game Data

All static game data (spells, items, creatures, quests, maps, etc.) is defined in Protobuf schemas at `src/shared/proto_data/*.proto`. The compiled data is loaded by the realm and world servers at startup from `data/editor/`.

### Docker / Server Deployment

Three Dockerfiles: `Dockerfile.login_server`, `Dockerfile.realm_server`, `Dockerfile.world_server`. Orchestrated via `compose.yml`. Environment variables for MySQL connection, REST API credentials, realm/world registration hashes.

## Code Style

From `copilot-instructions.md` and `cmake/mmo_options.cmake` — enforced project-wide:

- **Braces:** Allman style — every `{` and `}` on its own line.
- **If blocks:** Always use braces, even for single-line bodies.
- **Indentation:** Tabs.
- **Member variables:** `m_camelCase` prefix.
- **Methods (public and private):** `PascalCase` — `GetLevel()`, `IsAlive()`, `SetPosition()`.
- **Local variables / free functions in anonymous namespaces:** `camelCase`.
- **Files:** `snake_case.cpp` / `snake_case.h`.
- **Headers:** `#pragma once` universally, Doxygen documentation on all public members.
- **Copyright header:** `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on every source file.
- **Namespace:** Root is `mmo`; enum pseudo-namespaces use the pattern `namespace unit_state { enum Type { ... }; }`.

## Client Threading

The client uses a fork-join worker pool (`src/shared/base/task_system.h`) — "parallel
islands inside a single-threaded frame". Worker code must never touch `signal<>`, Lua,
`GraphicsDevice`, or resource managers; logging is safe (buffered off-main). See
[docs/threading.md](docs/threading.md) before adding any threaded code.

## Server Threading

Servers never initialize the TaskSystem. Beyond that the tiers differ, and the difference
matters:

- **Login server** — two io threads. Anything reaching across sessions must hop onto the
  target's strand via `AbstractConnection::Post`. Session lookups return `shared_ptr`, never
  raw pointers: the manager's mutex protects the list, not the lifetime of what comes out of it.
- **Realm and world servers** — single-threaded io, and they *depend* on it. Cross-session
  access uses raw `Player*` taken from the managers in ~30 places. Raising either tier's thread
  count requires doing the login server's `shared_ptr` + `Post` work there first.
- **Database** — one connection per tier behind one worker thread. Call sites rely on the
  implicit FIFO ordering that gives.

Anything owning an asio timer, acceptor, or work guard needs a `Stop()` wired into its tier's
shutdown handler, or the process will not exit. Verify with `python tools/shutdown_check.py`.
See [docs/testing-servers.md](docs/testing-servers.md).

## Error Handling

No exceptions (`SIMPLE_NO_EXCEPTIONS`). Use these macros from `src/shared/base/macros.h`:

- `ASSERT(x)` — debug invariant check.
- `VERIFY(x)` — assert in debug, evaluate-only in release.
- `UNREACHABLE()` — marks impossible code paths.
- `DLOG()` / `WLOG()` / `ELOG()` — debug / warning / error log output.

## Type Aliases

Defined in `src/shared/base/typedefs.h` and available globally:

```cpp
uint8, uint16, uint32, uint64   // unsigned
int8, int16, int32, int64       // signed
ObjectGuid  // uint64 — unique game object ID
GameTime    // uint64 — milliseconds
```

## Custom CMake Macros

`cmake/mmo_macros.cmake` provides convenience macros used in every `CMakeLists.txt`:

- `add_lib(name)` / `add_lib_recurse(name)` — static library from current directory.
- `add_exe(name)` / `add_exe_recurse(name)` — console executable.
- `add_gui_exe(name)` / `add_gui_exe_recurse(name)` — GUI/windowed executable (WIN32 + MACOSX_BUNDLE).

The `_recurse` variants scan subdirectories and automatically filter out `win/` folders on non-Windows and `macos/` folders on non-macOS.
