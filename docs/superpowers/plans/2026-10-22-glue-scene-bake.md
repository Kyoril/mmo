# Glue Scene Bake Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the glue screens' live world streaming with a build-time bake from mmo_edit: merged meshes, baked terrain, lights, sounds and particles described by a manifest that the client loads in under a second.

**Architecture:**
- Headless logic lives in `src/shared/game_common/` so both mmo_edit and the client link it: the glue definition, path sampling, visibility, the manifest and fingerprints.
- mmo_edit gets a `GlueSceneBaker` that:
  - loads the world through `WorldEditorInstance`
  - collects render items, lights, emitters and foliage visible from sampled glue cameras
  - merges meshes per chunk and material
  - bakes the terrain (near tiles keep their layered material, far tiles get a baked albedo)
  - writes the output to `data/client/Interface/GlueUI/GlueBake/`
- The client's `GlueScene` loads the manifest instead of a `ClientWorldInstance`, and falls back to streaming when the bake is missing or its camera hash is stale.

**Tech Stack:** C++17, Catch2, nlohmann/json, mmo_edit (D3D11 device present in headless jobs), frame_ui. Gate scripts are Python.

**Spec:** `docs/superpowers/specs/2026-10-22-glue-scene-bake-design.md`

## Global Constraints

- **Branch:** `feature/glue-scene-bake` in the worktree `H:/mmo/.claude/worktrees/plaza-char-select`, in both the main repo and `data/client`. Data commits go on the submodule branch, followed by a gitlink bump (`git add data/client` only).
- **Code style:** CLAUDE.md applies.
  - Allman braces and tabs, `m_camelCase` members, `PascalCase` methods.
  - Doxygen comments and the copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
  - No exceptions; nlohmann/json is used non-throwing (`parse(.., nullptr, false)` and `is_discarded`).
- **Tooling:** the bake is C++ in mmo_edit only. No committed Python or other scripts produce assets. The gate staleness checker is a gate check, not asset tooling, so Python is fine there.
- **Load target:** the gate view is ready **< 1 s** after `LoginState::OnEnter`, on a warm disk in a Debug build, with no pop-in afterwards.
- **Interiors:** WMO groups with Interior `0x2000` and without Exterior `0x8` are never baked.
- **Near terrain:** near tiles keep the **real layered terrain material**: the per-tile material plus a "Splatting" coverage texture built from `Page::GetLayersAt`, as in `terrain/tile.cpp:50-77`. Far tiles use a baked albedo with `Models/AlbedoNormal_Opaque_Base.hmat`. If crispness conflicts with speed, crispness on near tiles wins.
- **Carried over:** lights, sound emitters and particle emitters that are in view (or audible) go into the manifest and are recreated at runtime.
- **Staleness:**
  - The client falls back to streaming only on a `GlueScene.json` hash mismatch or a missing manifest.
  - A world-input fingerprint mismatch is a **warning only**, raised by the gate and the weekly audit.
- **Output folder:** `data/client/Interface/GlueUI/GlueBake/`, with the manifest file `GlueBake.json` in it.
- **Untouched:** `WorldState` and in-world streaming behaviour.
- **Machine use:** don't launch the client unless a task says so. The user plays on the second monitor: drive the client only through launch options (`-exec`, config files, the `script` console command), with no input injection and no focus stealing.

## Review Focus

1. **A camera pose the samples miss** (e.g. 21:9 or 4:3 windows, the gate drift extremes). Expected: no missing geometry at the frame edges. Test: `GlueBakeVisibility` includes an object visible only at a 21:9 edge (Task 1).
2. **Child WMOs with non-uniform or mirrored transforms.** Expected: merged geometry with the correct position and winding. Test: a merge test with a mirrored scale (Task 3).
3. **Bake output edited by hand or truncated.** Expected: the client falls back to streaming with a warning, never crashes or shows a partial scene. Test: manifest parse rejects missing or invalid fields (Task 1), and the runtime treats a parse failure as missing (Task 6).
4. **Near/far terrain boundary seen from the flight.** Expected: no visible crack or colour jump. Test: the edge vertices of the near and far meshes match exactly on shared borders (Task 4).
5. **A stale bake after a Haven edit.** Expected: a gate warning naming the changed files and the rebake command. Test: the checker test with a modified input file (Task 7).

---

### Task 1: Shared glue bake logic (definition move, sampling, visibility, manifest, fingerprints)

**Files:**
- Move: `src/shared/game_client/glue_scene_definition.{h,cpp}` → `src/shared/game_common/glue_scene_definition.{h,cpp}`
- Move: the pure path functions `GlueCameraKey` and `EvaluateGluePath` from `glue_camera_director.*` into `src/shared/game_common/glue_camera_path.{h,cpp}`. The director stays in game_client and includes the new header.
- Create: `src/shared/game_common/glue_bake.{h,cpp}`
- Create: `src/tests/game_common_tests/test_glue_bake.cpp`
- Modify: every include site of the moved headers. The existing game_client tests keep passing.

**Interfaces (produces, namespace `mmo`):**
- Pose sampling:
  - `struct GlueCameraPose { Vector3 position; Vector3 lookAt; };`
  - `std::vector<GlueCameraPose> SampleGlueCameraPoses(const GlueSceneDefinition& def, uint32 pathSamples = 48);`
  - Covers path samples t∈[0,1], gate pose ± drift (±0.3 m sideways), the plaza pose (last key), a push-in end pose toward each spot and toward `front` (40% of the way to position + 1.4 m up), and 8 samples along `entrance` → `front` at 1.7 m eye height looking toward the plaza pose.
- Visibility:
  - `struct GlueBakeFrustumSet { ... };`
  - `GlueBakeFrustumSet BuildGlueBakeFrusta(const std::vector<GlueCameraPose>& poses, float verticalFovDegrees, float farDistance);`
    - Builds one frustum per pose for each aspect ratio in {4/3, 16/9, 21/9}.
  - `bool GlueBakeFrustumSet::Intersects(const AABB& worldBounds, float margin) const;`
  - `float GlueBakeFrustumSet::MaxProjectedSizePixels(const AABB& worldBounds, float screenHeightPixels) const;`
- `bool IsGlueBakeInteriorGroup(uint32 groupFlags);`: true when `(flags & 0x2000) && !(flags & 0x8)`.
- Manifest:
  - `struct GlueBakeManifest` with fields:
    - `uint32 version` (1)
    - `String sceneHash` (hex FNV-1a 64 of the GlueScene.json bytes)
    - `std::vector<GlueBakeInputFile> inputs` (`{ String path; String hash; }`)
    - `std::vector<GlueBakeMesh> meshes` (`{ String mesh; Matrix4 transform; }`)
    - `std::vector<GlueBakeTerrainTile> terrainTiles` (`{ String mesh; String material; String splatting; }`; `splatting` is empty for far tiles)
    - `std::vector<GlueBakeFoliage> foliage` (`{ String mesh; Vector3 position; Quaternion rotation; Vector3 scale; }`)
    - `std::vector<GlueBakeLight> lights` (all point-light fields the WMO and kit lights carry; mirror `WorldModel`/`PropKit` light structs)
    - `std::vector<GlueBakeSound> sounds` (`{ uint32 soundEntry; Vector3 position; float radius; ... }`, mirroring `AmbientSoundEmitters`' input)
    - `std::vector<GlueBakeParticles> particles` (`{ String file; Matrix4 transform; }`)
    - `AABB bounds`
  - `String WriteGlueBakeManifest(const GlueBakeManifest&);`
  - `std::optional<GlueBakeManifest> ParseGlueBakeManifest(const String& json);`
    - Returns nullopt on any missing required field or wrong type, or when the version is not 1.
- Fingerprints:
  - `String HashGlueBakeBytes(const void* data, size_t size);`: FNV-1a 64, 16 lowercase hex chars.
  - `std::vector<String> FindStaleGlueBakeInputs(const GlueBakeManifest&, const std::function<std::optional<String>(const String& path)>& hashFile);`
    - Returns the paths whose current hash differs, or which are missing.

- [ ] **Step 1: Write failing tests** `[glue_bake]` in `test_glue_bake.cpp`:
  - The pose count equals 48 plus the gate, drift, plaza, push-in and entrance samples. The first path sample equals `EvaluateGluePath(def.flightPath, 0)`.
  - **21:9 edge case (Review Focus 1):** with one pose looking down −Z (vertical FOV 60°), an AABB placed just inside the horizontal edge of a 21:9 frustum but outside 16:9 is intersected.
  - An AABB behind the camera, or beyond `farDistance`, is not intersected.
  - `MaxProjectedSizePixels` of a 1 m box at 100 m with vertical FOV 60° at 1080 px is about 9.4 px (`Approx(9.35).epsilon(0.05)`).
  - `IsGlueBakeInteriorGroup(0x2000)` is true, `(0x2000|0x8)` is false, and `0x8` is false.
  - **Manifest round-trip:** `ParseGlueBakeManifest(WriteGlueBakeManifest(m))` equals `m` field by field. Rejected (Review Focus 3):
    - a manifest without `sceneHash`
    - version 2
    - `meshes` that is not an array
    - truncated JSON
  - `HashGlueBakeBytes("abc", 3) == "e71fa2190541574b"` (FNV-1a 64 of "abc").
  - **Stale inputs:** `FindStaleGlueBakeInputs` reports a changed hash and a missing file, and nothing else.
- [ ] **Step 2: Run and see it fail.** Run `cmake --build build --config Debug -t game_common_tests`. Expected: compile errors.
- [ ] **Step 3: Move the files, implement the units, and fix the includes.** Also run `cmake --build build --config Debug -t game_client_tests mmo_client` so the move doesn't break the client.
- [ ] **Step 4: Run the tests.** Run `bin/Debug/game_common_tests.exe "[glue_bake]"`. Expected: pass. `game_client_tests` must still pass. The headless Linux build of game_common must stay OK, checked with `MSYS_NO_PATHCONV=1 wsl bash tools/gate/linux_gate.sh /mnt/h/mmo/.claude/worktrees/plaza-char-select game_common_tests`.
- [ ] **Step 5: Commit** with the message "feat(game_common): glue bake sampling, visibility, manifest and fingerprints".

### Task 2: Editor collection of visible render items

**Files:**
- Create: `src/mmo_edit/glue_bake/glue_scene_collector.{h,cpp}`
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.{h,cpp}`. Add a narrow public API instead of exposing members:
  - `void ForEachLoadedPlacement(const std::function<void(const GlueBakePlacement&)>&)`
  - `void ForEachFoliageInstance(...)`
  - `void ForceLoadPages(const std::vector<PagePosition>&)` / `ReleaseForcedPages()`, reusing the minimap bake helpers at ~2193-2266
  - accessors for terrain pages and water

**Interfaces:**
- Consumes (Task 1): `SampleGlueCameraPoses`, `BuildGlueBakeFrusta`, `IsGlueBakeInteriorGroup`, and the `GlueBakeLight`, `GlueBakeSound` and `GlueBakeParticles` structs.
- Produces:
  - `struct GlueRenderItem { MeshPtr mesh; String meshPath; Matrix4 world; std::vector<String> materialOverrides; bool mergeable; AABB worldBounds; };`
  - `struct GlueCollection`, containing:
    - `std::vector<GlueRenderItem> items`
    - `std::vector<GlueBakeFoliage> foliage`
    - `std::vector<GlueBakeLight> lights`
    - `std::vector<GlueBakeSound> sounds`
    - `std::vector<GlueBakeParticles> particles`
    - `std::vector<PagePosition> terrainPages`
    - `std::vector<GlueBakeInputFile> inputs`
    - stats: `skippedInterior`, `skippedSmall`, `unmergeable`
  - `GlueCollection CollectGlueScene(WorldEditorInstance& world, const GlueSceneDefinition& def, const GlueBakeFrustumSet& frusta, float smallObjectPixels = 2.0f);`
- **Traversal:**
  - every `.wobj` placement on the candidate pages
  - WMO groups (`WorldModelGroup::meshes`, a group skipped when interior)
  - child WMOs, recursive with their TRS (as `nav_build/map.cpp` ~272 does)
  - doodads of the active doodad set
  - prop kits (`PropKitManager`)
  - plain mesh entities
  - foliage `.hfol`
  - **Lights:** from WMOs and kits.
  - **Sounds:** from the same sources `CollectSoundEmitters` uses.
  - **Particles:** from doodads and kits that reference `.hpar`.
  - **Candidate pages:** the pages whose bounds intersect the frusta.
  - **Recording:** every file read goes into `inputs`, with its hash computed by `HashGlueBakeBytes` over the file bytes from the `AssetRegistry`.
- `mergeable` = the mesh vertex layout is the standard static layout and the mesh is not skinned. Use the existing `CanMergeMesh` from `world_model_instance_merge.cpp`, extracted in Task 3; or, in this task, a local equivalent that Task 3 then replaces with the shared one.

- [ ] **Step 1: Implement** the API additions and the collector.
- [ ] **Step 2: Add a debug flag `--glue-bake-dry-run <GlueScene.json>`** to `mmo_edit.cpp`, following the existing job pattern (log file, `PostQuitMessage`). It runs only the collection and logs the stats and counts per category.
- [ ] **Step 3: Run it.**
  - Run `bin/Debug/mmo_edit.exe --glue-bake-dry-run data/client/Interface/GlueUI/GlueScene.json`.
  - Expected: exit code 0. The log shows non-zero items, lights and pages, a non-zero `skippedInterior`, and the Haven gate and plaza WMOs present by mesh path.
  - mmo_edit opens a window. Run it while the user isn't in the way, or accept the brief window; it must not take input.
- [ ] **Step 4: Commit** with the message "feat(edit): collect glue-visible render items, lights, sounds, particles".

### Task 3: Merge and write meshes

**Files:**
- Create: `src/shared/scene_graph/mesh_merge.{h,cpp}`. Move `ReadSubMesh`, `MaterialBucket`, `CreateMergedMesh` and `CanMergeMesh` out of the anonymous namespace in `world_model_instance_merge.cpp`, and make that file use the shared versions.
- Create: `src/mmo_edit/glue_bake/glue_mesh_writer.{h,cpp}`
- Test: `src/tests/scene_graph_tests/test_mesh_merge.cpp`. It uses the existing `null_device` there for GPU-less buffers, if it supports Map; otherwise test the CPU-side `AppendTransformedGeometry` path only.

**Interfaces:**
- Produces:
  - `std::vector<GlueBakeMesh> WriteMergedGlueMeshes(const std::vector<GlueRenderItem>& items, const String& outDir, float chunkSize = 48.0f);`
    - Groups the items by chunk (world XZ cell of the item bounds' centre) and by material name (after overrides).
    - Merges with `AppendTransformedGeometry` into world space and splits a bucket above 1,000,000 vertices.
    - Writes `<outDir>/Chunk_<x>_<z>_<n>.hmsh` via `MeshSerializer`, the way `FbxImport::SaveMeshFile` does.
    - Returns manifest entries with an identity transform.
    - Items with `mergeable == false` are not merged: they are returned as `GlueBakeMesh{ meshPath, world }`, referencing the original mesh file.
  - Submesh names: `"m<index>"`, so names are never empty.

- [ ] **Step 1: Write failing tests.** In `[mesh_merge]`, two quads with the same material and different transforms merge into one submesh. The vertex positions equal the transformed inputs, and a mirrored scale `(-1,1,1)` flips the triangle winding (Review Focus 2).
- [ ] **Step 2: Extract the helpers.** `world_model_instance_merge.cpp` behaviour is unchanged. Run the existing `scene_graph_tests` and build `mmo_client`.
- [ ] **Step 3: Implement** `WriteMergedGlueMeshes`.
- [ ] **Step 4: Run the tests.** Run `bin/Debug/scene_graph_tests.exe "[mesh_merge]"`. Expected: pass.
- [ ] **Step 5: Commit** with the message "feat: shared mesh merge helpers and glue chunk mesh writer".

### Task 4: Terrain bake (near layered tiles, far albedo chunks)

**Files:**
- Create: `src/mmo_edit/glue_bake/glue_terrain_baker.{h,cpp}`
- Read: `mmo_edit/editors/world_editor/terrain_lod_baker.cpp` and `page_bake_utils.h` for the top-down render and the DXT1 writer, and `terrain/tile.cpp:50-77` for the near-tile material and coverage.

**Interfaces:**
- Consumes: `GlueCollection::terrainPages` and `GlueBakeFrustumSet`.
- Produces `std::vector<GlueBakeTerrainTile> BakeGlueTerrain(WorldEditorInstance& world, const GlueBakeFrustumSet& frusta, const std::vector<GlueCameraPose>& poses, const String& outDir, float nearDistance = 80.0f);`.
- **Visible tiles:** a tile is any of the 16×16 tiles per page whose bounds intersect the frusta.
- **Near tiles** (within `nearDistance` of any pose):
  - one mesh per tile at full resolution, with heights and normals from the page and terrain holes removed
  - a vertex layout that the terrain material's shader accepts
  - the `material` field holds the tile's real material name
  - the `splatting` field holds `<outDir>/Splat_<px>_<pz>_<tx>_<ty>.htex`, written as uncompressed RGBA8 with the exact pixel layout `Tile` builds (`buffer[localY + localX * PixelsPerTile] = page.GetLayersAt(x, y)`)
- **Far tiles:** merged per page into one mesh, decimated at stride 2, with an albedo bake per page (`<outDir>/FarAlbedo_<px>_<pz>.htex`) using the `.tlod` render approach at 512². The material is a `MaterialInstance` of `Models/AlbedoNormal_Opaque_Base.hmat`, set up the way `far_terrain.cpp:300` does.
- **Seams:** where a near tile borders a far one, the far mesh uses full-resolution vertices along the shared edge, so the edges match exactly.

- [ ] **Step 1: Spike first, before building the rest.**
  - Write one near tile as a mesh with its material and splat texture.
  - Load it in mmo_edit, or in a small throwaway client path, next to the live tile, and compare: the same look, and no shader or vertex-layout error in the log.
  - If the terrain material cannot render a regular mesh's vertex layout, stop and report NEEDS_CONTEXT, with the vertex format the terrain shader expects. The controller then decides between a small `BakedTerrainTile` movable at runtime and a matching vertex layout.
- [ ] **Step 2: Write a failing test** in `game_common_tests` for a pure helper you factor out. For example `std::vector<Vector3> GlueTerrainEdgeVertices(...)` asserts that the near and far edge vertex positions are identical on a shared border (Review Focus 4).
- [ ] **Step 3: Implement** `BakeGlueTerrain`.
- [ ] **Step 4: Run the test and a dry bake** that logs the tile counts (near and far), texture memory and triangle count.
- [ ] **Step 5: Commit** with the message "feat(edit): glue terrain bake, near tiles keep the layered material".

### Task 5: Bake command, manifest, editor entry, and the Haven bake

**Files:**
- Create: `src/mmo_edit/glue_bake/glue_scene_baker.{h,cpp}`. This orchestrates Tasks 2–4 and writes the manifest.
- Modify: `src/mmo_edit/mmo_edit.cpp` (the `--bake-glue-scene <GlueScene.json>` job) and the world editor's menu (a "Bake Glue Scene" entry that runs the same function with the open world).
- Output: `data/client/Interface/GlueUI/GlueBake/*` (committed in data/client)

**Interfaces:**
- Consumes Tasks 1–4.
- Produces `bool BakeGlueScene(const String& glueSceneJsonPath, String& outSummary);`. It:
  - empties `GlueBake/` first, but only files it owns: those listed in the old manifest plus `GlueBake.json`
  - writes the meshes, terrain and manifest (sceneHash = the hash of the GlueScene.json bytes, inputs = the collector's inputs)
  - prints a summary: triangle count, mesh and material counts, texture memory, skipped interiors and small objects, unmerged meshes, light, sound and particle counts, and total output size in MB

- [ ] **Step 1: Implement and run** `bin/Debug/mmo_edit.exe --bake-glue-scene data/client/Interface/GlueUI/GlueScene.json`. Expected: exit code 0, and the summary in the log.
- [ ] **Step 2: Check the output size.**
  - If the total exceeds 40 MB, reduce the far albedo resolution or increase the small-object threshold and rebake.
  - Report the final numbers.
- [ ] **Step 3: Commit** the code ("feat(edit): --bake-glue-scene") and the bake output (data/client: "Glue bake of the Haven gate and plaza", followed by a gitlink bump).

### Task 6: Runtime loader in GlueScene

**Files:**
- Modify: `src/mmo_client/glue/glue_scene.{h,cpp}`
- Test: extend `test_glue_bake.cpp` only for pure helpers (e.g. a `ShouldUseGlueBake(...)` decision).

**Interfaces:**
- Consumes `ParseGlueBakeManifest`, `HashGlueBakeBytes` and the manifest structs (Task 1).
- Produces `bool ShouldUseGlueBake(const std::optional<GlueBakeManifest>& manifest, const String& currentSceneHash);`. It returns true only for a parsed manifest whose `sceneHash` matches.
- **Loading:**
  - On construction, read `Interface/GlueUI/GlueBake/GlueBake.json` through the `AssetRegistry`.
  - If `ShouldUseGlueBake` is true:
    - create entities under the world root for `meshes`, `foliage` and `terrainTiles`
    - give terrain tiles a `MaterialInstance` of their material with the "Splatting" texture parameter, when `splatting` is set
    - use rendering distance 0, meaning unlimited
    - create point lights, sound emitters (via `AmbientSoundEmitters`) and particle systems
  - Skip `ClientWorldInstance`, page loading and the loader thread entirely.
  - The gate and plaza are ready once all entities exist (loaded synchronously in `Update` over at most a few frames).
  - Log "glue bake loaded in N ms (meshes M, tris T)".
  - **Otherwise:** WLOG the reason (missing, unparsable, or scene hash mismatch) and keep today's streaming path unchanged.

- [ ] **Step 1: Write failing tests:** `ShouldUseGlueBake(nullopt, h) == false`, a mismatching hash gives false, and a matching hash gives true.
- [ ] **Step 2: Implement.** Build `mmo_client` and `game_common_tests`, and run the tests.
- [ ] **Step 3: Client check with launch options only** (see Global Constraints):
  - Launch with the bake present and close it with WM_CLOSE. Read `Client.log` and confirm the bake-loaded line and the gate-ready time, which must be < 1000 ms.
  - Screenshot the client window's own rect at the gate view, mid-flight (`glueCameraPath` cvar) and the plaza.
  - Then temporarily rename `GlueBake.json`, confirm the fallback WLOG and streaming, and restore it.
- [ ] **Step 4: Commit** with the message "feat(client): load the glue bake instead of streaming the world".

### Task 7: Staleness warning in the gate and the weekly audit

**Files:**
- Create: `tools/glue_bake_check.py` (gate check, not asset tooling)
- Create: `tools/tests/test_glue_bake_check.py`
- Modify:
  - `tools/gate/verify.ps1`: a non-failing step that prints the checker's output as WARNING lines
  - the weekly content audit script that writes to `tools/gate/reports/` (find it, e.g. `grep -rl "content audit" tools`): add the same check
  - `src/mmo_edit/editors/world_editor/world_editor_instance.cpp` save path: after saving a world listed in `GlueScene.json`'s `world`, log a reminder: "Glue bake may be stale; rerun mmo_edit --bake-glue-scene data/client/Interface/GlueUI/GlueScene.json"

**Interfaces:**
- `python tools/glue_bake_check.py [--root <repo>]`:
  - always exits 0
  - prints nothing when the bake is fresh
  - otherwise prints "WARNING: glue bake stale: <n> input(s) changed (<first 5 paths>) — rerun: mmo_edit --bake-glue-scene data/client/Interface/GlueUI/GlueScene.json"
  - also warns when the sceneHash mismatches GlueScene.json
- The FNV-1a 64 must match `HashGlueBakeBytes`. The test checks "abc" → `e71fa2190541574b`.

- [ ] **Step 1: Write failing tests:**
  - a fresh temp tree gives no output
  - a modified input gives one warning line naming it (Review Focus 5)
  - a missing input warns
  - a changed GlueScene.json warns
  - the hash of "abc" is correct
- [ ] **Step 2: Implement** the checker, the gate step, the audit hook and the editor reminder.
- [ ] **Step 3: Run the tests.** Run `python -m pytest tools/tests/test_glue_bake_check.py -q`. Expected: pass. The gate's tool-tests step picks the file up.
- [ ] **Step 4: Commit** with the message "feat(gate): warn when the glue bake is stale".

### Task 8: Verification and gate

- [ ] **Step 1: Fast gate.** Run `MMO_GATE_PYTHON=<python with protobuf> powershell -File tools/gate/verify.ps1 -Tier fast`. Expected: GREEN, with no glue staleness warning right after a bake.
- [ ] **Step 2: Show the user** the before and after gate-ready times and the three screenshots, and get their approval of the look.
- [ ] **Step 3: `/ship`** only on the user's word. Pushing also needs the user's word, with data master pushed first (memory `submodule-pointer-landing`).
