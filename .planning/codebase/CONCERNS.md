# Technical Concerns

## Technical Debt

### Incomplete Implementations (TODOs)
Significant number of TODO/FIXME markers throughout the codebase, particularly concentrated in:

- **`src/world_server/trigger_handler.cpp`** — 10+ unimplemented trigger actions:
  - `ACTION_SET_WORLD_OBJECT_STATE` (line 264)
  - `ACTION_SET_VIRTUAL_EQUIPMENT_SLOT` (line 523)
  - `ACTION_SET_PHASE` (line 528)
  - `ACTION_DISMOUNT` (line 655)
  - `ACTION_SET_MOUNT` (line 661)
  - `SetSpawnState` for ObjectSpawner (lines 297, 331)
- **`src/world_server/player_dev_handlers.cpp`** — Multiple stub handlers (lines 137-138, 172-173)
- **`src/world_server/program.cpp`** — TODO stubs at lines 128, 160
- **`src/shared/game_client/unit_movement.h`** — Swimming and water detection not implemented (lines 442-454)
- **`src/shared/network/server.h`** — Empty TODO at line 126
- **`src/shared/game/object_type_id.h`** — Additional object types needed
- **`src/shared/game/character_view.h`** — Missing item display ID attributes

### Code Smells
- **`FATAL` macro** in release mode falls back to plain `ASSERT` instead of proper fatal error handling (`src/shared/base/macros.h`)
- **`src/shared/virtual_dir/reader.h`** — `bool openAsText` parameter should be an enum
- **`src/shared/updater/prepared_update.h`** — Missing move operations (lines 26, 46)
- **`src/mmo_edit/editor_host.h`** — Context menu design acknowledged as needing redesign (line 44)

## Security Considerations

### Authentication
- **SRP6a** implementation is custom — not using a vetted third-party library
- **SHA1** used for password hashing (server-to-server auth) — SHA1 is considered weak for new applications
- **Web API authentication** uses basic HTTP auth (`LOGIN_WEB_API_USER` / `LOGIN_WEB_API_PASSWORD`) — credentials sent in cleartext unless TLS is enforced
- **Docker compose** has hardcoded placeholder passwords (`MYSQL_PASSWORD=MYSQL_PASSWORD`, `LOGIN_WEB_API_PASSWORD=test`) — risk of deployment with default credentials

### Network
- **Game protocol encryption** (`GameCrypt`) is custom — no documentation of the cipher used or security analysis
- **Packet parsing** uses raw pointer casts and reinterpretation (e.g., `*((uint32*)(&buffer[1]))` in tests) — potential alignment and endianness issues

### Data
- **MySQL credentials** passed via environment variables in Docker — no secrets management
- **No input validation** visible at network boundary for many packet handlers

## Performance Concerns

### Architecture
- **Single-threaded event loop** per server (main `asio::io_service`) with separate DB thread — could bottleneck under high player load
- **Protobuf v3.21.12** — not the latest; newer versions may have performance improvements
- **MSVC iterator debugging disabled** by default (`MMO_DISABLE_ITERATOR_DEBUG ON`) — suggests debug build performance has been a concern

### Rendering (Client)
- **Deferred shading** pipeline in `src/shared/deferred_shading/` — standard approach but may need optimization for complex scenes
- **Octree scene management** — good for medium scenes, may need tuning for large open worlds
- **Material-based render queue sorting** documented in `docs/material_based_render_queue_sorting.md`

### World Simulation
- **Tiled unit finder** (`src/shared/game_server/world/tiled_unit_finder.h`) — O(n) scan per tile for nearby units
- **Visibility grid** system manages what each player can see — likely the main scalability bottleneck
- **Creature AI** runs per-creature state machines — could be expensive with many NPCs

## Fragile Areas

### Inventory System
- Recently refactored (many docs in `docs/inventory_*.md`) — may still have edge cases
- Complex command pattern with multiple managers (`BagManager`, `EquipmentManager`, `SlotManager`, `ItemFactory`, `ItemValidator`)
- Unit of work pattern for transactional integrity — if any step fails, rollback must work correctly

### Protocol Layer
- **Auth and game protocol** are tightly coupled to binary layout — any change requires coordinated client/server updates
- **Packet size limits** — some packets reference caps (e.g., `// TODO: 512 cap` in `player_npc_handlers.cpp`)
- **Game crypt** — encryption state must be synchronized between client and server

### World Instance Management
- `src/shared/game_server/world/world_instance.cpp` and `world_instance_manager.cpp` — manages multiple map instances
- Creature spawning, visibility, and unit finding are tightly interconnected
- `solid_visibility_grid.cpp` — visibility calculations affect what packets are sent

### Trigger System
- `src/world_server/trigger_handler.cpp` — large switch-based handler with many unimplemented cases
- `src/shared/game_server/trigger_handler.h` — references TODO for context object replacement
- Triggers connect quest/AI/script systems — changes can have wide-reaching effects

### Client UI (Frame UI)
- Custom UI framework (`src/shared/frame_ui/`) instead of using an established library
- Multiple hyperlink-related fix documents suggest this area has been problematic:
  - `docs/hyperlink_crash_fixes.md`
  - `docs/hyperlink_fixes.md`
  - `docs/hyperlink_color_fix.md`
  - `docs/hyperlink_usage_example.md`

## Missing Capabilities
- **No CI/CD pipeline** visible in the repository
- **No automated deployment** beyond Docker images
- **No monitoring/observability** infrastructure (logging only)
- **No rate limiting** on authentication or web APIs
- **No health check endpoints** for servers
- **Physics system** directory exists (`src/shared/physics/`) but is empty — physics not implemented
- **No automated database migrations** — manual SQL update scripts only
- **No crash reporting** service integrated (error reporter is Windows-only tool)
- **No load testing** or performance benchmarking infrastructure

## Dependency Risks
- **Protobuf v3.21.12** — pinned version fetched from GitHub at build time
- **All other deps bundled** — reduces external risk but means manual updates for security patches
- **FMOD** — proprietary audio library, prebuilt binaries only (`deps/fmod/lib/x64/fmod.dll`)
- **OpenSSL** — system-installed, version not pinned — could vary between build environments
- **MySQL client** — system-installed, must match server version
