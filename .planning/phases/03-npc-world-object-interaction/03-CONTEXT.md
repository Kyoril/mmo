# Phase 3: NPC & World Object Interaction - Context

**Gathered:** 2026-04-05
**Status:** Ready for planning

<domain>
## Phase Boundary

Players can buy from vendors, learn from trainers, navigate gossip menus, and interact with world objects (doors, chests). The server-side logic (buy/sell handlers, trainer spell purchase, gossip action routing, validation) is largely complete. This phase focuses on:
1. Implementing the missing Lua UI frames (VendorFrame, TrainerFrame, GossipFrame)
2. Wiring Lua events and script function registrations
3. Implementing the world object interaction pipeline (UseObject opcode, server handler, state broadcast)
4. Ensuring quest-gated objects enforce requirements server-side

Out of scope: buyback system (Phase 7), extended cost currencies, repair NPCs, skill requirement validation (Phase 6).

</domain>

<decisions>
## Implementation Decisions

### Vendor UI Feature Scope
- No buyback system — defer to Phase 7 (Loot & Economy)
- Display stock count visually only — no server-side interval enforcement
- Extended cost items (badge/token currencies) — skip, not in success criteria
- Repair NPC functionality — skip, defer to Phase 7

### Gossip & Dialog UX
- Show NPC name/portrait header in gossip dialog
- Fire Lua events (GOSSIP_SHOW, GOSSIP_CLOSED) following same pattern as VendorClient
- Multi-page gossip uses prev/next navigation buttons (GossipMenu action type chains)
- Quest givers show available quest icons in gossip using the show_quests proto flag

### World Object Interaction Scope
- Implement both doors and chests
- Chest opened: play open animation + stub loot window (Phase 7 owns full loot tables)
- Quest-gated objects enforce requirements server-side (block interaction if conditions not met)
- Door/chest state changes broadcast to all nearby players

### Trainer Implementation Depth
- Skill requirements on trainer spells deferred to Phase 6 (Character Progression) — TODO in code stands
- Already-known spells shown greyed-out in trainer UI with "Known" label (server sends isKnown field)
- Trainer type (class/profession/mount) reflected in window title using TrainerType enum + title field
- Spell tooltips on hover — reuse existing spell tooltip system for consistency

### the agent's Discretion
- Exact Lua frame layout and styling
- Specific opcode names for the new UseObject packet
- How to structure the object state machine implementation details

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `VendorClient` (vendor_client.h/cpp) — manages vendor state, item list; exposes GetVendorNumItems(), CloseVendor() to Lua
- `TrainerClient` (trainer_client.h/cpp) — manages trainer list, spell purchase; TrainerSpellEntry has isKnown field
- `game_creature_s.h` — IsInteractable() virtual method, npc_flags namespace (Gossip, Vendor, Trainer, QuestGiver etc.)
- `GameObjectS` / `GameWorldObjectType` enum — Chest=0, Door=1 types defined; game_world_object_flags (InUse, Locked, NotInteractable)
- Existing frame types: cooldown_frame.h, minimap_frame.h, model_frame.h — reference for FrameUI frame creation patterns
- Inventory command pattern — already used for buy/sell item operations
- Condition system — already used for gossip menu/option conditionId checks

### Established Patterns
- Client-side system registration: VendorClient::RegisterScriptFunctions() pattern for Lua binding
- Lua events: game client fires named events (e.g., VENDOR_OPENED) that Lua frames listen to
- Packet handler registration in Player constructor (player.h lines 287-375)
- Distance check: 5.0f interaction radius used consistently across all NPC interactions
- Validation order: distance → alive check → faction check → specific business logic
- Proto-based game data: vendors.proto, trainers.proto, gossip_menus.proto already exist

### Integration Points
- `player_npc_handlers.cpp` — add OnGameObjectUse handler here
- `player.h` — register new UseObject opcode handler
- `VendorClient` / `TrainerClient` — extend RegisterScriptFunctions() to expose BuyItem, SellItem, BuySpell
- `game::client_realm_packet` / `game::realm_client_packet` — add UseObject / ObjectStateChanged opcodes
- FrameUI Lua scripts — create vendor.lua, trainer.lua, gossip.lua UI frame definitions

</code_context>

<specifics>
## Specific Ideas

- Follow VendorClient Lua event pattern for TrainerClient and GossipClient
- Gossip window should mirror WoW-like gossip: NPC name at top, large text area, option buttons below
- Chest stub loot window should show "Empty" or "Nothing interesting" to keep the interaction feeling complete
- Trainer window should show spell icon, name, level requirement, cost, and Known/Available status

</specifics>

<deferred>
## Deferred Ideas

- Buyback system (sell-back recently sold items) — Phase 7
- Extended cost items (badge/token currencies) — Phase 7
- Repair NPC functionality — Phase 7
- Skill requirement validation on trainer spells — Phase 6
- Profession trainers (SKILL_TRAINER type) — Phase 6
- NPC reputation-based pricing — post-alpha

</deferred>
