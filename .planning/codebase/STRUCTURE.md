# Directory Structure

## Top-Level Layout

```
f:\mmo\
├── CMakeLists.txt              # Root build file
├── compose.yml                 # Docker Compose for server deployment
├── config.h.in                 # Template for SRP6 constants
├── version.h.in                # Template for version info from git
├── Dockerfile.login_server     # Multi-stage build for login server
├── Dockerfile.realm_server     # Multi-stage build for realm server
├── Dockerfile.world_server     # Multi-stage build for world server
├── bin/                        # Compiled executables (Debug/Release/RelWithDebInfo)
├── lib/                        # Compiled libraries
├── build/                      # CMake build directory
├── cmake/                      # CMake configuration modules
├── data/                       # Game data and assets
├── deps/                       # Third-party dependencies (bundled)
├── docker/                     # Docker runtime scripts
├── docs/                       # Documentation
├── src/                        # Source code
├── bruno/                      # API testing collections (Bruno HTTP client)
├── screenshots/                # Project screenshots
└── tools/                      # Additional tooling
```

## Source Code (`src/`)

### Executables
| Directory | Target | Description |
|-----------|--------|-------------|
| `src/login_server/` | `login_server` | Authentication server |
| `src/realm_server/` | `realm_server` | Character/realm server |
| `src/world_server/` | `world_server` | Game world simulation server |
| `src/mmo_client/` | `mmo_client` | Game client (Windows/macOS) |
| `src/mmo_edit/` | `mmo_edit` | Map/data editor (ImGui-based) |
| `src/launcher/` | launcher | Update launcher (Win/Mac) |
| `src/hpak_tool/` | `hpak_tool` | Archive packer tool |
| `src/nav_builder/` | `nav_builder` | Navigation mesh generator |
| `src/update_compiler/` | `update_compiler` | Update package builder |
| `src/mmo_error/` | `mmo_error` | Error reporter (Windows) |
| `src/graphics_test/` | graphics test | Graphics rendering test |
| `src/physics_test/` | physics test | Physics test |

### Test Executables
| Directory | Target | Description |
|-----------|--------|-------------|
| `src/unit_tests/` | `unit_tests` | Core library tests (24+ test files) |
| `src/game_server_unit_tests/` | `game_server_unit_tests` | Game server logic tests (7 test files) |

### Shared Libraries (`src/shared/`)

#### Foundation Layer
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/base/` | `base` | Static lib — core utilities, types, crypto, timers |
| `src/shared/log/` | `log` | Static lib — logging framework |
| `src/shared/math/` | `math` | Static lib — vector/matrix math |
| `src/shared/binary_io/` | `binary_io_hdrs` | Header-only — binary serialization |
| `src/shared/simple_file_format/` | `simple_file_format_hdrs` | Header-only — config file parser |

#### Networking Layer
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/network/` | `network_hdrs` | Header-only — TCP templates |
| `src/shared/auth_protocol/` | `auth_protocol` | Static lib — auth packets |
| `src/shared/game_protocol/` | `game_protocol` | Static lib — game packets |
| `src/shared/http/` | `http` | Static lib — HTTP server/client |
| `src/shared/http_client/` | `http_client` | Header-only — HTTP client |
| `src/shared/https_client/` | `https_client` | Static lib — TLS HTTP client |
| `src/shared/web_services/` | `web_services` | Static lib — REST API services |

#### Data Layer
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/proto_data/` | `proto_data` | Static lib — 50+ protobuf definitions |
| `src/shared/sql_wrapper/` | `sql_wrapper` | Static lib — abstract SQL interface |
| `src/shared/mysql_wrapper/` | `mysql_wrapper` | Static lib — MySQL implementation |
| `src/shared/base64/` | `base64` | Static lib — Base64 encoding |
| `src/shared/virtual_dir/` | `virtual_dir` | Static lib — virtual filesystem |
| `src/shared/assets/` | `assets` | Static lib — asset management |
| `src/shared/client_data/` | `client_data` | Static lib — client data loading |

#### Game Logic Layer
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/game/` | `game` | Static lib — shared game types |
| `src/shared/game_server/` | `game_server` | Static lib — server game logic |
| `src/shared/game_server/objects/` | (part of game_server) | Game object hierarchy (server-side) |
| `src/shared/game_server/ai/` | (part of game_server) | Creature AI state machine |
| `src/shared/game_server/spells/` | (part of game_server) | Spell casting and aura system |
| `src/shared/game_server/world/` | (part of game_server) | World instances and visibility |
| `src/shared/game_client/` | `game_client` | Static lib — client game objects |
| `src/shared/game_common/` | `game_common` | Static lib — shared client/server utils |

#### Rendering Layer (Client/Editor)
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/graphics/` | `graphics` | Static lib — abstract graphics |
| `src/shared/graphics_d3d11/` | `graphics_d3d11` | Static lib — D3D11 backend |
| `src/shared/graphics_metal/` | `graphics_metal` | Static lib — Metal backend |
| `src/shared/graphics_null/` | `graphics_null` | Static lib — null backend |
| `src/shared/deferred_shading/` | `deferred_shading` | Static lib — deferred renderer |
| `src/shared/scene_graph/` | `scene_graph` | Static lib — 3D scene management |
| `src/shared/terrain/` | `terrain` | Static lib — terrain rendering |
| `src/shared/frame_ui/` | `frame_ui` | Static lib — UI framework |
| `src/shared/paging/` | `paging` | Static lib — paged content loading |

#### Audio Layer
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/audio/` | `audio` | Static lib — audio interface |
| `src/shared/fmod_audio/` | `fmod_audio` | Static lib — FMOD backend |
| `src/shared/null_audio/` | `null_audio` | Static lib — silent fallback |

#### Asset Format Layer
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/hpak/` | `hpak` | Static lib — archive format |
| `src/shared/hpak_v1_0/` | `hpak_v1_0` | Static lib — archive v1.0 |
| `src/shared/tex/` | `tex` | Static lib — texture format |
| `src/shared/tex_v1_0/` | `tex_v1_0` | Static lib — texture v1.0 |
| `src/shared/xml_handler/` | `xml_handler` | Static lib — XML parsing |

#### Navigation
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/nav_mesh/` | `nav_mesh` | Static lib — navmesh queries |
| `src/shared/nav_build/` | `nav_build` | Static lib — navmesh generation |

#### Update System
| Directory | Library | Type |
|-----------|---------|------|
| `src/shared/updater/` | `updater` | Static lib — update system |
| `src/shared/update_compilation/` | `update_compilation` | Static lib — update packaging |

## Dependencies (`deps/`)

All third-party dependencies are bundled as source:
```
deps/
├── asio/                # Standalone ASIO networking
├── assimp/              # 3D model import (editor)
├── catch/               # Catch2 testing framework
├── cxxopts/             # Command-line parsing
├── discord-rpc/         # Discord rich presence
├── expat/               # XML parser
├── fmod/                # Audio engine (prebuilt)
├── freetype/            # Font rendering
├── glfw/                # Windowing/input
├── imgui/               # ImGui UI library
├── imgui-node-editor/   # ImGui node editor extension
├── json/                # nlohmann/json
├── lua/                 # Lua scripting engine
├── luabind_noboost/     # Lua-C++ binding
├── metal-cpp/           # Metal C++ wrapper (macOS)
├── metal-cpp-extensions/# Metal extensions
├── recastnavigation/    # Navigation mesh
├── stb_image/           # Image loading
├── stduuid/             # UUID generation
├── zlib/                # Compression
└── zstr/                # Streaming zlib wrapper
```

## Data (`data/`)

```
data/
├── client/              # Game client assets
│   ├── Cache/           # Asset cache
│   ├── ClientDB/        # Client-side database
│   ├── Config/          # Client configuration
│   ├── Editor/          # Editor-specific data
│   ├── Fonts/           # Font files
│   ├── Interface/       # UI layouts and scripts
│   ├── Locales/         # Localization files
│   ├── Models/          # 3D models
│   ├── Particles/       # Particle definitions
│   ├── Sound/           # Audio files
│   ├── Textures/        # Texture assets
│   └── Worlds/          # World/map data
├── editor/              # Editor data
├── login/               # Login server data (schema updates)
└── realm/               # Realm server data
```

## CMake (`cmake/`)

```
cmake/
├── mmo_compiler_settings.cmake  # Compiler detection and config
├── mmo_external_dependencies.cmake  # External library setup
├── mmo_macros.cmake             # Build helper macros
├── mmo_options.cmake            # Build options definitions
├── mmo_version.cmake            # Git-based versioning
├── mmo_compilers/               # Per-compiler settings
│   ├── msvc                     # MSVC settings
│   ├── gcc                      # GCC settings
│   ├── clang                    # Clang settings
│   └── apple-clang              # Apple Clang settings
└── mmo_dependencies/            # Find modules (FindMYSQL, FindASSIMP)
```

## Naming Conventions
- **Libraries:** `snake_case` (e.g., `game_server`, `auth_protocol`, `mysql_wrapper`)
- **Executables:** `snake_case` (e.g., `login_server`, `mmo_client`, `nav_builder`)
- **Header-only libs:** Suffixed with `_hdrs` (e.g., `network_hdrs`, `binary_io_hdrs`)
- **Server-side objects:** Suffixed with `_s` (e.g., `game_unit_s.h`, `game_player_s.h`)
- **Client-side objects:** Suffixed with `_c` (e.g., `game_unit_c.h`, `game_player_c.h`)
- **Source files:** `snake_case.cpp` / `snake_case.h`
- **CMake targets match directory names** in most cases
- **Solution folders:** `shared`, `servers`, `tests`, `deps`
