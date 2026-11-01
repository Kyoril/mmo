# PCG Phase 2 — Editors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the phase 1 graph core usable in mmo_edit. That means a graph editor with a per-node debug view in the world, a control type editor, placed controls and graph instances in the Procedural mode, and Rebuild/Delete through the existing apply core. A minimal spawner node is included so a rebuild produces real foliage and entities.

**Architecture:** Testable logic goes into the headless `pcg` library: the spawner node, placement collection with region clipping, the graph instance record, region overlap, and debug geometry. All of it is covered by Catch2. The mmo_edit side is three thin layers:
- **Graph editor and control type editor:** new `EditorBase` / `EditorInstance` pairs, following the material editor's pattern (imgui-node-editor, dockspace per instance).
- **Procedural mode:** gains placed controls and graph instances next to the existing oasis and section-path records.
- **Debug view:** a debug layer that the graph editor pushes into an open world editor instance, which draws it with a `ManualRenderObject`.

Editor features are checked by unattended flags that return exit code 0/1 and by `--capture-view`.

**Tech Stack:** C++17, ImGui, imgui-node-editor (`deps/imgui-node-editor`, compiled into mmo_edit), nlohmann/json (no exceptions), Catch2, existing `pcg` lib (`graph/`, `record_io`, `placement_reconcile`), `scene_graph::ManualRenderObject`.

**Spec:** `docs/superpowers/specs/2026-10-25-pcg-graphs-design.md` — Goals 1–5, the "Editor" section, "Execution", "Resolved" item 3 (regions) and "Decided while building phase 1".

## Global Constraints

- **Branch and data.**
  - Branch `feature/pcg-oasis-poc`; never push, ship or merge.
  - Data changes go only into the worktree's `data/client` submodule, on its branch `feature/pcg-oasis-poc`, never `master`.
  - Never touch `H:\mmo` outside this worktree.
  - Never kill the user's running editor; use a separate `mmo_edit` process for checks.
- **Map 0 is frozen.** No new PCG output, and no terrain, placement or NPC changes on map 0 (`Development.hwld`). The test site is world 2 (`Worlds/Collision`), pages x31–33 / z36–38.
- **Editor-only.** Graphs, control types, sections and records live under `data/client/Editor/` and never ship to clients. Folder layout: `Editor/Pcg/Biomes/<Biome>/{Graphs,Controls,Sections}`. Records live in `Editor/Pcg/Worlds/<world>/`.
- **Pure lib code** (`src/shared/pcg/`):
  - headless, gcc-compatible, no exceptions;
  - deterministic: hash-based randomness only, no unordered containers on output paths;
  - tests go in `src/tests/pcg_tests/`.
- **Evaluation output is unclipped.** Clipping to the region happens at apply time, after pruning (phase 1 decision).
- **Rebuild applies the saved graph file.** The debug view evaluates the in-memory graph, unsaved edits included.
- **Code style per CLAUDE.md:**
  - Allman braces, tabs, braces on every `if`;
  - `m_camelCase` members, `PascalCase` methods, `camelCase` locals, `snake_case` files;
  - `#pragma once`, Doxygen on public members;
  - copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- **Engine tooling is C++** (mmo_edit flags). No committed Python.
- **Build** from PowerShell:
  - `cmake --build build --config Debug -t pcg_tests -- /m /v:minimal` and `bin\Debug\pcg_tests.exe`;
  - `cmake --build build --config Debug -t mmo_edit -- /m /v:minimal`;
  - for new files: `cmake -S . -B build` first.
- **Commits** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **A record references a missing or renamed graph or control type.** The world still loads, and the record stays listed with a red error. Rebuild refuses with a message and leaves the existing generated content and the record file untouched. (Task 4 and Task 5 flag tests.)
2. **A graph with validation errors or a bad `$param` binding is rebuilt.** Rebuild refuses with the per-node issue text and changes nothing in the world. (Task 5 flag test.)
3. **Two graph instance areas overlap.** Creation and rebuild refuse ("regions may touch but not overlap"); areas that only share an edge are accepted. (Task 2 tests `AreasOverlap`; Task 5 flag test.)
4. **A debug view shows millions of points.** Drawing is capped, the panel shows "showing N of M", and the editor stays interactive. (Task 7 pure test of `BuildDebugGeometry` caps.)
5. **A graph contains a node type the registry does not know** (a file from a newer build). It opens and shows a red "Unknown: <type>" node. Saving keeps its settings and links unchanged. (Task 6: a round-trip test of the editor model, `PcgGraphDocument`.)

The same five lines are pinned by the tests named in the owning tasks.

---

### Task 1: Spawn Meshes node and placement collection

**Files:**
- Create: `src/shared/pcg/graph/nodes_spawn.cpp`, `src/shared/pcg/graph/graph_output.h/.cpp`
- Modify:
  - `src/shared/pcg/graph/node_registry.h` (add `SettingDef::widget`);
  - `builtin_nodes.h` / `node_registry.cpp` (register spawn nodes).
- Test: `src/tests/pcg_tests/test_graph_spawn.cpp`

**Interfaces:**
- Consumes: phase 1 `NodeTypeDef`, `Evaluate`, `EvalResult`, `Collection`, `pcg::Placement`, `PlacementKind`, `HashToUnit`.
- Produces:
```cpp
namespace mmo::pcg::graph
{
	// SettingDef gains (not part of any cache key, editor hint only):
	//   String widget;   // "" | "asset:<ext;ext>" | "assetList:<ext;ext>" | "color"

	void RegisterSpawnNodes(NodeRegistry& registry);   // called from RegisterBuiltinNodes

	/// Points emitted by spawner nodes on their "Spawns" pin, converted to placements and clipped to the region.
	struct CollectedPlacements
	{
		std::vector<Placement> placements;   // sorted by key
		std::vector<String> warnings;        // key collisions resolved, empty assets skipped, ...
		size_t clipped = 0;                  // points dropped for lying outside the region
	};

	/// Collects every node's "Spawns" output (Points items with a String column "asset").
	/// Item attributes: "kind" (String "Entity"|"Foliage"|"WorldModel"), "collides" (Bool).
	/// Key = Hash(nodeId, item.sourceId, point.seed); a repeated key is re-hashed Hash(key, n) with one warning.
	/// Only points inside the closed region spline (XZ) are kept.
	CollectedPlacements CollectPlacements(const Graph& graph, const EvalResult& result, const PathSpline& region);

	/// Deterministic JSON dump of an evaluation (per node: type, ms, fromCache, warnings, error, per pin:
	/// item kinds, tags, point count, first 16 points with attributes). Used by --pcg-eval-graph.
	String WriteEvalDump(const Graph& graph, const EvalResult& result);
}
```
- **Spawn Meshes node.**
  - Type `SpawnMeshes`, category `Spawners`.
  - Input `In`: Points.
  - Output `Spawns`: Points.
  - Settings:
    - `meshes` String: weighted list `"path|weight;path|weight"`, widget `assetList:.hmsh;.hwmo`;
    - `kind` enum `Entity|Foliage|WorldModel`, default Foliage;
    - `collides` Bool, default true;
    - `assetAttribute` String, default `""`: if set, a point's String attribute of that name overrides the pick.
  - The pick is weighted by `HashToUnit(Hash(nodeSeed, point.seed))`.
  - It writes column `asset` and item attributes `kind` / `collides`. Points whose weights sum to 0 or whose asset is empty are dropped with one warning.
  - `contextDeps`: None.

- [ ] **Step 1: Write failing tests** `[graph_spawn]`:
  - `SpawnMeshes` with `meshes="a.hmsh|3;b.hmsh|1"` over 4000 points gives an a:b ratio within 2.7–3.3. Two runs are identical.
  - `assetAttribute` overrides the pick.
  - Empty `meshes` gives no output and one warning.
  - Unknown `kind` makes `ValidateGraph` report an issue.
  - `CollectPlacements`:
    - a point outside a 10×10 region is dropped (`clipped == 1`);
    - a point exactly on the edge follows `RowIndexedPolygon` containment (document which);
    - keys are stable when an unrelated point is removed;
    - two identical points give distinct keys and one warning;
    - `kind`/`collides` map onto `Placement`.
  - `WriteEvalDump` is byte-identical across two evaluations.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_spawn]"`. Expected: FAIL (unknown node type / missing symbols).
- [ ] **Step 3: Implement** the node, `CollectPlacements` (rotation/scale from the point; position from the point) and `WriteEvalDump`. Add `widget` to `SettingDef` and set it on the phase 1 nodes' asset/colour settings, where they have any.
- [ ] **Step 4: Run** the full `bin\Debug\pcg_tests.exe`. Expected: all pass. Also run the gcc-13 `-fsyntax-only -Wall -Wextra` check in WSL on the new TUs.
- [ ] **Step 5: Commit** `feat(pcg): spawn meshes node and placement collection`

### Task 2: Graph instance record and region overlap

**Files:**
- Create: `src/shared/pcg/graph_instance_record.h/.cpp`
- Modify: `src/shared/pcg/section_path_record.h/.cpp` (`RecordKind::GraphInstance`, `PeekRecordKind`)
- Test: `src/tests/pcg_tests/test_graph_instance_record.cpp`

**Interfaces:**
- Consumes: `record_io` (`PutOwnedContent`, `GetOwnedContent`, `PutSplinePoint`, `PutString`), `graph/json_util.h`, `AttributeSet`.
- Produces:
```cpp
namespace mmo::pcg
{
	struct GraphInstanceRecord
	{
		uint64 id = 0;
		String name;
		String graphPath;                 // e.g. "Editor/Pcg/Biomes/FalwynForest/Graphs/Forest.hpcgg"
		PathSpline area;                  // closed loop, XZ region; heights unused
		std::vector<uint64> pointIds;     // per area point, like SectionPathRecord
		uint64 nextPointId = 1;
		uint64 seed = 1;
		graph::AttributeSet parameterOverrides;
		size_t pointLimit = 2'000'000;
		OwnedContent content;
	};
	/// Magic PCGG, version 1: id, then a JSON text blob (name, graphPath, seed, overrides, pointLimit), area
	/// points (PutSplinePoint), pointIds, nextPointId, then PutOwnedContent.
	bool WriteGraphInstanceRecord(io::Writer& writer, const GraphInstanceRecord& record);
	bool ReadGraphInstanceRecord(io::Reader& reader, GraphInstanceRecord& out);

	/// True if the two closed XZ areas share interior (edge or vertex contact alone is not overlap).
	bool AreasOverlap(const PathSpline& a, const PathSpline& b);
}
// RecordKind gains GraphInstance (magic PCGG); PeekRecordKind recognises it.
```

- [ ] **Step 1: Write failing tests** `[graph_instance_record]`:
  - A full record round-trips, including overrides of every attribute type and owned content with placements, and re-writes byte-identically.
  - `PeekRecordKind` returns `GraphInstance`.
  - Truncated data and a wrong version give false without a crash.
  - A JSON blob with unknown keys still reads.
  - `AreasOverlap`:
    - two disjoint squares → false;
    - squares sharing an edge → false;
    - squares sharing a corner → false;
    - one square inside the other → true;
    - a partial overlap → true;
    - a non-convex L-shape against a square in its notch → false.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_instance_record]"`. Expected: FAIL.
- [ ] **Step 3: Implement.** For `AreasOverlap`: if any proper segment pair crosses, return true. Otherwise return true if a point strictly inside `a` (centroid of a sampled triangle, or the midpoint of a vertex nudged inward) is inside `b`, or the reverse. Areas are sampled with the same `SampledPath` step the graph nodes use.
- [ ] **Step 4: Run** the full suite. Expected: all pass. Then the gcc syntax check.
- [ ] **Step 5: Commit** `feat(pcg): graph instance record and region overlap`

### Task 3: Editor terrain sampler and the open-world registry

**Files:**
- Create: `src/mmo_edit/editors/world_editor/pcg/editor_terrain_sampler.h/.cpp`
- Modify:
  - `world_editor_instance.h/.cpp`: open-instance registry and terrain revision;
  - the `IWorldEditor` header that declares `EnsurePagesLoaded`: add `GetTerrainRevision`;
  - the terrain edit paths: bump the revision.

**Interfaces:**
- Consumes: `pcg::graph::ITerrainSampler` (evaluator.h); `terrain::Terrain::{TryGetSmoothHeightAt, GetSmoothNormalAt, GetLayersAt, GetWaterHeightAtWorldPos, HasWaterAtWorldPos}`.
- Produces:
```cpp
namespace mmo
{
	class EditorTerrainSampler final : public pcg::graph::ITerrainSampler
	{
	public:
		/// @param revision Read on every Revision() call (the world's terrain revision).
		EditorTerrainSampler(terrain::Terrain& terrain, std::function<uint64()> revision);
		// Height: false where no page is loaded. SplatWeight: weight 0..1 of the layer whose albedo texture key
		// matches (same matching rule as section paint: texture key, Albedo0N fallback). WaterLevel: false if dry.
	};

	// IWorldEditor:
	//   virtual uint64 GetTerrainRevision() const = 0;   // bumped on every terrain height/splat/water change
	// WorldEditorInstance:
	//   static const std::vector<WorldEditorInstance*>& GetOpenInstances();   // ctor adds, dtor removes
	//   uint64 GetInstanceSerial() const;   // unique per process, never reused (stable handle for lookups)
	//   static WorldEditorInstance* FindOpenInstance(uint64 serial);
}
```
- **Terrain revision.** Find the single place where terrain pages are marked changed (or, failing that, every terrain edit mode's change and `pcg::Apply`/`Unapply`) and bump `m_terrainRevision` there. Document the hook in the header.

- [ ] **Step 1: Implement** the sampler, the registry and the revision hook.
- [ ] **Step 2: Build** `mmo_edit`. Expected: no errors and no new warnings.
- [ ] **Step 3: Verification is deferred** to Task 5, check (h): `--pcg-eval-graph` heights must equal `GetSmoothHeightAt`. The reviewer of this task checks the revision hook covers every terrain edit path.
- [ ] **Step 4: Commit** `feat(pcg-editor): editor terrain sampler and open world registry`

### Task 4: Placed controls in the Procedural mode

**Files:**
- Create: `src/mmo_edit/editors/world_editor/pcg/control_library.h/.cpp`, `src/mmo_edit/editors/world_editor/pcg/control_details.h/.cpp`
- Modify: `procedural_edit_mode.h/.cpp`; `world_editor_instance.h/.cpp` (forwarders); `src/mmo_edit/mmo_edit.cpp` (flag)

**Interfaces:**
- Consumes: `graph::ControlType`, `ControlInstance`, `ParseControlType`, `WriteControlInstance`, `ResolveControl`; `SplineEditController`; `ISplinePointOwner`; `AssetPickerWidget`; `DrawEnumCombo`.
- Produces:
```cpp
namespace mmo
{
	/// Loads and caches control types by asset path; lists biomes and their types.
	class ControlLibrary
	{
	public:
		std::vector<String> ListBiomes() const;                          // folder names under Editor/Pcg/Biomes
		std::vector<String> ListControlTypes(const String& biome) const; // asset paths of Controls/*.hctl
		const pcg::graph::ControlType* Find(const String& typePath, String* error = nullptr); // cached
		void Invalidate(const String& typePath);   // called when the control type editor saves
		uint64 Revision() const;                   // bumped by Invalidate
	};

	/// Typed property-override editor for one placed control (reset-to-default per field).
	/// @return true if anything changed.
	bool DrawControlProperties(const pcg::graph::ControlType& type, pcg::graph::ControlInstance& instance);

	// ProceduralEditMode gains:
	//   ControlKind::Placed (records <id>.hpcgc JSON, same world folder); biome combo; "New Control" menu of
	//   the active biome's types, created at viewport centre (loop: 4-point square 32 m; spline: 2 points 32 m;
	//   volume: box halfExtents 16; point);
	//   bool CreatePlacedControl(const String& typePath, const std::vector<std::pair<float,float>>& points,
	//                            const String& name, uint64& id, String& error);
	//   uint64 GetControlsRevision() const;   // bumped on any placed-control or control-type change
	//   void ResolvePlacedControls(std::vector<pcg::graph::ResolvedControl>& out, std::vector<String>& warnings);
}
```
- **Shape editing.** Loops and splines use the existing `m_splineEditor` and `SelectedSplinePoint` path, with pointIds handled like section paths. Volumes and points get a `SelectedControlTransform` (a `Selectable` with translate and yaw rotate; volumes also take scale into `halfExtents`).
- **Details panel:** name, type (read-only, with a red error if missing), extra tags (comma list), and property groups drawn by `DrawControlProperties`.
- **Missing type:** the record is kept and listed in red, and it is skipped by resolution with a warning.
- **Flag:** `--pcg-new-control <world> --type <typePath> --points x,z;... [--name n] [--set prop=value]...` creates the control and saves the world. Exit code 0 on success, 1 on error (with the message in `pcg_new_control.log`).

- [ ] **Step 1: Implement** `ControlLibrary`, the placed-control load/save in `ProceduralEditMode::Load/Save` (the `.hpcgc` list next to `.hpcg`), creation, editing and the details panel.
- [ ] **Step 2: Build** `mmo_edit`. Expected: clean.
- [ ] **Step 3: Flag tests** on a scratch copy of world 2. Copy `Worlds/Collision` to `Worlds/PcgScratch` in the worktree data; never commit the copy.
  - `--pcg-new-control` with a test `.hctl` gives exit 0, and the `.hpcgc` file exists with the given points and `--set` values.
  - With `--type` pointing at a missing file it gives exit 1 with "control type ... not found" in the log, and no file is written.
- [ ] **Step 4: Manual smoke**, in a separate mmo_edit process opened on the scratch world:
  - place a loop control and drag its points;
  - edit a property and save;
  - reopen and check that the values persisted.
  - Record a screenshot path in the task report.
- [ ] **Step 5: Commit** `feat(pcg-editor): placed controls in the procedural mode`

### Task 5: Graph instances, Rebuild and Delete

**Files:**
- Create: `src/mmo_edit/editors/world_editor/pcg/graph_instance_builder.h/.cpp`
- Modify:
  - `procedural_edit_mode.h/.cpp`;
  - `pcg/generated_content.cpp`: terrain step optional;
  - `world_editor_instance.h/.cpp`;
  - `mmo_edit.cpp`: flags.

**Interfaces:**
- Consumes:
  - Task 1 `CollectPlacements`, `WriteEvalDump`;
  - Task 2 `GraphInstanceRecord`, `AreasOverlap`;
  - Task 3 `EditorTerrainSampler`;
  - Task 4 `ResolvePlacedControls`, `GetControlsRevision`;
  - `ApplyGeneratedContent` / `RemoveGeneratedContent`; `graph::ParseGraph`, `ValidateGraph`, `MergeParameters`, `Evaluate`.
- Produces:
```cpp
namespace mmo
{
	/// Builds the evaluation context for an instance (region = area, seed, merged parameters with warnings,
	/// resolved controls, editor terrain sampler, pointLimit).
	struct GraphInstanceContext
	{
		pcg::graph::EvalContext context;
		std::vector<pcg::graph::ResolvedControl> controls;   // storage the context points at
		std::vector<String> warnings;
	};

	/// Loads the saved graph file, validates it, evaluates it, collects placements and applies them through
	/// ApplyGeneratedContent (no terrain step in phase 2). Refuses, leaving the world and record untouched, on:
	/// missing graph, parse error, validation issues (message lists them), evaluation error, overlap with another
	/// instance's area, or missing spawn assets.
	GeneratorBuildReport RebuildGraphInstance(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
		const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls);
	GeneratorBuildReport DeleteGraphInstanceContent(IWorldEditor& editor, pcg::GraphInstanceRecord& record);
}
// ApplyGeneratedContent: when content.terrain is empty, skip Unapply-compare/Apply for terrain and only reconcile
// placements (existing terrain contribution, if any, stays as is).
```
- **ProceduralEditMode.**
  - "New Graph Instance" creates a 128 m square at the viewport centre, refused if it overlaps another instance. Its area is edited with the spline editor.
  - The details panel shows:
    - a graph picker (`AssetPickerWidget` over `.hpcgg`, filtered to the active biome by default);
    - seed with Randomize;
    - parameter overrides typed from the graph's `ParameterDef`s, each with a reset;
    - the point limit;
    - the last build report.
  - Rebuild, Rebuild All and Delete behave as they do for the other records.
- **Flags:**
  - `--pcg-new-graph-instance <world> --graph <path> --points x,z;... [--seed n] [--name n] [--set param=value]...`
  - `--pcg-rebuild` and `--pcg-delete` accept graph instance ids (hex). `--pcg-rebuild` with no id rebuilds everything, as today.
  - `--pcg-eval-graph <world> --id <hex> --out <file.json>` evaluates without applying and writes `WriteEvalDump`.

- [ ] **Step 1: Implement** the builder, the optional terrain step, the procedural mode UI and the flags.
- [ ] **Step 2: Build** `mmo_edit`. Expected: clean.
- [ ] **Step 3: Flag tests** on the scratch world, with a test graph: Get Controls(forest) → Sample Area → Spawn Meshes (Foliage, one Falwyn tree mesh). Each must hold:
  - (a) `--pcg-new-graph-instance` then `--pcg-rebuild` gives exit 0, and foliage instances exist inside the area only (count from `--pcg-eval-graph` minus clipped).
  - (b) A second `--pcg-rebuild` leaves the record file, foliage files and `.hwld` byte-identical. Compare SHA-256 before and after.
  - (c) After moving one forest control point with `--pcg-new-control` (a re-created control), a rebuild reports kept > 0 and created/removed only near the change.
  - (d) `--pcg-delete` removes all owned foliage, and the record's owned content is empty.
  - (e) A graph path pointing at a missing file gives exit 1, and the record and foliage are unchanged (hashes equal).
  - (f) A graph with an unbound `$missing` parameter gives exit 1 naming the node, with the world unchanged.
  - (g) Creating an overlapping instance gives exit 1 with "overlap"; an edge-touching instance gives exit 0.
  - (h) `--pcg-eval-graph` heights match `GetSmoothHeightAt` at three sample points (this confirms Task 3).
- [ ] **Step 4: Manual smoke** in a separate mmo_edit process: create an instance, pick the graph, Rebuild, see the foliage, Delete. Record a screenshot path in the report.
- [ ] **Step 5: Commit** `feat(pcg-editor): graph instances with rebuild and delete`

### Task 6: Graph editor instance

**Files:**
- Create:
  - `src/shared/pcg/graph/graph_document.h/.cpp`: the editor model. It is pure (no ImGui), so it is testable.
  - In `src/mmo_edit/editors/pcg_graph_editor/`: `pcg_graph_editor.h/.cpp` (EditorBase), `pcg_graph_editor_instance.h/.cpp` (EditorInstance), `setting_widgets.h/.cpp`.
- Modify: `src/mmo_edit/mmo_edit.cpp` (register the editor); `src/mmo_edit/CMakeLists.txt` if sources are not globbed.
- Test: `src/tests/pcg_tests/test_graph_document.cpp`

**Interfaces:**
- Consumes: `graph::Graph`, `ParseGraph`, `WriteGraph`, `NodeRegistry::All/Find`, `ValidateGraph`, `IsParameterBinding`, `SettingDef::widget`; `AssetPickerWidget`; the material editor's patterns (`CommitMaterialNodes`, `ItemBuilder`, `ItemDeleter`, `CreateNodeDialog`, `InitializeDockLayout`).
- Produces:
```cpp
namespace mmo::pcg::graph
{
	/// Editable graph with stable editor ids. Pin id = (uint64(nodeId) << 16) | (isOutput << 15) | pinIndex;
	/// link id = Hash(fromPinId, toPinId). Unknown node types keep their settings, links and pin names verbatim.
	class GraphDocument
	{
	public:
		bool Load(const String& json, const NodeRegistry& registry, String& error);
		String Save() const;                          // WriteGraph; unknown nodes round-trip unchanged
		uint32 AddNode(const String& type, float x, float y);   // defaults from SettingDefs
		bool RemoveNode(uint32 id);                   // also removes its links
		bool CanLink(uint64 fromPin, uint64 toPin, String& reason) const;   // kind compatibility, no cycle, single-input pins replace
		bool AddLink(uint64 fromPin, uint64 toPin);
		bool RemoveLink(uint64 linkId);
		bool SetSetting(uint32 node, const String& name, const AttributeValue& value);
		const Graph& GetGraph() const;
		Graph& EditGraph();                            // parameters, comments, positions
		uint64 Revision() const;                       // bumped by every mutation
		bool IsUnknownNode(uint32 id) const;
	};
}
```
- **Editor instance** (opened for `.hpcgg`; "Create New PCG Graph" in the asset browser creation menu writes an empty v1 graph). It is a dockspace with four windows:
  - **Graph:** the imgui-node-editor canvas. Nodes are coloured by category, with pins by kind. Unknown nodes are red. Nodes with errors get a red border and the message as a tooltip; nodes with warnings get a yellow mark. Each node shows its last ms and a cache marker.
  - **Palette:** a searchable list grouped by category. The background context menu and a link dropped on empty space open it.
  - **Details:** the selected node's settings, drawn by `setting_widgets` according to type, enum and `widget` (asset / assetList / colour). Each setting has a "bind to parameter" combo listing the graph parameters of the matching type.
  - **Parameters:** add, remove and rename parameters with their default and description.
- Positions are stored in `editorX/Y` through `ed::SetNodePosition` / `GetNodePosition`. `ed::DestroyEditor` is called in the destructor. Save writes through `AssetRegistry`; the `modified` and `saved` signals are wired.

- [ ] **Step 1: Write failing tests** `[graph_document]`:
  - Load and Save of a graph with an unknown node type (settings and links intact) is text-identical.
  - `AddNode` uses the setting defaults.
  - `CanLink` refuses mismatched kinds, self-links and cycles.
  - Linking into a single-input pin replaces the old link.
  - `RemoveNode` drops its links.
  - Pin and link ids are stable across Save and Load.
  - `Revision` increases on every mutation.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_document]"`. Expected: FAIL.
- [ ] **Step 3: Implement** `GraphDocument`. Run the tests; expected: PASS. Run the gcc syntax check.
- [ ] **Step 4: Implement** the editor and editor instance, register them, and build `mmo_edit`. Expected: clean.
- [ ] **Step 5: Manual smoke** in a separate mmo_edit process:
  - create a graph and add Get Controls → Sample Area → Spawn Meshes;
  - link them, set settings, bind one setting to a new parameter;
  - save, close and reopen, and check that positions, links and settings persisted.
  - Record a screenshot path in the report.
- [ ] **Step 6: Commit** `feat(pcg-editor): graph editor`

### Task 7: Per-node debug view in the world

**Files:**
- Create:
  - `src/shared/pcg/graph/debug_geometry.h/.cpp` (pure);
  - `src/mmo_edit/editors/world_editor/pcg/pcg_debug_layer.h/.cpp`.
- Modify:
  - `pcg_graph_editor_instance.h/.cpp`: target selector, evaluation loop, legend;
  - `world_editor_instance.h/.cpp`: host the debug layers.
- Test: `src/tests/pcg_tests/test_graph_debug_geometry.cpp`

**Interfaces:**
- Consumes: Task 3 `GetOpenInstances` / `FindOpenInstance` / `GetTerrainRevision`; Task 4 `GetControlsRevision`; Task 5 `GraphInstanceContext`; Task 6 `GraphDocument::Revision`; `Evaluate` with a persistent `GraphCache`.
- Produces:
```cpp
namespace mmo::pcg::graph
{
	struct DebugLine { Vector3 a, b; uint32 argb; };
	struct DebugGeometry { std::vector<DebugLine> lines; size_t pointsTotal = 0, pointsDrawn = 0; };
	/// Points: oriented boxes from rotation/scale/bounds (12 lines; beyond `boxLimit` points, a short vertical tick
	/// per point; beyond `pointLimit`, an every-k-th subsample, recorded in pointsDrawn). Splines/areas: polylines at
	/// the SampledPath step. Colour per node from `argb`, brightness scaled by density.
	DebugGeometry BuildDebugGeometry(const Collection& items, uint32 argb, size_t boxLimit = 20'000, size_t pointLimit = 200'000);
}
namespace mmo
{
	// WorldEditorInstance:
	//   void SetPcgDebugLayer(uint64 ownerKey, std::vector<pcg::graph::DebugLine> lines);  // replaces, redraws
	//   void ClearPcgDebugLayer(uint64 ownerKey);
	// One ManualRenderObject per owner ("Editor/Wireframe.hmat", no shadows, query flags 0), destroyed on clear
	// and in the world instance's destructor. Debug layers are never saved.
}
```
- **Graph editor "Debug" window:**
  - The target combo lists the graph instances in every open world whose `graphPath` equals this asset. It stores (world serial, instance id) and looks the world up every frame through `FindOpenInstance`, never as a stored pointer.
  - Node debug toggles come from `NodeInstance::debug`, with a toggle button on each node. Each node gets a colour from a fixed palette by its index.
  - The legend lists node, colour and "showing N of M".
  - Debug is re-evaluated when any of these changes: document revision, controls revision, terrain revision, control-type library revision, or the instance's settings.
  - Evaluation uses the in-memory document. The panel shows "Rebuild uses the saved file" while there are unsaved changes.
  - Closing the graph editor or the target world clears the layer.

- [ ] **Step 1: Write failing tests** `[graph_debug]`:
  - 1 point gives 12 lines.
  - 30 000 points with `boxLimit` 20 000 give 20 000×12 + 10 000 lines, with `pointsDrawn` 30 000.
  - 500 000 points with `pointLimit` 200 000 give `pointsDrawn` ≤ 200 000 and `pointsTotal` 500 000, and the subsample is deterministic.
  - A closed spline gives a closed polyline.
  - An empty collection gives no lines.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_debug]"`. Expected: FAIL. **Step 3: Implement** it. **Step 4:** PASS, then the gcc check.
- [ ] **Step 5: Implement** the debug layer and the graph editor Debug window, then build `mmo_edit`.
- [ ] **Step 6: Manual smoke** in a separate mmo_edit process, with the scratch world and the Task 5 graph open:
  - toggling debug on Sample Area draws cubes;
  - dragging a forest control point or changing `minSpacing` refreshes within a second;
  - closing the world clears the layer with no crash;
  - an unsaved change shows the hint.
  - Record screenshot paths.
- [ ] **Step 7: Commit** `feat(pcg-editor): per-node debug view`

### Task 8: Control type editor

**Files:**
- Create: `src/mmo_edit/editors/pcg_control_type_editor/pcg_control_type_editor.h/.cpp`, `pcg_control_type_editor_instance.h/.cpp`
- Modify: `src/mmo_edit/mmo_edit.cpp` (register the editor)

**Interfaces:**
- Consumes: `graph::ControlType`, `ParseControlType`, `WriteControlType`, `PropertyType`, `AttributeTypeOf`; Task 4 `ControlLibrary::Invalidate`. The library is reached through the open world instances' procedural modes, so every open world invalidates.
- Produces: an editor for `.hctl`. "Create New Control Type" writes a default `ClosedLoop` type with no groups.
- **Panel:**
  - shape combo;
  - default tags (comma list);
  - property groups (add, remove, rename, reorder);
  - per property: name, type, default (widget by type, using the same `setting_widgets` from Task 6), min/max, enum values, description.
- **Validation before save:** duplicate property names and an empty name block saving, with a red message. Save writes the JSON and calls `Invalidate`.

- [ ] **Step 1: Implement** the editor and register it. Build `mmo_edit`. Expected: clean.
- [ ] **Step 2: Manual smoke** in a separate mmo_edit process:
  - create `Forest Area.hctl`, add a `density` Float (0.5, range 0–1) and `minSpacing` (6);
  - save, reopen, and check that the values persisted;
  - with a world open that has a placed control of this type, the new property appears in its details without reopening the world.
  - Record screenshot paths.
- [ ] **Step 3: Commit** `feat(pcg-editor): control type editor`

### Task 9: First biome content and end-to-end acceptance in world 2

**Files (data submodule, branch `feature/pcg-oasis-poc`):**
- `data/client/Editor/Pcg/Biomes/FalwynForest/Controls/{ForestArea,Exclusion,Road}.hctl`
- `data/client/Editor/Pcg/Biomes/FalwynForest/Graphs/ForestTest.hpcgg`
- `Worlds/Collision` records

**Interfaces:**
- Consumes: everything above, through the editor (authoring) and the flags (reproducible build).
- **Control types:**
  - `ForestArea`: ClosedLoop, tag `forest`, `density` 0.5, `minSpacing` 6.
  - `Exclusion`: ClosedLoop, tag `exclusion`.
  - `Road`: OpenSpline, tags `road` and `exclusion`, `clearance` 6.
- **Graph `ForestTest`:**
  - Get Controls(forest) → Sample Area (density from the `density` attribute, `minSpacing` bound to `$minSpacing`) → Difference(Get Controls(exclusion)) → Self Prune(`$minSpacing`) → Filter by Terrain(slope ≤ 30°) → Transform Points (yaw 0–360, scale 0.8–1.2, upright) → Spawn Meshes (Foliage; 2–3 existing Falwyn tree meshes, weighted).
  - Parameter `minSpacing` = 6.
- **Placement in world 2** (`Worlds/Collision`), inside the phase 0 valley (pages x31–33 / z36–38):
  - one graph instance covering the valley floor;
  - two forest areas;
  - one exclusion where the user will build a camp;
  - one road across.
  - All are created with the flags, so the build is reproducible. Record the exact commands in the report.

- [ ] **Step 1: Author** the control types and the graph in the editors, and place the controls and instance with the flags.
- [ ] **Step 2: `--pcg-rebuild`** world 2. Expected: exit 0, and foliage inside the instance area only.
- [ ] **Step 3: Acceptance checks**, each with its evidence in the report:
  - **No foliage in excluded space:** none inside the exclusion or within 6 m of the road (from `--pcg-eval-graph` output plus the clipped count).
  - **Rebuild is idempotent:** a second rebuild leaves the files byte-identical (SHA-256).
  - **Hand content survives:** a hand-placed entity inside the exclusion is still there after the rebuild.
  - **Hand terrain edits survive:** a hand terrain edit in the area survives the rebuild (phase 2 writes no terrain).
  - **Delete is clean:** delete removes all generated foliage, and a rebuild restores identical content.
  - **Map 0 is untouched:** `git -C data/client diff --stat` shows no map 0 files.
- [ ] **Step 4: `--capture-view`** of the valley from two viewpoints, with the debug view off. Save to `docs/superpowers/specs/pcg-graphs/phase2-*.png`.
- [ ] **Step 5: Commit** the data in the submodule (`data(pcg): Falwyn forest test biome and world 2 forest instance`). Then commit the gitlink bump and the captures in the main repo (`data(pcg): bump data/client to the phase 2 forest test`).

---

## Out of scope for phase 2 (later)

- **Phase 3 nodes:** terrain, water and section nodes. With them, terrain nodes in the debug view will need to read the base terrain as it is after Unapply.
- **Evaluation engine:** cancellation and progress for `Evaluate`, and shared (not copied) node outputs.
- **Editor features:**
  - a *Debug Probe* node;
  - subgraphs;
  - undo/redo in the graph editor;
  - per-sample ownership across overlapping regions.
- **Moving content into biomes:** existing oasis and Section Path records stay as they are, and existing sections move under biomes only when a biome uses them.
