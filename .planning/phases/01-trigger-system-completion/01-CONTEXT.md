# Phase 1: Trigger System Completion - Context

**Gathered:** 2026-03-31
**Updated:** 2026-04-02
**Status:** Complete (implemented)

<domain>
## Phase Boundary

Complete all unimplemented trigger actions in `trigger_handler.cpp` so scripted content (encounters, quests, world interactions) works end-to-end. Add server-side Lua event hooks for quest events, NPC dialog, and world object interaction.

Specifically: implement 7 stub trigger actions (SetWorldObjectState, SetVirtualEquipmentSlot, SetPhase, SetCombatMovement, Dismount, SetMount, SetSpawnState for ObjectSpawner), add required object field infrastructure, and create a server-side Lua scripting system with event callbacks.

</domain>

<decisions>
## Implementation Decisions

### World Object State (SetWorldObjectState)
- **D-01:** Use raw uint32 state values — trigger sets `object_fields::State` directly. Client interprets the state meaning. Matches existing spawner pattern.
- **D-02:** Use named spawner lookup — add `FindObjectSpawner(name)` to `WorldInstance`, mirroring `FindCreatureSpawner`. Trigger uses `action.targetname()` to find the spawner, then sets state on spawned objects.
- **D-03:** Also implement SetSpawnState and SetRespawnState for `WorldObjectSpawner` — add `SetState(bool)` and `SetRespawn(bool)` mirroring `CreatureSpawner`.

### Mount & Virtual Equipment Fields
- **D-04:** Add `MountDisplayId` to `UnitFields` enum in `object_type_id.h`. `SetMount` sets this field plus movement speed modifier. `Dismount` clears it. Client reads the field for mount model display.
- **D-05:** Add `VirtualItem0`, `VirtualItem1`, `VirtualItem2` (3 uint32 display ID fields) to `UnitFields` for all units. `SetVirtualEquipmentSlot` trigger sets these. Also fix `CreatureCombatScript::SetVirtualEquipment` to use these fields.

### SetCombatMovement
- **D-06:** Add a `bool m_combatMovementEnabled` flag to `GameCreatureS`. `SetCombatMovement` trigger sets it. AI chase logic checks the flag before pursuing targets. Default: true.

### SetPhase (AI Combat Phase)
- **D-07:** SetPhase means AI combat phase only — not visibility phasing. The trigger calls through to `CreatureCombatScript::SetPhase(uint32)` on the target creature. No visibility phasing system in this phase.

### Server-Side Lua Scripting (TRIG-03)
- **D-08:** Event-hook scripts — per-creature/NPC/world-object Lua scripts with specific event callbacks. Minimal API surface, not a full scripting engine.
- **D-09:** Use luabind for Lua bindings — already in deps/ and used client-side. Consistent approach across client and server.
- **D-10:** Core event callbacks only: `OnGossipHello`, `OnGossipSelect`, `OnQuestAccept`, `OnQuestComplete`, `OnUse` (world objects). No combat events in this phase.
- **D-11:** Scripts live in `data/scripts/` folder, loaded by world_server at startup. One file per creature/object entry (e.g., `creature_1234.lua`, `object_5678.lua`).

### Lua API Growth Strategy (added 2026-04-02)
- **D-12:** Lua API expands per-phase — each downstream phase adds the bindings it needs. Phase 1 ships with minimal bindings (GetName, GetHealth, GetMaxHealth on Creature/Player). Phase 2 will add quest-related bindings, Phase 3 will add gossip/vendor bindings, etc.
- **D-13:** Bindings are thin C++ method wrappers via luabind only — no Lua-side helper libraries, abstractions, or utility layers. Scripts call C++ methods directly.
- **D-14:** Lua error model is log-and-continue — errors are logged server-side, script continues or silently fails. No strict mode, no script disabling on error. Matches existing LuaScriptMgr catch pattern.
- **D-15:** Scripts are loaded at startup only — no hot-reload. Server restart required for script changes. Runtime reload deferred to future consideration.

### Agent's Discretion
- Internal implementation details of field replication to client
- Error handling and logging patterns (follow existing conventions)
- Lua API object names and method signatures (follow idiomatic patterns)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Trigger System
- `src/world_server/trigger_handler.cpp` — Main implementation file with all Handle* methods
- `src/world_server/trigger_handler.h` — TriggerHandler class definition
- `src/shared/game_server/trigger_handler.h` — ITriggerHandler interface and TriggerContext
- `src/shared/proto_data/trigger_helper.h` — trigger_actions enum, trigger_action_target enum, trigger_event enum

### Object Fields
- `src/shared/game/object_type_id.h` — ObjectFields, UnitFields, PlayerFields enums (add MountDisplayId, VirtualItem0-2 here)

### World Objects
- `src/shared/game_server/objects/game_world_object_s.h` — GameWorldObjectS class
- `src/shared/game_server/world/world_object_spawner.h` — WorldObjectSpawner (needs SetState/SetRespawn)
- `src/shared/game_server/world/world_object_spawner.cpp` — Spawner implementation
- `src/shared/game_server/world/world_instance.h` — WorldInstance (has commented-out FindObjectSpawner)
- `src/shared/game_server/world/world_instance.cpp` — WorldInstance implementation

### Creature AI
- `src/shared/game_server/ai/creature_combat_script.h` — SetPhase, SetVirtualEquipment methods
- `src/shared/game_server/ai/creature_combat_script.cpp` — Script implementations (SetVirtualEquipment is TODO)

### Editor (trigger UI)
- `src/mmo_edit/editor_windows/trigger_editor_window.cpp` — Editor UI for triggers (already has cases for all actions)

### Existing Patterns (for Lua)
- `src/mmo_client/game_script.cpp` — Client-side Lua with luabind (reference for binding patterns)
- `deps/luabind_noboost/` — Luabind dependency

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **ITriggerHandler interface** — clean abstract base with ExecuteTrigger. All action routing via switch + macro dispatch.
- **CreatureSpawner pattern** — `FindCreatureSpawner(name)` + `SetState(bool)` + `SetRespawn(bool)` — mirror this for WorldObjectSpawner.
- **CreatureCombatScript** — already has `SetPhase(uint32)` and `SetVirtualEquipment(slot, displayId)` methods. Both need field infrastructure to actually work.
- **Object field system** — `Get<T>(field)` / `Set<T>(field, value)` with client replication. Adding new fields to UnitFields enum is straightforward.
- **luabind** — client-side Lua uses `LUABIND_MODULE` macro pattern with `luabind::class_<>` and `luabind::def`. Same pattern should work server-side.

### Established Patterns
- **Trigger action pattern:** Get target via `GetActionTarget()`, validate type (`IsUnit()`, `IsPlayer()`), extract data via `GetActionData()`, perform the action.
- **Named target lookup:** `action.target() == trigger_action_target::NamedCreature` → `world->FindCreatureSpawner(action.targetname())`. Same for NamedWorldObject → FindObjectSpawner.
- **Field replication:** Changes to object fields are automatically replicated to nearby clients via the existing update system.

### Integration Points
- **UnitFields enum** — New fields added before UnitFieldCount. Client must also know the offsets (shared header).
- **WorldInstance** — Uncomment and implement `FindObjectSpawner`.
- **GameCreatureS** — Add `m_combatMovementEnabled` flag checked by AI movement logic.
- **World server startup** — New Lua VM initialization point for loading scripts from `data/scripts/`.

</code_context>

<specifics>
## Specific Ideas

No specific requirements — follow existing codebase patterns. Key pattern references:
- HandleSay/HandleYell as examples for target resolution and validation
- HandleSetSpawnState (creature branch) as template for the WorldObjectSpawner branch
- Client-side game_script.cpp luabind usage as template for server-side Lua bindings

</specifics>

<deferred>
## Deferred Ideas

- **Visibility phasing** — WoW-style phasing where different players see different world states. Noted for potential future phase.
- **Extended Lua combat events** — OnCombatStart, OnDeath, OnSpawn, OnReachWaypoint. Deferred to after core NPC/quest hooks are proven.
- **Full Lua scripting engine** — Rich API for spawning creatures, modifying world, running timers. Beyond scope of event hooks.
- **Lua hot-reload** — Runtime reload command for scripts without server restart. Deferred to future consideration.
- **Lua helper library** — Lua-side utility layer for common patterns. Decided against (D-13: thin bindings only).

</deferred>

---

*Phase: 01-trigger-system-completion*
*Context gathered: 2026-03-31*
*Context updated: 2026-04-02*
