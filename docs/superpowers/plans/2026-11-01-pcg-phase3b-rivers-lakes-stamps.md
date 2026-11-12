# PCG Phase 3b — Rivers, Lake Basins, Stamped Sections and Waterfalls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Graphs carve rivers that carry water and flow, sink lakes into basins, stamp a section at any point, and express the waterfall rule. The Falwyn valley in world 2 gets a stream that falls into its lake over a waterfall.

**Architecture:** Everything new stays inside the phase 3a model:
- New terrain work is new op types (`River`, plus a basin on `WaterArea`) composed by `PreparedTerrain` in stack order.
- Stamp Section at Points emits ordinary `SectionPath` ops on a short generated two-point spline. The section code stays as it is.
- River flow is the one output that is not a terrain sample. It is per page (WFLW) and computed from all river paths of a world at once. The graph instance record therefore stores its river paths. After every rebuild or delete the editor recomputes flow from the world's hand rivers (`.hflow`) plus the paths of every graph instance, using the existing `Terrain::ApplyWaterFlow`. The hand river editor includes the generated paths too, so neither side wipes the other.
- Explicit `stackOrder` and seam-aware border fades close two phase 3a leftovers.

**Tech Stack:** C++17, `pcg` lib (`graph`, `section_path`, `terrain_contribution`), `terrain_io::water_flow`, mmo_edit, Catch2, unattended flags and `--capture-view`.

**Spec:** `docs/superpowers/specs/2026-10-25-pcg-graphs-design.md`, specifically:
- "Node set v1": *River* (bed + water heights + flow from a spline), *Water Area*, *Stamp Section at Points*;
- the waterfall rule paragraph under "Node set v1" (the acceptance example for expressiveness);
- "Terrain stacking", "Resolved" items 1 and 3;
- "Carried into phase 3".

Phase 3a's plan (`docs/superpowers/plans/2026-11-01-pcg-phase3a-terrain-sections.md`) defines every interface this plan builds on: `TerrainOp`, `TerrainOpInput`, `CollectTerrainOps`, `PreparedTerrain`, the rebuild flow in `graph_instance_builder.cpp`, and `BaseTerrainView`.

## Global Constraints

- **Branch and data.**
  - Branch `feature/pcg-oasis-poc`; never push, ship or merge.
  - Data goes only into the worktree's `data/client` submodule, on its branch `feature/pcg-oasis-poc`, never `master`.
  - Never touch `H:\mmo` outside this worktree.
  - Never kill an mmo_edit process you did not start.
  - Never stage files the user made with their own editor use (for example a stray `Editor/Pcg/*.hpcgg`); ask the controller.
- **No UI driving.** No computer use; the user declined it. Editor checks use unattended flags and `--capture-view`. Interactive checks go on a list for the user.
- **Map 0 is frozen.** No new PCG output, and no terrain, placement, NPC or flow changes on map 0 (`Development.hwld`). The test site is world 2 (`Worlds/Collision`).
- **Editor-only.** Nothing PCG ships to clients. Flow data written into pages is ordinary terrain data, like heights.
- **Pure library code** (`src/shared/pcg/`):
  - headless, gcc-compatible, no exceptions;
  - deterministic: hash-based randomness only, world-anchored noise, no unordered containers on output paths;
  - tests go in `src/tests/pcg_tests/`.
- **Existing graphs keep their output.** Every new setting defaults to the phase 3a behaviour: `stackOrder` 0, `WaterArea.depth` 0, no seam edges unless a neighbour exists. Rebuilding the committed FalwynValley instance before Task 7 changes no file.
- **Relative terrain.** River banks, basins and stamps follow the phase 3a rule: a target is computed from the running height. Inside a river bed or basin the op owns the height, like a section corridor ("Resolved" item 1).
- **Clipping at apply time.** Terrain writes are clipped to the region with the border fade. River flow paths are clipped to the region too (Task 4).
- **Code style per CLAUDE.md:**
  - Allman braces, tabs, braces on every `if`;
  - `m_camelCase` members, `PascalCase` methods, `camelCase` locals, `snake_case` files;
  - `#pragma once`, Doxygen on public members;
  - copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- **Build:**
  - `cmake --build build --config Debug -t pcg_tests mmo_edit -- /m /v:minimal`;
  - for new files: `cmake -S . -B build` first;
  - gcc-13 `-fsyntax-only -Wall -Wextra` in WSL on changed pure-lib files.
- **Commits** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **A hand-drawn river (`.hflow`) shares pages with a generated river.** Rebuilding or deleting the graph instance keeps the hand river's flow, and editing the hand river in Water mode keeps the generated flow. (Task 5 flag check (c); the Water-mode half goes on the user's UI checklist.)
2. **A river spline that climbs downstream, or one drawn upstream-first.** The water surface never rises downstream: it is clamped, and a warning names the control and the climb. No water sheet hangs above the banks. (Task 4 composition test.)
3. **Rebuild twice with a river, a basin and stamps.** Pages, including their WFLW flow chunks, foliage, entities and the record are byte-identical. (Task 4 determinism test; Task 5 and Task 7 flag checks.)
4. **A lake whose base rim is lower than its level.** The water would spill past the margin as a floating sheet. Prepare warns with the control named and the lowest rim height; the basin still carves. (Task 2 test.)
5. **Stamps and rivers crossing the region border.** Terrain fades as in 3a, stamped props are dropped where the fade is below 0.5, flow paths stop at the border, and nothing outside the region changes except flow on pages within the flow falloff (documented). (Task 3 and Task 4 tests; Task 5 check.)

---

### Task 1: Explicit stack order

**Files:**
- Modify:
  - `src/shared/pcg/graph/terrain_op.h/.cpp` (field, hash);
  - `src/shared/pcg/graph/terrain_compose.h/.cpp` (`CollectTerrainOps` sort);
  - `src/shared/pcg/graph/nodes_terrain.cpp`, `nodes_section.cpp` (setting);
  - `src/shared/pcg/graph/graph_output.cpp` (eval dump).
- Test: `src/tests/pcg_tests/test_graph_terrain_compose.cpp`, `test_graph_terrain_op.cpp`

**Interfaces:**
- Produces:
```cpp
// terrain_op.h — TerrainOp gains:
int32 stackOrder = 0;   // composition order across nodes; ties fall back to op.order (node id), then item index
```
- Every node that emits Terrain items (the five terrain nodes, both Lay Section nodes, and the nodes of Tasks 3–4) gets the setting `stackOrder` (Int, default 0, range −1000..1000, description "Lower values are stacked first; equal values keep node order"). It can be overridden per item by an item attribute of the same name, like the other settings.
- `CollectTerrainOps` sorts by `(stackOrder, order, node id, item index)`. Placement keys keep using `op.order`, so changing `stackOrder` never changes placement identity.
- `HashTerrainOp` covers `stackOrder`. The eval dump writes it only when it is not 0, so existing dumps stay byte-identical.

- [ ] **Step 1: Write failing tests** `[graph_terrain_op]`, `[terrain_compose]`:
  - Node 5 with `stackOrder` −1 is composed before node 2 with `stackOrder` 0: a SectionPath op and a Carve op over the same sample give the other result than with both at 0.
  - All at 0: the order equals the phase 3a order (same `TerrainTarget` at 50 samples as before the change).
  - Changing `stackOrder` changes the content hash but not `SectionPlacementKey`s of the section's placements.
  - An eval dump of a graph without the setting is byte-identical to the committed one in `test_graph_terrain_op.cpp`.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_terrain_op],[terrain_compose]"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the full `pcg_tests` (expected: all pass) and build `mmo_edit`. Then run the gcc check. Then `--pcg-rebuild --id 54f16e831be4eb82` on world 2: `git status` in `data/client` shows no change.
- [ ] **Step 5: Commit** `feat(pcg): explicit stack order for terrain ops`

### Task 2: Lake basin on Water Area

**Files:**
- Modify:
  - `src/shared/pcg/graph/nodes_terrain.cpp` (`WaterArea` settings);
  - `src/shared/pcg/graph/terrain_compose.h/.cpp` (WaterArea writes heights when `depth` > 0; rim check in `Prepare`; outline);
  - `src/shared/pcg/graph/terrain_debug.cpp` (basin drawing).
- Test: `test_graph_terrain_compose.cpp`, `test_graph_nodes_terrain.cpp`, `test_graph_terrain_debug.cpp`

**Interfaces:**
- `WaterArea` gains `depth` (Float, 0 = no basin, 0..200) and `shoreWidth` (Float, 6, 0.5..200).
- **Composition with `depth` > 0.** Let `d` be the inward distance from the area edge and `s = smoothstep(0, shoreWidth, d)`. Inside the area, target = `min(h, h + (level + levelOffset − depth − h) × s)` at weight 1, so the shore starts at the running height and the bed reaches `level + levelOffset − depth` after `shoreWidth`. Outside the edge the heights are untouched. Water is as in 3a. With `depth` 0 the op writes no heights, exactly as before.
- **Rim check.** `Prepare` samples the base ground every 2 m along the area outline. If any sample is below `level + levelOffset`, it records one warning: `WaterArea (node <id>, control <sourceId>): base rim drops to <min> m, below the water level <level> m; water will show past the shore`.
- **Channels.** A heights-only `Evaluate` runs the basin; a water-only one does not.
- **Outline.** The basin's outline carries the area points and draws in the debug view as the shore loop plus one "depth" tick at the area's centroid.

- [ ] **Step 1: Write failing tests** `[terrain_compose]`, `[graph_nodes_terrain]`:
  - On a flat base at 25 m with level 21.5, depth 3, shoreWidth 6: height 25 at the edge, 21.75 at 3 m inside (s = 0.5), 18.5 at 10 m inside; water height 21.5 inside.
  - A hand bump of +2 m at 10 m inside is still clamped to 18.5 (the basin owns the bed). A hand bump outside the area survives.
  - `depth` 0 gives the same `TerrainTarget` as before the change at 50 samples.
  - Rim lower than the level → exactly one warning with the documented text; a rim above → none.
  - Water-only `Evaluate` with `depth` 3 returns heightWeight 0.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[terrain_compose],[graph_nodes_terrain]"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the full `pcg_tests` (PASS) and the gcc check.
- [ ] **Step 5: Commit** `feat(pcg): lake basins on Water Area`

### Task 3: Stamp Section at Points

**Files:**
- Modify:
  - `src/shared/pcg/graph/nodes_section.cpp` (new node, registered in `RegisterSectionNodes`);
  - `src/shared/pcg/graph/builtin_nodes.h` (comment).
- Test: `src/tests/pcg_tests/test_graph_nodes_section.cpp`

**Interfaces:**
- Consumes: `TerrainOp` (type `SectionPath`), `SetSectionPathParam`, `SectionAssetProvider` (via `PreparedTerrain`).
- Produces the node `StampSectionAtPoints`, category `Spawners`:
  - input `In` (Points);
  - output pin `Terrain`, one Terrain item per point, `op.type = SectionPath`;
  - settings: `section` (String, widget `asset:.hsec`), `anchor` (`center` | `start`, default `center`), `length` (Float, 0 = the asset's length), plus `stackOrder` and every section param the Lay Section nodes accept, with the same defaults except `heightMode`, which defaults to `FollowSpline` (a stamp follows its point's height).
- **Geometry.** Direction `f` = the point's rotation applied to +Z, projected onto XZ and normalised. If it is degenerate, use +Z and record one warning per node. With `L` = the length:
  - `center`: spline points `p − f·L/2` and `p + f·L/2`;
  - `start`: spline points `p` and `p + f·L`.

  Both points take the point's y. The spline is open and linear.
- **Identity.** `pointIds` = `{ Hash(point seed, 0), Hash(point seed, 1) }` and `sourceId` = the point's item `sourceId`. Two stamps of one node never share a placement key.
- **Order.** Items follow point order, so `itemIndex` = point index across the node's input items, in input order.

- [ ] **Step 1: Write failing tests** `[graph_nodes_section]`:
  - Three points with yaws 0°, 90° and 180° give three items with the documented spline ends for `center` and `start`.
  - The point's y lands in both spline points, and `heightMode` is `FollowSpline` unless overridden.
  - Composing one stamp of an in-memory 20 m test section through `PreparedTerrain` lays exactly one copy, and its props sit at the expected world positions.
  - Two stamps 1 m apart with different seeds give distinct placement keys.
  - A point outside the region gives no props, and a stamp half across the border keeps only props where the fade is ≥ 0.5.
  - A missing `section` is a validation issue naming the node.
  - Zero points give zero items.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_nodes_section]"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the full `pcg_tests` (PASS) and the gcc check.
- [ ] **Step 5: Commit** `feat(pcg): Stamp Section at Points node`

### Task 4: River op and node

**Files:**
- Modify:
  - `src/shared/pcg/graph/terrain_op.h/.cpp` (`TerrainOpType::River`, name "River");
  - `src/shared/pcg/graph/nodes_terrain.cpp` (node);
  - `src/shared/pcg/graph/terrain_compose.h/.cpp` (prepare, compose, flow paths, outline);
  - `src/shared/pcg/graph/terrain_debug.cpp` (drawing);
  - `src/shared/pcg/CMakeLists.txt` (link `terrain_io` if it is not linked yet; `water_flow` must stay usable on the headless Linux build).
- Test: `test_graph_terrain_compose.cpp`, `test_graph_nodes_terrain.cpp`, `test_graph_terrain_debug.cpp`

**Interfaces:**
- Node `River`, category `Terrain`, input Spline, output pin `Terrain`.

  | Setting | Default | Meaning |
  |---|---|---|
  | `width` | 5 | half width of the bed floor (per side, like Carve) |
  | `bank` | 4 | width beyond the bed over which the bank rises back to the running height |
  | `depth` | 1.5 | bed depth below the water surface |
  | `surfaceOffset` | −0.4 | water surface relative to the spline height |
  | `waterMargin` | 2 | water extends this far beyond the bed, so the bank's waterline is covered |
  | `material` | `Worlds/Water_River.hmat` | widget `asset:.hmat`, stored in `op.asset` |
  | `stackOrder` | 0 | |

- **Water surface.** The spline is sampled every 1 m downstream, from the first point to the last. `surface(s)` is the running minimum of `y(s) + surfaceOffset`. If the spline climbs by more than 0.5 m against that minimum, `Prepare` records one warning naming the node, the control and the largest climb. That catches a river drawn upstream-first.
- **Heights.** With `d` = the distance to the spline and `t` = the surface at the projected arc distance: inside `width`, target = `t − depth`. Over `bank`, the target blends to the running height by `smoothstep(width + bank, width, d)` and is never above it (`min(h, …)`). The weight is 1 wherever `d ≤ width + bank`.
- **Water.** `waterType` 1 and `waterHeight = t` where `d ≤ width + waterMargin`. Water is last writer wins, as for `WaterArea`.
- **Flow.** `PreparedTerrain::GetFlowPaths(std::vector<terrain_io::water_flow::FlowPath>& out) const` returns, in op order, the river samples every 4 m with `halfWidth = width + waterMargin`, cut into runs where the region fade is > 0. Each run of two points or more is one path, in downstream order.
- **Material.** `op.asset` takes part in `GetWaterMaterial` like a WaterArea material. When a region has both, River materials win: the flow only shows with a material that reads it, and lakes on the same page still show still water at minimum speed. Record a warning when they differ.
- **Outline and debug.** The river draws its surface line (blue) and its bed edges at `±width` (brown).

- [ ] **Step 1: Write failing tests** `[terrain_compose]`, `[graph_nodes_terrain]`:
  - On a 2 % slope with a straight river whose spline lies on the ground: bed = surface − 1.5 at the centre; at `width + bank` the height equals the base; there is water inside `width + waterMargin` and none beyond.
  - A spline with a 2 m bump in the middle: the surface stays flat over the bump (running minimum) and one warning names the climb (≥ 1.9 m).
  - A spline reversed (uphill): one warning, and the surface stays flat at the first point's height minus the offset.
  - `GetFlowPaths` of a river crossing the region border gives one path that ends at the last sample with fade > 0, with a 4 m point spacing and the documented half width.
  - A River and a WaterArea with different materials: the River's material, plus one warning.
  - Two prepares of the same input give identical targets at 200 samples and identical flow paths.
  - The node: an attribute override of `width` works; Area input is a validation issue.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[terrain_compose],[graph_nodes_terrain]"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the full `pcg_tests` (PASS), build `mmo_edit`, run the gcc check.
- [ ] **Step 5: Commit** `feat(pcg): River op with bed, water surface and flow paths`

### Task 5: River flow in rebuild, delete and Water mode

**Files:**
- Create: `src/mmo_edit/editors/world_editor/flow_paths_file.h/.cpp` (moved out of `WaterEditMode::LoadFlowPaths` / `SaveFlowPaths`)
- Modify:
  - `src/shared/pcg/graph_instance_record.h/.cpp` (field, version 2);
  - `src/mmo_edit/editors/world_editor/pcg/graph_instance_builder.cpp` (apply flow after the terrain write and after delete);
  - `src/mmo_edit/editors/world_editor/edit_modes/water_edit_mode.h/.cpp` (use the file helper; include generated paths);
  - `src/mmo_edit/editors/world_editor/pcg/procedural_edit_mode.h/.cpp` (getter for the generated paths of all instances);
  - `src/mmo_edit/editors/world_editor/world_editor_instance.cpp` (wire the getter).
- Test: `test_graph_instance_record.cpp`

**Interfaces:**
```cpp
// graph_instance_record.h — GraphInstanceRecord gains:
std::vector<terrain_io::water_flow::FlowPath> flowPaths;   // river paths written by the last build, op order
// Format version 2 = version 1 plus the flow paths (count, then per path count and x, z, halfWidth floats).
// Version 1 reads with no paths. Writing an instance without paths still writes version 2.

// flow_paths_file.h
namespace mmo
{
	struct FlowPathsFile
	{
		std::vector<std::pair<String, PathSpline>> rivers;     // name, path (downstream order)
		terrain_io::water_flow::FlowSettings settings;
	};
	/// Reads a world's .hflow file; a missing file gives no rivers and default settings (true).
	bool ReadFlowPathsFile(const String& fileName, FlowPathsFile& out, String& error);
	bool WriteFlowPathsFile(const String& fileName, const FlowPathsFile& file, String& error);
}

// procedural_edit_mode.h
/// Flow paths of every graph instance of the world, instances ordered by id.
std::vector<terrain_io::water_flow::FlowPath> CollectGeneratedFlowPaths() const;
```
- **Rebuild.** After a successful terrain write (and only then), `paths` = the hand rivers from the world's `.hflow` (via `ReadFlowPathsFile`, in file order) followed by `CollectGeneratedFlowPaths()` with this instance's paths replaced by `PreparedTerrain::GetFlowPaths`. Call `Terrain::ApplyWaterFlow(paths, settings, previous, settings, material)` where:
  - `previous` = this instance's old `flowPaths`;
  - `settings` = the `.hflow` settings;
  - `material` = `Worlds/Water_River.hmat` if the new paths are not empty, else empty.

  Then store the new paths in the record. A refused rebuild or a failed prepare touches no flow.
- **Delete** does the same with empty new paths.
- **No flow change.** When old and new paths are equal (bit-exact), skip `ApplyWaterFlow`. That keeps "rebuild twice" byte-identical without relying on page rewrites.
- **Water mode.** `WaterEditMode::CollectFlowPaths` appends `CollectGeneratedFlowPaths()` after the hand rivers, and so does its "previous" set. Generated paths are shown read-only in the overlay (grey arrows, not selectable). Saving `.hflow` writes only the hand rivers.
- **Rebuild All** keeps its order (terrain writers, then graph instances by id). Flow is applied per instance as it is rebuilt.

- [ ] **Step 1: Write failing tests** `[graph_instance_record]`:
  - A record with two flow paths round-trips.
  - A version 1 blob reads with no paths.
  - The written bytes are deterministic.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_instance_record]"`. Expected: FAIL. **Step 3: Implement** the record, `flow_paths_file`, the builder and the Water mode wiring. **Step 4:** full `pcg_tests` PASS, `mmo_edit` builds.
- [ ] **Step 5: Flag checks** on a scratch copy of world 2 inside the worktree (`Worlds/Collision`; restore with `git checkout` in `data/client` afterwards). Use a temporary River control through `--pcg-new-control` and a scratch graph `Editor/Pcg/Biomes/FalwynForest/Graphs/RiverScratch.hpcgg` (deleted afterwards):
  - (a) Rebuild: pages under the river gain a WFLW chunk; the record lists the paths.
  - (b) Rebuild again: no file changes.
  - (c) Add a hand river to `Worlds/Collision.hflow` (scratch, hand-written JSON in the `.hflow` format that `WriteFlowPathsFile` writes) crossing a generated-river page. Rebuild, then delete the instance: the hand river's pages keep their flow, and the pages only the generated river reached lose theirs.
  - (d) A rebuild refused by a missing section leaves every WFLW chunk byte-identical.
  - Record the commands and hashes in the report.
- [ ] **Step 6: Commit** `feat(pcg-editor): generated river flow shares the world's flow with hand rivers`

### Task 6: Shared borders between touching regions

**Files:**
- Modify:
  - `src/shared/pcg/graph/terrain_compose.h/.cpp` (`Prepare` takes neighbour areas; `RegionFade` ignores seam edges);
  - `src/shared/pcg/graph/base_terrain_view.cpp` only if it calls `Prepare`;
  - `src/mmo_edit/editors/world_editor/pcg/graph_instance_builder.cpp` and `pcg_graph_editor_instance.cpp` (pass the neighbours; include them in the debug cache key).
- Test: `test_graph_terrain_compose.cpp`

**Interfaces:**
```cpp
// PreparedTerrain::Prepare gains a trailing parameter:
//   const std::vector<PathSpline>& neighbours
// Areas of the other graph instances in the world that contain terrain nodes. A region edge segment is a seam
// where it lies on a neighbour edge (both end points within 0.05 m of that edge, collinear within 0.05 m).
// RegionFade measures the distance only to non-seam edges, so terrain does not fade at a seam.
```
- The caller passes every other graph instance whose graph has terrain ops (the R11 rule), sorted by id. Writes still stop at the region edge. The partition rule of phase 2 (ruling R7) keeps each seam sample owned by exactly one region.
- **Documented consequence.** When a neighbour is created or deleted, the regions next to it must be rebuilt for their seams to change. Rebuild All does that. A single rebuild logs `seams changed since the last build of <neighbour name>; rebuild it too` when the seam set differs from the one recorded for the last build. Store the seam set as a hash in the record's JSON blob, key `seamHash`; a missing key reads as 0.

- [ ] **Step 1: Write failing tests** `[terrain_compose]`:
  - Two squares A and B share one edge, with a Noise op covering both. At the seam, A's weight is 1 on A's side and B's is 1 on B's side, and the heights at 0.25 m on each side differ by less than the noise change over 0.5 m.
  - A's three outer edges still fade over `borderFade`.
  - With no neighbours the result is identical to before at 100 samples.
  - A neighbour that only touches at a corner creates no seam.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[terrain_compose]"`. Expected: FAIL. **Step 3: Implement.** **Step 4:** full suite PASS, gcc check, `mmo_edit` builds, and the FalwynValley rebuild changes no file (it has no neighbour).
- [ ] **Step 5: Commit** `feat(pcg): no border fade along seams with touching regions`

### Task 7: A stream with a waterfall into the Falwyn lake

**Files (data submodule, branch `feature/pcg-oasis-poc`):**
- `Biomes/FalwynForest/Controls/River.hctl`;
- `Biomes/FalwynForest/Sections/Waterfall/` (section world) and `Waterfall.hsec`;
- `Biomes/FalwynForest/Graphs/FalwynValley.hpcgg`;
- `Worlds/Collision` records, terrain, foliage and entities.

Main repo: captures in `docs/superpowers/specs/pcg-graphs/phase3b-*.png`.

**Interfaces:**
- Consumes everything above through the flags. Control types and graphs are written as JSON in `WriteControlType`/`WriteGraph` format and validated through the flags (phase 2 ruling R23).
- **`River` control type:** OpenSpline, tags `river`, `water` and `exclusion`. Properties `width` (Float 4) and `depth` (Float 1.5).
- **`Waterfall` section:** built with `--new-section Waterfall --folder Editor/Pcg/Biomes/FalwynForest/Sections`, edited with scratch Python in the scratchpad (as for ValleyWall) and baked with `--bake-section`. It has one Straight variant, L ≈ 12 m and halfWidth ≈ 6 m:
  - a rock lip at local z 0–3 that is level with 0;
  - one `Models/Desert/Water/SM_Waterfall*.hmsh` (pick the size that fits a 3–6 m drop; record the choice) at z ≈ 3 facing +Z;
  - a few Falwyn rocks on both sides.

  It is a placeholder; the user refines it by hand.
- **Graph additions to `FalwynValley`:**
  - Get Controls(river) → River (`width`/`depth` from the control's attributes).
  - Waterfall rule, as in the spec and the phase 1 acceptance test, with real nodes:
    1. Get Controls(river) → Sample Spline(`endsOnly`, `alignToPath`);
    2. Filter by Attribute(`distance` > 0), which keeps the downstream end;
    3. Distance To(Get Controls(water), `distanceToWater`) → Filter(`distanceToWater` ≤ 15);
    4. Copy Attributes From Control(`level`, from the water controls) → Attribute Math(`drop` = `position.y` − `level`) → Filter(`drop` ≥ 3);
    5. Stamp Section at Points(`Waterfall.hsec`, `anchor` start, `stackOrder` 10, so it lies after the river bed).
  - Water Area for lakes with `depth` 3 and `shoreWidth` 6.
  - The forest Difference already takes Get Controls(water); the river's `exclusion` tag also clears trees from it.
- **World 2 content:**
  - one River control from higher ground in the valley down to MeadowLake, ending 8–12 m from the lake shore with a drop of at least 3 m to the lake level 21.5;
  - where the base has no such drop, block one out with the terrain tools / `terrain_tool` as hand base (world 2 is not frozen), and record it.

- [ ] **Step 1: Snapshot** captures of the current valley at the lake (phase 3a viewpoints) as the baseline.
- [ ] **Step 2: Author** the section, control type, graph and control as above. Rebuild. Record every command.
- [ ] **Step 3: Acceptance**, with evidence in the report:
  - **Stream:** its bed is carved and it holds water; the water surface never rises downstream (sample 20 points). Flow arrows exist on its pages (WFLW present). No tree stands within the river width + 2 m.
  - **Waterfall:** exactly one is stamped, at the downstream end; none at the upstream end. Changing the River's end to 20 m from the lake and rebuilding removes it; restore afterwards.
  - **Lake basin:** the bed is ≤ 18.5 m at its middle, the shore stays dry outside the margin, and there is no rim warning (or the rim was fixed and that is recorded).
  - **Walls, road and forest:** unchanged against the phase 3a state outside the river corridor and the lake (heights Δ 0.000 at 50 samples away from them; tree count difference explained by the river exclusion).
  - **Idempotent:** a second rebuild leaves all files, WFLW included, byte-identical.
  - **Hand content survives:** a hand terrain edit survives (restore it afterwards), and non-PCG content is unchanged.
  - **Delete restores the base:** deleting the instance removes all generated water, flow and props and restores the base. Then re-create and rebuild for the committed state.
  - **Map 0 untouched:** no map 0 file in any diff.
- [ ] **Step 4: Captures** `phase3b-stream.png`, `phase3b-waterfall.png`, `phase3b-lake-basin.png`.
- [ ] **Step 5: Commit** in the data submodule (`data(pcg): Falwyn stream with waterfall and lake basin`). Then commit the gitlink bump and the captures in the main repo, and append "Decided while building phase 3b" to the spec with the rulings made during execution.

---

## Out of scope (phase 4 and later)

- **Debug Probe node** and per-point attribute inspection.
- **Cancellation and progress** for long evaluations.
- **Overlap checks against the region outline** instead of its bounding box (phase 3a F4).
- **The ocean-to-terrain edge case** noted in phase 3a F4.
- **Stale `.tlod` far-terrain bakes** for the valley pages (the user's decision).
- **Rivers that cross region borders with different graphs on each side.** Flow is per world and fine; the beds meet with the seam rule of Task 6 only if both graphs carve them.
- **Flow reach outside the region.** `ApplyWaterFlow` may write flow to pages within the flow falloff (60 m) outside the region where water exists. It is deterministic and undone on delete, but not clipped.
- **Moving the oasis into a graph.**
- **Interactive UI checks.** The user runs them by hand; each task report ends with its checklist.
