# External Integrations

## Databases
- **Type:** MySQL / MariaDB (relational)
- **Connection:** Direct MySQL C client library via `src/shared/mysql_wrapper/`
- **Abstraction:** `src/shared/sql_wrapper/` provides a generic SQL interface; `mysql_wrapper` is the concrete implementation
- **Each server has its own database:** `mmo_login`, `mmo_realm_01`, `mmo_world_01`
- **Schema management:** SQL update scripts in `data/login/updates/`, `data/realm/updates/` (copied into Docker images)
- **Database classes per server:**
  - `src/login_server/mysql_database.h` / `src/login_server/database.h`
  - `src/realm_server/mysql_database.h` / `src/realm_server/database.h`
  - `src/mmo_edit/mysql_database.h` / `src/mmo_edit/database.h`
- **Async pattern:** Separate `asio::io_service` for DB operations with dedicated worker thread(s)

## External APIs
- **No external cloud API calls** — the project is a self-hosted MMO server/client ecosystem
- **Inter-server REST APIs:**
  - Login server exposes REST API on port 8090 for realm/world node registration
  - Realm server exposes REST API on port 8092 for world node management
  - REST implemented via `src/shared/web_services/` with `src/shared/http/`

## Authentication
- **Protocol:** SRP6a (Secure Remote Password) — custom implementation
  - Constants configured via CMake: `MMO_SRP6_N`, `MMO_SRP6_g` (in `config.h.in`)
  - `src/shared/base/big_number.h` — arbitrary precision arithmetic for SRP
  - `src/shared/base/sha1.h` — SHA1 hashing
  - `src/shared/base/hmac.h` — HMAC operations
- **Packet encryption:** `src/shared/game_protocol/game_crypt.h` — packet header encryption after authentication
- **Auth protocol:** `src/shared/auth_protocol/` — client-login and realm-login authentication packets
- **Server-to-server auth:** Realm nodes authenticate to login server; world nodes authenticate to realm server (SHA1 password hashes in config)
- **Web API auth:** Basic HTTP authentication for REST APIs (`LOGIN_WEB_API_USER` / `LOGIN_WEB_API_PASSWORD`)

## File I/O
- **HPAK archives:** Custom archive format for bundled game assets (`src/shared/hpak/`, `src/shared/hpak_v1_0/`)
- **TEX format:** Custom texture format (`src/shared/tex/`, `src/shared/tex_v1_0/`)
- **Binary I/O:** `src/shared/binary_io/` — abstract reader/writer with sinks and sources (vector, memory, file-based)
- **Simple File Format (SFF):** Custom configuration format (`src/shared/simple_file_format/`) — used for server config files
- **XML:** Parsed via Expat (`deps/expat/`), used by `src/shared/xml_handler/` for UI layouts
- **Protobuf data:** Static game data (creatures, spells, items, quests, etc.) stored as `.proto` definitions in `src/shared/proto_data/`
- **Client data directory:** `data/client/` — contains `Cache/`, `ClientDB/`, `Config/`, `Fonts/`, `Interface/`, `Locales/`, `Models/`, `Particles/`, `Sound/`, `Textures/`, `Worlds/`
- **Chunk-based serialization:** `src/shared/base/chunk_reader.h`, `src/shared/base/chunk_writer.h` — RIFF-style chunk format for meshes, skeletons, materials
- **Virtual filesystem:** `src/shared/virtual_dir/` — abstract filesystem layer for asset loading

## Network
- **Library:** Standalone ASIO (non-Boost) for async TCP networking
- **Protocol layers:**
  - **Auth protocol** (`src/shared/auth_protocol/`): Login handshake, SRP6a challenge/proof
  - **Game protocol** (`src/shared/game_protocol/`): Encrypted game packets between client↔realm and client↔world
  - **HTTP** (`src/shared/http/`): REST API for server management
  - **HTTPS** (`src/shared/https_client/`): TLS-secured HTTP client
- **Network templates:** `src/shared/network/` — header-only template library for connection, connector, server patterns
- **Ports:**
  - Login server: 3724 (auth), 8090 (REST API), 6279
  - Realm server: 8130 (game), 8092 (REST API), 6280
  - World server: 8093 (REST API)
- **Architecture:** Distributed server model:
  - Client → Login Server (authentication, realm list)
  - Client → Realm Server (character management, world routing)
  - Realm Server → Login Server (registration)
  - World Server → Realm Server (world hosting, player handoff)
- **Packet structure:** Custom binary packet format with opcode + size header, encrypted after authentication

## Third-Party Services
- **Discord RPC:** Optional rich presence integration (`src/mmo_client/discord.h`, `deps/discord-rpc/`)
  - Controlled by `MMO_WITH_DISCORD_RPC` cmake option
  - Uses `MMO_DISCORD_APP_ID` for application identification
- **FMOD:** Audio engine (Windows only, `deps/fmod/`)
- **Docker Hub:** Server images published as `kyoril/mmo_login_server`, `kyoril/mmo_realm_server`, `kyoril/mmo_world_server`, `kyoril/mmo_data`
