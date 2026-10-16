# World-Model Prop Kits Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let every placed world model (building) carry an ordered list of separate prop-kit assets (props + lights in the building's local space) so copies of one building can be furnished differently.

**Architecture:**
- **Data and loading.** A new `.hkit` chunked asset (`PropKit` + serializer + `PropKitManager` in `scene_graph`). A world-model placement (`.wobj`) gets an optional `WKIT` chunk that lists kit paths. `WorldEntityLoader` reads it into `MapEntity::kitPaths`; old readers skip it.
- **Rendering.** `WorldModelInstance` realizes kit props through the existing doodad pipeline (room derivation, batches, collision proxies) and kit lights through the light pipeline.
- **Navmesh.** `nav_build` adds kit props as extra collision instances per placement.
- **Editing.** An unattended `mmo_edit --apply-prop-kit` job comes first, so agents can furnish buildings. Then a kit editor and the world-editor kit list / in-place furnishing.

**Tech Stack:** C++17, Catch2 (`mmo_add_test`), nlohmann::json (mmo_edit only), ImGui (mmo_edit), Python 3.14 worldkit (tools/world).

**Spec:** `docs/superpowers/specs/2026-10-16-world-model-prop-kits-design.md`

## Global Constraints

- Code style (CLAUDE.md):
  - Allman braces; braces on every `if`; tabs.
  - `m_camelCase` members; `PascalCase` methods; `snake_case` files.
  - `#pragma once`, Doxygen on public members.
  - Header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
  - No exceptions (`ASSERT`/`VERIFY`/`ELOG`/`WLOG`).
- Placement files stay **version 3**. Kits go in an **optional chunk** after `WWMO`. A file without the chunk means "no kits", and old readers must still load the building shell.
- Kit props block **player movement and the navmesh**, but **not server line of sight**: `server_collision_map` is not touched.
- Every prop belongs to exactly one kit. There are **no per-placement overrides**. No kit means an **empty interior**: no default kit.
- Kits do not nest kits or world models. They have no particles or sounds in this version.
- Engine/content tooling is C++ in `mmo_edit`, with an unattended `--flag`. Python stays scratch-only, except for the existing worldkit readers, which must understand the new chunk.
- No network protocol change.
- Do not run `dress.py publish-nav` or rebuild the real navmesh unless the user asks.
- **Dependency:**
  - Tasks 3, 5 and later need `feature/haven-town-wmo` (child world-model instances) merged into `develop` and merged into this branch first. It edits `world_model_instance.{h,cpp}` and `nav_build/map.{h,cpp}`.
  - Tasks 1–2 can start before that.

## Review Focus

1. **A kit path that is missing or fails to load.** The building shell and the other kits still appear, and one warning names the placement and the kit. Covered by Task 4 and Task 6 tests.
2. **The same kit on many placements.** It is loaded once (`PropKitManager` cache), and each placement realizes it independently with its own transform. Covered by Task 1 and Task 3 tests.
3. **A kit prop outside every room of the building.** For example, a lantern on the porch: it stays always visible and is never portal-culled away. Covered by Task 3 test.
4. **An old reader meeting a new placement file.** Simulated by a loader without the kit handler: the WMO entity still loads, with an empty kit list. Covered by Task 2 test.
5. **A colliding kit prop within 0.6 m of a portal.** The unattended job reports it by prop name and still writes the kit. Covered by Task 7 test.

---

### Task 1: PropKit asset, serializer and manager

**Files:**
- Create: `src/shared/scene_graph/prop_kit.h`, `src/shared/scene_graph/prop_kit_serializer.h`, `src/shared/scene_graph/prop_kit_serializer.cpp`, `src/shared/scene_graph/prop_kit_manager.h`, `src/shared/scene_graph/prop_kit_manager.cpp`
- Test: `src/tests/scene_graph_tests/test_prop_kit_format.cpp`

**Interfaces:**
- Produces:
  - `struct PropKit { String name; String previewWorldModel; std::vector<WorldModelMeshRef> props; std::vector<WorldModelLight> lights; };` and `typedef std::shared_ptr<PropKit> PropKitPtr;` in `prop_kit.h`.
  - `class PropKitSerializer { public: void Serialize(const PropKit& kit, io::Writer& writer); };`
  - `class PropKitDeserializer final : public ChunkReader { public: explicit PropKitDeserializer(PropKit& kit); };`
  - `class PropKitManager final : public NonCopyable { public: static PropKitManager& Get(); PropKitPtr Load(const String& path); PropKitPtr Find(const String& path) const; void Remove(const String& path); void Clear(); };`
  - `PropKitManager` copies `WorldModelManager`. Its cache is `std::map<String, PropKitPtr, StrCaseIComp>`, and it caches only on success.
- **Format:**
  - Chunks are written with `ChunkWriter`. Magics:
    - `MakeChunkMagic('KVER')`: u32 version 1, required, read first.
    - `'KNAM'`: str16 name.
    - `'KPRV'`: str16 preview world model.
    - `'KPRP'`: u32 count, then per prop: str16 mesh, str16 name, str16 material override, 3f position, 4f rotation (w,x,y,z), 3f scale, u8 visible.
    - `'KLIT'`: u32 count, then per light the same 60-byte layout as `MOLT` version 2.1 in `world_model_serializer.cpp:195-232`.
  - Unknown chunks are ignored.

- [ ] **Step 1: Write the failing tests** in `test_prop_kit_format.cpp`, tag `[prop_kit]`, using the in-memory `io::VectorSink` / `io::MemorySource` pattern from `test_world_model_light_format.cpp`:
  - `"Prop kit round-trips name, preview, props and lights"`:
    - A kit with name `"Small House - Default"`, preview `"Models/FalwynPlains/Buildings/HouseTest.hwmo"` and 2 props: `SM_hc_Bed_A` at (-4.69, 1, 5.69) scale (1,1,1) with material override `"Textures/FalwynPlains/Props/Bed_Wood.hmi"`, and a second prop with `visible=false`.
    - 1 omni light: colour `0xffce8115`, intensity 2.2, `attenuationEnd` 11, `fogScattering` 0.5.
    - Every field must compare equal after reading it back.
  - `"Prop kit reader skips unknown chunks"`: a `'KZZZ'` chunk between `KNAM` and `KPRP` is skipped, and the props still read.
  - `"Prop kit without a version chunk fails to load"`: `Read` returns false.
  - `"Prop kit manager returns the cached kit for the same path"`: a kit created with a manual-insert helper (`PropKitManager::Get().Insert(path, kit)`, exposed for tests and the editor) is returned by `Find` and by a second `Load`, and both pointers are equal.
- [ ] **Step 2: Run them and see them fail.** Run: `cmake --build build --config Debug -t scene_graph_tests` and expect a compile failure, because the headers do not exist yet.
- [ ] **Step 3: Implement** the three headers and two cpp files. `Insert(const String& path, PropKitPtr kit)` belongs on `PropKitManager` with the interfaces above. The files are picked up by the `scene_graph` library glob.
- [ ] **Step 4: Run** `bin\Debug\scene_graph_tests.exe "[prop_kit]"` and expect all passed.
- [ ] **Step 5: Commit** with `feat(scene_graph): prop kit asset, serializer and manager`.

### Task 2: Kit list on world-model placements

**Files:**
- Modify: `src/shared/game_common/world_entity_loader.h`, `src/shared/game_common/world_entity_loader.cpp`
- Modify: `tools/world/worldkit/formats/wobj.py`
- Test: `src/tests/game_common_tests/world_entity_loader_tests.cpp` (new), `tools/tests/test_world_formats.py`

**Interfaces:**
- Produces:
  - `MapEntity::kitPaths` (`std::vector<String>`).
  - `void WriteWorldModelKitChunk(io::Writer& writer, const std::vector<String>& kitPaths);` as a free function in `world_entity_loader.h`. It writes nothing when the list is empty. Chunk `MakeChunkMagic('WKIT')` holds a u8 count, then str16 paths.
  - Python: `WorldEntity.kits: tuple[str, ...]` (default `()`), and `wobj_bytes(..., kits=())`.
- **Reader:**
  - `OnWorldModelChunk` registers an optional `WKIT` handler after it removes the entity handlers.
  - A `WKIT` chunk after a `WMSH` entity is ignored, through `m_ignoreUnhandledChunks`.

- [ ] **Step 1: Write the failing tests** in `world_entity_loader_tests.cpp`, tag `[game_common][world_entity]`. Build the files with `ChunkWriter` exactly as `WorldEditorInstance::Save` does (`world_editor_instance.cpp:1072-1105`):
  - `"World model placement without a kit chunk has no kits"`: `kitPaths` is empty, and the asset and position read correctly.
  - `"World model placement reads its kit list in order"`: the paths `"Kits/Inn_Base.hkit"` and `"Kits/Harvest.hkit"` come back in that order.
  - `"A reader without the kit handler still loads the building"`: run a plain `ChunkReader` subclass with ignore on over the same bytes. This proves the unknown chunk is skipped.
  - `"Mesh placement ignores a kit chunk"`.
- [ ] **Step 2: Run them and see them fail.** Run: `cmake --build build --config Debug -t game_common_tests`. It fails because `kitPaths` and `WriteWorldModelKitChunk` are missing.
- [ ] **Step 3: Implement the C++ side.** The `.wobj` version stays 3.
- [ ] **Step 4: Write the failing Python tests** in `test_world_formats.py`:
  - `test_wmo_kit_chunk`: `wobj_bytes(kind="wmo", ..., kits=("Kits/A.hkit",))` parses with `kits == ("Kits/A.hkit",)`.
  - `test_wmo_without_kits`: `kits == ()`.
  - Keep `test_every_wobj_parses` green.
- [ ] **Step 5: Implement the Python side.** `parse_wobj` accepts a third chunk `b"TIKW"` (the on-disk form of `'WKIT'`) and rejects any other third chunk. Run `py -3.14 -m unittest discover -s tools/tests -p "test_world_*.py"` and expect OK.
- [ ] **Step 6: Run** `bin\Debug\game_common_tests.exe "[world_entity]"` and expect all passed.
- [ ] **Step 7: Commit** with `feat(game_common): optional prop kit list on world model placements`.

### Task 3: Realize kits in WorldModelInstance

**Precondition:** `feature/haven-town-wmo` merged here.

**Files:**
- Modify: `src/shared/scene_graph/world_model_instance.h`, `src/shared/scene_graph/world_model_instance.cpp`
- Test: `src/tests/scene_graph_tests/test_world_model_prop_kits.cpp`

**Interfaces:**
- Consumes: `PropKitPtr` (Task 1).
- Produces:
  - `void SetPropKits(std::vector<PropKitPtr> kits);` If geometry already exists it clears the kit content and rebuilds it, like `SetActiveDoodadSet`. Before attach it only stores the kits.
  - `[[nodiscard]] size_t GetKitPropInstanceCount() const;` counts unbatched entities plus batched instances.
  - `[[nodiscard]] size_t GetKitLightCount() const;`
  - `[[nodiscard]] int32 GetKitPropGroup(size_t index) const;` gives the derived room, or -1.
- **Implementation:**
  - Extract `CreateDoodads`' realization (everything after gathering placements) into a private `void RealizePlacements(Scene& scene, const std::vector<WorldModelPlacementInput>& placements, std::vector<DoodadInstance>& instances, std::vector<BatchEntry>& batches, std::vector<CollisionEntry>& proxies);`. It takes the transform and `materialOverride` from the placement itself. This fixes the `doodads[placement.sourceIndex]` read and the dropped material override in the unbatched path (`world_model_instance.cpp:1222-1240`).
  - Extract the per-light body of `CreateLights` into `void CreateLightFrom(Scene& scene, const WorldModelLight& light, std::vector<LightInstance>& target);`.
  - Add kit members:
    - `m_kits`
    - `m_kitInstances`
    - `m_kitBatches`
    - `m_kitCollisionProxies`
    - `m_kitLightInstances`
  - Add `CreateKitContent` and `ClearKitContent`. Realize kit content after the doodads and before the child instances.
  - Add the kit lists everywhere the doodad lists appear:
    - `UpdatePortalCulling`
    - `RefreshBatchTransforms` / `RefreshCollisionProxies`
    - the empty check at ~cpp:759
    - `ClearBatches`
    - the destructor and `Destroy`

- [ ] **Step 1: Write the failing tests**, tag `[world_model][prop_kit]`. Set up like `test_world_model_nesting.cpp` (`EnsureNullDevice()`, `OctreeScene`, `WorldModelManager::Get().CreateManual`):
  - `"Kit props and lights are created when the instance attaches"`: one group with a room box; the kit has 2 props using a mesh created with `MeshManager::Get().CreateManual`, and 1 light. After `AttachObject` the counts are 2 and 1.
  - `"Changing kits after attach rebuilds only the kit content"`: going from 2 props to an empty kit list gives 0, and the shell's mesh-ref count is unchanged.
  - `"Two instances share one kit"`: the same `PropKitPtr` on two instances gives 2 props each.
  - `"A kit prop outside every room is never culled"`: one prop inside the room box gives `GetKitPropGroup(0) == 0`, and one outside gives `GetKitPropGroup(1) == -1`.
  - `"Unbatched kit props use their own transform and material override"`: with batching off, the entity's node position equals the prop position, and its submesh material name equals the override.
- [ ] **Step 2: Run them and see them fail** by building `scene_graph_tests`.
- [ ] **Step 3: Implement.** Existing doodad tests (`[world_model]`) must stay green.
- [ ] **Step 4: Run** `bin\Debug\scene_graph_tests.exe "[world_model]"` and expect all passed.
- [ ] **Step 5: Commit** with `feat(scene_graph): world model instances realize prop kits`.

### Task 4: Client loads placement kits

**Files:**
- Modify: `src/mmo_client/world_deserializer.h:65`, `src/mmo_client/world_deserializer.cpp:223-330`

**Interfaces:**
- Consumes: `MapEntity::kitPaths` (Task 2), `PropKitManager` (Task 1), `WorldModelInstance::SetPropKits` (Task 3).
- **Change:**
  - `CreateWorldModelEntity` gains a parameter `const std::vector<String>& kitPaths`.
  - It loads each path with `PropKitManager::Get().Load`. A kit that fails gets one `WLOG("Placement <uniqueId>: prop kit <path> failed to load")` and is skipped.
  - It calls `SetPropKits` **before** `AttachObject`.
  - `InternalLoadPageEntity` passes `entity.kitPaths`.

- [ ] **Step 1: Implement and build** `mmo_client` (Debug). Expect no warnings in the touched files.
- [ ] **Step 2: Manual check, deferred to Task 8.** There is no client unit-test harness for world loading. The failure path is covered by `PropKitManager::Load` returning null (Task 1) plus this guard.
- [ ] **Step 3: Commit** with `feat(client): load prop kits for world model placements`.

### Task 5: Kit props in the navmesh

**Precondition:** the Haven merge, which adds `WorldModelEntity::AppendWorldModel`.

**Files:**
- Modify: `src/shared/nav_build/map.h`, `src/shared/nav_build/map.cpp` (placement loop ~:481-555)
- Test: `src/tests/nav_build_tests/prop_kit_collision_tests.cpp`

**Interfaces:**
- Produces:
  - `void AppendPropKitTriangles(const PropKit& kit, const Matrix4& placementTransform, const std::function<const AABBTree*(const String& meshPath)>& collisionOf, std::vector<Vector3>& vertices, std::vector<int32>& indices);` as a free function in `nav_build/map.h`.
  - It skips invisible props and props whose mesh has no collision tree. Vertices go through `placementTransform * propTransform`.
- **Map integration:**
  - The cached `WorldModelEntity` stays per path, **never** baked with kits.
  - For a WMO placement with `kitPaths`, load each kit (`PropKitDeserializer` via `AssetRegistry::OpenFile`, the same lightweight style as the mesh loader at :166-178).
  - Insert the triangles as an extra `MapEntityInstance`-style instance keyed by the placement id, using `InsertMapEntityInstance`.

- [ ] **Step 1: Write the failing tests**, tag `[nav_build][prop_kit]`, with a fake `collisionOf` returning a unit-cube `AABBTree`:
  - `"Kit props add their collision triangles at the placement transform"`: a kit with one prop at (2,0,0) under a placement at (100,0,0). Every vertex x is in [101.5, 102.5].
  - `"Invisible and collision-less kit props add nothing"`.
- [ ] **Step 2: Run them and see them fail** by building `nav_build_tests`.
- [ ] **Step 3: Implement** the function and the Map integration.
- [ ] **Step 4: Run** `bin\Debug\nav_build_tests.exe "[prop_kit]"` and expect all passed.
- [ ] **Step 5: Commit** with `feat(nav_build): prop kit props feed the navmesh`.

### Task 6: World editor kit list per placement

**Files:**
- Modify:
  - `src/mmo_edit/editors/world_editor/world_editor_instance.h:87` (editor `MapEntity`: `std::vector<String> m_kitPaths` with `GetKitPaths()`/`SetKitPaths()`, which marks the entity modified)
  - `world_editor_instance.cpp`: `LoadPageEntities` :2445 applies the kits; `Save` writes `WriteWorldModelKitChunk` after :1105; `Visit(SelectedMapEntity&)` WMO branch :2976 gets the kit-list UI

**Interfaces:**
- Consumes: Tasks 1–3.
- **UI**, under "Prop Kits" in the WMO details:
  - An ordered list showing each kit's name, or its path when the kit fails to load.
  - An add button through the existing asset picker widget, filtered to `.hkit`.
  - Remove, plus move up/down.
  - Any change calls `SetKitPaths` and then `GetWorldModelInstance()->SetPropKits(...)`.

- [ ] **Step 1: Implement and build** `mmo_edit`.
- [ ] **Step 2: Manual check:**
  - Open the Development world and select the inn placement.
  - Add a kit, save, and reopen. The kit list persists and the props show.
  - Remove the kit and save. The props are gone, and the `.wobj` has no `WKIT` chunk (check with `py -3.14 -c "from worldkit.formats.wobj import parse_wobj; ..."`).
- [ ] **Step 3: Commit** with `feat(mmo_edit): prop kit list on world model placements`.

### Task 7: Unattended `--apply-prop-kit` job

**Files:**
- Create: `src/mmo_edit/prop_kit_job.h`, `src/mmo_edit/prop_kit_job.cpp`
- Create: `src/shared/scene_graph/prop_kit_checks.h`, `src/shared/scene_graph/prop_kit_checks.cpp`
- Modify: `src/mmo_edit/mmo_edit.cpp` (flag block ~:343-510, plus the comment block above it)
- Test: `src/tests/scene_graph_tests/test_prop_kit_checks.cpp`

**Interfaces:**
- Produces:
  - `struct PortalClearanceIssue { String propName; int32 portalIndex; float distance; };`
  - `std::vector<PortalClearanceIssue> FindPropsNearPortals(const WorldModel& model, const PropKit& kit, const std::function<std::optional<AABB>(const String& meshPath)>& collisionBoundsOf, float margin = 0.6f);`
    - Uses each prop's world-model-space collision AABB against each portal's quad (`Portal::GetWorldVertices()`).
    - The distance is AABB to portal polygon, approximated by the portal's AABB inflated by `margin`.
  - `bool ApplyPropKitJob(const String& jsonPath);` in `prop_kit_job.h`.
- **Job JSON:**
  ```json
  {"kit": "Kits/Oakenshire_Inn.hkit", "name": "Oakenshire Inn", "preview": "Models/.../Oakenshire_Inn_001.hwmo",
   "props": [{"mesh": "...hmsh", "name": "...", "position": [x,y,z], "yaw": deg, "rotation": [w,x,y,z], "scale": s|[x,y,z], "material": "", "visible": true}],
   "lights": [{"type": "omni", "color": "ffce8115", "position": [x,y,z], "intensity": 2.2, "range": 11.0, "fogScattering": 1.0}],
   "assign": [{"world": "Development", "uniqueId": "8290733669967064449", "kits": ["Kits/..."]}],
   "removeMeshRefs": {"worldModel": "...hwmo", "names": ["SM_hc_Bed_A"], "removeLights": false}}
  ```
  - `yaw` and `rotation` are alternatives.
  - `assign` rewrites the placement's `.wobj` through the same writer code as `WorldEditorInstance::Save`. Factor that writer into `WriteWorldModelPlacement(io::Writer&, const MapEntity&)` in `world_entity_loader.h` so both use it.
  - `removeMeshRefs` deletes the named mesh refs from the world model and saves it with `WorldModelSerializer`. When `removeLights` is true, it also clears all of the model's lights. It is used for the migration in Task 8.
  - The job logs every `PortalClearanceIssue` as `WLOG` and still writes. It exits 1 only on load or write failures.
  - The log file is `prop_kit_job.log`, following the `--set-submesh-material` pattern.

- [ ] **Step 1: Write the failing tests** for the checks, tag `[prop_kit]`:
  - `"A prop in a doorway is reported"`: a 1 m box prop centred in a 2 m-wide portal is reported with portal index 0.
  - `"A prop 1 m away from every portal is not reported"`.
  - `"Props without collision are ignored"`: `collisionBoundsOf` returns `std::nullopt`.
- [ ] **Step 2: Run them and see them fail**, then implement `prop_kit_checks`. Run `bin\Debug\scene_graph_tests.exe "[prop_kit]"` and expect all passed.
- [ ] **Step 3: Implement the job and the flag.** Build `mmo_edit`.
- [ ] **Step 4: Check the job** with a scratch JSON that creates `Kits/Test.hkit` with one prop, assigned to nothing. Run `bin\Debug\mmo_edit.exe --apply-prop-kit <json>` and expect exit 0. Then load the kit back in a quick scene_graph test or the editor. Delete the scratch kit afterwards.
- [ ] **Step 5: Commit** with `feat(mmo_edit): --apply-prop-kit job with doorway clearance warnings`.

### Task 8: Migrate the small house and furnish Oakenshire (content)

**Files:**
- Data, uncommitted for the user:
  - `data/client/Kits/*.hkit`
  - `Models/FalwynPlains/Buildings/HouseTest.hwmo`
  - the five Oakenshire placements' `.wobj`
- Commit: the job JSONs under `data/world/kits/` (tooling-side inputs, like dressing manifests)

- [ ] **Step 1: Small house default kit.**
  - Job `small_house_default.json` copies HouseTest's 7 furniture mesh refs (transforms from the `.hwmo`) and its candle light into `Kits/SmallHouse_Default.hkit`.
  - It assigns that kit to the house placement `3134932674000232684` and removes the refs from `HouseTest.hwmo`.
  - Remove the model's candle light from `HouseTest.hwmo` too, with `"removeLights": true` in the job's `removeMeshRefs`.
  - Verify by rendering the house before and after. They must look identical.
- [ ] **Step 2: Two more house interiors** for the other placements, sized to the house plan (single room x -8..2, z -8..8, door on +x, floor y 1.0):
  - `Kits/SmallHouse_Farmhouse.hkit` for `8290733669967064449`: Farmer Haldor's corner, with haystack sacks, a workbench, barrels and a bed.
  - `Kits/SmallHouse_Healer.hkit` for `1248239675558434552`: Healer Mirenna, with a writing table, bookshelf, vases, a cauldron, candles and a bed. Keep her spot at local (-1.6, 1.0, 2.5) and the object spawn at local (-0.3, 1.0, 3.5) clear.
  - Every kit keeps a walkway of 1.2 m or more from the door to the room centre, and gets 1–2 lights (candle or wall lamp).
- [ ] **Step 3: Inn kit** `Kits/Oakenshire_Inn.hkit`, floors y 2.2 (ground), 6.6 (upper) and -1.6 (cellar):
  - **Ground floor:** the bar counter along the west partition (x -8, z -2..4), 3–4 dinner tables with chairs and benches in the hall (x -6..10, z -10..6), wine racks, a chandelier, wall lamps. Old Harbin's spot (-8.9, 2.6, 0.1) stays clear. Keep the fireplace (x -2..2, z 7..10), the entry (x 10..12, z -3..2) and the stairwell (x -12..-6, z -11..-2) clear.
  - **Upper floor:** beds, bedside tables, wardrobes.
  - **Cellar:** barrels, crates and wine casks along the walls only. The six cellar rat spawns must stay reachable.
  - **Lights:** keep the model's 2 lights in the model, and add the kit's lights (chandelier, wall lamps, cellar lantern).
- [ ] **Step 4: Town Hall kit** `Kits/Oakenshire_TownHall.hkit`, ground y 2.5 / 2.9, upper y 7.1:
  - Benches and a throne / writing table for the hall, bookcases, banners where the assets exist.
  - Keep the trainers' spots clear: Clarissa (-9.1, 2.9, 5.2), Elira (4.2, 2.9, 5.1), Baldric (-7.7, 2.9, 11.5), Milly (12.6, 2.4, -5.8), Edda (10.4, 2.4, -6.0), Vivienne (-5.3, 7.1, -14.2), Malrik (3.0, 7.1, -6.8).
  - Keep the vestibule path (x 5..15, z -14..-5) and the stair hole (x -8..-3, z -7..-5) clear.
- [ ] **Step 5: Check.**
  - Every job reports no portal warnings, or each warning is reviewed and accepted.
  - Run `dress.py check --nav` with a scratch nav build. The protected routes stay walkable, and the inn cellar and town hall upper floor are reachable from the door.
- [ ] **Step 6: Commit** the job JSONs with `content(oakenshire): prop kits for the inn, town hall and houses`.

### Task 9: Kit editor

**Files:**
- Create: `src/mmo_edit/editors/prop_kit_editor/prop_kit_editor.{h,cpp}`, `prop_kit_editor_instance.{h,cpp}`
- Modify: `src/mmo_edit/mmo_edit.cpp:318` (register the editor)
- Modify: `src/mmo_edit/editors/world_model_editor/panels/world_model_properties_panel.{h,cpp}`. Split `DrawMeshRefProperties` and `DrawLightProperties` into overloads that take `WorldModelMeshRef&` and `WorldModelLight&`, so both editors use them.

**Interfaces:**
- `PropKitEditor : EditorBase`:
  - `CanLoadAsset(".hkit")`.
  - "Create New Prop Kit" context menu. It writes a valid empty kit through `PropKitSerializer`. **Do not** copy `WorldModelEditor::CreateNewWorldModelObject`'s stub writer, which uses the wrong magics.
- `PropKitEditorInstance`:
  - A viewport showing the preview world model (picked with the asset picker, stored as `previewWorldModel`) plus the kit props and lights.
  - A prop list, and drag & drop of `.hmsh` from the asset browser.
  - Gizmo transform, lights add/remove/edit, and a "Show collision" toggle using the existing collision debug display.
  - `Save()` serializes the kit, then refreshes `PropKitManager`: `Remove` then `Insert`, so open worlds pick up the change on reload.

- [ ] **Step 1: Implement and build** `mmo_edit`.
- [ ] **Step 2: Manual check.**
  - Create a kit and set the preview to `HouseTest.hwmo`.
  - Drag `SM_hc_Chair_A` in, move it, add a light, save, and reopen. The state persists.
  - Check that the world model editor's properties panel still edits mesh refs and lights.
- [ ] **Step 3: Commit** with `feat(mmo_edit): prop kit editor`.

### Task 10: In-place furnishing in the world editor

**Files:**
- Modify: `world_editor_instance.{h,cpp}` (WMO details, selection, Save)
- Create: `src/mmo_edit/editors/world_editor/selected_kit_prop.{h,cpp}` (a `Selectable` for a kit prop, modelled on `SelectedMapEntity`)

**Interfaces and behaviour** (spec, "Editing kits in place"):
- **Active kit.** The details panel gets an "Active kit" combo over the placement's kit list. Dropped meshes (asset browser → viewport) on a selected building go into the active kit, in building-local space.
- **Selecting and editing.** Clicking a kit prop selects it as `SelectedKitProp{placementId, kitPath, propIndex}`. The gizmo edits that kit, and the kit is marked dirty.
- **World save.**
  - Writes every dirty kit.
  - Before writing a kit used by other placements, it shows a modal: "<kit> is used by N other buildings. Save anyway?" N counts the loaded placements whose kit list contains the path.
- **"Save as new kit…"** writes the active kit's current content to a new path. It replaces the active kit in this placement's list and leaves the old file unchanged.
- **Refresh.** After any kit edit, every loaded placement using that kit calls `SetPropKits` again.

- [ ] **Step 1: Implement and build** `mmo_edit`.
- [ ] **Step 2: Manual check.**
  - In two houses sharing one kit, move a chair in one. Both update.
  - Saving shows the "used by 1 other building" prompt.
  - "Save as new kit" on one house leaves the other unchanged after reload.
- [ ] **Step 3: Commit** with `feat(mmo_edit): furnish world model placements in place`.

### Task 11: Docs and gate

**Files:**
- Create: `docs/world-model-prop-kits.md`, covering the format, authoring paths, the job JSON, and the navmesh / line-of-sight rules.
- Modify: `.agents/skills/world-dresser/SKILL.md`. Add a short section: interiors are furnished with prop kits via `mmo_edit --apply-prop-kit`, not with `.wobj` props.

- [ ] **Step 1: Write the docs.**
- [ ] **Step 2: Run the gate:** `powershell -File tools/gate/verify.ps1 -Tier fast` must be green, including the WSL Linux step. `nav_build` and `game_common` build on Linux.
- [ ] **Step 3: Commit** with `docs: world model prop kits`.
