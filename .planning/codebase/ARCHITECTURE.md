# Architecture

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
The shared library is the core of the codebase, containing ~45 sub-libraries:

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
  - `objects/` — `GameObjectS`, `GameUnitS`, `GamePlayerS`, `GameCreatureS`, `GameItemS`, `GameBagS`
  - `ai/` — Creature AI state machine (idle, combat, death, reset, prepare states)
  - `spells/` — Spell casting, aura effects, target resolution
  - `world/` — World instances, visibility grid, unit finder, creature spawning
  - `inventory/` — Command-pattern inventory system (add/remove/swap items, bags, equipment, slots)
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
Client → [SRP6a Challenge] → Login Server
Client ← [SRP6a Proof] ← Login Server
Client → [Realm List Request] → Login Server
Client ← [Realm List] ← Login Server
```

### Game Session Flow
```
Client → [Auth] → Realm Server → [Verify] → Login Server
Client ← [Character List] ← Realm Server
Client → [Enter World] → Realm Server → [Route] → World Server
Client ↔ [Game Packets (encrypted)] ↔ World Server
```

### Server Registration Flow
```
Realm Server → [Register] → Login Server (REST API)
World Server → [Register] → Realm Server (REST API)
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
