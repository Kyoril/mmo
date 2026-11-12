# PCG Phase 0 — Valley Wall Spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove, before the graph core is built, that a hand-built cliff section with corner variants laid around a closed valley loop can look like a convincing mountain wall — on a rough Oakenshire-like base in world 2.

**Architecture:** Extend the existing section-template system (no graphs yet): variant roles (straight / inner corner / outer corner) in section assets, closed loops with inward-facing layout and corner placement at sharp control points, and a smoothed-ground height mode that follows the hand-made base. Then block out a base in `Worlds/Collision`, build a placeholder `ValleyWall` section, lay a valley loop and capture it for the user's judgement.

**Tech Stack:** C++17, CMake, Catch2, ImGui, existing `src/shared/pcg` + `mmo_edit`.

**Spec:** `docs/superpowers/specs/2026-10-25-pcg-graphs-design.md` (phase 0; "Resolved after the builder check" items 1–2), building on `docs/superpowers/specs/2026-10-25-pcg-section-templates-design.md`.

## Global Constraints

- Branch `feature/pcg-oasis-poc`. No `/ship`, no merge, no push. Data commits only on the data submodule branch `feature/pcg-oasis-poc` (never `master`).
- **Map 0 is frozen:** no new PCG output on `Worlds/Development`, no changes to its terrain, placements or NPC placements. The existing oasis and TestRoad records there stay as they are (do not rebuild them).
- Test site: **world 2 = `Worlds/Collision`**, on pages not yet used (used today: x 29–36 / z 30–33); confirm map id 2 ↔ `Collision` in the map proto data before writing.
- New biome folder: `data/client/Editor/Pcg/Biomes/FalwynForest/` with `Sections/ValleyWall/…` and `Sections/ValleyWall.hsec` (editor-only, not shipped).
- Section frame (unchanged): along +Z, across X (+X right looking along +Z), up +Y. For walls, **floor side = −X** (the valley side), heights relative to 0 at the floor edge of the centre line.
- Determinism, no exceptions, code style per CLAUDE.md (Allman, tabs, braces on every `if`, `m_camelCase`, `PascalCase`, `#pragma once`, Doxygen, copyright header). Every new ImGui widget gets a unique `##id`.
- Build from PowerShell; never kill the user's running editor (temp `/p:OutDir=…\bin\DebugTmp\` if `bin\Debug\mmo_edit.exe` is locked; never run that build with `bin\Debug` as the working directory). Every world save rewrites the world's `.hwld` (pre-existing); restore it before judging cleanliness.
- Python only as scratch help in the session scratchpad (`py -3.13 -I`), never committed.

## Review Focus

1. **A control point turning the other way (inner vs outer corner) on a loop of either winding** — the right corner role is chosen and the wall still faces the valley. (Task 2 test.)
2. **Two sharp control points closer together than one corner piece** — corners must not overlap; the second falls back to bending. (Task 2 test.)
3. **A loop with no corner variants in the section** — still lays (bending only), no failure. (Task 2 test.)
4. **Smoothed-ground mode on a hand base with a steep step** — the wall follows the large shape without spikes at the step. (Task 3 test.)
5. **Old `.hsec` / `.hsecdef` / section path records** — still load (no roles = straight; open paths unchanged). (Tasks 1, 4.)

---

### Task 1: Variant roles in sections

**Files:** `src/shared/pcg/section_bake.h/.cpp`, `section_asset.h/.cpp`; editor `src/mmo_edit/editors/world_editor/section_panel.*`; tests `src/tests/pcg_tests/test_section_asset.cpp`, `test_section_bake.cpp`.

**Interfaces — produces:**
```cpp
namespace mmo::pcg
{
	enum class SectionVariantRole : uint8 { Straight, InnerCorner, OuterCorner };
	// SectionVariantDef gains: SectionVariantRole role = Straight;  ("role": "straight" | "innerCorner" | "outerCorner" in .hsecdef)
	// SectionVariant  gains: SectionVariantRole role = Straight;    (PSEC version 3; v1/v2 read as Straight)
	// SectionAsset gains: float cornerAngleDeg = 35.0f;              (turn angle above which a corner piece is used; .hsecdef "cornerAngleDeg")
}
```
A corner variant is authored like any variant (same length/halfWidth/sideFalloff), centred on its rect's middle (local z = length/2 is the corner point); the panel gets a role combo per variant and the corner angle field.

- [ ] **Step 1: Failing tests** — `[section_asset]` PSEC v3 round trip with roles; a v2 stream reads with all roles Straight; `[section_bake]` `.hsecdef` round trip with roles and `cornerAngleDeg`; missing keys default to Straight / 35.
- [ ] **Step 2: Run** `bin\Debug\pcg_tests.exe "[section_asset],[section_bake]"` — FAIL.
- [ ] **Step 3: Implement** fields, format bump, parse/write, panel combo (`##variantRole`) and angle field (`##cornerAngle`).
- [ ] **Step 4: Run** — PASS; whole `pcg_tests` green.
- [ ] **Step 5: Commit** `feat(pcg): section variant roles for corners`

---

### Task 2: Closed loops with corners

**Files:** `src/shared/pcg/section_path.h/.cpp`; test `src/tests/pcg_tests/test_section_loops.cpp`.

**Interfaces — produces:** `PrepareSectionPath` accepts closed splines (`spline.closed = true`); `SectionPathParams` gains `bool floorInside = true;` (closed loops: the section's −X side faces the loop interior, independent of winding). `SectionLayout` gains, per control point, the chosen corner role (`std::vector<SectionVariantRole> pointRoles`).

Rules:
- Turn angle at control point i = XZ angle between the incoming and outgoing tangent directions of the sampled path at that point (signed by turn direction relative to the interior). If |angle| ≥ `asset.cornerAngleDeg` and the section has a variant of the matching role (turning toward the interior = inner corner, away = outer corner), place one corner copy centred on the control point covering arc range [d_i − L/2, d_i + L/2] (stretch 1). The neighbouring spans then fit their straight copies into what remains (`max(1, round(remaining / L))`).
- If two corner ranges would overlap or a span's remainder is < L/2, the later corner falls back to bending (no corner piece at that point).
- Without matching corner variants: bending only, as for open paths.
- Closed paths join the last span to the first (no seam): prop runs and terrain joins cross-fade across the wrap.
- Keys stay span-local: a corner copy's key uses Hash(point id, role).

- [ ] **Step 1: Failing tests** `[section_loops]`: a square loop (4 points, 100 m sides) with a section L 20 having inner and outer corner variants → 4 inner corners for a CCW loop and 4 for a CW loop with `floorInside` (interior always on the −X side: sample the terrain target just inside and outside the loop and assert the floor side is inside); an L-shaped (concave) 6-point loop → exactly one outer corner at the reflex vertex; two sharp points 12 m apart with L 20 → only one corner piece; a section with no corner variants → loop lays with bending only; determinism; moving one point changes only its own corner and its two neighbouring spans' keys.
- [ ] **Step 2: Run** `"[section_loops]"` — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS; whole suite green (open-path tests unchanged).
- [ ] **Step 5: Commit** `feat(pcg): section loops with corner pieces`

---

### Task 3: Smoothed-ground height mode

**Files:** `src/shared/pcg/section_path.h/.cpp`; test `src/tests/pcg_tests/test_section_path_terrain.cpp`.

**Interfaces — produces:** `SectionHeightMode::SmoothedGround`; `SectionPathParams` gains `float groundSmoothing = 20.0f;` (window length in metres). Centre-line height(s) = average of `baseGround` sampled along the path (and the section's centre line) over [s − w/2, s + w/2] (wrapping for closed paths). Section heights are added on top. `FollowSpline` and `FollowGround` stay unchanged.

- [ ] **Step 1: Failing tests**: on a base with a 10 m step across the path, SmoothedGround centre height changes monotonically over ≈ the window width (no spike, max slope bounded); on flat base equals base; window 0 equals FollowGround; closed loop wraps.
- [ ] **Step 2–4:** RED → implement → GREEN; whole suite green.
- [ ] **Step 5: Commit** `feat(pcg): smoothed-ground height mode for sections`

---

### Task 4: Section loops in the editor

**Files:** `src/mmo_edit/editors/world_editor/edit_modes/procedural_edit_mode.*`, `pcg/section_path_builder.*`, `src/shared/pcg/section_path_record.*` (record: closed flag + new params, PCGP version 3, v1/v2 read as open/defaults), `src/mmo_edit/mmo_edit.cpp`.

**Produces:** "New Section Loop" (8-point closed loop r = 60 m at the viewport centre, height mode SmoothedGround); params UI gains height mode SmoothedGround + window, `floorInside`, and shows the corner roles chosen per point in the viewport overlay (small marker per corner); `--pcg-new-path` gains `--closed`; the details warning for tight bends stays.

- [ ] **Step 1: Implement** (record round-trip test for closed + params in `test_section_path_record.cpp`, RED → GREEN).
- [ ] **Step 2: Build** `mmo_edit pcg_tests`; green. Map-0 records still load (Development opens; no rebuild run on map 0).
- [ ] **Step 3: Commit** `feat(editor): section loops with corners and smoothed ground`

---

### Task 5: Spike site in world 2, ValleyWall section, captures

**Files:** data only (submodule branch): `Worlds/Collision/...` new pages, `Editor/Pcg/Biomes/FalwynForest/Sections/ValleyWall/...`, `ValleyWall.hsec`, `Editor/Pcg/Worlds/Collision/<id>.hpcg`; docs `docs/superpowers/specs/pcg-graphs/phase0/` (JPEG captures ≤ 400 KB).

- [ ] **Step 1: Confirm** map id 2 ↔ `Worlds/Collision` and choose a free block of ≥ 3×3 pages away from x 29–36 / z 30–33.
- [ ] **Step 2: Block out the base** with `terrain_tool import` (heightmap generated in the scratchpad): a valley floor ≈ 700 m across at a low height, higher land behind the surrounding ring (≈ +40 m), a gate opening on one side; Falwyn terrain material (the one Oakenshire uses on map 0 — look it up, read-only). This is the "hand base".
- [ ] **Step 3: Build `ValleyWall`** with `--new-section` in the FalwynForest biome folder (new sections may need `--new-section` to accept a target folder — add a `--folder` option if missing, own commit): straight variant + inner corner + outer corner (L ≈ 40 m, halfWidth ≈ 25 m, sideFalloff ≈ 15 m): terrain rising from 0 at the floor side to ≈ 35–45 m with a stylised rock face, dressed with Falwyn/Oakenshire cliff and rock meshes (look up the meshes Oakenshire's mountains use on map 0, read-only) and a few pines on the top; edit section worlds with scratch Python writing `.tile`/`.wobj` as for TestRoad. Bake.
- [ ] **Step 4: Lay a valley loop** (`--pcg-new-path … --closed --section …/ValleyWall.hsec`) around the valley floor, leaving the gate open by ending the wall at the gate (if closed loops can't leave a gap, use an open path around the valley instead and note it); rebuild twice ("updated 0" the second time).
- [ ] **Step 5: Captures** (daytime): an overview from above the valley, an eye-level view from the valley floor towards a straight wall stretch, one towards an inner corner, one towards an outer corner (if the loop has one), and — for comparison — one of Oakenshire's mountains on map 0 from a similar viewpoint (capture only; no map-0 writes). JPEG into `docs/superpowers/specs/pcg-graphs/phase0/`.
- [ ] **Step 6: Commit** data on the submodule branch, then gitlink + captures in the main repo. Run `all_tests` + `ctest` — green.
