---
phase: 04-chat-system
plan: "03"
subsystem: mmo_client/chat
tags: [chat, system-message, whisper, raid, name-cache, lua-events]
dependency_graph:
  requires: [04-01]
  provides: [ChatType::System-guard, RAID-type-string, CHAT_MSG_WHISPER_INFORM]
  affects:
    - src/mmo_client/game_states/world_state.cpp
    - src/mmo_client/game_script.cpp
tech_stack:
  added: []
  patterns: [early-return-guard, lua-event-fire, type-string-mapping]
key_files:
  created: []
  modified:
    - src/mmo_client/game_states/world_state.cpp
    - src/mmo_client/game_script.cpp
decisions:
  - "System guard placed as first statement inside else branch so UnitSay/UnitEmote/UnitYell path is unaffected"
  - "WHISPER_INFORM fires after m_realmConnector.SendChatMessage so invalid whispers (no target) never trigger the event"
  - "target and message passed as std::string() to TriggerLuaEvent to guard against null const char* dereference"
metrics:
  duration: "10 minutes"
  completed: "2026-04-05"
  tasks: 2
  files_modified: 2
---

# Phase 04 Plan 03: Client Chat Routing Fixes Summary

**One-liner:** Guarded GUID-0 system messages from name-cache lookup, mapped RAID type string, and fired local CHAT_MSG_WHISPER_INFORM after whisper sends in the client C++ layer.

## What Was Built

### Task 1 — `src/mmo_client/game_states/world_state.cpp`

**System message early-return guard in `WorldState::OnChatMessage()`:**

Inserted as the **first statement inside the `else` branch** (line 1616–1621), immediately before `m_cache.GetNameCache().Get(...)`:

```cpp
// System messages (GUID 0) do not require a name lookup
if (type == ChatType::System)
{
    FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SYSTEM", message);
    return PacketParseResult::Pass;
}
```

- The `UnitSay`/`UnitEmote`/`UnitYell` path (the `if` branch above) is completely unaffected.
- The guard fires `CHAT_MSG_SYSTEM` with the raw message string and returns early — no name cache call is made for GUID 0.
- All other chat types (SAY, YELL, PARTY, GUILD, WHISPER, RAID) continue through `GetNameCache().Get()` as before.

**Name cache call confirmed still present:** line 1623 (`m_cache.GetNameCache().Get(...)`), now guarded by the System early-return above it.

### Task 2 — `src/mmo_client/game_script.cpp`

**Change 1 — RAID entry added to `s_typeStrings[]` (line 1297):**

```cpp
{"RAID", ChatType::Raid}
```

Appended before the closing `};` of the static array. Lua calls to `SendChatMessage(msg, "RAID")` now correctly map to `ChatType::Raid` instead of falling through to the `Unknown` type error.

**Change 2 — `CHAT_MSG_WHISPER_INFORM` fired after whisper send (lines 1330–1335):**

```cpp
if (chatType == ChatType::Whisper)
{
    FrameManager::Get().TriggerLuaEvent("CHAT_MSG_WHISPER_INFORM",
        std::string(target ? target : ""),
        std::string(message));
}
```

Placed immediately after `m_realmConnector.SendChatMessage(...)`. The existing early-return for whispers with no target (line 1316–1320) ensures this block only runs for valid whispers.

## Commit Hashes

| Task | Commit     | Description                                                  |
|------|------------|--------------------------------------------------------------|
| 1    | `5cd35119` | feat(04-03): add ChatType::System early-return guard in OnChatMessage |
| 2    | `f54cb64d` | feat(04-03): add RAID type string and fire CHAT_MSG_WHISPER_INFORM    |

## Build Status

`cmake --build build --target mmo_client` — **no errors, exit code 0**.

## Verification Results

| Check | Result |
|-------|--------|
| `ChatType::System` in world_state.cpp (line 1617) | ✓ 1 hit, before GetNameCache (line 1623) |
| `CHAT_MSG_SYSTEM` TriggerLuaEvent fires on System guard | ✓ |
| `GetNameCache` still present after guard | ✓ line 1623 |
| `{"RAID", ChatType::Raid}` in s_typeStrings[] | ✓ line 1297 |
| `CHAT_MSG_WHISPER_INFORM` TriggerLuaEvent call | ✓ line 1332 |
| WHISPER_INFORM after SendChatMessage (lower line number) | ✓ 1328 < 1332 |
| WHISPER mapping in world_state.cpp switch | ✓ line 1632 |
| RAID mapping in world_state.cpp switch | ✓ line 1633 |
| Client build clean | ✓ no errors |

## Deviations from Plan

None — plan executed exactly as written.

## Known Stubs

None — all three gaps documented in the plan objective are fully resolved.

## Self-Check: PASSED

- `src/mmo_client/game_states/world_state.cpp` — `ChatType::System` guard at line 1617, `GetNameCache` at line 1623 ✓
- `src/mmo_client/game_script.cpp` — `{"RAID", ChatType::Raid}` at line 1297, `CHAT_MSG_WHISPER_INFORM` at line 1332 ✓
- Commits `5cd35119` and `f54cb64d` present in git log ✓
- `cmake --build build --target mmo_client` — no errors ✓
