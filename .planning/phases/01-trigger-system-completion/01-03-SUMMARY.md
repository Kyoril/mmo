---
phase: 01-trigger-system-completion
plan: 03
subsystem: world-server
tags: [lua-scripting, event-hooks, luabind, gossip, quest-events, world-object-scripting]

requires:
  - phase: 01-trigger-system-completion
    provides: "WorldObjectSpawner, GameCreatureS, GameWorldObjectS infrastructure"
provides:
  - LuaScriptMgr class with full Lua VM lifecycle management
  - Event dispatch for OnGossipHello, OnGossipSelect, OnQuestAccept, OnQuestComplete, OnUse
  - luabind bindings exposing GameCreatureS, GamePlayerS, GameWorldObjectS to Lua
  - Script registration via RegisterCreatureScript(entryId, table) and RegisterObjectScript(entryId, table)
  - Automatic script loading from data/scripts/ directory at server startup
  - Two example scripts demonstrating NPC gossip and world object interaction
affects: [quest-system, npc-interaction, world-objects, editor]

tech-stack:
  added:
    - "Server-side Lua via luabind (consistent with client)"
  patterns:
    - "Register per-entry script tables in Lua registry, dispatch via luaL_ref"
    - "luabind::object bindings pass pointers to avoid non-copyable issues"
    - "Static C callbacks for RegisterCreatureScript/RegisterObjectScript"

key-files:
  created:
    - src/world_server/lua_script_mgr.h
    - src/world_server/lua_script_mgr.cpp
    - data/scripts/example_npc.lua
    - data/scripts/example_object.lua
  modified:
    - src/world_server/program.cpp
    - src/shared/game_server/world/world_instance.h

key-decisions:
  - "Used luabind for C++ bindings (consistent with client-side game_script.cpp)"
  - "Script tables keyed by entry ID — RegisterCreatureScript(entryId, {OnGossipHello=fn, ...})"
  - "Lua registry refs for script tables (luaL_ref) — efficient dispatch without global table lookups"
  - "Passed game object pointers to luabind (not references) to avoid non-copyable object issues"
  - "WorldInstance stores optional LuaScriptMgr pointer, set via instanceCreated signal in program.cpp"
  - "Scripts loaded from data/scripts/ directory — all .lua files loaded at startup"

patterns-established:
  - "Server-side Lua event hook pattern: RegisterXScript(entryId, table) → OnEvent dispatch"
  - "LuaScriptMgr lifetime: created in program.cpp, injected into WorldInstance via signal"

requirements-completed: [TRIG-03]

duration: 15min
completed: 2026-03-31
---

# Phase 01 Plan 03: Server-Side Lua Event Hook System Summary
