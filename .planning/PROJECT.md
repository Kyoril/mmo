# MMO Project

## What This Is

A custom MMO game engine and server ecosystem built in C++17, featuring distributed login/realm/world servers, a native 3D game client (D3D11/Metal), an ImGui-based editor, and a Lua scripting layer. The project targets a playable fantasy MMO experience with WoW-inspired architecture.

## Core Value

A fully functional, self-hosted MMO server/client stack where players can authenticate, enter a 3D world, fight creatures, cast spells, manage inventory, and group with other players — all running on custom infrastructure with no third-party engine dependency.

## Requirements

### Validated

- ✓ Distributed server architecture (login/realm/world) with TCP + REST inter-server communication — existing
- ✓ SRP6a client authentication and realm selection — existing
- ✓ Character creation, selection, and persistence — existing
- ✓ Enter-world flow: client → realm → world routing with encrypted game protocol — existing
- ✓ 3D client rendering with D3D11 (Windows) and Metal (macOS) backends — existing
- ✓ Deferred shading render pipeline with material-based sorting — existing
- ✓ Terrain system with paged tiles and coverage maps — existing
- ✓ Scene graph with octree spatial management — existing
- ✓ Foliage system — existing
- ✓ Particle system and visual effects — existing
- ✓ Spell casting system with auras, effects, and target resolution — existing
- ✓ Spell visualization: projectiles, lights, ribbon trails, channeling, tints — existing
- ✓ Creature AI state machine (idle, combat, death, reset, prepare) — existing
- ✓ Extensible creature combat scripting system — existing
- ✓ Auto-attack with melee combat table — existing
- ✓ Inventory system with command pattern (add/remove/swap, bags, equipment) — existing
- ✓ Navigation mesh generation and pathfinding — existing
- ✓ Custom UI framework (FrameUI) with frames, buttons, fonts, scroll bars, text fields — existing
- ✓ Lua scripting integration for game logic — existing
- ✓ ImGui-based editor with model preview, spell visualizer, texture editor — existing
- ✓ Movement interpolation with Hermite splines — existing
- ✓ Dungeon instance binding and group handling — existing
- ✓ GPU occlusion culling (D3D11) — existing
- ✓ Environmental and fall damage — existing
- ✓ Global cooldowns for spells — existing
- ✓ MySQL/MariaDB persistence per server — existing
- ✓ Docker deployment with multi-stage builds — existing
- ✓ Unit testing with Catch2 — existing
- ✓ HPAK/TEX custom asset formats — existing
- ✓ Discord RPC integration (optional) — existing
- ✓ Update/launcher system — existing

### Active

- [ ] Complete trigger system (10+ unimplemented trigger actions)
- [ ] Quest system completion (triggers, objectives, rewards flow)
- [ ] NPC interaction polish (dialog, vendors, quest givers)
- [ ] Group/party gameplay features (group finder, instance resets)
- [ ] Chat system (channels, whispers, guild chat)
- [ ] Guild system completion (ranks, permissions, guild features)
- [ ] Character progression (leveling, XP, talent/ability unlocking)
- [ ] Loot system (creature loot tables, loot distribution)
- [ ] World object interaction (usable objects, doors, chests)
- [ ] Editor workflow improvements (creature spawning, trigger editing)

### Out of Scope

- PvP / arena system — focus on PvE first
- Auction house / player trading — post-alpha feature
- Achievement system — polish feature for later
- Voice chat — not planned
- Mobile client — desktop only

## Context

- Brownfield C++ project with substantial existing infrastructure (~45 shared libraries, 3 server executables, client, editor)
- Active development with recent focus on combat mechanics, spell visualization, and editor improvements
- Uses custom formats (HPAK for archives, TEX for textures, SFF for configuration) rather than standard alternatives
- Build system: CMake with optional unity builds, targeting MSVC/GCC/Clang
- All dependencies bundled in `deps/` — fully self-contained build
- Database: MySQL/MariaDB with per-server schemas and SQL update scripts
- Deployment: Docker Compose with Ubuntu-based multi-stage builds
- No external cloud dependencies — entirely self-hosted

## Constraints

- **Tech stack**: C++17, CMake 3.12+, standalone ASIO, Protobuf, MySQL — established and not changing
- **Platform**: Windows primary (D3D11), macOS secondary (Metal) — no Linux client currently
- **Code style**: Allman braces, Doxygen headers, tabs for indentation, `m_camelCase` members — enforced per copilot-instructions.md
- **Architecture**: Single-threaded event loop per server with separate DB thread — scalability ceiling per world instance
- **Protocol**: Custom binary TCP protocols — changes require coordinated client/server updates
- **Testing**: Catch2, no mocking framework — manual mock classes

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Custom engine (no Unity/Unreal) | Full control over networking, game logic, and rendering pipeline | ✓ Good — deep customization possible |
| SRP6a for authentication | WoW-compatible auth protocol, strong password security | ✓ Good — works but custom implementation needs review |
| Protobuf for game data | Efficient binary serialization with schema evolution | ✓ Good |
| Standalone ASIO (no Boost) | Lightweight async I/O without Boost dependency | ✓ Good |
| Custom UI framework (FrameUI) | Game-specific UI with minimal overhead | ⚠️ Revisit — has caused recurring issues (hyperlink bugs) |
| Command pattern for inventory | Transactional operations with undo/redo capability | ✓ Good — recently refactored and stabilized |
| MySQL per server | Simple isolation, independent schemas | ✓ Good |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-03-31 after initialization*
