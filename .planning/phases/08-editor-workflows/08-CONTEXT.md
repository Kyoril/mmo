# Phase 8: Editor Content Workflows — Context

## Goal
Editor supports creature spawns with patrol paths, world objects with per-spawn loot/trigger config, and a trigger chain node-graph visualizer.

## Decisions Made
- **Patrol waypoints**: Viewport click-to-place (click in 3D world adds waypoint; drag moves it)
- **Trigger chain**: Node-graph canvas using imgui-node-editor (same lib used by material editor)
- **Object loot**: Per-spawn loot_entry override field in ObjectSpawnEntry proto

## Key Technical Facts
- imgui-node-editor available at `F:\mmo\deps\imgui-node-editor` — used by material_editor_instance.cpp (`namespace ed = ax::NodeEditor;`)
- SpawnEditMode base class `WorldEditMode` has `OnMouseDown/Moved/Up/Hold()` virtual hooks — all unimplemented by SpawnEditMode currently
- World-space line rendering via `ManualRenderObject::AddLineListOperation(materialPath)` returning a line list op with `AddLine(p1, p2)` — used by area_trigger_edit_mode for visualization
- ObjectSpawnEntry proto has fields 1-10; loot_entry = field 11 is free
- `game_world_object_s.cpp` creates LootInstance from `m_entry.objectlootentry()` — needs override path
- Trigger editor already has full event+action editing; needs node-graph tab added

## Plans
- **08-01**: Patrol Path Waypoint Editor (wave 1)
- **08-02**: World Object Per-Spawn Config (wave 1, independent of 08-01)
- **08-03**: Trigger Chain Node Graph (wave 2)
