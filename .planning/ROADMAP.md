# Roadmap: MMO Project

**Created:** 2026-03-31
**Milestone:** v1 — Game Systems Polish & Playable Loop
**Phases:** 8
**Granularity:** Standard

## Overview

| # | Phase | Goal | Requirements | Success Criteria |
|---|-------|------|--------------|------------------|
| 1 | Trigger System Completion | All trigger actions functional, world objects scriptable | TRIG-01, TRIG-02, TRIG-03 | 3 |
| 2 | Quest System Polish | End-to-end quest flow from discovery to reward | QUEST-01..06 | 4 |
| 3 | NPC & World Object Interaction | Vendors, trainers, gossip, and usable world objects | NPC-01..04, WOBJ-01..03 | 4 |
| 4 | Chat System | All chat channels functional between players | CHAT-01..05 | 3 |
| 5 | Group & Guild Features | Party gameplay, loot sharing, guild management | GRP-01..05 | 4 |
| 6 | Character Progression | XP gain, leveling, ability learning | PROG-01..04 | 3 |
| 7 | Loot & Economy | Creature loot, world object loot, vendor economy | LOOT-01..05 | 4 |
| 8 | Editor Content Workflows | Editor tooling for spawns, triggers, quests | EDIT-01..04 | 3 |

## Phase Details

### Phase 1: Trigger System Completion

**Goal:** Complete all unimplemented trigger actions so scripted content (events, encounters, world interactions) works end-to-end.

**Requirements:** TRIG-01, TRIG-02, TRIG-03

**Why first:** Triggers are the foundation for quest events, NPC scripting, and world object behavior. Later phases depend on triggers working reliably.

**Plans:** 3 plans

Plans:
- [x] 01-01-PLAN.md — Infrastructure: object fields, world object spawner methods, combat movement flag
- [x] 01-02-PLAN.md — All 7 trigger action implementations
- [x] 01-03-PLAN.md — Server-side Lua event hook system

**Success criteria:**
1. All TODO trigger actions in `trigger_handler.cpp` are implemented and tested — SetWorldObjectState changes door/object state, SetVirtualEquipmentSlot alters NPC visuals, SetPhase moves objects between phases, Dismount/SetMount work, SetSpawnState works for ObjectSpawner, SetCombatMovement enables/disables movement
2. World objects respond to trigger-based state changes (a trigger can open a door, activate an object, or spawn a chest)
3. Lua scripting hooks exist for quest events, NPC dialog customization, and world object use

**Depends on:** Nothing (foundational)

---

### Phase 2: Quest System Polish

**Goal:** Deliver a complete quest experience from NPC quest icon discovery through objective tracking to reward collection.

**Requirements:** QUEST-01..06

**Why after Phase 1:** Quest events fire triggers (accept, complete, fail). Trigger system must be solid first.

**Success criteria:**
1. Quest giver NPCs display correct status icons (!, ?) on the client based on quest availability and completion state
2. Quest log UI shows active quests with objectives, descriptions, and real-time progress updates
3. Quest objectives track kill counts and item collection in real-time, updating the client immediately
4. Quest chains function end-to-end — completing a quest unlocks the next in the chain, turn-in rewards (items, XP, gold, spells) are applied correctly

**Depends on:** Phase 1 (triggers for quest events)

---

### Phase 3: NPC & World Object Interaction

**Goal:** Players can buy from vendors, learn from trainers, navigate gossip menus, and interact with world objects.

**Requirements:** NPC-01..04, WOBJ-01..03

**Why after Phase 2:** Some vendors/trainers are quest-gated. Quest infrastructure must work first.

**Success criteria:**
1. Vendor buy/sell interface works — player can browse items, buy items (gold deducted, item added to inventory), and sell items (item removed, gold added)
2. Trainer interface lists available abilities — player can learn abilities that meet level requirements (gold deducted, spell granted)
3. Gossip menus support multi-page dialog and route to vendor/trainer/quest interfaces correctly
4. World objects (doors, chests) respond to player Use action — quest-gated objects enforce requirements, loot-bearing objects open loot window

**Depends on:** Phase 2 (quest-gated objects)

---

### Phase 4: Chat System

**Goal:** Players can communicate through all chat channels (say, yell, group, guild, whisper).

**Requirements:** CHAT-01..05

**Why here:** Chat is needed for group coordination (Phase 5) and is independent of combat/loot systems.

**Success criteria:**
1. Say/Yell messages are visible to nearby players at appropriate ranges, displayed in chat window with sender name
2. Group/Raid chat messages are delivered to all party/raid members regardless of location
3. Whisper messages reach the target player by name across world instances, with "player not found" feedback if offline

**Depends on:** Nothing directly, but placed here for logical flow before group content

---

### Phase 5: Group & Guild Features

**Goal:** Party gameplay works — invites, shared XP, group loot rules, and guild management.

**Requirements:** GRP-01..05

**Why after Phase 4:** Group play needs chat for coordination. Loot distribution (Phase 7) builds on group structure.

**Success criteria:**
1. Player can create/join groups via invite — invite notification shown, accept/decline works, group shows in UI
2. XP from creature kills is shared among nearby group members using distance-based sharing formula
3. Guild roster correctly shows members, ranks, online/offline status — leader can promote/demote/invite/remove
4. Group loot rules are configurable (free-for-all, round-robin, master loot) and enforced when looting creature corpses

**Depends on:** Phase 4 (chat for coordination)

---

### Phase 6: Character Progression

**Goal:** Players gain XP, level up, and unlock abilities — creating a progression loop.

**Requirements:** PROG-01..04

**Why here:** Progression depends on working quests (Phase 2) and creature combat (existing). Leveling feeds into trainer ability learning (Phase 3).

**Success criteria:**
1. Killing creatures awards XP based on level difference formula (existing `experience.h` logic) — displayed to player
2. Quest completion awards configured XP — quest reward dialog shows XP gained
3. Level-up triggers stat increases, level display updates on client, and new abilities become available at trainers

**Depends on:** Phases 2, 3 (quests give XP, trainers teach abilities)

---

### Phase 7: Loot & Economy

**Goal:** Creatures drop loot, world objects provide loot, vendors provide basic economy.

**Requirements:** LOOT-01..05

**Why here:** Loot depends on creature death (existing), world objects (Phase 3), and group loot rules (Phase 5).

**Success criteria:**
1. Creature corpses contain loot (items + gold) based on configured loot tables — loot window shows available items
2. World objects (chests, gathering nodes) produce loot when used, with quest-gated restrictions enforced
3. Group loot distribution follows configured rules — need/greed rolls or round-robin rotation works correctly
4. Vendor buy/sell economy functions — items have buy/sell prices, gold transactions are atomic

**Depends on:** Phases 3, 5 (world objects, group loot)

---

### Phase 8: Editor Content Workflows

**Goal:** Editor supports efficient content creation — creature spawns, world objects, triggers, and quests.

**Requirements:** EDIT-01..04

**Why last:** Editor improvements are most valuable after the runtime systems are stable, so editors can create content that actually works.

**Success criteria:**
1. Editor supports placing creature spawns on the map with configurable patrol paths and spawn parameters
2. Editor supports placing world objects with type configuration, quest requirements, and trigger assignment
3. Trigger editor provides visual chain editing with action/condition configuration and test execution

**Depends on:** All prior phases (editor surfaces runtime features)

---

## Dependency Graph

```
Phase 1 (Triggers) ──→ Phase 2 (Quests) ──→ Phase 3 (NPC/WorldObj) ──→ Phase 7 (Loot)
                                                      │
Phase 4 (Chat) ──→ Phase 5 (Groups) ────────────────→─┘
                                                      │
                   Phase 6 (Progression) ←── Phases 2,3
                                                      
Phase 8 (Editor) ←── All phases
```

## Coverage Validation

All 37 v1 requirements mapped. No unmapped requirements.

---
*Roadmap created: 2026-03-31*
*Last updated: 2026-03-31 after initialization*
