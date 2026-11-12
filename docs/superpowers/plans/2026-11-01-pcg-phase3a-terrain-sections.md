# PCG Phase 3a — Terrain and Section Nodes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Graphs write terrain and lay hand-built sections.
- A road control drives both the forest clearance and the road sections.
- A valley control lays the cliff walls.
- Forests roughen the ground, lakes fill with water, and paint follows the content.
- Everything is rebuilt and removed through the existing ownership core, and hand terrain edits survive.

**Architecture:** Terrain-writing nodes do not compute heights inside the graph. Each one emits **terrain ops**: small, hashable descriptors carried on a new `Terrain` item kind (op type, parameters, geometry from the control). Rebuild then does these steps in order:
1. Undo the instance's previous contribution.
2. Evaluate the graph on that restored base terrain, inside `GeneratedContent::prepare`.
3. Prepare the ops in order, loading section assets and probing the ground.
4. Compose them into one `TerrainTargetFunction` that stacks every op on the hand-made base, and clip it to the region with a fade at the border.
5. Apply.
6. Re-ground spawned placements on the final terrain, and add section placements.

The composition is pure library code and fully unit-tested. The editor side changes only the rebuild wiring, the debug view and content.

**Tech Stack:** C++17, existing `pcg` lib (`section_path`, `terrain_contribution`, `graph`), mmo_edit, Catch2, unattended flags and `--capture-view`.

**Spec:** `docs/superpowers/specs/2026-10-25-pcg-graphs-design.md`, specifically:
- "Terrain stacking", "Execution" and "Node set v1" (Terrain and Spawners);
- "Resolved" items 1–3;
- "Carried into phase 3".

The user decided that roads go first: a road control lays road sections and clears the forest in the same graph.

## Global Constraints

- **Branch and data.**
  - Branch `feature/pcg-oasis-poc`; never push, ship or merge.
  - Data goes only into the worktree's `data/client` submodule, on its branch `feature/pcg-oasis-poc`, never `master`.
  - Never touch `H:\mmo` outside this worktree.
  - Never kill an mmo_edit process you did not start.
- **No UI driving.** No computer use; the user declined it. Editor checks use unattended flags and `--capture-view`. Interactive checks go on a list for the user.
- **Map 0 is frozen.** No new PCG output, and no terrain, placement or NPC changes on map 0 (`Development.hwld`). The test site is world 2 (`Worlds/Collision`).
- **Editor-only.** Nothing PCG ships to clients.
- **Pure library code** (`src/shared/pcg/`):
  - headless, gcc-compatible, no exceptions;
  - deterministic: hash-based randomness only, world-anchored noise, no unordered containers on output paths;
  - tests go in `src/tests/pcg_tests/`.
- **Terrain is relative to the hand-made base.** Every op computes its target from the base height it is given, so hand sculpting under generated content survives rebuilds (`base' = current − (written − base)`, existing contribution logic).
- **Ops apply in a fixed order:** node id, then item index within the node's output. The order is documented and deterministic.
- **Clipping at apply time.** Evaluation output stays unclipped. Terrain writes are clipped to the region, and their weight fades to 0 over `borderFade` metres inside the region border. Spawns are clipped as in phase 2.
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

1. **Rebuild twice with terrain ops.** It must give byte-identical terrain, foliage and record files. Evaluation must see the base terrain, not the instance's own previous output. (Task 2 composition test; Task 5 flag check.)
2. **Hand sculpting inside the region between two rebuilds.** The hand delta survives the second rebuild. (Task 2 relative-stacking test; Task 5 flag check.)
3. **A terrain node is removed from the graph, or every op disappears.** The rebuild restores the old terrain exactly: undo, then write nothing. No leftover terrain needs Delete. (Task 5 flag check.)
4. **The region border.** There is no height step at the border. A road that crosses the border fades out smoothly. Writes stop at the border and touch no samples outside it. (Task 2 fade test; Task 5 check.)
5. **A missing or stale section asset, or a section param it cannot accept.** Rebuild refuses with the node and asset named, and the world stays untouched. (Task 3 node test; Task 5 flag check.)

---

### Task 1: Terrain item kind, point ids and op descriptors

**Files:**
- Create: `src/shared/pcg/graph/terrain_op.h/.cpp`
- Modify:
  - `src/shared/pcg/graph/graph_data.h/.cpp` (kind, fields, hashing);
  - `src/shared/pcg/graph/nodes_input.cpp` (pass point ids);
  - `src/shared/pcg/graph/graph_output.cpp` (eval dump: kind name, op JSON);
  - `src/mmo_edit/editors/pcg_graph_editor/pcg_graph_editor_instance.cpp` (`KindColor`, `KindNames`).
- Test: `src/tests/pcg_tests/test_graph_terrain_op.cpp`

**Interfaces:**
- Consumes: `DataItem`, `HashItem`/`ContentHash`, `Hasher`, `HashAttributes`.
- Produces:
```cpp
namespace mmo::pcg::graph
{
	// graph_data.h
	enum class ItemKind : uint8 { Points, Spline, Area, Params, Terrain };
	// DataItem gains:
	//   std::vector<uint64> pointIds;   // per spline point, from the producing control (empty if generated)
	//   TerrainOp op;                    // valid when kind == Terrain

	// terrain_op.h
	enum class TerrainOpType : uint8 { SectionPath, Noise, Raise, Carve, Paint, WaterArea };
	struct TerrainOp
	{
		TerrainOpType type = TerrainOpType::Noise;
		String asset;             // SectionPath: .hsec path; WaterArea: water material (.hmat), may be empty
		AttributeSet params;      // op parameters by name (section params use SetSectionPathParam names)
		uint32 order = 0;         // producing node id (set by the node), composition order
	};
	void HashTerrainOp(Hasher& h, const TerrainOp& op);
	const char* TerrainOpTypeName(TerrainOpType type);
}
```
- `GetControls` copies `ControlInstance::pointIds` into `DataItem::pointIds` for splines and areas.
- `HashItem` covers `pointIds` and, for Terrain items, `op`.
- `Terrain` items keep `spline` (and `points` where an op uses them) as their geometry.

- [ ] **Step 1: Write failing tests** `[graph_terrain_op]`:
  - `ContentHash` changes when any op field, any op param, `order` or a point id changes.
  - Two equal items hash equal.
  - `GetControls` output carries the control's point ids in order.
  - The eval dump names kind "Terrain" and lists `op.type`/`asset`/`params`.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_terrain_op]"`. Expected: FAIL.
- [ ] **Step 3: Implement.** The graph editor gets a pin colour and a name for the `Terrain` kind.
- [ ] **Step 4: Run** the full `pcg_tests` and build `mmo_edit`; expected: all pass and a clean build. Then run the gcc check.
- [ ] **Step 5: Commit** `feat(pcg): terrain item kind and op descriptors`

### Task 2: Terrain op preparation and composition

**Files:**
- Create: `src/shared/pcg/graph/terrain_compose.h/.cpp`
- Test: `src/tests/pcg_tests/test_graph_terrain_compose.cpp`

**Interfaces:**
- Consumes:
  - Task 1 `TerrainOp`;
  - `section_path.h`: `PrepareSectionPath`, `EvaluateSectionTerrain`, `BuildSectionPlacements`, `GetInfluenceRect`, `SetSectionPathParam`, `LayerRemapFunction`;
  - `terrain_contribution.h`: `TerrainTarget`, `WorldRect`;
  - `pcg_random.h`: `FractalNoise`;
  - `RowIndexedPolygon` / `SampledPath` containment as the graph nodes use it.
- Produces:
```cpp
namespace mmo::pcg::graph
{
	/// Supplies section assets by path (the editor loads them through the asset registry; tests pass in-memory assets).
	using SectionAssetProvider = std::function<const SectionAsset*(const String& path, String& error)>;
	/// Maps a texture key to the tile's layer index at (x, z), -1 if the tile has no layer with that texture.
	using TextureLayerLookup = std::function<int8(float x, float z, const String& textureKey)>;

	struct TerrainOpInput { TerrainOp op; PathSpline spline; std::vector<uint64> pointIds; PointSet points; uint64 sourceId = 0; };

	/// Ops gathered from every node's "Terrain" output pin, sorted by (op.order, item index).
	std::vector<TerrainOpInput> CollectTerrainOps(const Graph& graph, const EvalResult& result);

	class PreparedTerrain
	{
	public:
		/// Prepares every op on the given base ground (called after Unapply). Fails with a message naming the node
		/// and asset for a missing asset, a rejected section param or a degenerate section path.
		bool Prepare(const std::vector<TerrainOpInput>& ops, const std::function<float(float, float)>& baseGround,
			const SectionAssetProvider& sections, const PathSpline& region, float borderFade, String& error);
		/// Union of op influences clipped to the region bounds; empty if there are no ops.
		WorldRect GetInfluence() const;
		bool IsEmpty() const;
		/// Stacks all ops on `baseHeight` in order (each op blends toward its target from the running height),
		/// splat and water last-writer-wins in order, everything multiplied by the region fade
		/// (1 inside, smooth to 0 over borderFade metres at the border, 0 outside).
		TerrainTarget Evaluate(float x, float z, float baseHeight, const TextureLayerLookup& layers) const;
		/// Section placements of every SectionPath op, keyed Hash(order, sourceId, sectionKey), clipped to the region.
		void BuildPlacements(const std::function<float(float, float)>& ground, std::vector<Placement>& out) const;
		/// First non-empty WaterArea material (warning if several differ).
		const String& GetWaterMaterial() const;
		const std::vector<String>& GetWarnings() const;
	};
}
```
- **Op semantics.** The base is the running height `h`. Every op returns `(target, weight)`, then `h = lerp(h, target, weight × fade)`.
  - **SectionPath:** `EvaluateSectionTerrain(shape, x, z, h, remap)`. The remap is built from `layers` over the asset's `layerTextures`.
  - **Noise:** inside the area, `target = h + amplitude × (FractalNoise(world x, z; wavelength, octaves) − 0.5) × 2`. The weight goes from 0 at the area edge to 1 at `falloff` metres inside.
  - **Raise:** along the spline. The profile peak is `max(0, spline.y − h_spline)`, where `h_spline` is the base under the spline centre from Prepare. `target = h + peak × smoothstep` over `width` either side.
  - **Carve:** along the spline, `target = h − depth × profile` over `width`, with `bank` metres of falloff.
  - **Paint:** the layer is looked up from `textureKey` at (x, z). It is painted inside the area, or within `width` of the spline, at `weight` with `falloff`. Skipped where the lookup gives −1 (warning).
  - **WaterArea:** inside the closed area, `waterType = 1` and `waterHeight = level`. The level comes from the op param or the item attribute `level`. Water quads extend `margin` metres (default 4) past the shore, as the oasis does.

- [ ] **Step 1: Write failing tests** `[graph_terrain_compose]`:
  - Empty ops give `IsEmpty()`, and `Evaluate` returns weight 0 everywhere.
  - A Noise op with amplitude 2 changes `h` by at most 2 inside and by 0 outside. Two calls are identical. Translating the area keeps world-anchored values at shared points.
  - **Relative stacking:** `Evaluate(x, z, base + 5)` minus `Evaluate(x, z, base)` equals 5 for a Noise op at full weight.
  - **Order:** a Raise op then a Carve op on the same spline gives a different `h` than the reverse order, and matches a hand-computed value.
  - **Region fade:** with a region square of 0..100 and borderFade 10, weight is 1 at 50, 0.5±0.05 at 5, and 0 at −1. There is no step over a 0.5 m walk across the border (max |Δh| < amplitude × 0.15).
  - **SectionPath with an in-memory `TestRoad`-like asset:**
    - the target equals a direct `EvaluateSectionTerrain` on the same shape;
    - `BuildPlacements` keys are stable when an unrelated op is added;
    - a missing asset makes `Prepare` false with the path in the error;
    - an unknown section param makes `Prepare` false naming the param.
  - **Paint:** where the lookup returns −1, there is no splat and one warning.
  - **WaterArea:** `waterType` 1 and `waterHeight == level` inside; the margin is respected; dry beyond it.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_terrain_compose]"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
  - `CollectTerrainOps` scans each node's output pin named `"Terrain"`.
  - `Prepare` runs `PrepareSectionPath` once per SectionPath op with `baseGround`, and caches spline samples, distance indices and noise per op.
- [ ] **Step 4: Run** the full suite and the gcc check. Expected: PASS.
- [ ] **Step 5: Commit** `feat(pcg): terrain op composition`

### Task 3: Section nodes

**Files:**
- Create: `src/shared/pcg/graph/nodes_section.cpp`
- Modify: `builtin_nodes.h` / `node_registry.cpp` (register).
- Test: `src/tests/pcg_tests/test_graph_nodes_section.cpp`

**Interfaces:**
- Consumes: Task 1 `TerrainOp`; Task 2 (only for tests that compose); `SetSectionPathParam` names.
- Produces two node types, `LaySectionAlongSpline` and `LaySectionAlongLoop` (category `Sections`). They share one executor:
  - **Input `In`:**
    - Along Spline takes Spline items (open or closed);
    - Along Loop takes Area items (closed loops) and forces `closed`.
  - **Output `Terrain`:** one Terrain item per input item. It copies `spline`, `pointIds`, `sourceId` and `tags`, and sets `op.type = SectionPath` and `op.order = nodeId`.
  - **Settings:**
    - `section`: String, widget `asset:.hsec`. The fallback is the item attribute named by `sectionAttribute`.
    - `sectionAttribute`: String, default `"section"`. The item attribute wins when present and non-empty.
    - every `SectionPathParams` field under its `SetSectionPathParam` name, with the same defaults as `SectionPathParams`;
    - Along Loop also has `floorInside`, default true.
  - **Item attributes** whose name matches a section param override the node setting for that item, so a Road control can carry its own `groundSmoothing`.
  - Every resolved param is written into `op.params`. `op.asset` is the resolved section path.
  - **Errors:** an empty section path, or a param value `SetSectionPathParam` rejects, is a node error. The check runs at execute time; asset existence is checked at Prepare.
  - `contextDeps`: None.

- [ ] **Step 1: Write failing tests** `[graph_nodes_section]`:
  - One road spline with attribute `section = "Editor/Pcg/Sections/TestRoad.hsec"` and clearance 6 gives one Terrain item with that asset, the spline's point ids and the default params.
  - A node `section` setting is overridden by the item attribute.
  - An item attribute `groundSmoothing = 40` overrides the setting.
  - Along Loop given an open spline in an Area item reports an error. Along Loop sets the `closed` param true.
  - An invalid `heightMode` value is a node error naming the setting.
  - Empty input gives empty output.
  - The output is deterministic, with a stable `ContentHash`.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_nodes_section]"`. Expected: FAIL. **Step 3: Implement** it. **Step 4:** PASS (full suite), then the gcc check.
- [ ] **Step 5: Commit** `feat(pcg): lay section nodes`

### Task 4: Terrain shape nodes

**Files:**
- Create: `src/shared/pcg/graph/nodes_terrain.cpp`
- Modify: registration.
- Test: `src/tests/pcg_tests/test_graph_nodes_terrain.cpp`

**Interfaces:**
- Consumes: Task 1 `TerrainOp`; Task 2 semantics (the params named here are the ones Task 2's `Prepare` reads).
- Produces five node types, all in category `Terrain`. Each emits one Terrain item per input item on output pin `Terrain`, with `op.order = nodeId`.

| Node | Input | Settings (default) |
|---|---|---|
| `TerrainNoise` | Area | `amplitude` (1.5), `wavelength` (24), `octaves` (3), `falloff` (8) |
| `RaiseAlongSpline` | Spline | `width` (40), `falloffPower` (2) |
| `CarveAlongSpline` | Spline | `width` (4), `depth` (1.5), `bank` (3) |
| `PaintSplat` | Area/Spline | `texture` (String, widget `asset:.htex`, stored as the texture key), `weight` (1), `width` (4, splines only), `falloff` (2) |
| `WaterArea` | Area | `level` (0, overridden by item attribute `level`), `levelOffset` (0), `material` (widget `asset:.hmat`), `margin` (4) |

- **Override rule.** Every setting can be overridden per item by an item attribute of the same name, the same rule as Task 3.
- `contextDeps`: None (ops are prepared later, on the base terrain).

- [ ] **Step 1: Write failing tests** `[graph_nodes_terrain]`:
  - Each node turns an input item into one Terrain item with the documented `op.type` and resolved params.
  - An item attribute overrides a setting.
  - The wrong input kind is a validation issue.
  - Empty input gives empty output.
  - Composing the output through Task 2 gives the documented effect. Spot checks:
    - Noise: bounded by amplitude.
    - Carve: depth at the centre.
    - WaterArea: level inside.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_nodes_terrain]"`. Expected: FAIL. **Step 3: Implement** it. **Step 4:** PASS (full suite), then the gcc check.
- [ ] **Step 5: Commit** `feat(pcg): terrain shape nodes`

### Task 5: Rebuild writes terrain (evaluate after undo)

**Files:**
- Modify:
  - `src/mmo_edit/editors/world_editor/pcg/graph_instance_builder.h/.cpp`;
  - `pcg/generated_content.h/.cpp` (only if needed: a water-material and influence contract);
  - `procedural_edit_mode.cpp` (overlap check: graph instances become terrain writers, reversing ruling R16; `GetBuiltRects`);
  - `src/shared/pcg/graph_instance_record.h/.cpp` (`borderFade` in the JSON blob, default 8, unknown-key tolerant);
  - `mmo_edit.cpp` (`--set borderFade=` on `--pcg-new-graph-instance`, if the flag does not already pass record fields).

**Interfaces:**
- Consumes:
  - Task 2 `CollectTerrainOps`, `PreparedTerrain`;
  - phase 2 `BuildGraphInstanceContext`, `CollectPlacements`;
  - `section_path_builder`: `LoadSectionAsset`, `TileLayerRemap` (generalise it into a `TextureLayerLookup` adapter in `pcg/editor_texture_layers.h/.cpp`, and have `section_path_builder` use the same adapter).
- Produces: `RebuildGraphInstance` with this flow (no new public API):
  1. **Before `ApplyGeneratedContent`:**
     - load the graph and validate it;
     - resolve controls with the F1 refusal;
     - set `content.influence` = area bounds (writes are clipped to the area anyway);
     - set `content.terrain` = a lambda that is **always** set, so the old contribution is undone every rebuild and an op-free graph writes nothing.
  2. **`content.prepare(ground)`**, running after Unapply on the restored base:
     - `BuildGraphInstanceContext` + `Evaluate` (the sampler now sees the base);
     - the refusal checks (issues, node errors, missing spawn assets);
     - `CollectTerrainOps`, then `PreparedTerrain::Prepare(ops, ground, provider, area, borderFade)`;
     - `CollectPlacements`.
     - Any refusal returns false. `ApplyGeneratedContent` then re-applies nothing, so the world must end exactly as before. Verify this; if the existing contract leaves the terrain undone on a prepare failure, re-apply the old contribution or move the refusal checks into a dry evaluation before the call. Pick one, document it, and test it with check (e).
  3. `content.terrain(x, z, base)` = `prepared.Evaluate(x, z, base, layers)`. `content.waterMaterial` = `prepared.GetWaterMaterial()`.
  4. **`content.placements(ground, slope)`:**
     - Spawn points are re-grounded: `y = ground(x, z) + (point.y − evalGround(x, z))`, where `evalGround` is the base the graph saw.
     - Section placements come from `prepared.BuildPlacements(ground)`.
     - The two key spaces are kept disjoint by salting.
  5. **The debug view keeps evaluating on live terrain.** Task 6 adds the base view.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** `mmo_edit` and run the full `pcg_tests`. Expected: clean.
- [ ] **Step 3: Flag checks** on a scratch copy of world 2 (`Worlds/PcgScratch`, never committed, deleted at the end):
  - a test biome `Editor/Pcg/Biomes/PcgScratchBiome/` (deleted at the end);
  - a graph with Get Controls(road) → Lay Section Along Spline (`TestRoad.hsec`), plus Get Controls(forest) → Terrain Noise, plus the phase 2 forest chain.

  Each check must hold:
  - (a) Rebuild gives exit 0. The road is laid (terrain changed along the spline, section props present), and trees are on the final ground: |tree y − ground| < 0.05 at 20 sampled trees.
  - (b) A second rebuild leaves every world file byte-identical (SHA-256).
  - (c) After a hand terrain edit inside the area (`+2 m` on a 3×3 vertex patch through the same tile-edit method Task 9 of phase 2 used), the rebuild keeps the +2 m delta at those vertices.
  - (d) Removing the noise and section nodes from the graph, then rebuilding, makes the terrain equal the pre-PCG terrain everywhere outside the hand patch (tile hashes against a pre-build snapshot).
  - (e) A missing `.hsec` gives exit 1 naming the node and asset, and every file is identical to before the attempt.
  - (f) A road crossing the area border: the heights at the border equal the base within 0.01 m, and no write lands outside the area.
  - (g) A graph instance area overlapping a section path's built rects is refused with "overlap".
- [ ] **Step 4: Commit** `feat(pcg-editor): graph rebuild writes terrain`

### Task 6: Debug view for terrain ops on the base terrain

**Files:**
- Create:
  - `src/shared/pcg/graph/terrain_debug.h/.cpp` (pure);
  - `src/mmo_edit/editors/world_editor/pcg/base_terrain_sampler.h/.cpp`.
- Modify: `pcg_graph_editor_instance.cpp` (evaluate with the base sampler; draw Terrain items).
- Test: `src/tests/pcg_tests/test_graph_terrain_debug.cpp`

**Interfaces:**
- Consumes: Task 2 `PreparedTerrain`; phase 2 debug layer, budgets and `BuildDebugGeometry`; `TerrainContribution` entries.
- Produces:
```cpp
namespace mmo::pcg::graph
{
	/// Height ticks on a grid over the op influence: a line from base to composed height per sample, coloured by the
	/// sign of the change; plus each op's spline/area outline. Respects the line budget like BuildDebugGeometry.
	DebugGeometry BuildTerrainDebugGeometry(const PreparedTerrain& prepared, const std::function<float(float, float)>& base,
		float step, uint32 argbRaise, uint32 argbLower, size_t lineLimit);
}
namespace mmo
{
	/// Reads the terrain as it would be after Unapply of a contribution: live height minus the bilinear
	/// (written − base) of the outer vertices around (x, z) taken from the contribution; other channels pass through.
	class BaseTerrainSampler final : public pcg::graph::ITerrainSampler { /* wraps EditorTerrainSampler */ };
}
```
- **What the debug view evaluates on.** It uses `BaseTerrainSampler` built from the target instance's owned contribution, so previews match what a rebuild produces. The sampler's revision mixes the live revision with a hash of the contribution.
- **Terrain node with debug on:**
  - The composed op output of that node alone is drawn as height ticks, using a 4 m step and the node's share of the line budget.
  - Section ops also draw their copy rectangles.
  - Ops are prepared through the same provider the rebuild uses, and failures show as node errors.

- [ ] **Step 1: Write failing tests** `[graph_terrain_debug]`:
  - A Noise op gives at most one tick per grid sample inside the influence.
  - The line budget is respected.
  - Empty prepared terrain gives no lines.
  - Raise ticks use `argbRaise`, lower ticks use `argbLower`.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_terrain_debug]"`. Expected: FAIL. **Step 3: Implement** it. **Step 4:** PASS, then the gcc check, then build `mmo_edit`.
- [ ] **Step 5: Verify** through `--pcg-eval-graph`: its dump uses the base sampler when the instance has a contribution. A Filter by Terrain height threshold evaluated after a rebuild must give the same result as before the rebuild.
- [ ] **Step 6: Commit** `feat(pcg-editor): terrain debug view on the base terrain`

### Task 7: Falwyn valley as one region graph in world 2

**Files (data submodule, branch `feature/pcg-oasis-poc`):**
- `Biomes/FalwynForest/Controls/{Valley,Lake}.hctl`; `Road.hctl` (add `section`);
- `Biomes/FalwynForest/Graphs/FalwynValley.hpcgg`;
- `Worlds/Collision` records, terrain and foliage.

Main repo: captures in `docs/superpowers/specs/pcg-graphs/phase3a-*.png`.

**Interfaces:**
- Consumes: everything above, through the flags. Control types and graphs are written as JSON in `WriteControlType`/`WriteGraph` format and validated through the flags (ruling R23 of phase 2).
- **Control types:**
  - `Valley`: ClosedLoop, tag `valley`. Properties: `section` (Asset, default `Editor/Pcg/Biomes/FalwynForest/Sections/ValleyWall.hsec`), plus the wall params of the current ValleyWall record:

    | Param | Value |
    |---|---|
    | `heightMode` | SmoothedGround |
    | `groundSmoothing` | 60 |
    | `joinBlend` | 4 |
    | `massEnabled` | true |
    | `massHeight` | 95 |
    | `massDepth` | 220 |
    | `massRise` | 0.6 |
    | `massNoiseAmplitude` | 35 |
    | `massNoiseWavelength` | 180 |
    | `massDetailAmplitude` | 8 |
    | `massDetailWavelength` | 35 |
    | `massPaintLayer` | 0 |
    | `mirrorCopies` | true |
    | `facadeHeightJitter` | 0.2 |

    These are read from the record; the existing record is an *open* path of 21 points. Keep the same points and decide whether the valley control is closed or open (keep it open via a Spline control type `ValleyWall` if closing changes the look). Record the decision.
  - `Road`: add `section` (Asset, default `Editor/Pcg/Sections/TestRoad.hsec`).
  - `Lake`: ClosedLoop, tags `water` and `exclusion`, `level` (Float).
- **Graph `FalwynValley`:**
  - the phase 2 forest chain;
  - Get Controls(road) → Lay Section Along Spline;
  - Get Controls(valley) → Lay Section Along Loop or Along Spline (per the decision above);
  - Get Controls(forest) → Terrain Noise (amplitude 1.2) and Paint Splat (forest floor texture, if a Falwyn forest-floor texture exists in the tile material, otherwise skipped with a note);
  - Get Controls(water) → Water Area.
  - The forest Difference also takes Get Controls(water).
- **Migration** in world 2:
  1. Delete the standalone ValleyWall section path record (`0e7b59cb7423d3a9`) and the phase 2 ForestTest instance, using `--pcg-delete`, which restores their terrain and foliage.
  2. Create the Valley control from the wall record's points and one graph instance covering the valley including the walls' mass reach, with `borderFade` 8.
  3. Keep the existing ForestWest/ForestEast/CampClearing/ValleyRoad controls, and add one small Lake in a forest gap.
  4. Rebuild.

- [ ] **Step 1: Snapshot** the current world 2 captures (same two viewpoints as phase 2) as the baseline.
- [ ] **Step 2: Author and migrate** as above. Record every command.
- [ ] **Step 3: Acceptance**, with evidence in the report:
  - **Walls:** they look equivalent to the baseline. Compare captures, and compare wall heights at 10 points along the facade with the old record (Δ < 0.5 m expected; explain any difference).
  - **Road:** it lays TestRoad sections along ValleyRoad. Trees stay ≥ 6 m from it.
  - **Lake:** it holds water at its level, with no trees inside.
  - **Idempotent:** a second rebuild leaves all files byte-identical.
  - **Hand content survives:** non-PCG content in world 2 is unchanged.
  - **Hand terrain edit survives:** a hand terrain edit survives a rebuild (restore it afterwards).
  - **Delete restores the base:** deleting the instance restores the terrain to the pre-PCG state outside hand edits. Then re-create and rebuild for the committed state.
  - **Map 0 untouched:** no map 0 file in any diff.
- [ ] **Step 4: Captures** `phase3a-valley-overview.png`, `phase3a-road.png`, `phase3a-lake.png`.
- [ ] **Step 5: Commit** in the data submodule (`data(pcg): Falwyn valley region graph with roads, walls and lake`). Then commit the gitlink bump and captures in the main repo.

---

## Out of scope (phase 3b and later)

- **River node:** bed, water heights and flow. Flow data (`Terrain::ApplyWaterFlow`, WFLW) has no contribution channel and needs its own owned record.
- **Waterfall rule content:** river ends near lower water → Stamp Section at Points. It needs Stamp Section at Points and a waterfall section.
- **Stamp Section at Points.**
- **Debug Probe node.**
- **Cancellation and progress** for long evaluations.
- **Moving the oasis into a graph.**
- **Interactive UI checks.** The user runs them by hand; the checklist is at the end of each task report.
