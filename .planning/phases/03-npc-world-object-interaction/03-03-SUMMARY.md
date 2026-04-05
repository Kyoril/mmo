---
phase: 03-npc-world-object-interaction
plan: "03"
subsystem: world-objects
tags: [world-objects, doors, chests, loot, server-handler, use-object]
dependency_graph:
  requires: [03-02]
  provides: [WOBJ-01, WOBJ-02, WOBJ-03]
  affects: [player_npc_handlers, game_world_object_s, loot]
tech_stack:
  added: []
  patterns: [packet-dispatch, object-type-branch, auto-broadcast-via-Set]
key_files:
  created: []
  modified:
    - src/shared/game_server/objects/game_world_object_s.cpp
    - src/world_server/player.h
    - src/world_server/player.cpp
    - src/world_server/player_npc_handlers.cpp
decisions:
  - "Door open/closed state uses object_fields::State (1=open, 0=closed) — not a new flag in world_object_flags"
  - "OnGameObjectUse delegates all IsUsable() checks to GameWorldObjectS::Use() (single responsibility)"
  - "5.0f unit distance threshold for UseObject validation"
metrics:
  duration: "~20 minutes"
  completed: "2026-04-05"
  tasks_completed: 2
  files_modified: 4
---

# Phase 03 Plan 03: World Object Use Pipeline Summary

**One-liner:** Server-side UseObject pipeline: door toggle via State field, chest loot flow with zero-entry guard, 5.0f distance validation in OnGameObjectUse.

## What Was Built

Completed the authoritative server-side UseObject implementation. When a player clicks a world object (door or chest), the client sends a `UseObject` packet containing the object GUID. The world server now:

1. Dispatches `UseObject` → `Player::OnGameObjectUse()` 
2. Reads GUID, finds `GameWorldObjectS` in world instance
3. Validates distance (≤5.0f units)
4. Calls `GameWorldObjectS::Use()` which runs `IsUsable()` internally
5. For doors: toggles `object_fields::State` (0↔1), auto-broadcast via `Set<>()` → `AddObjectUpdate()`
6. For chests: creates `LootInstance` (with zero-loot-entry guard), calls `LootObject()`

## Tasks Completed

| Task | Description | Commit |
|------|-------------|--------|
| 1 | Fix world object type hardcoding + door branch in Use() + zero loot guard | f30527e5 |
| 2 | OnGameObjectUse handler: declare, dispatch, implement | 68598a0b |

## Changes Per File

### `src/shared/game_server/objects/game_world_object_s.cpp`
- **Initialize():** Replace `static_cast<uint32>(GameWorldObjectType::Chest)` with `static_cast<uint32>(m_entry.type())` — world objects now report their configured type from proto data
- **Use():** Replaced flat loot-always body with `switch(GetType())` branch:
  - `Chest`: zero-entry guard (`objectlootentry() == 0` → early return), then existing LootInstance creation
  - `Door`: read `object_fields::State`, toggle 0↔1, auto-broadcast via `Set<>()`
  - `default`: `WLOG` warning for unhandled types

### `src/world_server/player.h`
- Added `OnGameObjectUse(uint16, uint32, io::Reader&)` declaration in private NPC handlers section with Doxygen `///` comment block

### `src/world_server/player.cpp`
- Added `case game::client_realm_packet::UseObject:` dispatch case calling `OnGameObjectUse(opCode, buffer.size(), reader)`

### `src/world_server/player_npc_handlers.cpp`
- Added `#include "game_server/objects/game_world_object_s.h"`
- Implemented `Player::OnGameObjectUse()`: reads GUID, finds object via `FindByGuid<GameWorldObjectS>`, validates 5.0f distance, delegates to `worldObject->Use(*m_character)`

## Requirements Satisfied

- **WOBJ-01:** Door world objects toggle open/closed (State=1 on open, State=0 on close, auto-broadcast). Chest world objects begin loot flow (LootInstance created, LootObject called).
- **WOBJ-02:** Quest-gated objects block interaction — `IsUsable()` called inside `Use()` checks `RequiresQuest` flag + `GetQuestStatus()`.
- **WOBJ-03:** Trigger-disabled objects block interaction — `IsUsable()` checks `world_object_flags::Disabled` flag set by `SetEnabled(false)`.

## Deviations from Plan

None - plan executed exactly as written.

## Known Stubs

None — all behavior is fully wired end-to-end.

## Self-Check: PASSED

- `src/shared/game_server/objects/game_world_object_s.cpp` — contains `m_entry.type()`, `GameWorldObjectType::Door`, `object_fields::State`, `objectlootentry() == 0`
- `src/world_server/player.h` — contains `void OnGameObjectUse(uint16 opCode, uint32 size, io::Reader& contentReader)`
- `src/world_server/player.cpp` — contains `case game::client_realm_packet::UseObject:`
- `src/world_server/player_npc_handlers.cpp` — contains `void Player::OnGameObjectUse(`
- Commits f30527e5 and 68598a0b verified in git log
