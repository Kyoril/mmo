# Trigger Editor: named enum values and a usable Chain View

Date: 2026-09-08

## Problem

Two independent complaints about `src/mmo_edit/editor_windows/trigger_editor_window.cpp`:

1. **Raw integers for values that are really enums.** `PlaySpellVisual` shows
   `Event (0-8, 4 = Impact)` as an `InputInt`. The author has to know the
   `proto::SpellVisualEvent` numbering by heart. The same holds for the spell cast
   target, stand state, world object state and the virtual equipment slot. IDs that
   reference other project data (spells, quests, creatures, sounds, ...) are typed as
   bare numbers even though the editor already has the names loaded.
2. **The Chain View is unusable.** It renders every trigger in the project, gives none
   of them a position, makes nodes runaway-wide, and kicks the user back to the Edit
   view the instant a node is selected.

## Root causes of the Chain View breakage

- `DrawChainView` iterates `m_manager.getTemplates()` — the whole project — with no
  filter and no relation to the selected trigger.
- No `SetNodePosition` call anywhere, and `config.SettingsFile = nullptr`, so every
  node lands at the editor origin stacked on top of the others.
- The output pin is placed with
  `ImGui::SetCursorPosX(ax::NodeEditor::GetNodeSize(nodeId).x - 24.0f)`. The cursor is
  window-relative while the value is a node width, so the pin is pushed far to the
  right and the node grows to contain it — compounding every frame because
  `GetNodeSize` reports the previous frame's size.
- `GetSelectedNodes(...) > 0` sets `m_jumpToTriggerId` every frame a node is selected,
  so selecting a node (a prerequisite for dragging it) immediately leaves the view.
- Link ids are `(triggerId << 16) | (targetId & 0xFFFF)`: collides for duplicate edges
  and truncates trigger ids above 65535.

## Design

### 1. Reusable picker layer

New header `src/mmo_edit/editor_windows/proto_entry_picker.h`:

- `DrawEnumCombo(id, label, int& value, const char* const* names, int count, tooltip)`
- `DrawEntryPicker(id, label, int& value, const Manager&, tooltip)` — templated over
  `proto::TemplateManager`, renders a searchable `BeginCombo` of `"name (id)"` with a
  filter box and a `None (0)` row. An id with no matching entry renders in red as
  `<missing #N>` so broken references are visible instead of blank.

Both return `bool changed`. They are placed in a shared header rather than inside the
trigger editor so the other entry editors can adopt the same widget later.

`DrawTriggerAction` and `DrawTriggerEvent` are free functions in an anonymous namespace
with no access to `m_project`; they gain a `const proto::Project&` parameter.

### 2. Field mapping

True enums become combos:

| Field | Values |
|---|---|
| `PlaySpellVisual` data[1] | the nine `proto::SpellVisualEvent` names |
| `CastSpell` data[1] | `trigger_spell_cast_target`: Caster / Current Target / Triggering Unit |
| `SetStandState` data[0] | `unit_stand_state`: Stand / Sit / Sleep / Dead / Kneel |
| `SetWorldObjectState` data[0] | Closed (0) / Open (1) |
| `SetVirtualEquipmentSlot` data[0] | Main Hand / Off Hand / Ranged |
| `OnEncounterStateChanged` data[1] | Any / Not Started / In Progress / Done / Fail |

Ids become by-name pickers over project data: spells, quests, units, sounds, emotes,
maps, variables, gossip menus, model data, spell visualizations, and — most useful of
all — the `Trigger` action's target trigger.

Two label corrections with no behaviour change, matching what the world server actually
reads: `SetVirtualEquipmentSlot` data[1] is a **display id** written straight to
`object_fields::VirtualItem0..2`, not an item entry; `SetMount` data[0] is a **mount
display id**. Both become model-data pickers.

Two fields the server ignores are labelled as such rather than removed, so authored
data is not silently dropped:

- Say/Yell `Language` (data[1]) — `HandleSay`/`HandleYell` never read it.
- `BroadcastMessage` data[0] — documented in `trigger_helper.h` as
  `0=system, 1=raid-warning`, but `HandleBroadcastMessage` always sends
  `ChatType::System`. The stale comment is corrected too.

### 3. Chain View

Scoped to the selected trigger, read-only.

- **Membership** — transitive BFS in both directions over `trigger_actions::Trigger`
  edges from the selected trigger: everything it calls, and everything that calls it.
- **Layout** — longest-path column assignment relaxed at most `N` times so cycles
  terminate; within a column, nodes are ordered by discovery. Applied with
  `SetNodePosition` followed by `NavigateToContent`, and only recomputed when the chain
  composition changes, so hand-dragged positions survive. A `Re-layout` button forces
  a recompute.
- **Node body** — three groups (`input pin | fixed-width content | output pin`),
  replacing the `SetCursorPosX` hack. The name is ellipsized to the content width. The
  selected trigger gets a highlight border. A hover tooltip (inside
  `Suspend()`/`Resume()`) shows the full name, the events, the actions, and which unit
  and object entries list this trigger.
- **Interaction** — a single click only selects; **double-click**
  (`GetDoubleClickedNode`) switches to the Edit view for that trigger.
- **Ids** — node `= triggerId`, pins in a disjoint range, links from a sequential
  counter.

## Out of scope

Creating or deleting chain links from the graph, node deletion, and persisting the
graph layout to disk.

## Testing

The editor is a GUI target with no test harness, so verification is a Release build of
`mmo_edit` plus a manual pass in the editor: pick a trigger that chains (the Hollow
Choir boss triggers chain through `Trigger` actions), confirm the chain view lays out
readably, that a single click no longer ejects, and that double-click navigates. The
picker layer itself is header-only and compiled by that build.
