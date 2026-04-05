---
phase: 05-group-guild-features
plan: "01"
subsystem: loot-enforcement
tags: [loot, group, protocol, tdd, world-sync]
dependency_graph:
  requires: []
  provides: [loot-method-enforcement, SetLootMethod-opcode, PlayerGroupLootMethodChanged-auth-packet, loot-method-world-sync]
  affects: [realm_server, world_server, game_server_unit_tests, shared_game_server]
tech_stack:
  added: []
  patterns: [auth-protocol-sync, realm-world-packet, TDD-Red-Green, loot-enforcement-single-point]
key_files:
  created:
    - src/game_server_unit_tests/loot_instance_test.cpp
  modified:
    - src/shared/game_protocol/game_protocol.h
    - src/shared/auth_protocol/auth_protocol.h
    - src/shared/game_server/player_group.h
    - src/shared/game_server/objects/game_player_s.h
    - src/shared/game_server/loot_instance.h
    - src/shared/game_server/loot_instance.cpp
    - src/shared/game_server/ai/creature_ai_death_state.cpp
    - src/realm_server/player_group.h
    - src/realm_server/player.h
    - src/realm_server/player.cpp
    - src/realm_server/world.h
    - src/realm_server/world.cpp
    - src/world_server/realm_connector.h
    - src/world_server/realm_connector.cpp
    - src/world_server/player.h
    - src/world_server/player.cpp
decisions:
  - "LootMethod stored on GamePlayerS (alongside m_groupId) — same pattern as groupId, synced from realm per-member"
  - "MasterLoot GUID sentinel: client sends 0, realm OnSetLootMethod replaces with leader characterId before propagating"
  - "Enforcement lives entirely in LootInstance::TakeItem() — single enforcement point, no bypass via OnAutoStoreLootItem"
  - "RoundRobin pre-assignment uses weakRecipients vector order (aggro/join order), not GUID-sorted map order"
  - "OnSetLootMethod: only group leader may change loot method (assistants cannot in Phase 5)"
metrics:
  duration_minutes: 19
  tasks_completed: 3
  tasks_total: 3
  files_created: 1
  files_modified: 16
  completed_date: "2026-04-05"
---

# Phase 5 Plan 01: Loot Method Enforcement Summary

Server-side loot enforcement pipeline end-to-end: `SetLootMethod` opcode → realm validation + `SendUpdate` + world sync per member → `UpdateCharacterGroupLootMethod` on GamePlayerS → `LootInstance` constructed with loot method from first recipient → `TakeItem()` enforces MasterLoot/RoundRobin/FreeForAll. Six Catch2 unit tests prove all enforcement rules.

## Tasks Completed

| # | Task | Commit | Key Files |
|---|------|--------|-----------|
| 1 | Protocol layer + GamePlayerS + PlayerGroup accessors | `fbb9d1df` | game_protocol.h, auth_protocol.h, player_group.h (×2), game_player_s.h |
| 2 | LootInstance enforcement + unit tests (TDD) | `172409b7` | loot_instance.h, loot_instance.cpp, loot_instance_test.cpp |
| 3 | World sync pipeline + realm OnSetLootMethod + creature death state | `81ebddc3` | world.h/.cpp, player.h/.cpp (realm+world), realm_connector.h/.cpp, creature_ai_death_state.cpp |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Used `m_manager` instead of `m_playerManager` in OnSetLootMethod**
- **Found during:** Task 3
- **Issue:** Plan referenced `m_playerManager` but the realm server `Player` class uses `m_manager` for its `PlayerManager&` reference
- **Fix:** Changed to `m_manager.GetPlayerByCharacterGuid()`
- **Files modified:** `src/realm_server/player.cpp`
- **Commit:** `81ebddc3`

**2. [Rule 2 - Missing accessor] Added `GetMembers()` to realm-server PlayerGroup**
- **Found during:** Task 3
- **Issue:** Plan assumed `PlayerGroup::GetMembers()` existed; it was not declared
- **Fix:** Added `const MembersByGUID& GetMembers() const { return m_members; }` to public section
- **Files modified:** `src/realm_server/player_group.h`
- **Commit:** `81ebddc3`

**3. [Rule 2 - Missing include] Added `quest_status_data.h` include to `game_player_s.h`**
- **Found during:** Task 2 TDD compilation
- **Issue:** `game_player_s.h` forward-declared `QuestStatusData` but didn't include its definition; triggered a compile error when including `game_player_s.h` in the test without the transitive include chain
- **Fix:** Added `#include "game_server/quest_status_data.h"` to `game_player_s.h`
- **Files modified:** `src/shared/game_server/objects/game_player_s.h`
- **Commit:** `172409b7`

**4. [Note] Protocol opcodes were already committed in a prior session**
- `SetLootMethod` and `PlayerGroupLootMethodChanged` were already present in their respective protocol headers from a prior commit (`b7001498` and `5202d51c`). Verified they were in place, no duplicate additions needed.

## TDD Execution

- **RED:** `loot_instance_test.cpp` written first — failed to compile (`C2661: no overloaded function takes 9 arguments`) confirming no constructor with LootMethod/lootMasterGuid params existed
- **GREEN:** Updated `loot_instance.h` and `loot_instance.cpp` — all 8 assertions in 3 test cases pass
- **Test cases:** LOOT-01..06 fully covered (MasterLoot reject/accept, RoundRobin reject/accept/rotation, FreeForAll)

## Pre-existing Test Failures (Out of Scope)

The following test failures existed before this plan and are unrelated to loot enforcement:
- `item_factory_test.cpp` lines 226, 247, 357 — 3 assertions
- `item_validator_test.cpp` line 352 — 1 assertion

These are pre-existing failures in unrelated tests. Logged to deferred-items.md.

## Known Stubs

None — all loot enforcement code is fully wired end-to-end. `GetLootMethod()`/`GetLootMasterGuid()` on `GamePlayerS` default to `FreeForAll`/`0` until changed by a `SetLootMethod` packet, which is the correct initial state.

## Self-Check: PASSED
