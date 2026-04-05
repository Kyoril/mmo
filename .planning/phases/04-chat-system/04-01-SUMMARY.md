---
phase: 04-chat-system
plan: "01"
subsystem: realm_server/chat
tags: [chat, whisper, raid, packet-parsing, enum]
dependency_graph:
  requires: []
  provides: [ChatType::System, whisper-routing, raid-chat-broadcast]
  affects: [src/shared/game/chat_type.h, src/realm_server/player.cpp]
tech_stack:
  added: []
  patterns: [packet-handler, broadcast-pattern, system-error-response]
key_files:
  created: []
  modified:
    - src/shared/game/chat_type.h
    - src/realm_server/player.cpp
decisions:
  - "Use io::read_container<uint8> (length-prefixed) to parse whisper target name — matches client wire format (realm_connector.cpp:856)"
  - "Return PacketParseResult::Pass early on failed whisper to skip DB log — not logging offline-target attempts is intentional"
  - "Whisper error uses ChatType::System with packed GUID 0 — consistent with how server-generated messages are structured"
metrics:
  duration: "15 minutes"
  completed: "2026-04-05"
  tasks: 2
  files_modified: 2
---

# Phase 04 Plan 01: Whisper and Raid Chat Routing Summary

**One-liner:** Fixed whisper target-name buffer corruption and wired whisper/raid routing with ChatType::System error feedback in the realm server chat handler.

## What Was Built

### `src/shared/game/chat_type.h`
- Appended `ChatType::System` (integer value **13**) after `UnitEmote` — the last existing value
- Added Doxygen doc comment following project conventions
- No existing enum values were reordered; wire protocol remains intact

### `src/realm_server/player.cpp` — `OnChatMessage()`

**Change 1 — Target-name parse fix (buffer corruption eliminated):**
- Added `std::string targetName` declaration alongside `chatType` and `message`
- After reading `chatType` + `message`, conditionally reads `targetName` via `io::read_container<uint8>` when `chatType` is `Whisper` or `Channel`
- Previously, the Whisper target bytes sat unread in the buffer, corrupting subsequent field reads

**Change 2 — Whisper case (new):**
- Looks up `targetName` via `m_manager.GetPlayerByCharacterName(targetName)`
- **Target found:** sends whisper packet to target via `target->SendPacket()` (preserves GameCrypt encryption)
- **Target not found:** sends a `ChatType::System` error packet back to sender ("No player named '...' is currently online.") and returns `PacketParseResult::Pass` early — intentionally skips the DB log for failed whisper attempts

**Change 3 — Raid case (stub replaced):**
- Replaced `WLOG("Raid chat is not implemented yet!")` with a full group broadcast
- Pattern mirrors the existing `ChatType::Group` case exactly
- `m_group` null check guards against non-grouped players sending raid chat

## Commit Hashes

| Task | Commit | Description |
|------|--------|-------------|
| 1 | fc5c83dc | feat(04-01): add ChatType::System enum value to chat_type.h |
| 2 | 21802b76 | feat(04-01): fix OnChatMessage whisper parsing and routing, implement Raid case |

## Deviations from Plan

None — plan executed exactly as written.

## Known Stubs

None — `ChatType::Channel` remains a WLOG stub, but that was pre-existing and out of scope for this plan.

## Self-Check: PASSED

- `src/shared/game/chat_type.h` — `System,` present on line 49, after `UnitEmote,` on line 46 ✓
- `src/realm_server/player.cpp` — `case ChatType::Whisper:` (line 794), `GetPlayerByCharacterName` (line 802), `ChatType::System` (line 812), `case ChatType::Raid:` (line 835), `BroadcastPacket` (line 841) ✓
- Commits `fc5c83dc` and `21802b76` present in git log ✓
- `cmake --build build --target realm_server` — no errors ✓
