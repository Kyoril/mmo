---
phase: 03-npc-world-object-interaction
plan: "01"
subsystem: npc-ui
tags: [lua, trainer, gossip, vendor, events, npc-interaction]
dependency_graph:
  requires: []
  provides: [TRAINER_UPDATE-handler, GOSSIP_SHOW-event, GOSSIP_CLOSED-event, trainer-title-binding, NPC-name-in-gossip-header]
  affects: [TrainerFrame, QuestFrame, VendorFrame, quest_client, trainer_client, game_script]
tech_stack:
  added: []
  patterns: [Lua-event-handler, luabind-def-lambda, io-write-dynamic-range, submodule-commit]
key_files:
  created: []
  modified:
    - data/client/Interface/GameUI/TrainerFrame.lua
    - data/client/Interface/GameUI/QuestFrame.lua
    - src/mmo_client/systems/trainer_client.h
    - src/mmo_client/systems/trainer_client.cpp
    - src/mmo_client/game_script.cpp
    - src/world_server/player.cpp
decisions:
  - "Trainer title added to TrainerList packet (server sends title field from proto, client stores in m_trainerTitle)"
  - "TRAINER_UPDATE fires after BuyTrainerSpell succeeds or fails — C++ event was never fired before (auto-fix)"
  - "GOSSIP_SHOW fires in addition to (not instead of) QUEST_GREETING for pure gossip menus"
  - "GOSSIP_CLOSED fires in OnGossipComplete before CloseQuest — keeps QUEST_FINISHED separate"
metrics:
  duration_minutes: 30
  completed_date: "2026-04-05"
  tasks_completed: 3
  tasks_total: 3
  files_modified: 6
---

# Phase 3 Plan 1: NPC UI Event Wiring Summary

**One-liner:** Wired TRAINER_UPDATE/GOSSIP_SHOW/GOSSIP_CLOSED Lua events, fixed TrainerFrame refresh handler, added NPC name to gossip header, and plumbed trainer title field through packet + Lua binding.

## Objective

Fix three concrete NPC UI gaps: (1) `TrainerFrame_OnTrainerUpdate` was an empty function body, (2) QuestFrame gossip header never showed the NPC's name, (3) GOSSIP_SHOW/GOSSIP_CLOSED events were not fired from quest_client.cpp. Additionally verify VendorFrame sell path and implement the trainer-type title locked decision.

## Tasks Completed

| # | Task | Commit | Files |
|---|------|--------|-------|
| 1 | Fix TrainerFrame_OnTrainerUpdate + trainer title | d8d07d18 | TrainerFrame.lua, trainer_client.h/.cpp, game_script.cpp, player.cpp |
| 2 | Add NPC name to QuestFrame gossip header | fa2e44b9 | QuestFrame.lua |
| 3 | Fire GOSSIP_SHOW and GOSSIP_CLOSED events | 8fbe0519 | quest_client.cpp |

## Decisions Made

1. **Trainer title in TrainerList packet** — The server-side `SendTrainerList` previously sent only `trainerGuid` and `spellCount`. Added `io::write_dynamic_range<uint8>(trainer.title())` to the packet, and the client reads it via `io::read_container<uint8>(trainerTitle)`. Stored in `TrainerClient::m_trainerTitle`, exposed via `GetTrainerTitle()` getter and Lua binding.

2. **TRAINER_UPDATE C++ event was never fired** — The Lua frame registered `TRAINER_UPDATE → TrainerFrame_OnTrainerUpdate`, but `TrainerClient::OnTrainerBuySucceeded` and `OnTrainerBuyError` never fired it. Fixed by adding `FrameManager::Get().TriggerLuaEvent("TRAINER_UPDATE")` to both handlers (Rule 1 auto-fix).

3. **GOSSIP_SHOW coexists with QUEST_GREETING** — For pure gossip menus (`showQuests == false`), both events fire. Quest givers only receive QUEST_GREETING. This follows Research Pitfall 5 and the CONTEXT.md locked decision.

4. **GOSSIP_CLOSED fires before CloseQuest** — The existing `CloseQuest()` fires `QUEST_FINISHED`. Added `GOSSIP_CLOSED` before `CloseQuest()` in `OnGossipComplete` so both events fire in the right order.

## Verification Results

| Check | Result |
|-------|--------|
| `TrainerList_Update(TrainerFrame)` inside `TrainerFrame_OnTrainerUpdate` | ✅ Line 12 |
| `TrainerFrame_OnLoad` registers all 6 events | ✅ Lines 119-124 |
| `TrainerBuyButton_OnClick` calls `BuyTrainerSpell` | ✅ Line 21 |
| `GetTrainerTitle()` Lua binding exists | ✅ game_script.cpp line 1156 |
| `TrainerFrame_OnTrainerShow` uses `GetTrainerTitle()` | ✅ TrainerFrame.lua line 4 |
| `QuestFrame_OnQuestGreeting` sets `target:GetName()` in title | ✅ Line 50 |
| `GossipActionButton_OnClick` calls `GossipAction(self.id)` | ✅ Line 43 |
| `GossipActionList` populated from `GetNumGossipActions()` | ✅ Lines 95-102 |
| `VendorFrame` has `BuyVendorItem` and `VENDOR_SHOW` | ✅ Lines 94, 128 |
| `SellItem` sell path exists in game_script.cpp | ✅ Line 1445 via UseContainerItem |
| `TriggerLuaEvent("GOSSIP_SHOW")` gated on `!showQuests` | ✅ Line 551 |
| `TriggerLuaEvent("GOSSIP_CLOSED")` in OnGossipComplete | ✅ Line 965 |
| `TriggerLuaEvent("QUEST_GREETING")` not removed | ✅ Lines 545, 623 |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] TRAINER_UPDATE event never fired from C++**
- **Found during:** Task 1
- **Issue:** `TrainerClient::OnTrainerBuySucceeded` and `OnTrainerBuyError` fired `TRAINER_BUY_SUCCEEDED`/`TRAINER_BUY_ERROR` but never fired `TRAINER_UPDATE`. The Lua frame registered for `TRAINER_UPDATE` but it never triggered, making the empty handler fix insufficient alone.
- **Fix:** Added `FrameManager::Get().TriggerLuaEvent("TRAINER_UPDATE")` to both `OnTrainerBuySucceeded` and `OnTrainerBuyError` in `trainer_client.cpp`.
- **Files modified:** `src/mmo_client/systems/trainer_client.cpp`
- **Commit:** d8d07d18

**2. [Rule 2 - Missing Functionality] Trainer title not included in TrainerList packet**
- **Found during:** Task 1
- **Issue:** CONTEXT.md locked decision requires trainer entry title in window header. The TrainerList packet sent only `trainerGuid + spellCount + spells`. The client had no way to obtain the trainer's title string.
- **Fix:** Added title to packet (server `SendTrainerList` → `io::write_dynamic_range<uint8>(trainer.title())`), added `m_trainerTitle` to `TrainerClient`, added `GetTrainerTitle()` Lua binding in `game_script.cpp`, updated `TrainerFrame_OnTrainerShow` to call `GetTrainerTitle()`.
- **Files modified:** `src/world_server/player.cpp`, `src/mmo_client/systems/trainer_client.h`, `src/mmo_client/systems/trainer_client.cpp`, `src/mmo_client/game_script.cpp`, `data/client/Interface/GameUI/TrainerFrame.lua`
- **Commit:** d8d07d18

## Known Stubs

None — all modified paths produce real data. The sell path works via `UseContainerItem` → `VendorClient::SellItem`. The trainer title falls back to empty string if the proto entry has no title set (intentional: no crash, just no title text shown).

## Self-Check: PASSED

Files exist:
- `data/client/Interface/GameUI/TrainerFrame.lua` ✅
- `data/client/Interface/GameUI/QuestFrame.lua` ✅
- `src/mmo_client/systems/trainer_client.h` ✅
- `src/mmo_client/systems/trainer_client.cpp` ✅
- `src/mmo_client/game_script.cpp` ✅
- `src/world_server/player.cpp` ✅
- `src/mmo_client/systems/quest_client.cpp` ✅

Commits exist:
- d8d07d18 ✅
- fa2e44b9 ✅
- 8fbe0519 ✅
