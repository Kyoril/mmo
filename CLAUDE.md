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
```bash
# From repo root after build:
./bin/unit_tests                   # shared library tests
./bin/game_server_unit_tests       # game server logic tests
./bin/login_server_tests           # login server tests
```

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
