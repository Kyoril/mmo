<!-- GSD:project-start source:PROJECT.md -->
## Project

**MMO Project**

A custom MMO game engine and server ecosystem built in C++17, featuring distributed login/realm/world servers, a native 3D game client (D3D11/Metal), an ImGui-based editor, and a Lua scripting layer. The project targets a playable fantasy MMO experience with WoW-inspired architecture.

**Core Value:** A fully functional, self-hosted MMO server/client stack where players can authenticate, enter a 3D world, fight creatures, cast spells, manage inventory, and group with other players — all running on custom infrastructure with no third-party engine dependency.

### Constraints

- **Tech stack**: C++17, CMake 3.12+, standalone ASIO, Protobuf, MySQL — established and not changing
- **Platform**: Windows primary (D3D11), macOS secondary (Metal) — no Linux client currently
- **Code style**: Allman braces, Doxygen headers, tabs for indentation, `m_camelCase` members — enforced per copilot-instructions.md
- **Architecture**: Single-threaded event loop per server with separate DB thread — scalability ceiling per world instance
- **Protocol**: Custom binary TCP protocols — changes require coordinated client/server updates
- **Testing**: Catch2, no mocking framework — manual mock classes
<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->
## Technology Stack

## Languages
- **Primary:** C++ (C++17 standard, targeting VS 2017+, GCC 8+, Clang)
- **Secondary:** C (some dependency code), Lua (game scripting via `deps/lua/` and `deps/luabind_noboost/`), Protocol Buffers (data definitions)
- **Build/Config:** CMake (minimum 3.12), Docker (Ubuntu-based)
## Build System
- **Tool:** CMake 3.12+ with multi-platform support (MSVC, GCC, Clang, AppleClang)
- **Root:** `CMakeLists.txt`
- **Compiler configs:** `cmake/mmo_compilers/msvc`, `cmake/mmo_compilers/clang`, `cmake/mmo_compilers/gcc`, `cmake/mmo_compilers/apple-clang`
- **Options file:** `cmake/mmo_options.cmake` — toggles for client, editor, tools, tests, Discord RPC, dev commands
- **Macros:** `cmake/mmo_macros.cmake` — `add_lib`, `add_lib_recurse`, `add_exe`, `add_gui_exe_recurse`, unity build support
- **External deps:** `cmake/mmo_external_dependencies.cmake` — finds OpenSSL, MySQL, ASSIMP, sets up ASIO, Luabind, Catch, GLFW, ImGui
- **Version:** `cmake/mmo_version.cmake` — generates `version.h` from git metadata
- **Config gen:** `config.h.in` → `build/config.h` (SRP6 constants), `version.h.in` → `build/version.h` (git branch/commit/revision)
- **Output dirs:** Binaries → `bin/`, Libraries → `lib/`
- **Unity build:** Optional batching of .cpp files via `MMO_UNITY_BUILD` option
## Core Dependencies
| Dependency | Version | Purpose |
|-----------|---------|---------|
| Protobuf | v3.21.12 | Data serialization for game data (proto files in `src/shared/proto_data/`) |
| ASIO (standalone) | bundled | Async I/O networking (`deps/asio/`) |
| OpenSSL | system | TLS, cryptography, SRP6 authentication |
| MySQL / MariaDB | system | Persistent database storage |
| Lua | bundled | Game scripting engine (`deps/lua/`) |
| Luabind (no-boost) | bundled | Lua-C++ binding layer (`deps/luabind_noboost/`) |
| zlib | bundled | Compression (`deps/zlib/`) |
| Recast Navigation | bundled | Navigation mesh generation and pathfinding (`deps/recastnavigation/`) |
| nlohmann/json | bundled | JSON parsing (`deps/json/`) |
| Expat | bundled | XML parsing (`deps/expat/`) |
| stduuid | bundled | UUID generation (`deps/stduuid/`) |
| zstr | bundled | Streaming zlib compression wrapper (`deps/zstr/`) |
## Client/Editor Dependencies
| Dependency | Purpose |
|-----------|---------|
| GLFW | Windowing and input (`deps/glfw/`) |
| FreeType | Font rendering (`deps/freetype/`) |
| stb_image | Image loading (`deps/stb_image/`) |
| ImGui | Editor UI (`deps/imgui/`, `deps/imgui-node-editor/`) |
| ASSIMP | 3D model import (editor only, system-installed) |
| FMOD | Audio engine (`deps/fmod/`, Windows only) |
| Metal-cpp | macOS Metal rendering API (`deps/metal-cpp/`) |
| Discord RPC | Discord integration (`deps/discord-rpc/`, optional) |
## Dev Dependencies
| Dependency | Purpose |
|-----------|---------|
| Catch2 | Unit testing framework (`deps/catch/`) |
## Configuration
- **Build options** (in `cmake/mmo_options.cmake`):
- **Runtime config files:** `config/login_server.cfg`, `config/realm_server.cfg`, `config/world_server.cfg` (simple file format)
- **Environment variables** (Docker): `MYSQL_HOST_NAME`, `MYSQL_USER_NAME`, `MYSQL_PASSWORD`, `MYSQL_DATABASE`, `LOGIN_WEB_API_USER`, `LOGIN_WEB_API_PASSWORD`
## Runtime Requirements
- **Windows:** Primary development platform, D3D11 graphics, FMOD audio
- **macOS:** Metal graphics backend, Cocoa framework
- **Linux:** Server-only builds by default (client option OFF), GCC 13+ in Docker
- **Docker:** Ubuntu-based multi-stage builds for server deployment (`Dockerfile.login_server`, `Dockerfile.realm_server`, `Dockerfile.world_server`)
- **Docker Compose:** `compose.yml` orchestrates login_server, realm_server, world_server with shared data volume
- **System dependencies:** OpenSSL, MySQL/MariaDB client libraries, uuid-dev (Linux)
- **Defines:** `ASIO_STANDALONE`, `DT_POLYREF64`, `XML_STATIC`, `UUID_SYSTEM_GENERATOR`
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

## Code Style
### Formatting
- **Braces:** Allman style — each opening and closing brace on its own line
- **If blocks:** Always use braces, even for single-line bodies (no single-line if blocks)
- **Indentation:** Tabs for indentation
- **Blank lines:** Between methods, between logical sections
### Example
## Naming Conventions
### Types
- **Classes/Structs:** `PascalCase` (e.g., `Program`, `PlayerManager`, `GameUnitS`, `NonCopyable`)
- **Enums:** `PascalCase` for type, `PascalCase` for values (e.g., `ObjectTypeId::Unit`)
- **Enum namespaces:** Sometimes `snake_case` namespace with `Type` enum inside (e.g., `unit_state::Type::Stunned`)
### Members
- **Member variables:** `m_camelCase` prefix (e.g., `m_logFile`, `m_ioService`, `m_clients`)
- **Static members:** `PascalCase` (e.g., `Program::ShouldRestart`)
- **Local variables:** `camelCase` (e.g., `logOptions`, `testString`)
- **Constants:** `PascalCase` or `camelCase` depending on context
### Functions
- **Methods:** `PascalCase` for public (e.g., `GetLevel()`, `IsAlive()`, `Start()`)
- **Private methods:** Also `PascalCase`
- **Free functions:** `camelCase` in anonymous namespaces (e.g., `generateLogFileName`)
- **Getters:** `GetPropertyName()` or `IsPropertyName()` pattern
- **Setters:** `SetPropertyName()` pattern
### Files
- **Source files:** `snake_case.cpp` and `snake_case.h` (e.g., `player_manager.cpp`, `game_unit_s.h`)
- **Server-side objects:** `game_*_s.h` suffix (e.g., `game_player_s.h`)
- **Client-side objects:** `game_*_c.h` suffix (e.g., `game_player_c.h`)
- **Test files:** `test_*.cpp` (e.g., `test_auth_protocol.cpp`) or `*_test.cpp` (e.g., `item_validator_test.cpp`)
### Namespaces
- **Root namespace:** `mmo`
- **Sub-namespaces:** `mmo::web`, `mmo::net::http`, `mmo::auth`, `mmo::io`
- **Enum pseudo-namespaces:** `namespace unit_state { enum Type { ... }; }` pattern
## Common Patterns
### Ownership & Lifecycle
- **`std::shared_ptr`** for shared ownership (e.g., network connections, clients)
- **`std::unique_ptr`** for exclusive ownership
- **`NonCopyable`** base class to prevent copying of resource-managing objects
- **RAII** for resource management throughout
- **`scoped_connection`** for signal lifetime management (auto-disconnect on destruction)
### Observer Pattern (Signals)
### Asynchronous I/O
### Protocol Packets
### Server Program Pattern
### Game Object Hierarchy (Server-side)
### AI State Machine
### Inventory Command Pattern
### Configuration
## Error Handling
- **Assertions:** `ASSERT(x)` macro wrapping `assert()` — used for invariants
- **`VERIFY(x)`** — asserts in debug, evaluates in release
- **`FATAL(x, msg)`** — assertion with message (debug), plain assert (release)
- **`UNREACHABLE()`** — marks impossible code paths
- **Return codes:** `int32` return from `Program::run()` (0 = success, 1 = failure)
- **Logging:** `DLOG()`, `WLOG()`, `ELOG()` macros for debug, warning, error output
- **No exceptions** — `SIMPLE_NO_EXCEPTIONS` defined, signals use error codes instead
## Documentation
- **Doxygen format** for header file documentation
- **Copyright headers:** `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on all source files
- **`#pragma once`** used universally for include guards
- **`[[nodiscard]]`** and `noexcept` used on modern interfaces (e.g., `IPlayerValidatorContext`)
## Type System
- Custom type aliases in `src/shared/base/typedefs.h`:
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

## Overall Pattern
- **Distributed client-server MMO architecture** with three distinct server tiers and a native game client
- **Service-oriented**: Each server is an independent process with its own database, communicating via custom TCP protocols and REST APIs
- **Event-driven async I/O**: All servers use standalone ASIO for non-blocking network operations with `asio::io_service` event loops
- **Entity-component model**: Game objects use a field map / property system rather than a traditional ECS, with inheritance-based object hierarchy
## Server Tiers
### Login Server (`src/login_server/`)
- **Role:** Authentication gateway and realm registry
- **Flow:** Client authenticates via SRP6a → receives realm list → selects realm
- **Components:** `Program` → `PlayerManager` (manages auth connections) → `Player` (per-client state) → `RealmManager` → `Realm` (registered realm nodes)
- **Database:** `mmo_login` — accounts, realm registrations
- **APIs:** Auth protocol on port 3724, REST on port 8090
### Realm Server (`src/realm_server/`)
- **Role:** Character management, world routing, social features
- **Flow:** Authenticated client connects → character list → enters world → routed to world node
- **Components:** `Program` → `PlayerManager` → `Player`, `WorldManager` → `World`, `LoginConnector` (upstream to login), `FriendMgr`, `GuildMgr`, `MotdManager`
- **Database:** `mmo_realm_01` — characters, guilds, friends
- **APIs:** Game protocol on port 8130, REST on port 8092
### World Server (`src/world_server/`)
- **Role:** Authoritative game simulation — movement, combat, spells, AI, quests, inventory
- **Flow:** Realm routes player → world node hosts map instances → game simulation runs
- **Components:** `Program` → `PlayerManager` → `Player` (with split handler files: `player_npc_handlers.cpp`, `player_inventory_handlers.cpp`, `player_dev_handlers.cpp`), `RealmConnector`, `GroupManager`, `TriggerHandler`
- **Database:** `mmo_world_01` — world state, inventory persistence
### Game Client (`src/mmo_client/`)
- **Role:** Rendering, input, UI, network communication with servers
- **Components:** `ClientApplication` → `Screen`/`GameStates`, `RealmConnector` (in `net/`), `PlayerController`, UI system (`ui/`), Console system (`console/`), Discord integration
- **Rendering:** D3D11 (Windows) or Metal (macOS) via abstract `GraphicsDevice`
### Editor (`src/mmo_edit/`)
- **Role:** Map editing, creature spawns, game data management
- **Components:** ImGui-based UI, database-backed, `EditorHost` interface, `TransformWidget` for 3D manipulation
## Shared Library Layer (`src/shared/`)
### Foundational
- `base/` — Type definitions, assertions, SHA1, BigNumber, timers, signals, filesystem, service base class
- `log/` — Logging framework with configurable streams and log levels
- `math/` — Math types (vectors, matrices, quaternions)
- `binary_io/` — Abstract binary read/write with sources and sinks
- `simple_file_format/` — Custom config file parser (SFF)
### Networking
- `network/` — Header-only templates for TCP connection, connector, server patterns (ASIO-based)
- `auth_protocol/` — Authentication packet definitions and serialization
- `game_protocol/` — Game packet definitions with encryption (`GameCrypt`)
- `http/` — HTTP server and client implementation
- `https_client/` — TLS HTTP client
- `web_services/` — REST API service layer
### Data
- `proto_data/` — Protobuf definitions for all game data (50+ .proto files: spells, items, creatures, quests, maps, etc.)
- `sql_wrapper/` — Abstract SQL database interface
- `mysql_wrapper/` — MySQL/MariaDB concrete implementation
- `client_data/` — Client-side data asset loading
### Game Logic
- `game/` — Core game types (items, spells, auras, characters, maps, movement)
- `game_server/` — Server-side game logic (objects hierarchy, AI, spells, inventory, world simulation)
- `game_client/` — Client-side game objects (`GameObjectC`, `GameUnitC`, `GamePlayerC`, etc.), object manager, movement
- `game_common/` — Shared client/server utilities (world entity loader, projectiles)
### Rendering (Client/Editor only)
- `graphics/` — Abstract graphics device, buffers, textures, materials, shaders
- `graphics_d3d11/` — Direct3D 11 implementation
- `graphics_metal/` — Metal implementation (macOS)
- `graphics_null/` — Null graphics device (for headless/testing)
- `scene_graph/` — Scene management (octree, entities, meshes, skeletons, animations, particles, foliage, portals)
- `terrain/` — Terrain system with paged tiles and coverage maps
- `deferred_shading/` — Deferred rendering pipeline
- `frame_ui/` — Custom UI framework (frames, buttons, fonts, hyperlinks, scroll bars)
- `tex/`, `tex_v1_0/` — Custom texture format
- `hpak/`, `hpak_v1_0/` — Custom archive format
### Audio
- `audio/` — Abstract audio interface
- `fmod_audio/` — FMOD implementation (Windows)
- `null_audio/` — Null audio (silent fallback)
### Navigation
- `nav_mesh/` — Navigation mesh runtime queries
- `nav_build/` — Navigation mesh generation (uses Recast)
- `paging/` — Paged terrain/navigation loading
## Data Flow
### Authentication Flow
```
```
### Game Session Flow
```
```
### Server Registration Flow
```
```
## Key Abstractions
- **`NonCopyable`** — Base class to prevent object copying (`src/shared/base/non_copyable.h`)
- **`signal/scoped_connection`** — Observer pattern via signal/slot system (`src/shared/base/signal.h`)
- **`TimerQueue`** — Async timer management (`src/shared/base/timer_queue.h`)
- **`GraphicsDevice`** — Abstract renderer with D3D11/Metal/Null backends
- **`IPlayerValidatorContext`** — Interface for validating player actions (testable via mocks)
- **Field map system** — Game objects expose properties via indexed field maps for network serialization
## Entry Points
- `src/login_server/main.cpp` → `Program::run()` → login server
- `src/realm_server/main.cpp` → `Program::run()` → realm server
- `src/world_server/main.cpp` → `Program::run()` → world server
- `src/mmo_client/` → client application (platform-specific: `win/`, `macos/`)
- `src/mmo_edit/mmo_edit.cpp` → editor application
- `src/unit_tests/main.cpp` → Catch2 test runner
- `src/game_server_unit_tests/main.cpp` → Catch2 game server test runner
- `src/hpak_tool/` → archive packer tool
- `src/nav_builder/` → navigation mesh builder tool
- `src/update_compiler/` → update package compiler
<!-- GSD:architecture-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
