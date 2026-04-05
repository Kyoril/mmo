---
plan_id: 08-03
phase: 08
plan: 03
subsystem: editor
tags: [editor, imgui-node-editor, triggers, ui]
dependency_graph:
  requires: []
  provides: [trigger-chain-view]
  affects: [trigger_editor_window]
tech_stack:
  added: []
  patterns: [imgui-node-editor canvas, lazy context creation, toggle-view pattern]
key_files:
  created: []
  modified:
    - src/mmo_edit/editor_windows/trigger_editor_window.h
    - src/mmo_edit/editor_windows/trigger_editor_window.cpp
decisions:
  - Node editor context created lazily on first Chain View draw and destroyed in explicit destructor
  - Toggle button placed inside DrawDetailsImpl for minimal invasiveness; no Draw() override needed
  - m_jumpToTriggerId queued from DrawChainView and processed at the start of next DrawDetailsImpl call
metrics:
  duration_minutes: 15
  completed_date: "2026-04-05"
  tasks_completed: 1
  files_changed: 2
---

# Phase 08 Plan 03: Trigger Chain Node Graph Editor Summary

## One-liner
Added Chain View toggle to the trigger editor that renders all triggers as draggable imgui-node-editor nodes with directed edges for Trigger-action chains.

## What Was Built

### Chain View toggle button
A `[Chain View]` / `[Edit View]` toggle button was added at the top of `DrawDetailsImpl()`.  When toggled on, the existing edit UI is bypassed and `DrawChainView()` fills the details panel.  When toggled off, the existing edit UI renders unchanged.

### DrawChainView
`TriggerEditorWindow::DrawChainView(const proto::Triggers& triggers)`:
- Creates an `ax::NodeEditor::EditorContext` lazily on first call (`SettingsFile = nullptr` — no layout persistence).
- Iterates `triggers.entry_size()` / `triggers.entry(i)` to render one node per `TriggerEntry`.
- Each node shows: `[ID] Name`, a min-width spacer (160 px), and an event/action count row.
- Input pin ID = `trigger.id() * 3 + 1`; output pin ID = `trigger.id() * 3 + 2`.
- After all nodes, iterates actions to draw `ax::NodeEditor::Link` for every action where `action.action() == trigger_actions::Trigger` (0) and `action.data_size() > 0`.
- Detects node selection via `ax::NodeEditor::GetSelectedNodes()` and queues `m_jumpToTriggerId`.

### Node-click jump-back
`m_jumpToTriggerId` is checked at the start of `DrawDetailsImpl()`.  If non-zero, `SelectEntryById()` is called to highlight the target trigger in the list, `m_showChainView` is cleared, and the function returns (new edit-mode frame starts next render tick).

### Destructor cleanup
`TriggerEditorWindow::~TriggerEditorWindow()` explicitly calls `ax::NodeEditor::DestroyEditor(m_nodeEditorCtx)` when the context is non-null.

## Commits

| Hash | Description |
|------|-------------|
| efd5165e | feat(08-03): trigger chain node graph editor |

## Deviations from Plan

None — plan executed exactly as written.  The `▶` arrows were changed to `>` ASCII to avoid potential UTF-8 encoding issues in the MSVC source file, which is a purely cosmetic difference.

## Known Stubs

None — all trigger data is live from `m_manager.getTemplates()`.

## Self-Check: PASSED

- [x] `src/mmo_edit/editor_windows/trigger_editor_window.h` exists and contains `m_nodeEditorCtx`, `m_showChainView`, `m_jumpToTriggerId`, `DrawChainView` declaration
- [x] `src/mmo_edit/editor_windows/trigger_editor_window.cpp` contains `DrawChainView` implementation and explicit destructor
- [x] Commit `efd5165e` verified in git log
- [x] Build succeeded with zero errors
