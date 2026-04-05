---
phase: 05-group-guild-features
created: 2026-04-05
status: planning
---

# Phase 5 — Context & Decisions

## Phase Goal
Party gameplay works — invites, shared XP, group loot rules, and guild management.

## Scout Findings

### Group System (~85% complete)
- Full invite/accept/decline/leave/disband flow implemented server-side (realm_server/player.cpp)
- PartyFrame.lua exists with health/mana bars, leader indicator, context menu
- Shared XP distribution already implemented (creature_ai_death_state.cpp with group XP rate + level weighting)
- `SetLootMethod()` exists on PlayerGroup but zero enforcement logic — enum stored, ignored during looting
- Need/Greed rolling opcodes exist but registered as proxy packets (no implementation)
- `ConvertToRaidGroup()` exists server-side but no client wire-up

### Guild System (~90% complete)
- Full create/invite/accept/leave/disband/promote/demote/roster pipeline implemented
- Permission system (9 permission types), rank system, guild events broadcast all working
- GuildFrame.lua exists with roster display and event handling
- Guild MOTD: handler stub exists at player.cpp:3783 with TODO at line 3819 — not implemented
- Guild chat / Officer chat channels: permission flags defined but no chat routing wired

### Already Working (no changes needed)
- Group invite/accept/decline/leave/disband (fully implemented)
- Party UI — PartyFrame.lua health/mana bars
- Shared XP on kill (creature_ai_death_state.cpp)
- Guild management (create, invite, accept, leave, promote, demote, roster, events)
- GuildFrame.lua (roster display, events)

## Grey Area Decisions

| # | Question | Decision |
|---|----------|----------|
| 1 | How should the group leader change the loot method? | **Both**: slash command (`/lootmethod`) AND right-click party frame dropdown menu |
| 2 | Include raid group conversion (ConvertToRaidGroup) in Phase 5? | **No** — Phase 5 is party-focused; raid conversion deferred |
| 3 | Complete Guild MOTD TODO? | **Yes** — implement storage + broadcast on roster; handler stub already exists at player.cpp:3819 |

## Implementation Scope

### What Phase 5 will build

1. **Loot Method Enforcement** (server + client)
   - Server: Wire actual enforcement in LootInstance/TakeItem based on stored loot method
     - FreeForAll: any recipient can take any item (already de-facto behavior — confirm it)
     - RoundRobin: items pre-assigned to recipients in rotation when creature dies; only assigned player can take each item slot
     - MasterLoot: only the loot master (`GetLootMasterGuid()`) can click any item slot
   - Client: `/lootmethod freeforall|roundrobin|masterloot [player]` slash command
   - Client: Right-click party leader frame → dropdown with loot method options

2. **Guild MOTD** (server + client)
   - Server: Store MOTD string on Guild object; broadcast MOTD in `GuildEvent::Motd` packet; display on login and roster open
   - Client: GuildFrame.lua MOTD display field; `/gmotd <text>` slash command routes to existing GuildMotd opcode

3. **Group/Guild UI Polish** (verify existing UI works end-to-end)
   - Verify group invite notification displays in PartyFrame/ChatFrame
   - Verify guild join/leave/promote/demote events show in chat
   - Fix any broken event handlers found in scout

## Out of Scope
- Raid group conversion (deferred)
- Need/Greed rolling UI (deferred to Phase 7 Loot & Economy)
- Master loot item-distribution UI (MasterLoot just means only the master can click — no separate give-item dialog)
- Guild chat / Officer chat channel routing (Phase 4 covers `/guild` chat; officer-only channel deferred)
