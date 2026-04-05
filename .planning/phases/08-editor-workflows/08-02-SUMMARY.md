---
plan_id: 08-02
phase: 08
title: World Object Per-Spawn Loot & Trigger Config
subsystem: game_server/world, mmo_edit
tags: [world-objects, loot, spawner, editor, protobuf]
dependency_graph:
  requires: []
  provides: [per-spawn-loot-override, per-spawn-trigger-id-field]
  affects: [world_object_spawner, game_world_object_s, world_instance, mmo_edit]
tech_stack:
  added: []
  patterns: [per-spawn override injected at spawner construction time, proto optional fields with default=0]
key_files:
  created: []
  modified:
    - src/shared/proto_data/maps.proto
    - src/shared/game_server/objects/game_world_object_s.h
    - src/shared/game_server/objects/game_world_object_s.cpp
    - src/shared/game_server/world/world_object_spawner.h
    - src/shared/game_server/world/world_object_spawner.cpp
    - src/shared/game_server/world/world_instance.cpp
    - src/mmo_edit/editors/world_editor/world_editor_instance.cpp
decisions:
  - "Override injected at WorldObjectSpawner construction time (not post-construction) because SpawnOne() runs inside the ctor"
  - "trigger_id proto field added; runtime wiring deferred as TODO comment in game_world_object_s.cpp"
  - "Editor UI matches existing InputScalar style (no table layout) to stay consistent with Visit(SelectedObjectSpawn)"
metrics:
  duration: ~10 minutes
  completed: 2025-01-01
  tasks: 7
  files_changed: 7
---

# Phase 08 Plan 02: World Object Per-Spawn Loot & Trigger Config Summary

## One-liner
Per-spawn loot_entry/trigger_id fields in ObjectSpawnEntry proto, override wired through WorldObjectSpawner into GameWorldObjectS.OnUse, with editor UI inputs in the object spawn details panel.

## What Was Built

### Data Layer (maps.proto)
Added two new optional fields to `ObjectSpawnEntry`:
- `loot_entry = 11 [default = 0]` — overrides the base `ObjectEntry.objectlootentry` for this specific spawn
- `trigger_id = 12 [default = 0]` — reserved for future per-spawn trigger override (proto field only)

Both fields default to 0, making this a fully backwards-compatible change.

### Runtime Layer (game_world_object_s)
- Added `uint32 m_lootEntryOverride = 0` member
- Added `SetLootEntryOverride(uint32)` setter (inline, Doxygen documented)
- `Use()` → chest case now resolves loot entry as:
  ```cpp
  const uint32 lootEntryId = (m_lootEntryOverride != 0) ? m_lootEntryOverride : m_entry.objectlootentry();
  ```
- Guard check updated: `lootEntryId == 0` (covers both no-override and no-base-entry cases)
- TODO comment added for future `trigger_id` runtime wiring

### Spawn Pipeline (world_object_spawner + world_instance)
- `WorldObjectSpawner` constructor extended with `uint32 lootEntryOverride = 0` (default = backwards compatible)
- `m_lootEntryOverride` member stored in initializer list
- `SpawnOne()` calls `spawned->SetLootEntryOverride(m_lootEntryOverride)` when non-zero
- `world_instance.cpp` passes `spawn.loot_entry()` as the new trailing argument

### Editor UI (world_editor_instance.cpp)
In `Visit(SelectedObjectSpawn&)`, after Animation Progress:
- **Loot Entry** — `ImGui::InputScalar` for `uint32`, persisted via `set_loot_entry()`
- **Base trigger count** — read-only `ImGui::TextDisabled("Base: %d trigger(s)", ...)` from `ObjectEntry`
- **Trigger ID** — `ImGui::InputScalar` for `uint32`, persisted via `set_trigger_id()`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing guard] Updated loot guard to cover override case**
- **Found during:** Task 3 (game_world_object_s.cpp)
- **Issue:** Original guard checked `m_entry.objectlootentry() == 0` but with an override the entry could be 0 while override is non-zero; restructured to compute `lootEntryId` first then check it.
- **Fix:** Merged loot entry resolution and guard into a single block.
- **Files modified:** `game_world_object_s.cpp`
- **Commit:** e0dc2cd1

**2. [Rule 1 - Convention] Editor UI uses InputScalar not table layout**
- **Found during:** Task 7 (world_editor_instance.cpp)
- **Issue:** Plan showed `ImGui::TableNextRow/TableNextColumn` but the existing `Visit(SelectedObjectSpawn&)` uses flat `InputScalar` calls with no table context — using table calls outside a table would be undefined behavior.
- **Fix:** Used `ImGui::InputScalar` matching the existing pattern. Also removed `MarkDirty()` (does not exist in this codebase).
- **Files modified:** `world_editor_instance.cpp`
- **Commit:** e0dc2cd1

## Known Stubs

| File | Location | Description |
|------|----------|-------------|
| `game_world_object_s.cpp` | chest case in `Use()` | `trigger_id` runtime wiring is a TODO — proto field and editor UI exist but the server does not yet fire a trigger on object use |

## Self-Check: PASSED

All 7 modified source files present. Commit `e0dc2cd1` verified in git log. Build completed with 0 errors (pre-existing warnings in unrelated files only).
