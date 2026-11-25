# PCG Field Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A rectangle `Field` control for Falwyn Forest that paints its area with the farmland layer and lays a fenced edge section around it, plus whole-control move/rotate/scale for every placed PCG control.

**Architecture:** Everything is a control type plus graph nodes, like `Road` and `Lake`. The shared `pcg` library gains a rectangle shape, a `keepGround` section height mode, exclusion for section props, and a per-tile material channel that `PaintSplat` uses to switch tiles to the farmland material. `mmo_edit` gains rectangle handles and a whole-control gizmo; all geometry maths lives in the shared library so it is unit-tested.

**Tech Stack:** C++17, Catch2 (`src/tests/pcg_tests`, one glob-built executable), ImGui editor (`mmo_edit`), JSON data files (`.hctl`, `.hpcgc`, `.hpcgg`, `.hsecdef`).

**Spec:** `docs/superpowers/specs/2026-11-12-pcg-field-control-design.md` — read it before any task.

## Global Constraints

- Branch `feature/pcg-field-control`; data changes are commits in the `data/client` submodule on a branch of the same name. Never push.
- Code style per `CLAUDE.md`: Allman braces, tabs, braces on every `if`, `m_camelCase`, `PascalCase` methods, Doxygen on public members, copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`, no exceptions (`ASSERT` / `VERIFY` / `ELOG`).
- `src/shared/pcg` must build on headless Linux (gcc-13): no graphics, ImGui or editor includes there.
- Determinism: same controls and graph give the same bytes. No iteration over unordered containers in anything that reaches output or hashes.
- A graph that uses none of the new inputs/settings must produce byte-identical output: new fields enter hashes only when non-empty / non-default (the `stackOrder` pattern in `HashTerrainOp`, `src/shared/pcg/graph/terrain_op.cpp:10`).
- Cutting a gap only removes fence pieces; every other piece keeps position and key.
- No protocol, client, server or material compiler change. No temporary scripts in `tools/` (use the scratchpad).
- Build and test: `cmake --build build --config Debug -t pcg_tests -- /m /v:minimal`, then `bin\Debug\pcg_tests.exe "<tag>"`. Editor: `-t mmo_edit`.
- No UI driving. Editor behaviour is verified through unattended `mmo_edit` flags and `--capture-view`; interactive checks go on the author's checklist (Task 10).

## Review Focus

1. A rectangle side dragged past its opposite side, at any yaw: the size clamps at 1 m and the rectangle never flips or produces NaN (Task 7).
2. An exclusion area that covers a field's whole edge: the section yields zero props and no error or warning (Tasks 3, 4).
3. A field that touches a tile only with a corner, so the tile centre is outside the field: that tile is still switched to the farmland material and painted (Task 6).
4. Old files: `.hctl` / `.hpcgc` / `.hpcgg` and legacy `PCGP` version 4 records load unchanged, and the existing acceptance tests give identical hashes (Tasks 1, 2, 4).
5. Whole-control scale with a factor of 0, a negative factor or NaN from the gizmo: clamped so points never collapse onto the pivot or mirror (Task 8).
6. Known limit, to be written into the docs in Task 10 rather than fixed: two graph instances whose fields share a tile — deleting one restores the tile material under the other until that one is rebuilt.

---

### Task 1: Rectangle control shape (shared library, type editor, bounds)

**Files:**
- Modify: `src/shared/pcg/graph/controls.h:17-30`, `src/shared/pcg/graph/controls.cpp:25-30`
- Create: `src/shared/pcg/graph/rectangle_shape.h`, `src/shared/pcg/graph/rectangle_shape.cpp`
- Modify: `src/shared/pcg/graph/nodes_input.cpp:152-187`
- Modify: `src/mmo_edit/editors/world_editor/pcg/graph_instance_builder.cpp:423-435`
- Modify: `src/mmo_edit/editors/pcg_control_type_editor/pcg_control_type_editor_instance.cpp:34,249-253`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/procedural_edit_mode.cpp:216-230` (`ShapeName`)
- Test: `src/tests/pcg_tests/test_graph_rectangle.cpp`

**Interfaces:**
- Produces: `ControlShape::Rectangle` (appended last; `"rectangle"` in `.hctl`), and in `rectangle_shape.h` (namespace `mmo::pcg::graph`):
  - `std::array<SplinePoint, 4> RectangleCorners(const ControlInstance& inst);` — yaw only, order local (-x,-z), (+x,-z), (+x,+z), (-x,+z) (same as `VolumeFootprint`), every corner `y = inst.position.y`.
  - `inline constexpr float MinRectangleHalfExtent = 0.5f;`

- [ ] **Step 1: Write the failing tests** in `test_graph_rectangle.cpp`, tag `[graph_rectangle]`, using `ControlWorld` / `RunNode` from `test_graph_nodes_input.cpp` (move them into `graph_node_test_util.h` if they are file-local):

```cpp
TEST_CASE("rectangle type round trips", "[graph_rectangle]")
// WriteControlType of a type with shape Rectangle contains "\"shape\": \"rectangle\"";
// ParseControlType gives shape == ControlShape::Rectangle; ResolveControl accepts an instance with no spline points.

TEST_CASE("GetControls emits a rectangle as a closed linear area", "[graph_rectangle]")
// position (10, 3, 20), halfExtents (2, 9, 4), identity rotation:
// kind == ItemKind::Area, spline.closed, interpolation == Linear, 4 points,
// corners (8,16) (12,16) (12,24) (8,24), every y == 3.0f,
// pointIds == {1, 2, 3, 4}, attributes has no "volumeHeight".

TEST_CASE("rectangle point ids and order survive resize and rotation", "[graph_rectangle]")
// same instance with halfExtents (6, 9, 1) and yaw 90 deg: pointIds still {1,2,3,4};
// corner 0 is position + R(yaw) * (-6, -1).

TEST_CASE("a volume is unchanged", "[graph_rectangle]")
// the existing Volume expectations of test_graph_nodes_input.cpp:221-285 still hold (volumeHeight present, no pointIds).
```

- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_rectangle]"` — expected: does not compile (`ControlShape::Rectangle` unknown).
- [ ] **Step 3: Implement.** Append the enum value and name-table entry; `RectangleCorners`; a `case ControlShape::Rectangle` in `ExecuteGetControls` that sets `item.pointIds = { 1, 2, 3, 4 }` and does not touch the context dependencies (`contextDeps` stays `Controls | Region`). In `GetPlacedControlBounds` treat Rectangle like Volume. Add `"Rectangle (area)"` as the fifth entry of `s_shapeNames` (the combo is enum-indexed) and to `ShapeName`.
- [ ] **Step 4: Run** `bin\Debug\pcg_tests.exe "[graph_rectangle],[graph_controls],[graph_nodes_input]"` — expected: all pass. Build `mmo_edit`.
- [ ] **Step 5: Commit** `Add the rectangle control shape`.

---

### Task 2: `keepGround` section height mode

**Files:**
- Modify: `src/shared/pcg/section_path.h:120-129,310-313`, `src/shared/pcg/section_path.cpp:1068,1151,1373-1392,1575-1576,1618`
- Modify: `src/shared/pcg/section_path_record.cpp:26,51,99-109`, `src/shared/pcg/section_path_record.h:39-49` (doc)
- Modify: `src/shared/pcg/graph/nodes_section.cpp:197`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/procedural_edit_mode.cpp:3470-3486`, `src/mmo_edit/mmo_edit.cpp:1396`
- Test: `src/tests/pcg_tests/test_section_keep_ground.cpp`

**Interfaces:**
- Produces: `SectionHeightMode::KeepGround` (value 3, appended); text `"keepGround"` for `SetSectionPathParam` and the node enum; `PCGP` `FormatVersion = 5`.

- [ ] **Step 1: Write the failing tests**, tag `[section_keep_ground]`, with the fixtures of `test_section_runs.cpp` (`MakeRunAsset`, `Polyline`, `Prepare`, `Build`) and `test_section_path_placements.cpp` (`MakeAsset`):

```cpp
TEST_CASE("keepGround contributes no height but still paints", "[section_keep_ground]")
// asset whose variant has a non-zero height grid and painted coverage; params.heightMode = KeepGround.
// For x across the section at several z: EvaluateSectionTerrain(...).heightWeight == 0.0f,
// and splatWeight equals the value the same shape gives with FollowGround.

TEST_CASE("keepGround needs no base ground and puts props on the ground", "[section_keep_ground]")
// PrepareSectionPath with an empty baseGround succeeds.
// BuildSectionPlacements with ground = [](float x, float z){ return 0.1f * x + 7.0f; }:
// every placement y == ground(x, z) + its heightAboveTerrain (within 1e-4).

TEST_CASE("keepGround refuses the mass layer", "[section_keep_ground]")
// params.massEnabled = true -> PrepareSectionPath returns false, error contains "keepGround".

TEST_CASE("SetSectionPathParam parses keepGround", "[section_keep_ground]")
TEST_CASE("section path record version 5 keeps keepGround, version 4 bytes still read", "[section_keep_ground]")
// write a record with KeepGround, read it back equal; a v4 fixture (write with another mode, patch nothing)
// still reads; a v4 stream whose height mode byte is 3 is refused.
```

- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[section_keep_ground]"` — expected: does not compile.
- [ ] **Step 3: Implement.** In `PrepareSectionPath`: KeepGround does not require `baseGround`; centre heights use `baseGround` when given, else the spline height (only read when placements are built without a ground); `massEnabled` with KeepGround fails with `"keepGround cannot be combined with the mass layer"`. In `EvaluateSectionTerrain`: `heightWeight = 0` in KeepGround, paint unchanged. Record: write version 5; on read accept height mode 3 only for version >= 5. Add `"keepGround"` to the node's `enumValues`, `"Keep ground"` to the legacy path combo, and to `--height-mode` parsing.
- [ ] **Step 4: Run** `bin\Debug\pcg_tests.exe "[section_keep_ground],[section_path_record],[section_path_terrain],[section_runs]"` — expected: all pass.
- [ ] **Step 5: Commit** `Add the keepGround section height mode`.

---

### Task 3: Exclusion in section placements (pcg core)

**Files:**
- Modify: `src/shared/pcg/section_path.h:355-393`, `src/shared/pcg/section_path.cpp:2028-2090,2171-2241`
- Test: `src/tests/pcg_tests/test_section_exclusion.cpp`

**Interfaces:**
- Produces:
  - `using PlacementExcludeFunction = std::function<bool(float x, float z)>;`
  - `void BuildSectionPlacements(const SectionPathShape& shape, const std::function<float(float, float)>& ground, std::vector<Placement>& out, const PlacementExcludeFunction& excluded = {});`
  - Rule: a single placement is dropped if `excluded(final x, final z)` (after jitter); a run piece is dropped if `excluded` is true at the midpoint of its footprint ends `(x0,z0)`–`(x1,z1)`. The run is laid out exactly as without exclusion; dropped pieces are simply not pushed.

- [ ] **Step 1: Write the failing tests**, tag `[section_exclusion]`:

```cpp
TEST_CASE("an exclusion removes run pieces and leaves the others identical", "[section_exclusion]")
// Build(Polyline({{0,0},{0,40},{0,60}}), MakeRunAsset({ MakeRun("Fence", -4.6f, 0.0f, 5) })) gives 15 pieces.
// With excluded = z in [18, 30]: the result is exactly the 15 pieces minus those whose midpoint z is in [18, 30];
// every remaining piece equals its counterpart by key, position, rotation and scale.

TEST_CASE("an exclusion removes single placements by their jittered position", "[section_exclusion]")
TEST_CASE("an exclusion covering everything gives no placements", "[section_exclusion]")   // Review Focus 2
TEST_CASE("an empty exclude function changes nothing", "[section_exclusion]")
// byte-equal placement vectors with and without the default argument, on a closed loop with corners too.
```

- [ ] **Step 2: Run** — expected: does not compile.
- [ ] **Step 3: Implement** the parameter and the two drop points (`section_path.cpp` near `:2241` and in `RunLayer::Lay`).
- [ ] **Step 4: Run** `bin\Debug\pcg_tests.exe "[section_exclusion],[section_runs],[section_path_placements],[section_loops]"` — expected: all pass.
- [ ] **Step 5: Commit** `Let section placements skip excluded positions`.

---

### Task 4: `Exclude` input on the section nodes

**Files:**
- Modify: `src/shared/pcg/graph/graph_data.h:67-89`, `src/shared/pcg/graph/graph_data.cpp:140-186`
- Modify: `src/shared/pcg/graph/node_util.h`, `src/shared/pcg/graph/nodes_filter.cpp:114-125,437-511` (move helpers out)
- Modify: `src/shared/pcg/graph/nodes_section.cpp:203,277,372-425`
- Modify: `src/shared/pcg/graph/terrain_compose.h:31-51`, `src/shared/pcg/graph/terrain_compose.cpp:267-347,376-382,548-640,1257-1290`
- Test: `src/tests/pcg_tests/test_graph_section_exclude.cpp`

**Interfaces:**
- Consumes: `PlacementExcludeFunction`, `BuildSectionPlacements(..., excluded)` (Task 3).
- Produces:
  - `struct ExclusionShape { PathSpline spline; bool area = false; float radius = 0.0f; };` in `graph_data.h` (area: inside test, `radius` unused; spline: within `radius`, which already includes distance + clearance).
  - `std::vector<ExclusionShape> DataItem::exclusions;` and `TerrainOpInput::exclusions` (copied by `CollectTerrainOps`). Hashed by `HashItem` only when non-empty.
  - Moved to `node_util.h`: `struct SplineTarget`, `bool InsideAny(const std::vector<PreparedControl>&, float x, float z)`, `float ItemClearance(NodeOutput& out, const DataItem& item, const String& attribute)`.
  - All three section nodes: input `PinDef{ "Exclude", { ItemKind::Area, ItemKind::Spline }, true, false }`, settings `distance` (Float, 0, range 0..100) and `clearanceAttribute` (String, `"clearance"`), read exactly as `Difference` reads `Subtract` (a spline whose total radius is not positive is not carried).

- [ ] **Step 1: Write the failing tests**, tag `[graph_section_exclude]`, with `ExecNode` / `MakeSquare` / `MakeSplineItem` from `graph_node_test_util.h` and the terrain-stage fixtures of `test_graph_terrain_compose.cpp`:

```cpp
TEST_CASE("section nodes carry the Exclude input on their terrain items", "[graph_section_exclude]")
// LaySectionAlongLoop with In = one square, Exclude = one Area + one Spline with attribute clearance 6, distance 1:
// output item.exclusions.size() == 2; [0].area; [1].radius == 7.0f.
// A spline with clearance 0 and distance 0 is not carried.

TEST_CASE("an unconnected Exclude leaves the content hash unchanged", "[graph_section_exclude]")
// ContentHash of the node output equals the value computed before this task for the same input
// (pin the number from a run on the parent commit).

TEST_CASE("the terrain stage drops excluded section props", "[graph_section_exclude]")
// PreparedTerrain over one SectionPath op with a fence run and an exclusion Area over part of it:
// BuildPlacements returns the unexcluded set; the kept placements equal the no-exclusion result by key and position.

TEST_CASE("moving an exclusion changes the content hash", "[graph_section_exclude]")
```

- [ ] **Step 2: Run** — expected: does not compile.
- [ ] **Step 3: Implement.** Prepare the exclusion shapes once in the `SectionPath` branch of `PreparedTerrain::Prepare`, through the same `PrepareControl` path `Difference` uses so the inside/near test is bit-identical, and pass a lambda to `BuildSectionPlacements` in `BuildPlacements` (`terrain_compose.cpp:1268`). Terrain height and paint are not cut.
- [ ] **Step 4: Run** `bin\Debug\pcg_tests.exe "[graph_section_exclude],[graph_nodes_section],[graph_nodes_filter],[graph_terrain_compose],[graph_acceptance]"` — expected: all pass.
- [ ] **Step 5: Commit** `Add an Exclude input to the section nodes`.

---

### Task 5: Per-tile material names in the terrain contribution

**Files:**
- Modify: `src/shared/pcg/terrain_contribution.h:24-34,128-167`, `src/shared/pcg/terrain_contribution.cpp:395-406,574-596`
- Test: `src/tests/pcg_tests/test_terrain_contribution.cpp` (extend)

**Interfaces:**
- Produces:
  - `String TerrainTarget::material;` — tile material requested at this position; empty = none. Read in the `terrain_channel::Material` pass only.
  - `std::vector<String> TerrainSamples::materialNames;` — parallel to `materialSamples`, empty string = no per-tile name.
  - Write rule per tile: the sample's name if non-empty; else the call's `material` if `materialSamples` is 1; else nothing. A tile whose current material already equals the name is not recorded.

- [ ] **Step 1: Write the failing tests**, tag `[terrain_contribution]`, on `FakeTerrain`:

```cpp
TEST_CASE("a target can name a different material per tile", "[terrain_contribution]")
// target returns material "A.hmi" for tile (0,0) and "B.hmi" for tile (1,0), setMaterial false, call material "".
// After Apply: tile materials are A and B; contribution.materials has two entries with base "" and written A / B.
// Unapply restores both to "".

TEST_CASE("a tile that already has the named material is not recorded", "[terrain_contribution]")
TEST_CASE("setMaterial with the call's material still works", "[terrain_contribution]")   // oasis path unchanged
```

- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[terrain_contribution]"` — expected: does not compile.
- [ ] **Step 3: Implement** in `ComputeTerrainSamples` and `WriteTerrainSamples`.
- [ ] **Step 4: Run** `bin\Debug\pcg_tests.exe "[terrain_contribution],[oasis_terrain]"` — expected: all pass.
- [ ] **Step 5: Commit** `Let terrain targets name a material per tile`.

---

### Task 6: `PaintSplat` `tileMaterial`

**Files:**
- Modify: `src/shared/pcg/graph/nodes_terrain.cpp:100-104,171-177`
- Modify: `src/shared/pcg/graph/terrain_compose.h:54,151-220`, `src/shared/pcg/graph/terrain_compose.cpp:294-297,691-715,1078-1115`
- Modify: `src/mmo_edit/editors/world_editor/pcg/editor_texture_layers.h`, `.cpp:28-61`
- Modify: `src/mmo_edit/editors/world_editor/pcg/graph_instance_builder.cpp:762-765,947-957,1037-1046,1174-1180`
- Test: `src/tests/pcg_tests/test_graph_paint_tile_material.cpp`

**Interfaces:**
- Consumes: `TerrainTarget::material` (Task 5).
- Produces:
  - `PaintSplat` setting `tileMaterial` (String, default empty, overridable per item by an attribute of the same name).
  - `struct TileMaterialRequest { int32 tileX = 0; int32 tileZ = 0; String material; };`
  - `void PreparedTerrain::ResolveTileMaterials(const GridLayout& layout, const WorldRect& area);` — for every tile overlapping `area`: the material of the last-stacked Paint op with a non-empty `tileMaterial` whose painted region (the area polygon, or the spline grown by `width`) intersects the tile rectangle. Intersection is geometric (an edge crosses the tile, a vertex lies in it, or the tile centre is inside), not a tile-centre sample.
  - `const std::vector<TileMaterialRequest>& PreparedTerrain::GetTileMaterials() const;` — sorted by (tileZ, tileX).
  - `bool PreparedTerrain::IsTileMaterialPaint(float x, float z) const;` — true where a Paint op with `tileMaterial` paints with weight > 0.
  - `Evaluate` with `terrain_channel::Material`: `target.material` = the resolved material of the tile containing (x, z).
  - `void EditorTextureLayers::SetTileMaterialOverrides(const std::vector<pcg::graph::TileMaterialRequest>& overrides);` — called on the main thread before `Prewarm`; `FindLayer` then answers for the override material on those tiles.
  - Warnings (appended by `DescribeNotes` / the builder's `Commit` to `GeneratorBuildReport::warnings`):
    - conflict: `"Tile (<x>, <z>): '<loser>' and '<winner>' both requested; '<winner>' wins"`
    - side effect: `"Tile (<x>, <z>): switching to '<material>' changes layer <n> outside the painted area"`

- [ ] **Step 1: Write the failing tests**, tag `[graph_paint_tile_material]`, with a fixed `GridLayout` from `fake_terrain.h`:

```cpp
TEST_CASE("a paint op with tileMaterial requests every tile it touches", "[graph_paint_tile_material]")
// a square area lying inside tile (0,0) except one corner reaching 1 m into tile (1,1), whose centre is far outside:
// GetTileMaterials() == { (0,0,"Farm.hmi"), (1,1,"Farm.hmi") } and nothing else.           // Review Focus 3

TEST_CASE("the later stacked op wins a tile and the conflict is described", "[graph_paint_tile_material]")
// two areas on one tile with "Plowed.hmi" (node id 5) and "Sprouts.hmi" (node id 9):
// the tile resolves to Sprouts; DescribeNotes output contains "'Plowed.hmi' and 'Sprouts.hmi' both requested".

TEST_CASE("Evaluate reports the tile material in the material channel only", "[graph_paint_tile_material]")
TEST_CASE("a paint op without tileMaterial requests nothing and hashes as before", "[graph_paint_tile_material]")
TEST_CASE("the texture lookup is asked with the op's texture on switched tiles", "[graph_paint_tile_material]")
// lookup stub answers layer 2 for ("Farm" tiles, soil key) and -1 elsewhere: splat[2] == 255 inside the area,
// and notes.missingLayerOps stays empty.
```

- [ ] **Step 2: Run** — expected: does not compile.
- [ ] **Step 3: Implement the shared part** (node setting, prepared op field, `ResolveTileMaterials`, `Evaluate`, notes).
- [ ] **Step 4: Run** `bin\Debug\pcg_tests.exe "[graph_paint_tile_material],[graph_nodes_terrain],[graph_terrain_compose]"` — expected: all pass.
- [ ] **Step 5: Implement the editor part.** In the builder's Begin (main thread): after `Prepare`, call `ResolveTileMaterials` with the terrain's layout and the area rect, hand the result to `EditorTextureLayers::SetTileMaterialOverrides` before `Prewarm` (layer keys of an override come from `GetTerrainLayerTextureKey(MaterialManager::Get().Load(name), layer)`), and keep passing `String()` as the call material. In `Commit`: for every tile actually switched, compare old and new layer keys per slot (`NormalizeTextureKey`); for a differing slot, scan the tile's base splat and emit the side-effect warning if any pixel with weight > 0 in that slot has `IsTileMaterialPaint(x, z) == false`. `EditorTerrainAccess::GetTileMaterial` returns `""` for the default material; compare names on that basis.
- [ ] **Step 6: Build** `mmo_edit` — expected: builds. (End-to-end proof is Task 10.)
- [ ] **Step 7: Commit** `Let Paint Splat switch tiles to a material that carries its layer`.

---

### Task 7: Rectangle editing in the world editor

**Files:**
- Modify: `src/shared/pcg/graph/rectangle_shape.h`, `.cpp` (Task 1 files)
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/procedural_edit_mode.h`, `.cpp` (`:434` `IsPlacedVolume`, `:1308-1342`, `:1359-1376`, `:1439`, `:2878`, `:3001-3041`, `:3632-3785`, `:3831-3912`)
- Modify: `src/mmo_edit/mmo_edit.cpp:1484` (`--pcg-new-control`)
- Test: `src/tests/pcg_tests/test_graph_rectangle.cpp` (extend)

**Interfaces:**
- Consumes: `RectangleCorners`, `MinRectangleHalfExtent` (Task 1).
- Produces (in `rectangle_shape.h`):
  - `enum class RectangleHandle : uint8 { SideNegX, SidePosX, SideNegZ, SidePosZ, CornerNegNeg, CornerPosNeg, CornerPosPos, CornerNegPos };`
  - `Vector3 RectangleHandlePosition(const ControlInstance& inst, RectangleHandle handle);` — side handles at the side's midpoint, corner handles at the corner, `y = position.y`.
  - `void DragRectangleHandle(ControlInstance& inst, RectangleHandle handle, float worldX, float worldZ);` — moves the handle's side(s) to the cursor along the rectangle's own axes; the opposite side (or corner) stays where it is, so `position` and `halfExtents` change together; each half extent is clamped to `MinRectangleHalfExtent`; non-finite input leaves `inst` unchanged.
  - `--pcg-new-control` accepts `--half-extents x,z` and `--yaw <deg>` for transform shapes.

- [ ] **Step 1: Write the failing tests** (tag `[graph_rectangle]`):

```cpp
TEST_CASE("dragging a side keeps the opposite side", "[graph_rectangle]")
// position (0,0,0), halfExtents (10,_,10), yaw 0: DragRectangleHandle(SidePosX, 16, 3)
// -> position.x == 3, halfExtents.x == 13, z values unchanged; the -x side is still at x == -10.

TEST_CASE("dragging a corner keeps the opposite corner", "[graph_rectangle]")
TEST_CASE("dragging works in the rectangle's rotated frame", "[graph_rectangle]")     // yaw 90 and yaw 37 deg
TEST_CASE("a side dragged past its opposite clamps at one metre", "[graph_rectangle]") // Review Focus 1
// DragRectangleHandle(SidePosX, -50, 0): halfExtents.x == 0.5f, the -x side still at -10, no NaN.
TEST_CASE("non-finite cursor positions are ignored", "[graph_rectangle]")
```

- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[graph_rectangle]"` — expected: does not compile.
- [ ] **Step 3: Implement the maths**, run again — expected: pass.
- [ ] **Step 4: Editor.** In `procedural_edit_mode`:
  - creation: one click like a Volume; `halfExtents = Vector3(10, 0.5, 10)` (20 m x 20 m); menu and error texts name the shape;
  - `IsPlacedVolume` stays true for Rectangle (the existing `SelectedControlTransform` gizmo then moves, yaws and scales it; its `MinHalfExtent` is the same 0.5);
  - drawing: a flat outline on the ground through `RectangleCorners` at `GetHandleHeight`, plus the eight handles for the selected rectangle — no box;
  - mouse: a hit on a handle (same radius as `TransformHandleRadius`) starts a handle drag before the transform hit test; `OnMouseHold` intersects the cursor ray with the plane through `position.y` and calls `DragRectangleHandle`, then `MarkChanged` and `NotifyTransformChanged()` on the selectable; `OnMouseUp` ends it. `IsEditIdle` must be false during the drag so one rebuild follows it;
  - details panel: "Half extents" shows x and z only for a rectangle.
- [ ] **Step 5: Build** `mmo_edit`; in a scratch copy of a world run `mmo_edit --pcg-new-control <world> --type <a rectangle .hctl> --points 100,100 --half-extents 15,8 --yaw 30` — expected: exit 0 and a `.hpcgc` with those half extents.
- [ ] **Step 6: Commit** `Edit rectangle controls by their sides and corners`.

---

### Task 8: Whole-control move / rotate / scale

**Files:**
- Create: `src/shared/pcg/graph/control_transform.h`, `.cpp`
- Create: `src/mmo_edit/editors/world_editor/selected_control_shape.h`, `.cpp` (next to `selected_control_transform.h`)
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.h:310-340`, `.cpp:2116-2128,4710`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/procedural_edit_mode.h`, `.cpp` (`:486` `SelectControl`, `:2951-3060`, `:3632-3728`, drawing)
- Modify: `src/mmo_edit/mmo_edit.cpp` (new flag next to `--pcg-new-control`)
- Test: `src/tests/pcg_tests/test_graph_control_transform.cpp`

**Interfaces:**
- Produces (in `control_transform.h`, namespace `mmo::pcg::graph`; all three work on every shape — spline shapes through their points, transform shapes through `position` / `rotation` / `halfExtents`):
  - `Vector3 ControlPivot(const ControlInstance& inst);` — spline shapes: mean x/z/y of the points; transform shapes: `position`.
  - `void TranslateControl(ControlInstance& inst, const Vector3& delta);` — x/z always; y for Catmull-Rom splines and transform shapes, not for Linear splines (the rule `SelectedSplinePoint::Translate` follows).
  - `void RotateControl(ControlInstance& inst, float yawRadians);` — about the pivot around world up; rotates the tangent handles (`inX/inZ/outX/outZ`) with the points.
  - `void ScaleControl(ControlInstance& inst, float scaleX, float scaleZ);` — spline shapes: point offsets from the pivot along world x/z, tangent handles scaled the same; transform shapes: half extents. Each factor is clamped to [0.01, 100]; a non-finite factor counts as 1. Point ids, `nextPointId`, point heights and `width` never change.
  - `class SelectedControlShape : public Selectable` with owner interface `IControlShapeOwner` (`FindShapeControl(uint64)`, `OnControlShapeEdited(uint64)`, `OnControlShapeDeselected(const SelectedControlShape&)`); `Translate` / `Rotate` (yaw of the delta only) / `Scale` call the three functions; `SupportsScale()` true, `SupportsDuplicate()` false.
  - CLI: `mmo_edit --pcg-transform-control <world.hwld> --id <hex> [--move dx,dz] [--rotate <deg>] [--scale sx,sz]` — applies move, then rotate, then scale; saves; exit 0 on success, 1 for an unknown id.

- [ ] **Step 1: Write the failing tests**, tag `[graph_control_transform]`:

```cpp
TEST_CASE("translating a loop moves every point and keeps ids", "[graph_control_transform]")
TEST_CASE("rotating a spline turns points and tangent handles about the centroid", "[graph_control_transform]")
// square loop (0,0)(10,0)(10,10)(0,10), one point with manual tangents out (2,0): RotateControl(pi/2)
// -> centroid still (5,5); that point's out handle is (0,2) under the engine's yaw convention
//    (take the sign from SelectedControlTransform::FromYaw so both gizmos turn the same way); point y unchanged.
TEST_CASE("scaling scales offsets from the centroid and the tangents", "[graph_control_transform]")
// ScaleControl(2, 1): x offsets doubled, z unchanged, centroid unchanged.
TEST_CASE("scale factors are clamped and non-finite ones ignored", "[graph_control_transform]")   // Review Focus 5
// ScaleControl(0, -3) leaves four distinct points on the original side of the pivot; ScaleControl(NaN, 1) changes nothing.
TEST_CASE("a linear spline keeps its heights when translated in y", "[graph_control_transform]")
TEST_CASE("transform shapes route to position, rotation and half extents", "[graph_control_transform]")
```

- [ ] **Step 2: Run** — expected: does not compile.
- [ ] **Step 3: Implement the maths**, run `bin\Debug\pcg_tests.exe "[graph_control_transform]"` — expected: pass.
- [ ] **Step 4: Editor.** For a selected loop or spline control, draw a pivot marker at `ControlPivot` (ground height, distinct from point handles). A click on the marker (hit-tested before the spline editor, like `HitTestTransformControl`) selects a `SelectedControlShape`, which puts the editor's translate / rotate / scale gizmo on the pivot; a click on a point or segment returns to point editing exactly as today. The details panel gets a "Transform whole control" button that does the same and a one-line hint. Add the visitor overloads (details text `"Placed %s Control (whole)"`). Edits call `MarkChanged`; there is no undo in this mode, so none is added. Volume, Point and Rectangle keep `SelectedControlTransform`.
- [ ] **Step 5: Add the CLI flag**, build `mmo_edit`, and on a scratch world: create a loop control with `--pcg-new-control`, run `--pcg-transform-control --id <id> --move 10,0 --rotate 90 --scale 2,1` — expected: exit 0; the `.hpcgc` points match `TranslateControl` + `RotateControl` + `ScaleControl` applied to the original (check two points by hand).
- [ ] **Step 6: Commit** `Move, rotate and scale placed controls as a whole`.

---

### Task 9: Field control type, graph branch and edge section (data)

**Files (all in the `data/client` submodule, branch `feature/pcg-field-control`):**
- Create: `Editor/Pcg/Biomes/FalwynForest/Controls/Field.hctl`
- Modify: `Editor/Pcg/Biomes/FalwynForest/Graphs/FalwynValley.hpcgg`
- Create: `Editor/Pcg/Sections/FieldEdge/` (section world), `Editor/Pcg/Sections/FieldEdge.hsec` (bake output)

**Interfaces:**
- Consumes: shape `rectangle` (Task 1), `keepGround` (Task 2), `Exclude` pin (Task 4), `tileMaterial` (Task 6).

- [ ] **Step 1: `Field.hctl`** — copy the layout of `Road.hctl`. `"name": "Field"`, `"shape": "rectangle"`, `"defaultTags": ["exclusion", "field"]`, one group `"Field"` with:
  - `section` (asset) default `Editor/Pcg/Sections/FieldEdge.hsec`, description `Edge section (.hsec) laid around the field`
  - `tileMaterial` (asset) default `Models/Terrain/Briarwatch_Farmland_Plowed.hmi`, description `Tile material that carries the soil layer`
  - `texture` (asset) default `Textures/Tilesets/T_Field_Plowed_C.htex`, description `Terrain layer the area is painted with`
  - `falloff` (float, min 0, max 20) default `1.5`, description `Distance over which the soil fades out at the edge`

  No `propDensity` (spec: one keep roll per chain would drop whole fences).
- [ ] **Step 2: Graph.** Add to `FalwynValley.hpcgg` and set `nextNodeId` to 31:

| id | type | settings | links |
|---|---|---|---|
| 26 | `GetControls` | `tags` = `field` | |
| 27 | `PaintSplat` | defaults (`texture`, `falloff`, `tileMaterial` arrive as item attributes of the same name) | 26 Out → In |
| 28 | `LaySectionAlongLoop` | `sectionAttribute` = `section`, `heightMode` = `keepGround`, `floorInside` = true | 26 Out → In, 29 Out → Exclude |
| 29 | `GetControls` | `tags` = `exclusion`, `excludeTags` = `field` | also 29 Out → 13 Exclude (valley wall) |
| 30 | `GetControls` | `tags` = `exclusion`, `excludeTags` = `road,field` | 30 Out → 11 Exclude (road) |

  The spec draws the self-filter as a `Filter by Tag` node; `GetControls` has `excludeTags` itself, so two extra `GetControls` nodes do the same with fewer nodes. Node 27 has a lower id than 28, so the soil is stacked under the section's edge paint. Node 25 (waterfall) is not wired.
- [ ] **Step 3: Section world.** `mmo_edit --new-section FieldEdge --folder Editor/Pcg/Sections`; edit `FieldEdge.hsecdef`: `length` 20, `halfWidth` 2, `sideFalloff` 1, `cornerAngleDeg` 35, variants `[{ "originX": 0, "role": "straight" }, { "originX": 60, "role": "innerCorner" }]` (model the file on `ValleyWall.hsecdef`). The floor side (-X) faces the field.
- [ ] **Step 4: Dress the section.** Read which fence and bush meshes, scales and spacing the hand-built field on tiles `30_32` / `30_33` of `Worlds/Development` uses (the only tiles with the farmland material). Place a fence line along +Z at x ≈ +1 and bushes at x ≈ +2.5 in the straight variant, and an L-shaped fence in the corner variant, using the repository's existing content tooling for writing world entities (see `docs/` for the world-aware authoring tools; `TestRoad` is a dressed example); declare the fence pieces of each variant as a run named `Fence` (`keepChance` 1) in the `.hsecdef`, and a rule for the bush asset with `keepChance` 0.7, `positionJitter` 0.5, `yawJitterDeg` 180, `scaleJitter` 0.15. If no existing tool can write entities into a section world, leave the section without props, say so in the task report, and put "dress FieldEdge in the Section panel" on the author's checklist of Task 10 — do not write a one-off tool into `tools/`.
- [ ] **Step 5: Bake** `mmo_edit --bake-section <...>/FieldEdge/FieldEdge.hwld` — expected: exit 0, `FieldEdge.hsec` written; the log lists a straight and an inner corner variant.
- [ ] **Step 6: Check the graph loads:** `mmo_edit --pcg-eval-graph` on an existing `FalwynValley` instance of a scratch world copy — expected: exit 0, no node errors in the dump.
- [ ] **Step 7: Commit** in the submodule (`Add the Field control, its graph branch and the FieldEdge section`), then commit the submodule pointer in the main repo.

---

### Task 10: End-to-end verification, docs, gate

**Files:**
- Create: `docs/pcg-fields.md` (short authoring note: placing a field, cutting a gate with an Exclusion area or a road, the two tile-material limits from the spec, Review Focus 6, the world-aligned furrows)
- Create: `docs/superpowers/specs/2026-11-12-pcg-field-control.png` (capture)
- Test: `src/tests/pcg_tests/test_graph_acceptance.cpp` (extend)

- [ ] **Step 1: Acceptance test** in the style of the existing forest scene:

```cpp
TEST_CASE("field branch: soil, fence, gate and road gap", "[graph_acceptance]")
// a Field rectangle 40 m x 30 m, an Exclusion loop over 6 m of its north edge, a Road (clearance 6) crossing its east edge,
// a forest area around it; graph = the field branch of Task 9 built in code.
// - no forest point lies inside the field;
// - no fence piece has its midpoint inside the exclusion loop or within 6 m of the road;
// - fence pieces exist on all four edges outside those gaps;
// - the terrain target inside the field has heightWeight 0 and the soil layer at full weight 3 m from the edge;
// - removing the Exclusion loop adds pieces and changes no existing piece's key or position.
```

- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe` (whole suite) — expected: all pass.
- [ ] **Step 3: Unattended editor run** on a scratch copy of `Worlds/Collision` (`Worlds/PcgScratch`, deleted afterwards; follow the recipe of `docs/superpowers/plans/2026-11-01-pcg-phase3a-terrain-sections.md:300-363`):
  1. `--pcg-new-control` a `Field` inside an existing `FalwynValley` instance, with `--half-extents 20,15`, plus an `Exclusion` loop over one edge;
  2. `--pcg-rebuild` — expected: exit 0; the touched tiles now name `Briarwatch_Farmland_Plowed.hmi`; the report has no missing-layer warning;
  3. `--pcg-rebuild` again — expected: tile and entity files byte-identical (SHA-256);
  4. `--pcg-transform-control` the field (`--rotate 30`), rebuild — expected: exit 0;
  5. `--pcg-delete` the field, rebuild — expected: the tiles' material names and splat are back to their original bytes;
  6. `--capture-view` of the built field into the PNG above.
- [ ] **Step 4: Write `docs/pcg-fields.md`** and link it from the spec. Do not edit `CLAUDE.md`.
- [ ] **Step 5: Run `/gate`** (fast tier) — expected: green, including the WSL Linux step.
- [ ] **Step 6: Author's checklist** (hand over with the capture; none of these can be driven unattended):
  - place a field with the "New control" menu, drag sides and corners;
  - move / rotate / scale a road and a forest area as a whole via the pivot marker;
  - check that clicking a point still starts point editing;
  - refine `FieldEdge` in the Section panel (and dress it, if Task 9 Step 4 could not);
  - replace the hand-built field in `Worlds/Development`.
- [ ] **Step 7: Commit** `Verify the field control end to end and document it`.
