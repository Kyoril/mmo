# Phase 1: Trigger System Completion - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-31
**Phase:** 01-trigger-system-completion
**Areas discussed:** World object state model, Mount/Dismount fields, Lua scripting scope, SetPhase semantics

---

## World Object State Model

### SetWorldObjectState value type

| Option | Description | Selected |
|--------|-------------|----------|
| Raw uint32 state | Set object_fields::State to int value. Client interprets. Matches spawner pattern. | ✓ |
| Named state enum | Define world_object_state enum (Open/Closed/Active). Validate against it. | |

**User's choice:** Raw uint32 state
**Notes:** Matches existing spawner pattern where state is set as raw uint32.

### SetWorldObjectState target lookup

| Option | Description | Selected |
|--------|-------------|----------|
| Named spawner lookup | Mirror FindCreatureSpawner — add FindObjectSpawner to WorldInstance. | ✓ |
| Direct object by GUID | Look up the world object directly by GUID from context. | |

**User's choice:** Named spawner lookup
**Notes:** Consistent with creature spawner pattern already used in SetSpawnState.

### SetSpawnState/SetRespawnState for ObjectSpawner

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, mirror creature spawner | Add SetState/SetRespawn to WorldObjectSpawner, mirroring CreatureSpawner. | ✓ |
| Defer | Only implement SetWorldObjectState, defer spawner control. | |

**User's choice:** Yes, mirror creature spawner
**Notes:** Complete the TODO branches that already exist in trigger_handler.cpp.

---

## Mount/Dismount Fields

### Mount display approach

| Option | Description | Selected |
|--------|-------------|----------|
| Add MountDisplayId field | Add MountDisplayId to UnitFields. SetMount sets it + movement speed. Dismount clears it. | ✓ |
| Aura-based mount visuals | Use an aura with a visual override instead of a dedicated field. | |

**User's choice:** Add MountDisplayId field

### Virtual equipment fields

| Option | Description | Selected |
|--------|-------------|----------|
| Add 3 VirtualItem fields | Add VirtualItem0/1/2 (3 slots) to UnitFields for all units. | ✓ |
| Defer | Only creature display uses this — skip for now. | |

**User's choice:** Add 3 VirtualItem fields

### SetCombatMovement implementation

| Option | Description | Selected |
|--------|-------------|----------|
| Bool flag on creature | Add SetCombatMovement(bool) to GameCreatureS that toggles AI chase behavior. | ✓ |

**User's choice:** Bool flag on creature

---

## Lua Scripting Scope (TRIG-03)

### Scripting depth

| Option | Description | Selected |
|--------|-------------|----------|
| Event-hook scripts | Per-creature/NPC Lua scripts with event callbacks. Minimal API surface. | ✓ |
| Full scripting engine | Full Lua VM with rich API: spawn creatures, modify terrain, run timers. | |
| Defer Lua entirely | No server Lua this phase. Use only triggers for scripting. | |

**User's choice:** Event-hook scripts
**Notes:** Minimal API surface focused on quest/NPC/world object interactions.

### Lua binding approach

| Option | Description | Selected |
|--------|-------------|----------|
| luabind (existing dep) | Already in deps/ and used client-side. Consistent approach. | ✓ |
| sol2 or manual bindings | Lighter weight, header-only. New pattern. | |

**User's choice:** luabind (existing dep)

### Event callback scope

| Option | Description | Selected |
|--------|-------------|----------|
| Core NPC + quest + world obj events | OnGossipHello, OnGossipSelect, OnQuestAccept, OnQuestComplete, OnUse | ✓ |
| Extended with combat events | Above plus OnCombatStart, OnDeath, OnSpawn, OnReachWaypoint | |

**User's choice:** Core NPC + quest + world obj events

### Script file location

| Option | Description | Selected |
|--------|-------------|----------|
| data/scripts/ folder | Loaded by world_server at startup. One file per entry. | ✓ |
| Embedded in proto data | Scripts stored alongside creature/object definitions. | |

**User's choice:** data/scripts/ folder

---

## SetPhase Semantics

### Phase meaning

| Option | Description | Selected |
|--------|-------------|----------|
| AI combat phase only | Calls creature's combat script SetPhase(). Matches enum docs. | ✓ |
| AI phase + visibility phasing | Also include object/player visibility phasing. | |

**User's choice:** AI combat phase only
**Notes:** Visibility phasing noted as deferred idea.

---

## Agent's Discretion

- Internal field replication details
- Error handling and logging patterns
- Lua API naming conventions

## Deferred Ideas

- Visibility phasing (WoW-style)
- Extended Lua combat events (OnCombatStart, OnDeath, etc.)
- Full Lua scripting engine

---

# Update Session: 2026-04-02

**Areas discussed:** Lua API surface (post-implementation review)

---

## Lua API Growth Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Expand now | Add bindings needed for quest/NPC scripting now in Phase 1 scope | |
| Expand per phase | Each downstream phase adds the bindings it needs | ✓ |
| Define contract, implement later | Define full target API surface now, implement incrementally | |

**User's choice:** Expand per phase
**Notes:** Phase 1 ships with minimal bindings (GetName, GetHealth, GetMaxHealth). Downstream phases add what they need.

---

## Lua Binding Style

| Option | Description | Selected |
|--------|-------------|----------|
| Thin bindings only | Scripts call C++ methods via luabind. No Lua-side abstractions. | ✓ |
| Thin bindings + Lua helpers | Small Lua utility layer for common patterns | |
| Framework approach | Richer Lua framework with event registration, utility modules | |

**User's choice:** Thin bindings only
**Notes:** No Lua-side helper libraries or abstractions.

---

## Lua Error Model

| Option | Description | Selected |
|--------|-------------|----------|
| Log and continue | Errors logged server-side, script continues or silently fails | ✓ |
| Log and fallback | Errors logged, event returns false for C++ fallback | |
| Strict (disable on error) | Errors logged, script disabled for entry until reload | |

**User's choice:** Log and continue
**Notes:** Matches existing LuaScriptMgr catch pattern.

---

## Script Reloading

| Option | Description | Selected |
|--------|-------------|----------|
| Startup only (current) | Scripts loaded once at startup. Server restart for changes. | ✓ |
| Runtime reload command | Console command to reload all scripts without restart | |

**User's choice:** Startup only (current)
**Notes:** Hot-reload deferred to future consideration.

---

## Additional Deferred Ideas (from update session)

- Lua hot-reload — runtime reload command deferred
- Lua helper library — decided against (thin bindings only)
