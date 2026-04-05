---
phase: 07-loot-economy
created: 2026-04-05
status: planning
---

# Phase 7 — Context & Decisions

## Phase Goal
Creatures drop loot, world objects provide loot, vendors provide basic economy.

## Scout Findings — Already Working (~90%)

- Creature loot: fully implemented (LootInstance, death state, loot opcodes, loot handlers, client LootFrame UI)
- Vendor buy/sell: fully implemented (OnBuyItem, OnSellItem, VendorFrame.lua)
- Money system: fully implemented (object_fields::Money, ConsumeMoney, overflow protection)
- Group loot methods (Phase 5): fully implemented
- LootMoneyNotify server broadcast: already sent to group members on gold loot
- Lua handlers for loot notifications: ALREADY EXIST (ChatFrame_OnLootItemReceived, ChatFrame_OnMemberLootItemReceived, LOOT_ITEM_RECEIVED, MEMBER_LOOT_ITEM_RECEIVED events, ChatTypeInfo["LOOT"])
- Localization strings: YOU_RECEIVE_LOOT_SINGLE/MULTI, MEMBER_RECEIVE_LOOT_SINGLE/MULTI all exist in enUS

## Grey Area Decisions

| # | Question | Decision |
|---|----------|----------|
| 1 | Should world object chests drop gold? | **Yes** — use loot entry minmoney/maxmoney |
| 2 | Should group members see loot notifications? | **Yes** — implement LootItemNotify + LootMoneyNotify |

## Real Gaps — Phase 7 Implementation

### Gap 1: World Object Gold
`game_world_object_s.cpp` passes `0, 0` for minmoney/maxmoney when constructing LootInstance for chests.
Fix: read `lootEntry->minmoney()` / `lootEntry->maxmoney()` and pass them.

### Gap 2: LootItemNotify server broadcast
`player_inventory_handlers.cpp OnAutoStoreLootItem()` — after `m_loot->TakeItem()`, send `LootItemNotify`
to in-sight group members with: player name (uint8 len string) + item ID (uint32) + quality (uint8) + count (uint8).

### Gap 3: Client LootItemNotify parsing
`loot_client.cpp OnLootItemNotify()` is a DLOG stub. Parse: member name + item ID + quality + count.
Look up item name from item manager. Fire `MEMBER_LOOT_ITEM_RECEIVED` Lua event with
(memberName, itemName, itemId, quality, count). The Lua handler already exists in ChatFrame.lua.

### Gap 4: Client LootMoneyNotify display
`loot_client.cpp OnLootMoneyNotify()` is a DLOG stub. Fire `LOOT_MONEY_NOTIFY` Lua event with gold amount.
Add `ChatFrame_OnLootMoneyNotify` handler in ChatFrame.lua to show "You receive X gold." message.
Add LOOT_MONEY_NOTIFY locale string to all 4 locales.

## Files to Change
- `src/shared/game_server/objects/game_world_object_s.cpp` — pass minmoney/maxmoney to LootInstance
- `src/world_server/player_inventory_handlers.cpp` — broadcast LootItemNotify to group
- `src/mmo_client/systems/loot_client.cpp` — parse LootItemNotify + fire MEMBER_LOOT_ITEM_RECEIVED; fire LOOT_MONEY_NOTIFY
- `data/client/Interface/GameUI/ChatFrame.lua` — add LOOT_MONEY_NOTIFY handler
- `data/client/Locales/*/Localization.txt` (4 files) — add LOOT_MONEY_NOTIFY string
