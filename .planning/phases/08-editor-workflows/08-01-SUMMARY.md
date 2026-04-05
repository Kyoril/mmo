---
plan: 08-01
phase: 08
subsystem: editor
tags: [waypoints, patrol, spawn-editor, visualization, imgui]
dependency_graph:
  requires: []
  provides: [patrol-waypoint-editor]
  affects: [spawn-edit-mode, world-editor-instance]
tech_stack:
  added: []
  patterns: [ManualRenderObject line visualization, scene-node lifecycle, raycast-to-world-position]
key_files:
  created: []
  modified:
    - src/mmo_edit/editors/world_editor/edit_modes/spawn_edit_mode.h
    - src/mmo_edit/editors/world_editor/edit_modes/spawn_edit_mode.cpp
    - src/mmo_edit/editors/world_editor/world_editor_instance.cpp
decisions:
  - Waypoint UI placed in Visit(SelectedUnitSpawn&) rather than DrawDetails() — this is where the Movement combo lives; keeping related UI together
  - Single shared SceneNode+ManualRenderObject per waypoint marker avoids complex lifecycle tracking
  - SeparatorText replaced with Separator+Text — project uses ImGui 1.87 WIP which predates SeparatorText (added in 1.89)
  - SetSelectedSpawn uses pointer+active-flag equality guard to avoid unnecessary visualization rebuilds on every frame
metrics:
  duration: ~40 minutes
  completed: 2025-07-13
  tasks_completed: 1
  files_modified: 3
---

# Phase 08 Plan 01: Patrol Path Waypoint Editor Summary

## One-liner
Click-to-place waypoint editor for PATROL-movement creature spawns with real-time looping line path visualization via ManualRenderObject.

## What Was Built

### SpawnEditMode — new state and methods (`spawn_edit_mode.h` / `.cpp`)

| Addition | Purpose |
|---|---|
| `m_selectedUnitSpawn` | Tracks the proto entry being edited |
| `m_waypointEditActive` | True when a PATROL spawn is selected |
| `m_draggingWaypointIndex` | Which waypoint is being dragged (-1 = none) |
| `m_waypointPathObj` / `m_waypointPathNode` | Line-path render object + its scene node |
| `m_waypointSpheres` / `m_waypointMarkerNodes` | Per-waypoint cross-marker objects + nodes |
| `SetSelectedSpawn(entry*)` | Called from Visit() each frame; updates active flag and rebuilds viz on change |
| `RebuildWaypointVisualization()` | Destroys + recreates path lines and cross markers from proto data |
| `HitTestWaypoint(x,y,z)` | Returns nearest waypoint index within 1.5 m radius |
| `OnMouseDown(x,y)` | Raycasts to world; starts drag or adds new waypoint |
| `OnMouseMoved(x,y)` | Updates dragged waypoint position + rebuilds viz |
| `OnMouseUp(x,y)` | Clears drag index |
| `IsWaypointEditActive()` / `IsDraggingWaypoint()` | Query helpers for world_editor_instance |

### WorldEditorInstance changes

- **`OnMouseButtonDown`**: now calls `m_editMode->OnMouseDown` for left button when hovering
- **`OnMouseMoved`**: now calls `m_editMode->OnMouseMoved`; skips camera rotation when waypoint drag is active
- **`OnMouseButtonUp`**: skips spawn-selection raycaster when `IsWaypointEditActive()` is true (clicks are consumed by waypoint logic)
- **`Visit(SelectedUnitSpawn&)`**: calls `SetSelectedSpawn` every frame; adds Patrol Waypoints section below the Movement combo showing per-waypoint wait-time inputs, X delete buttons, and a Clear All button
- **`ClearSelection()`**: calls `SetSelectedSpawn(nullptr)` to destroy visualization when selection is cleared

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] ImGui::SeparatorText not available**
- **Found during:** Build
- **Issue:** `ImGui::SeparatorText` was added in ImGui 1.89; project uses 1.87 WIP
- **Fix:** Replaced with `ImGui::Separator()` + `ImGui::Text("Patrol Waypoints")` + `ImGui::Separator()`
- **Files modified:** `world_editor_instance.cpp`
- **Commit:** 6fb85bd5

### Design Adjustments

**1. Waypoint UI in `Visit(SelectedUnitSpawn&)` not `DrawDetails()`**
- The plan suggested adding waypoint UI to `DrawDetails()`, but the movement type combo already lives in `Visit(SelectedUnitSpawn&)`. Placing the waypoint section immediately after keeps the UX cohesive and avoids duplicating spawn-selection context.

**2. Waypoint markers use absolute world coordinates with scene-node-at-origin**
- Each marker is its own SceneNode + ManualRenderObject. Using world-space coordinates for all geometry inside a node at (0,0,0) local position matches how WorldGrid and the debug AABB work, avoiding coordinate-space confusion.

## Known Stubs

None — all waypoint positions persist in the `proto::UnitSpawnEntry::waypoints` repeated field and are written to disk when the map is saved via the existing `WorldEditorInstance::Save()` path.

## Self-Check: PASSED

- All modified source files confirmed present on disk
- Commit `6fb85bd5` confirmed in git log
- Clean build with 0 errors, 0 warnings (`cmake --build F:\mmo\build --target mmo_edit` exit code 0)
