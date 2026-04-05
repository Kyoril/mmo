---
phase: 03-npc-world-object-interaction
plan: "02"
subsystem: client-protocol
tags: [protocol, client, world-objects, opcode]
dependency_graph:
  requires: []
  provides: [UseObject-opcode, client-UseObject-sender]
  affects: [game_protocol.h, realm_connector, player_controller]
tech_stack:
  added: []
  patterns: [sendSinglePacket-lambda, io::write<uint64>, Doxygen-triple-slash]
key_files:
  created: []
  modified:
    - src/shared/game_protocol/game_protocol.h
    - src/mmo_client/net/realm_connector.h
    - src/mmo_client/net/realm_connector.cpp
    - src/mmo_client/player_controller.cpp
decisions:
  - "UseObject placed at end of client_realm_packet enum (before Count_) to avoid disrupting existing opcode numbering"
  - "Removed ERR_CANT_OPEN path entirely — world objects no longer require a bound 'open' spell"
metrics:
  duration: "10 minutes"
  completed: "2026-04-05"
  tasks_completed: 2
  files_modified: 4
---

# Phase 03 Plan 02: UseObject Opcode — Client Protocol & PlayerController Summary

## One-liner

Added `game::client_realm_packet::UseObject` opcode and `RealmConnector::UseObject(uint64)` sender; replaced fragile spell-cast world object interaction in PlayerController with direct opcode call.

## What Was Built

### Task 1 — UseObject opcode + RealmConnector sender

- **`src/shared/game_protocol/game_protocol.h`**: Added `UseObject,` to the `client_realm_packet` namespace enum immediately before `Count_,`. This enables server-side dispatch registration (Plan 03-03).

- **`src/mmo_client/net/realm_connector.h`**: Declared `void UseObject(uint64 objectGuid)` with Doxygen `///` comment following the existing Loot/LootRelease pattern, placed after `LootRelease`.

- **`src/mmo_client/net/realm_connector.cpp`**: Implemented `RealmConnector::UseObject(uint64 objectGuid)` using the `sendSinglePacket` lambda pattern with `io::write<uint64>`, consistent with `Loot()` and `LootRelease()` implementations.

### Task 2 — PlayerController click handler replacement

- **`src/mmo_client/player_controller.cpp`**: Replaced the entire spell-cast body of the `else if (IsWorldObject && IsUsable)` branch. The old path fetched `GetOpenSpell()`, showed `ERR_CANT_OPEN` if absent, or cast the spell on the object. The new path calls `m_connector.UseObject(m_hoveredObject->GetGuid())` directly — no spell dependency.

## Deviations from Plan

None — plan executed exactly as written.

## Commits

| Hash | Message |
|------|---------|
| `b7001498` | feat(03-02): add UseObject opcode to protocol enum and implement RealmConnector::UseObject() |
| `a1a29925` | feat(03-02): replace fragile spell-cast world object interaction with UseObject() |

## Known Stubs

None. The client-side changes are complete. The server-side handler (Plan 03-03) is the next required piece to complete the full pipeline.

## Self-Check: PASSED

- `src/shared/game_protocol/game_protocol.h` modified — `UseObject,` present at line 245 ✓
- `src/mmo_client/net/realm_connector.h` modified — declaration at line 306 ✓
- `src/mmo_client/net/realm_connector.cpp` modified — implementation at line 600 ✓
- `src/mmo_client/player_controller.cpp` modified — `m_connector.UseObject(...)` at line 810 ✓
- `GetOpenSpell` fully removed ✓
- Commits `b7001498` and `a1a29925` exist in git log ✓
