---
phase: 05-group-guild-features
plan: "03"
subsystem: client-loot-ui
tags: [loot, group, client, lua, slash-command, context-menu, party-frame]
dependency_graph:
  requires: [05-01]
  provides: [SetGroupLootMethod-client-send, PARTY_LOOT_METHOD_CHANGED-event, loot-method-slash-command, loot-method-context-menu]
  affects: [mmo_client, data_client_submodule]
tech_stack:
  added: []
  patterns: [luabind-def-binding, sendSinglePacket-opcode-send, RegisterContextMenu-submenu, PARTY_LOOT_METHOD_CHANGED-event-fire]
key_files:
  created: []
  modified:
    - src/mmo_client/net/realm_connector.h
    - src/mmo_client/net/realm_connector.cpp
    - src/mmo_client/game_script.cpp
    - src/mmo_client/systems/party_info.cpp
    - data/client/Interface/GameUI/SlashCommandStrings.lua
    - data/client/Interface/GameUI/ChatFrame.lua
    - data/client/Interface/GameUI/ContextMenu.lua
    - data/client/Interface/GameUI/PartyFrame.lua
    - data/client/Locales/Locale_enUS/Localization.txt
    - data/client/Locales/Locale_deDE/Localization.txt
    - data/client/Locales/Locale_frFR/Localization.txt
    - data/client/Locales/Locale_ruRU/Localization.txt
decisions:
  - "Locale files are in data/client/Locales/ (not data/client/Locale/ as the plan assumed); directory names use Locale_XX suffix"
  - "Fourth locale is ruRU not koKR; ruRU uses English placeholder strings (consistent with existing ruRU pattern)"
  - "data/client is a git submodule — Lua/locale changes committed to submodule first, then parent repo reference updated"
  - "PartyMemberFrame_OnLootMethodChanged() calls PartyMemberFrame_UpdateMember(self) for refresh; no loot-method label widget exists in the current PartyFrame.xml"
  - "m_lootMethod stored as LootMethod enum in party_info.h; cast uint8 packet read to LootMethod via static_cast before assignment"
metrics:
  duration_minutes: 25
  tasks_completed: 2
  tasks_total: 2
  files_created: 0
  files_modified: 12
  completed_date: "2026-04-05"
requirements: [GRP-03]
---

# Phase 5 Plan 03: Client Loot Method UI Summary

Client-side loot method pipeline end-to-end: `RealmConnector::SetGroupLootMethod()` sends `SetLootMethod` opcode → `GetLootMethod()`/`SetLootMethod()` Lua bindings → `/lootmethod` slash command + right-click party menu → `PARTY_LOOT_METHOD_CHANGED` event fires on group list update → 2-person group bug fixed.

## Tasks Completed

| # | Task | Commit | Key Files |
|---|------|--------|-----------|
| 1 | C++ client layer — party_info fix + RealmConnector send + Lua bindings | `c77dc9f4` | realm_connector.h/.cpp, game_script.cpp, party_info.cpp |
| 2 | Lua UI — slash command, context menu, PartyFrame handler, localization | `a04cbd7` (submodule) + `dc15be7f` (parent) | SlashCommandStrings.lua, ChatFrame.lua, ContextMenu.lua, PartyFrame.lua, 4× Localization.txt |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] data/client is a git submodule**
- **Found during:** Task 2 commit
- **Issue:** `git add data/client/Interface/...` fatal — pathspec in submodule. The plan treated these as ordinary repo files.
- **Fix:** Committed Lua/locale changes inside the `data/client` submodule first (`a04cbd7`), then updated the parent repo's submodule reference (`dc15be7f`).
- **Files modified:** data/client (submodule pointer)
- **Commit:** `dc15be7f`

**2. [Rule 1 - Bug] Locale directory path differs from plan**
- **Found during:** Task 2 pre-check
- **Issue:** Plan specified `data/client/Locale/{enUS,...}/Localization.txt` but actual paths are `data/client/Locales/Locale_enUS/Localization.txt`
- **Fix:** Used correct paths throughout.
- **Files modified:** All 4 Localization.txt files

**3. [Rule 1 - Bug] Fourth locale is ruRU, not koKR**
- **Found during:** Task 2 pre-check
- **Issue:** Plan listed `koKR` as 4th locale. Actual repo has `Locale_ruRU`.
- **Fix:** Added strings to `Locale_ruRU/Localization.txt` with English placeholder values (matches existing ruRU pattern — ruRU uses English stubs throughout).
- **Files modified:** `data/client/Locales/Locale_ruRU/Localization.txt`

**4. [Rule 2 - Missing] LEVEL_FORMAT entry preserved in ruRU after edit**
- **Found during:** Task 2 ruRU localization edit
- **Issue:** First edit attempt inadvertently consumed the `LEVEL_FORMAT` line. Detected immediately via file inspection.
- **Fix:** Re-inserted `(key = "LEVEL_FORMAT", string = "Level %d")` between the new loot strings and the guild section.
- **Files modified:** `data/client/Locales/Locale_ruRU/Localization.txt`

## Known Stubs

- `PartyMemberFrame_OnLootMethodChanged(self)` calls `PartyMemberFrame_UpdateMember(self)` but does not update a loot method label widget because no such widget exists in `PartyFrame.xml`. The handler is fully wired to the `PARTY_LOOT_METHOD_CHANGED` event; a future plan adding a loot method label to `PartyFrame.xml` will complete the visual display.

## Self-Check: PASSED
