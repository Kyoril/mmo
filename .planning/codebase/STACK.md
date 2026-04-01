# Technology Stack

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
  - `MMO_BUILD_CLIENT` — Build game client (default ON on Windows)
  - `MMO_BUILD_EDITOR` — Build editor (requires QT5/ASSIMP, default OFF)
  - `MMO_BUILD_TOOLS` — Build tools like nav_builder, hpak_tool (default OFF)
  - `MMO_BUILD_TESTS` — Build unit tests (default ON)
  - `MMO_BUILD_LAUNCHER` — Build launcher (Win/Mac only, default OFF)
  - `MMO_WITH_DEV_COMMANDS` — Enable developer commands on servers (default OFF)
  - `MMO_WITH_DISCORD_RPC` — Enable Discord integration (default OFF)
  - `MMO_UNITY_BUILD` — Batch .cpp compilation (default OFF)
  - `MMO_WITH_RELEASE_ASSERTS` — Enable assertions in release builds (default OFF)
  - `MMO_DISABLE_ITERATOR_DEBUG` — Disable MSVC iterator debugging (default ON)
  - `MMO_SRP6_N` / `MMO_SRP6_g` — SRP6a authentication constants
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
