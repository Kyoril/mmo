# PCG Section Templates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hand-built "section" mini-worlds that are baked into editor-only section assets and laid along 3D splines (bent, repeated per control-point span, with seeded variation), proven with a production-worthy road.

**Architecture:** Pure, headless code in `src/shared/pcg/` (section asset + bake, span/copy mapping, terrain target, placements, records) with Catch2 tests; the editor (`mmo_edit`) gets a generic "apply generated content" core extracted from the oasis builder, a Section panel for section worlds (variants, prop rules, bake), and a Section Path control in the Procedural mode. Everything reuses the PoC's terrain contribution, reconcile, ownership, page-readiness and drift guards.

**Tech Stack:** C++17, CMake (`add_lib`, `mmo_add_test`), Catch2, ImGui, nlohmann/json, `binary_io`.

**Spec:** `docs/superpowers/specs/2026-10-25-pcg-section-templates-design.md` (builds on `docs/superpowers/specs/2026-10-24-pcg-oasis-poc-design.md`).

## Global Constraints

- Branch `feature/pcg-oasis-poc` (continues the PoC). No `/ship`, no merge, no push. Data commits only on the data submodule branch `feature/pcg-oasis-poc`, never `master`.
- Editor-only: section worlds, section assets and records live under `data/client/Editor/Pcg/` (not shipped). `pcg` is linked only by `mmo_edit` and `pcg_tests`.
- Section world path: `data/client/Editor/Pcg/Sections/<Name>/<Name>.hwld`; section definition (variants + prop rules, the editable source besides the world): `data/client/Editor/Pcg/Sections/<Name>/<Name>.hsecdef` (JSON, deterministic key order); baked asset: `data/client/Editor/Pcg/Sections/<Name>.hsec`.
- Section frame: origin = start of the centre line, along = local +Z, across = X (+X right when looking along +Z), up = +Y. Heights in a section are relative to 0.
- Defaults: `sideFalloff` 4 m, `joinBlend` 2 m, variant spacing in a new section world 60 m along X, new section world = the 2×2 pages around world origin, flat at height 0.
- Copies are fitted per control-point span: span j of XZ length `S_j` gets `n_j = max(1, round(S_j / L))` copies of length `S_j / n_j`.
- Within `halfWidth` the section owns the height (weight 1), fading to 0 across `sideFalloff`; paint is written only where the section's paint differs from the section world's default paint, times the same falloff. Paint layers are matched to target tile layers by the layer's albedo texture path; unmatched layers are skipped and reported.
- Props move and turn with the curve but stay upright (yaw from the spline frame, local pitch/roll kept); their y = final ground + their baked height above the section terrain.
- Determinism: hash-based randomness only (`pcg::Hash`), no `std::` distributions/random_device/unordered containers on output paths; same inputs → identical files.
- Code style: CLAUDE.md (Allman, tabs, braces on every `if`, `m_camelCase`, `PascalCase` methods, `#pragma once`, Doxygen on public members, header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`, no exceptions). Test files `test_<topic>.cpp`.
- Build from PowerShell: `cmake -S . -B build` after adding files; `cmake --build build --config Debug -t <targets> -- /m /v:minimal`. Never kill the user's running editor; if `bin\Debug\mmo_edit.exe` is locked, link into a temporary `/p:OutDir=<repo>\bin\DebugTmp\` and delete it afterwards.
- The existing oasis must keep rebuilding unchanged: `mmo_edit --pcg-rebuild Worlds/Development/Development.hwld` → "updated 0"; after `git -C data/client checkout -- Worlds/Development/Development.hwld` only `.hpcg` files may differ.

## Review Focus

1. **Spline bends tighter than `halfWidth + sideFalloff`** — terrain from neighbouring copies must not fold over: nearest-span-point wins deterministically, and the path reports a minimum-radius warning. (Task 6 test.)
2. **A span shorter than one section length** — gets exactly one copy, squashed; props stay inside the corridor. (Task 5 test.)
3. **Section asset missing, unreadable or older than its section world** — Rebuild refuses (missing/unreadable) or warns (stale) and leaves the world untouched. (Task 10 step.)
4. **Inserting or deleting a control point** — only the affected spans' copies and props change; all other placement keys survive. (Task 7 test + Task 8 point-id test.)
5. **Road crossing tiles whose terrain material lacks a section layer** — paint for that layer is skipped there, reported per tile, nothing else breaks. (Task 6 test with a remap table missing a layer.)

---

### Task 1: Placements carry world models, kits and material overrides

**Files:**
- Modify: `src/shared/pcg/placement.h`, `src/shared/pcg/placement_reconcile.cpp`, `src/shared/pcg/oasis_record.h/.cpp`
- Test: `src/tests/pcg_tests/test_placement_reconcile.cpp`, `src/tests/pcg_tests/test_oasis_record.cpp`

**Interfaces:**
- Produces:

```cpp
namespace mmo::pcg
{
	enum class PlacementKind : uint8 { Entity, Foliage, WorldModel };
	struct PlacementMaterialOverride { uint8 materialIndex = 0; String materialName; };
	struct Placement
	{
		uint64 key = 0; PlacementKind kind = PlacementKind::Entity; String asset;
		Vector3 position; Quaternion rotation; Vector3 scale = Vector3(1, 1, 1); bool collides = true;
		std::vector<PlacementMaterialOverride> materialOverrides; // Entity only, sorted by index
		std::vector<String> kitPaths;                             // WorldModel only, ordered
	};
}
```

Oasis record format version 4 writes the new fields per placement; reader accepts 1..4 (older placements: no overrides, no kits).

- [ ] **Step 1: Failing tests** — `[reconcile]`: same key, same asset but different `kitPaths` → remove + create; different `materialOverrides` → remove + create; kind Entity→WorldModel → remove + create; identical → keep. `[oasis_record]`: v4 round trip with one WorldModel placement (2 kit paths) and one Entity with 2 overrides is byte-identical on re-write; a v3 stream (derive with the existing `ToOlderVersion` helper) reads with empty overrides/kits.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[reconcile],[oasis_record]"` — expect FAIL (compile).
- [ ] **Step 3: Implement** the fields, the reconcile "asset change" rule (kind, asset, kits, overrides), record v4.
- [ ] **Step 4: Run** the two tags, then all `pcg_tests` — PASS.
- [ ] **Step 5: Commit** `feat(pcg): placements carry world models, kits and material overrides`

---

### Task 2: Generic apply core in the editor (oasis moved onto it)

**Files:**
- Create: `src/mmo_edit/editors/world_editor/pcg/generated_content.h/.cpp`
- Modify: `src/mmo_edit/editors/world_editor/pcg/oasis_builder.cpp`, `edit_modes/world_edit_mode.h` (IWorldEditor), `world_editor_instance.h/.cpp`

**Interfaces:**
- Consumes: Task 1 `Placement`.
- Produces:

```cpp
namespace mmo
{
	/// Everything one generator wants in the world for one build.
	struct GeneratedContent
	{
		pcg::WorldRect influence;               // terrain influence of this build
		pcg::TerrainTargetFunction terrain;     // may be empty (no terrain contribution)
		String tileMaterial;                    // material set where a target asks for it ("" = none)
		std::function<void(const std::function<float(float, float)>& ground,
			const std::function<float(float, float)>& slope, std::vector<pcg::Placement>& out)> placements;
	};
	/// Owned state of one generator instance (part of each generator's record).
	struct OwnedContent
	{
		pcg::WorldRect influence{};
		pcg::TerrainContribution terrain;
		std::vector<pcg::OwnedPlacement> placements; // sorted by key
	};
	/// Validated by the caller. Loads pages for old ∪ new influence and owned placements, refuses if any
	/// page is not ready or the record/terrain drift guard fails, then Unapply → Apply → placements →
	/// Reconcile → remove/update/create. Same report as before.
	OasisBuildReport ApplyGeneratedContent(IWorldEditor& editor, const GeneratedContent& content, OwnedContent& owned, uint64 ownerId, const String& ownerLabel);
	OasisBuildReport RemoveGeneratedContent(IWorldEditor& editor, OwnedContent& owned);
}
```

`IWorldEditor` gains creation of world-model entities with kit paths and of mesh entities with material overrides (extend the existing `CreateMapEntity`/`CreateWorldModelEntity` path or add `CreatePlacement(const pcg::Placement&, uint64 uniqueId)`; the builder's choice, documented). `RebuildOasis`/`DeleteOasisContent` become thin wrappers over the core (the oasis prepares its shape and passes `EvaluateTerrain` + `BuildPlacements`); `OasisRecord` holds an `OwnedContent` (in-memory refactor only; record bytes unchanged apart from Task 1).

- [ ] **Step 1: Refactor + extend** as above; keep `OasisBuildReport` (alias `GeneratorBuildReport` allowed).
- [ ] **Step 2: Build** `mmo_edit pcg_tests`; `pcg_tests` green.
- [ ] **Step 3: Verify oasis unchanged** — from `bin\Debug`: `mmo_edit.exe --pcg-rebuild Worlds/Development/Development.hwld` → exit 0, "updated 0, kept 308"; restore `Development.hwld`; `git -C data/client status --porcelain` shows at most the `.hpcg` (v4 re-write).
- [ ] **Step 4: Commit** `refactor(editor): generic apply core for PCG generators`

---

### Task 3: Section asset and its file format

**Files:**
- Create: `src/shared/pcg/section_asset.h/.cpp`
- Test: `src/tests/pcg_tests/test_section_asset.cpp`

**Interfaces:**
- Produces:

```cpp
namespace mmo::pcg
{
	struct PropRule { String asset; float keepChance = 1; float positionJitter = 0; float yawJitterDeg = 0; float scaleJitter = 0; std::vector<String> swapGroup; };
	struct SectionPlacement { Placement placement; float heightAboveTerrain = 0; }; // position/rotation local to the variant origin
	struct SectionGrid
	{
		float spacing = 0; int32 originX = 0, originZ = 0; uint32 width = 0, depth = 0; // sample (i,j) at local (originX+i, originZ+j)*spacing
		std::vector<float> values;       // heights, row-major z then x
	};
	struct SectionPaint
	{
		float spacing = 0; int32 originX = 0, originZ = 0; uint32 width = 0, depth = 0;
		std::vector<uint32> packed;      // 4×8-bit weights, layer 0 low byte
		std::vector<uint8> coverage;     // 0..255, 0 = untouched default paint
	};
	struct SectionVariant { float originX = 0; SectionGrid heights; SectionPaint paint; std::vector<SectionPlacement> placements; };
	struct SectionAsset
	{
		String name; float length = 20, halfWidth = 4, sideFalloff = 4;
		std::array<String, 4> layerTextures;   // albedo texture path per paint layer of the section's terrain material
		std::vector<PropRule> rules;           // sorted by asset
		std::vector<SectionVariant> variants;
		uint64 sourceStamp = 0;                // hash of the section world's files at bake time (stale check)
	};
	bool WriteSectionAsset(io::Writer& writer, const SectionAsset& asset); // magic 'PSEC', version 1, deterministic
	bool ReadSectionAsset(io::Reader& reader, SectionAsset& out);
	float SampleHeight(const SectionGrid& grid, float localX, float localZ);          // bilinear, clamped at edges
	void SamplePaint(const SectionPaint& paint, float localX, float localZ, std::array<float, 4>& weights, float& coverage); // bilinear
}
```

- [ ] **Step 1: Failing tests** `[section_asset]`: round trip of a 2-variant asset (grids, paint, 3 placements incl. WorldModel with kits, 2 rules) equal and byte-identical on re-write; truncated stream → false; `SampleHeight` returns exact grid values at grid points, the midpoint average between two, and clamps beyond the edge; `SamplePaint` same for weights and coverage.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[section_asset]"` — FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(pcg): section asset and file format`

---

### Task 4: Section bake (pure part)

**Files:**
- Create: `src/shared/pcg/section_bake.h/.cpp`; move the test `FakeTerrain` into `src/tests/pcg_tests/fake_terrain.h` (shared by tests)
- Test: `src/tests/pcg_tests/test_section_bake.cpp`

**Interfaces:**
- Consumes: Task 3, `ITerrainAccess`, `GridLayout`.
- Produces:

```cpp
namespace mmo::pcg
{
	struct SectionVariantDef { float originX = 0; };
	struct SectionDefinition { String name; float length = 20, halfWidth = 4, sideFalloff = 4; std::vector<SectionVariantDef> variants; std::vector<PropRule> rules; };
	/// World placements of the section world, in world space.
	/// defaultPaint = the packed splat value of an untouched section world (coverage reference).
	bool BakeSection(const SectionDefinition& def, const ITerrainAccess& terrain, const std::vector<Placement>& worldPlacements,
		uint32 defaultPaint, const std::array<String, 4>& layerTextures, uint64 sourceStamp, SectionAsset& out, String& error);
	bool ParseSectionDefinition(const String& json, SectionDefinition& out);   // .hsecdef
	String WriteSectionDefinition(const SectionDefinition& def);               // deterministic key order
}
```

Capture rect per variant: local x ∈ [−halfWidth−sideFalloff, +halfWidth+sideFalloff], z ∈ [0, length], around world (originX, 0). Heights at outer-vertex spacing (inner vertices folded in by sampling the terrain surface at grid points via outer/inner heights — builder's choice, documented), paint at pixel spacing. A placement belongs to a variant if its world (x, z) lies in that variant's rect; its local position = world − (originX, 0, 0); `heightAboveTerrain` = y − terrain height at (x, z). Placements outside every rect are ignored (reported count). Errors: overlapping variant rects, no variants, length/halfWidth ≤ 0.

- [ ] **Step 1: Failing tests** `[section_bake]` with `FakeTerrain` (flat 0, a 1 m dip at local x ∈ [−1, 1] along the whole length of variant 0, paint layer 2 = 255 inside the dip): baked heights −1 in the dip and 0 elsewhere; paint coverage > 0 only in the dip; a prop at world (originX + 2, 0.5, 10) gets local (2, 10) and heightAboveTerrain 0.5; second variant at originX 60 gets its own placements; overlapping rects → false with error; `.hsecdef` JSON round trip identical.
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(pcg): bake section worlds into section assets`

---

### Task 5: Section path frames and span/copy mapping

**Files:**
- Create: `src/shared/pcg/section_path.h/.cpp`
- Test: `src/tests/pcg_tests/test_section_path_mapping.cpp`

**Interfaces:**
- Consumes: `PathSpline`, `SampledPath` (3D samples, XZ arc length).
- Produces:

```cpp
namespace mmo::pcg
{
	struct SectionFrame { Vector3 position; float forwardX = 0, forwardZ = 1; float rightX = 1, rightZ = 0; };
	struct SectionCopy { uint32 span = 0; uint32 copy = 0; float startDistance = 0; float stretchedLength = 0; uint64 spanKey = 0; };
	struct SectionLayout
	{
		std::vector<SectionCopy> copies;  // ordered along the spline
		float minimumBendRadius = 0;      // smallest XZ radius of curvature found (for the warning)
	};
	/// pointIds: stable id per control point (same size as spline.points); spanKey = Hash(id_j, id_{j+1}).
	SectionLayout LayOutSections(const PathSpline& spline, const std::vector<uint64>& pointIds, float sectionLength);
	SectionFrame FrameAt(const SampledPath& path, float distance);
	/// World (x, z) → nearest point on the path; returns false beyond maxAcross.
	bool ProjectToPath(const SampledPath& path, float x, float z, float maxAcross, float& distance, float& across);
	/// Copy containing an arc distance and the local z inside it (0..sectionLength).
	const SectionCopy* FindCopy(const SectionLayout& layout, float distance, float sectionLength, float& localZ);
}
```

Spans use the control-point arc distances of the sampled path (add `SampledPath` distance-at-control-point if missing, in Task 5's diff with a math test).

- [ ] **Step 1: Failing tests** `[section_path_map]`: straight 100 m spline, 2 points, L 20 → 5 copies of 20; 3 points (0, 45, 100 m) with L 20 → spans of 45 (2 copies of 22.5) and 55 (3 copies of 18.33); a 7 m span → 1 copy of 7 (Review Focus 2); `FrameAt` on a straight +Z spline gives forward (0,1), right (1,0); on a rising spline the frame y follows the spline y; a quarter circle r=30 reports `minimumBendRadius` ≈ 30 (±2); `ProjectToPath` of a point 3 m to the right of a straight spline returns across +3; moving point 2 of a 4-point spline changes only the copies of spans 1 and 2 (compare `startDistance`/`stretchedLength`/`spanKey` of span 0 and 3); inserting a point (new id) between points 1 and 2 leaves spans 0 and 2's spanKeys unchanged.
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(pcg): section path layout along spline spans`

---

### Task 6: Section path terrain target

**Files:**
- Modify: `src/shared/pcg/section_path.h/.cpp`
- Test: `src/tests/pcg_tests/test_section_path_terrain.cpp`

**Interfaces:**
- Consumes: Tasks 3, 5; `TerrainTarget`.
- Produces:

```cpp
namespace mmo::pcg
{
	enum class SectionHeightMode : uint8 { FollowSpline, FollowGround };
	struct SectionPathParams { uint64 seed = 1; bool sequentialVariants = false; float joinBlend = 2; float propDensity = 1; SectionHeightMode heightMode = SectionHeightMode::FollowSpline; };
	/// layerRemap[sectionLayer] = target tile layer index, or -1 = not available on that tile.
	using LayerRemapFunction = std::function<std::array<int8, 4>(float x, float z)>;
	struct SectionPathShape; // opaque: sampled path, layout, asset ref, params, influence
	bool PrepareSectionPath(const PathSpline& spline, const std::vector<uint64>& pointIds, const SectionAsset& asset,
		const SectionPathParams& params, const std::function<float(float, float)>& baseGround, SectionPathShape& out, String& error);
	WorldRect GetInfluenceRect(const SectionPathShape& shape);
	uint32 GetVariant(const SectionPathShape& shape, const SectionCopy& copy);   // Hash(seed, spanKey, copy) or sequence
	TerrainTarget EvaluateSectionTerrain(const SectionPathShape& shape, float x, float z, float baseHeight, const LayerRemapFunction& remap);
}
```

Rules (spec): centre-line height = spline y (FollowSpline) or `baseGround` under the spline (FollowGround); height target = centre + `SampleHeight`; height weight 1 for |across| ≤ halfWidth, smoothstep to 0 at halfWidth + sideFalloff; paint target = remapped section weights, paint weight = coverage × falloff; within `joinBlend` of a copy boundary cross-fade the two copies' targets (sample each copy's grid clamped beyond its end); `splat` of `TerrainTarget` gets the remapped weights, unmatched layers dropped and the remaining renormalised; `setMaterial` false (paint only).

- [ ] **Step 1: Failing tests** `[section_path_terrain]` (synthetic asset: L 20, halfWidth 3, sideFalloff 4, heights −0.5 for |x| ≤ 2 else 0, paint layer 2 coverage 255 for |x| ≤ 2): straight flat spline at y 10 → height 9.5 on the centre line, weight 1; at across 5 (inside falloff) weight in (0, 1); at across 8 weight 0; rising spline (y 10 → 20 over 100 m) → centre height at s=50 ≈ 14.5; across a copy join, |height(s=20−ε) − height(s=20+ε)| < 0.01 with two different variants whose edge heights differ by 0.4 (the cross-fade); tight bend (radius 3 < halfWidth+sideFalloff): two runs give identical targets and `minimumBendRadius` < 7 (Review Focus 1); remap with layer 2 → −1 at x > 50 → no layer-2 weight there and weights still sum to 255 (Review Focus 5).
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(pcg): section path terrain shaping and paint`

---

### Task 7: Section path placements

**Files:**
- Modify: `src/shared/pcg/section_path.h/.cpp`
- Test: `src/tests/pcg_tests/test_section_path_placements.cpp`

**Interfaces:**
- Produces:

```cpp
namespace mmo::pcg
{
	/// ground = final ground (base + this path's terrain). Output sorted by key, keys unique.
	void BuildSectionPlacements(const SectionPathShape& shape, const std::function<float(float, float)>& ground, std::vector<Placement>& out);
}
```

Per copy and variant placement i: world position = frame(s).position + right·x (s = copy start + localZ·stretch), y = ground(x, z) + heightAboveTerrain; rotation = yaw(frame) · local rotation; variation from `Hash(seed, spanKey, copy, i)`: keep if hash < keepChance × propDensity, position jitter in the XZ plane, yaw jitter, uniform scale jitter, swap to `swapGroup[hash % n]` (keep own asset when the group is empty); key = `Hash(spanKey, copy, variant, i)`.

- [ ] **Step 1: Failing tests** `[section_path_place]`: straight +Z spline, prop at local (2, 10, h 0.5) → world (x0+2, ground+0.5, z0+10), rotation yaw 0; spline turned 90° (along +X) → the same prop lands at (x0+10, …, z0−2) and is yawed 90°; upright: a rising spline does not tilt the prop (rotation's up axis = world up); determinism (two builds identical); keepChance 0 for an asset removes all its instances, `propDensity` 0.5 keeps roughly half (±15 % over 200 instances); swap group picks only listed assets; moving one control point keeps every key and transform of spans not touching it (Review Focus 4).
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(pcg): section path placements with seeded variation`

---

### Task 8: Section path record

**Files:**
- Create: `src/shared/pcg/section_path_record.h/.cpp`
- Test: `src/tests/pcg_tests/test_section_path_record.cpp`

**Interfaces:**
- Produces:

```cpp
namespace mmo::pcg
{
	struct SectionPathRecord
	{
		uint64 id = 0; String name; String sectionAsset; SectionPathParams params;
		PathSpline spline; std::vector<uint64> pointIds;  // open, CatmullRom, 3D; ids parallel to points
		uint64 nextPointId = 1;
		WorldRect influence{}; TerrainContribution terrain; std::vector<OwnedPlacement> placements;
	};
	bool WriteSectionPathRecord(io::Writer& writer, const SectionPathRecord& record); // magic 'PCGP', version 1, deterministic
	bool ReadSectionPathRecord(io::Reader& reader, SectionPathRecord& out);
	/// Peeks the magic: which record type a .hpcg file holds.
	enum class RecordKind : uint8 { Unknown, Oasis, SectionPath };
	RecordKind PeekRecordKind(io::Reader& reader);
}
```

Files keep the `.hpcg` extension in `Editor/Pcg/Worlds/<world>/`; the Procedural mode dispatches on `PeekRecordKind`.

- [ ] **Step 1: Failing tests** `[section_path_record]`: round trip of a populated record byte-identical; `PeekRecordKind` on an oasis stream → Oasis, on a path stream → SectionPath, on garbage → Unknown; `pointIds.size() != points.size()` → write returns false.
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(pcg): section path records`

---

### Task 9: Section worlds in the editor (create, define, bake)

**Files:**
- Create: `src/mmo_edit/editors/world_editor/section_panel.h/.cpp`, `src/mmo_edit/editors/world_editor/pcg/section_bake_job.h/.cpp`
- Modify: `world_editor.cpp` (menu "New Section…"), `world_editor_instance.h/.cpp`, `src/mmo_edit/mmo_edit.cpp`

**Interfaces:**
- Consumes: Tasks 3, 4; `EditorTerrainAccess`.
- Produces: `bool CreateSectionWorld(const String& name, const String& terrainMaterial, String& error)` (same code path as the menu); `bool BakeSectionWorld(WorldEditorInstance& world, String& error)` (writes `<Name>.hsec` next to the section folder); flags `mmo_edit --new-section <Name> [--material <asset>]` and `mmo_edit --bake-section Editor/Pcg/Sections/<Name>/<Name>.hwld` (exit 0/1, logs `new_section.log` / `bake_section.log`, reuse `RedirectUnattendedLog`/`OpenWorldForJob`).

Behaviour: "New Section…" asks for a name and creates the world (2×2 pages around origin, flat 0, the chosen terrain material — default `FalwynPlains_Terrain_Inst.hmi`) plus a `.hsecdef` with one variant at originX 0. A world is a section world when a `<Name>.hsecdef` sits next to its `.hwld`; then the Section panel shows: name, length, halfWidth, sideFalloff, variant list (add at the next free 60 m slot, remove, edit originX), prop rules per asset (pick from assets placed in the section; keep/jitter/swap), **Bake**, and a stale indicator (`sourceStamp` vs. current files). Overlays draw each variant rectangle, the centre line (+Z arrow) and the falloff band. Layer textures come from the section terrain material's `GetLayerBinding(layer).albedoTextureParam` → `GetTextureParameter`. `defaultPaint` = the paint of a freshly created section world (layer 0 = 255). Modes that need `Worlds/<n>/<n>.hwld` (spawns, atlas, area triggers) stay as they are and may report "not available" for section worlds.

- [ ] **Step 1: Implement** creation, panel, overlays, bake job and flag.
- [ ] **Step 2: Build** `mmo_edit`; `pcg_tests` green.
- [ ] **Step 3: Verify headless** — from `bin\Debug`: `mmo_edit.exe --new-section PlanCheck` → exit 0, folder `Editor/Pcg/Sections/PlanCheck/` with `.hwld`, pages and `.hsecdef`; `mmo_edit.exe --bake-section Editor/Pcg/Sections/PlanCheck/PlanCheck.hwld` → exit 0, `PlanCheck.hsec` written; running the bake again produces a byte-identical `.hsec`; `--bake-section` on a missing path → exit 1. Delete the `PlanCheck` files afterwards (`git -C data/client status --porcelain` empty).
- [ ] **Step 4: Commit** `feat(editor): section worlds with variants, prop rules and bake`

---

### Task 10: Section Path control in the Procedural mode

**Files:**
- Create: `src/mmo_edit/editors/world_editor/pcg/section_path_builder.h/.cpp`
- Modify: `edit_modes/procedural_edit_mode.h/.cpp`, `world_editor_instance.h/.cpp`, `src/mmo_edit/mmo_edit.cpp`

**Interfaces:**
- Consumes: Tasks 2, 5–8; `SplineEditController`/`SelectedSplinePoint` (open curved splines get tangent handles and the gizmo like oasis loops).
- Produces: `OasisBuildReport RebuildSectionPath(IWorldEditor&, pcg::SectionPathRecord&, const pcg::SectionAsset&)`, `DeleteSectionPathContent(...)`; Procedural mode lists controls of both kinds ("Oasis"/"Section Path" column); "New Section Path" (2 points 20 m apart along the camera's forward on the ground, asset picked from `Editor/Pcg/Sections/*.hsec`); params UI (asset, seed + Randomize, variant mode, joinBlend, propDensity, heightMode); point ids assigned from `nextPointId` on create/insert and removed with their point; flag `mmo_edit --pcg-new-path <world> --points x,z;x,z[;…] --section <asset> [--seed S] [--name N]` (points at terrain height).

Rebuild refuses (world untouched, red status, exit 1 for flags) when the section asset is missing/unreadable; warns (yellow status, continues) when the `.hsec` is older than its section world files (Review Focus 3). Layer remap per target tile: compare the tile material's layer albedo texture paths with `asset.layerTextures`; the report lists skipped (tile, layer) pairs. Overlap check: as for oases but against all controls; a path's influence rect is the union of per-copy rects (not one huge box). The details panel shows a warning when `minimumBendRadius < halfWidth + sideFalloff` (Review Focus 1).

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build** `mmo_edit pcg_tests`; green.
- [ ] **Step 3: Verify** missing asset → `--pcg-new-path ... --section Editor/Pcg/Sections/Missing.hsec` exits 1 and `git -C data/client status --porcelain` stays empty (after restoring `Development.hwld`); oasis still "updated 0".
- [ ] **Step 4: Commit** `feat(editor): Section Path control`

---

### Task 11: Road section, test path and comparison

**Files:**
- Data (submodule branch `feature/pcg-oasis-poc`): `data/client/Editor/Pcg/Sections/TestRoad/…`, `Editor/Pcg/Sections/TestRoad.hsec`, `Editor/Pcg/Worlds/Development/<id>.hpcg`, touched `Worlds/Development/Development/{Terrain,Entities,Foliage}`
- Docs: `docs/superpowers/specs/pcg-section-templates/` (captures, JPEG ≤ 400 KB each)

- [ ] **Step 1: Placeholder road section** — study an existing Oakenshire/Briarwatch road (paint layer `stone_road`, verge, lanterns `Models/FalwynPlains/Props/Village/Lantern.hmsh`, fences `Models/FalwynPlains/Props/WoodProps/SM_Fence_0x.hmsh`; capture one with `--capture-view`). Build `TestRoad` (2 variants, L 20, halfWidth 3, sideFalloff 4) in the section world: a slightly sunken (−0.3 m) road bed painted stone_road, a ground verge, one lantern per variant on alternating sides, a short fence run in variant 1; rules: lantern keep 0.8, fence keep 0.6 + yaw jitter 3°. (The user will replace it with a hand-built one; it proves plumbing and serves as an example.) Bake with `--bake-section`.
- [ ] **Step 2: Test path** in a meadow area of map 0 near Briarwatch (away from existing roads and placements): 5 points, a curve and a rise of ≥ 5 m, via `--pcg-new-path`.
- [ ] **Step 3: Determinism** — `--pcg-rebuild` twice; after restoring `Development.hwld` the second run leaves `git -C data/client status --porcelain` unchanged from the first. Then move one middle point (via a scratch edit of the record through the editor UI if available, else skip) — note result.
- [ ] **Step 4: Delete check** — `--pcg-delete` the path on a copy of the state: heights/splat restored, all owned objects gone (compare page files before/after, normals ±1 LSB accepted).
- [ ] **Step 5: Captures** — the test road from 2 viewpoints and the reference Oakenshire/Briarwatch road from comparable viewpoints, JPEG into `docs/superpowers/specs/pcg-section-templates/`.
- [ ] **Step 6: Commit data on the submodule branch, then gitlink + captures in the main repo.** No push.
- [ ] **Step 7: Run** `cmake --build build --config Debug -t all_tests` and `ctest -C Debug --output-on-failure` (from `build`) — all green.
