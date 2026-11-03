# Task 11 report: test oasis in the Sands of Korash, quality comparison

Status: DONE. Code, data and captures committed locally (nothing pushed). All 40 CTest suites green.

## Commits

Main repo, branch `feature/pcg-oasis-poc` (on top of 31cb1264):

| Commit | What |
|---|---|
| d0be5a12 | fix(editor): refuse oasis rebuilds when the recipe failed to load (R12) |
| d12301c2 | feat(editor): `--pcg-new-oasis` unattended flag (R11) |
| 9d06d0b1 | feat(editor): `--pcg-delete` unattended flag |
| ff25991e | fix(editor): PCG delete clears the water material it gave a dry page |
| c09bde7c | fix(pcg): carve the oasis pool into a dry basin floor (rule fix + tests) |
| ef1bb780 | feat(pcg): per-family sink for oasis rim rocks (rule fix + tests) |
| 8b2c65b0 | chore(data): gitlink data/client -> 0c546613 + captures |

data/client submodule, new branch `feature/pcg-oasis-poc` (from detached 16160a5b, not master):
0c546613 "PCG oasis PoC test site" (recipe, .hpcg record, 38_47.tile, 28 rock .wobj in Entities/9775,
Foliage/9775.hfol, Development.hwld).

## Code changes

- **R12** (`procedural_edit_mode.*`): `m_recipeLoaded`; `CheckRecipe()` refuses Rebuild / Rebuild All
  (and therefore `--pcg-rebuild`, exit 1) with status line + ELOG when OasisRecipe.json is missing or
  invalid JSON. Verified: with no recipe, `--pcg-new-oasis` logged the refusal and exited 1, world untouched.
- **R11** `mmo_edit --pcg-new-oasis <world> --at X Z [--radius R] [--seed S] [--name N]` (log
  `pcg_new_oasis.log`). The New Oasis button now calls `ProceduralEditMode::CreateOasisAt(x, z, radius,
  seed, name, ...)`; the flag goes through `WorldEditorInstance::CreateProceduralOasis` = CheckRecipe ->
  CreateOasisAt -> Rebuild -> Save; logs `created oasis <id>`. A refused build saves nothing.
  Note: with Start-Process, quote names containing spaces (`'"Korash PoC Oasis"'`).
- **`--pcg-delete <world> --oasis <hex>`** (log `pcg_delete.log`): `DeleteOasis(id, error)` is now public
  and returns bool; the Delete button and the flag share it.
- **Water material leak on Delete** (`editor_terrain_access.cpp`): a build gives a dry page the recipe's
  water material; Delete removed the quads but kept the material, so the page was saved with an empty
  MCWQ chunk (38_47.tile grew 66 KB after create+delete). Now a page whose last water quad is cleared
  loses its water material. (Trade-off: a page that had an authored material with zero quads before
  the oasis would lose that material on Delete; harmless, noted.)
- **Rule fix: pool carved into a dry floor** (`oasis.cpp`): water level was floor + waterDepth over the
  whole basin, so the land between pool and rim was under the water level: all vegetation there was
  rejected as "under water" (iteration 1 had only a handful of palms on the rim slope), and the pool
  showed the staircase outline of its 4.17 m water quads on flat ground. Now water = floor - 0.6 m,
  ground drops from the pool edge to a bed waterDepth below water over 5 m, quads reach 4 m past the
  edge under the dry shore so the terrain draws a smooth shoreline. `waterDepth` now means pool depth.
  Tests updated (centre values) + new test "pool is carved into a dry basin floor".
- **Rule fix: per-family sink** (`oasis.h/.cpp`, record, recipe, UI): `rockSink` (now cliffs),
  new `shelfSink`, `clumpSink` (appended to the params block; old records read defaults). The Korash
  cliffs are 19-30 m tall with narrow stems, clumps 2-5 m: one sink either floated the cliffs or
  buried the clumps. New test "rim rocks sink by family", record round-trip extended.

## Step 1: layer order and water

- Desert_Terrain.hmat splat order confirmed from its generator (`H:\tmp\desert_import\build_materials.py`
  build_terrain + build_korash_layers.py): R = DesertSand (0), G = DesertDirt (1), B = DesertGrass (2),
  A = Grass (3). Matches the constants; no change.
- Korash pages 37-39/49-51: all 256 tiles use `Models/Desert/Materials/Desert_Terrain.hmat` (copied into
  the recipe as terrainMaterial). Pools above sea level are water type 1 (sea is type 2), page water
  material `Worlds/Water_Ocean.hmat` -> recipe waterMaterial.

## Step 2: site

- Page 38_47 (area 22 on the whole page, no entities/foliage, heights 29.8-39.2, std 1.7 m), centre
  (3466, 8266): ground 34.1, slope 1.1 deg, placeable; 1600 m north of the demo origin, outside pages
  37-39/49-51.
- Final oasis: id **b416827517f55dd8**, "Korash PoC Oasis", centre (3466, 8266), radius 50 (8-point
  Catmull-Rom circle), seed 1. 308 placements (28 rocks as entities, 280 foliage), 38378 terrain samples,
  water level 20.93. Reference demo basin: the green pocket with the pool at about (3750, 9557), water 6.2.

## Step 3: tuning log (recipe `Editor/Pcg/OasisRecipe.json`)

Each iteration = delete previous oasis, `--pcg-new-oasis`, 3-4 captures (top, overview, mid, eye).

1. Defaults + Korash lists, radius 40, basinDepth 8, waterInset 14, rockSpacing 14, cliffs = Layered01-04 +
   Platform01-02. Tidy ring of rocks, flat olive floor, pool = blocky staircase of water quads, only ~8
   palms. Diagnosed the under-water floor -> rule fix c09bde7c.
2. Pool fix, radius 50, basinDepth 10, waterInset 22, dropped Platform (read as floating mushrooms),
   rockSink 2. Smooth shoreline, ring of palms around the pool. Rocks show their tapered stems.
3. rockSink 4, scale 0.8-1.2, spacing 12, bushDensity 0.5, palmDensity 0.5. Eye view from the pool now
   reads as an oasis; cliffs still float on stems, clumps would vanish with more sink -> rule fix ef1bb780.
4. rockSink 8 / shelfSink 3 / clumpSink 0.8, scale 0.9-1.4, spacing 11. Much more continuous plateau wall.
5. rockOffset 4 (rocks on the flat rim, caps overhang the basin), yaw jitter 40, palmNearWaterBoost 1.5.
6. basinDepth 13, rimWidth 16, terraceSteps 3/0.7. Deeper pocket, but the floor ring got too narrow
   (204 placements) and the slope showed bare sand.
7. grassBand 5. Greener slope (auto-cliff rock tint on the steep terrace steps).
8. Final: basinDepth 12, rimWidth 14, waterInset 28 (pool ~44 m across, 14 m floor ring). 308 placements,
   lush palm grove, bushes along the shore.

## Step 4: page corner check

- Temporary oasis 3fccc60387c27ff3 at the corner of pages 38/39 x 47/48 (3733.33, 8533.33), radius 40,
  seed 7: built on all four pages (log: "pages changed 38x47 38x48 39x47 39x48"), 214 placements.
- Captures `seam_corner_top.png` / `seam_corner_close.png` (corner = image centre): no cracks, no
  height steps, no splat discontinuity, one continuous pool across all four pages.
- After `--pcg-delete`: heights, inner heights, splat, areas, holes, water masks/types/heights and tile
  materials of all four pages were identical to before (parsed and compared). The files were not
  byte-identical: MCNM/MCNI (stored normals) differ by +-1 quantization step (14-65 bytes per page)
  around the corner, because the editor recomputes normals of edited vertices and its result differs by
  one LSB from the normals terrain_tool baked. Development.hwld also changes (see concerns).
  To keep the data commit clean I restored Worlds/Development from git and re-created the main oasis
  from scratch (hence the final id b416827517f55dd8); the committed data only contains the main oasis.

## Step 5: determinism (after the data commit 0c546613)

```
run1 exit=0
Rebuilt 'Korash PoC Oasis' in 821 ms: 38378 terrain samples, placements created 0, updated 0, removed 0, kept 308. ...
--pcg-rebuild: done
--- status after run 1:
run2 exit=0
Rebuilt 'Korash PoC Oasis' in 1014 ms: 38378 terrain samples, placements created 0, updated 0, removed 0, kept 308. ...
--pcg-rebuild: done
--- status after run 2:
--- end
```
`git -C data/client status --porcelain` was empty after both runs (not even Development.hwld changed).

## Step 6: captures (`docs/superpowers/specs/pcg-oasis-poc/`, 1280x960, time 0.5 = noon, no fog)

Viewpoints `x,y,z,yaw,pitch,fov`; yaw 0 = looking north. Matched relative to each basin: top and eye
by height above the pool's water, overview and mid by height above the local ground at the camera.

| File | PoC (water 20.9) | Demo (water 6.2) |
|---|---|---|
| `*_top.png` | 3466,330,8266,0,-89.5,45 | 3750,315,9557,0,-89.5,45 |
| `*_overview.png` | 3466,125,8405,0,-38,60 (89 m over ground) | 3750,127,9696,0,-38,60 (89 m) |
| `*_mid.png` | 3466,70,8360,0,-22,60 (35 m over ground) | 3750,70,9651,0,-22,60 (34 m) |
| `*_eye.png` | 3466,24,8280,0,-2,60 (3 m over water) | 3750,9.2,9571,0,-2,60 (3 m) |

Plus `poc_iteration1_overview.png` (before tuning / rule fixes), `seam_corner_top.png`,
`seam_corner_close.png`. Captures rendered correctly (no white/empty images).

## Step 8: tests

`cmake --build build --config Debug -t all_tests` then `ctest -C Debug --output-on-failure`:
**100% tests passed, 0 tests failed out of 40**. (The first build attempt hit a transient
`cl D8040` child-process error in game_client; a rebuild with /m:4 succeeded.) pcg_tests: 45 test cases,
all green.

## Honest assessment: PoC vs demo

Same character, clearly lower authorship. The PoC reads as "sunken green basin with a pool, layered rock
rim, palms clustered at the water" at every distance, and the eye-level view from the pool is close in
quality. What is missing compared with the demo basin:

1. **Context.** The demo pocket sits inside a field of plateau rocks and dunes; the PoC is an isolated
   ring in flat sand. From the overview this is the biggest difference, and it is outside the oasis rules
   (would need a "rock field / dune" rule around it or a site that already has relief).
2. **Shape.** New Oasis makes a circle, so pool, grass ring and rock ring are concentric circles. The demo
   pool is an irregular hourglass with sand banks and inlets. Editing the loop in the GUI helps; the rules
   also need an independent pool shape (noise on the pool edge, a second spline, or several pools).
3. **Rim composition.** Rocks are evenly spaced by arc length with one piece per step; the demo uses fewer,
   huge pieces that overlap into one wall, stacked layers, overhangs, and the exposed tapered undersides of
   the Layered meshes still read as "mushrooms on a ring". Needs per-family spacing/size and clustering.
4. **Water-edge detail.** The demo has rocks and clumps in and at the water, sand beaches, shore-rock
   boulders; the PoC shoreline is a clean grass ring. A "shore rocks" rule is missing.
5. **Vegetation variety.** PoC palms are denser and more uniform (one grove), all at similar distance
   from the water; the demo mixes sparse dry palms, cacti, twig roots and flowers, and varies density.
   PoC grass colour is duller: the terrain Grass layer is tinted by T_Grass_ColormapDesert centred on the
   demo site, so the same layer is olive 1.6 km north (material issue, not PCG).
6. **No terrain relief inside the basin** beyond terraces/noise (no mounds, no ledges for the pool).

## Concerns

- **Development.hwld HSEM chunk**: every world save rewrites the legacy mesh-name chunk from the
  entities currently loaded (only used for world versions <= 2). Headless runs therefore shrink it
  (1199 -> 555 bytes in the data commit, now listing only the PCG rock meshes). Harmless for v3 worlds and
  stable across runs (determinism held), but it is an unrelated data change riding along with any
  unattended save; worth making the editor stop writing (or preserve) that chunk.
- **Normals after Delete** differ by +-1 LSB from terrain_tool's baked normals (see Step 4); heights and
  paint are restored exactly. Fine visually, but "Delete returns the committed bytes" only holds for
  pages whose normals the editor wrote last.
- Water-material clearing on Delete clears an authored material on a page that had a material but no
  water quads before the oasis (edge case).
- Far-terrain `.tlod` of 38_47 is stale (not re-baked; the log says so). Not part of the data commit.
- Captures are 1-2.5 MB PNGs (about 18 MB in the main repo commit).
- The data branch exists only in this worktree's submodule clone (origin = local H:/mmo/data/client);
  the main repo commit 8b2c65b0 points at 0c546613, which must be landed in data/client before any push.

---

# Fix round 1

## Finding 1: duplicated unattended-job boilerplate (main repo 3d7bf811)

`src/mmo_edit/mmo_edit.cpp` now has three helpers in namespace `mmo`, used by `--pcg-rebuild`,
`--pcg-new-oasis` and `--pcg-delete`:
- `RedirectUnattendedLog(name)` truncates `<name>.log`, hooks it to the log signal, and in debug builds
  redirects CRT assert/error reports to `<name>_asserts.log`. Log file names are unchanged.
- `OpenWorldForJob(mainWindow, flag, path)` returns the `WorldEditorInstance*`, or logs
  `<flag>: failed to open <path>` and returns null.
- `ParseOasisId(text, id)` accepts a non-zero hex id and nothing else (shared by rebuild and delete).

`--capture-view` and `--bake-terrain-lod` are unchanged. Their setup differs (different log names,
plus extra options before the world opens), so converting them is not a pure no-op. The diff for the
three flags is +64/-66 lines.

Verification after rebuilding mmo_edit (Debug, no warnings):
```
rebuild exit=0
Rebuilt 'Korash PoC Oasis' in 786 ms: 38378 terrain samples, placements created 0, updated 0, removed 0, kept 308. ...
--pcg-rebuild: done
status:                      (git -C data/client status --porcelain: empty; hwld was still the 0c546613 version then)
bad-id delete exit=1
--pcg-delete: --oasis expects a non-zero hexadecimal id, got 'zz'
missing-world exit=1
--pcg-rebuild: failed to open Worlds/Development/NoSuch.hwld
status:                      (empty)
```

## Finding 2 / R14: Development.hwld back to master bytes

- data/client 7479a225 "Restore Development.hwld to master" (on feature/pcg-oasis-poc, on top of
  0c546613, which was not rewritten): `git -C data/client checkout master -- Worlds/Development/Development.hwld`,
  1199 bytes. `git -C data/client diff master --stat -- Worlds/Development/Development.hwld` prints nothing.
- Main repo 73fe1d03: gitlink -> 7479a225.

**Note:** every world save rewrites Development.hwld. This is existing editor behaviour: the save
rebuilds the legacy mesh-name chunk (HSEM) from whichever entities are loaded. So every unattended
`--pcg-*` run changes it. The determinism check has to be read with that in mind, and the file has to
be restored after each run:
```
rebuild exit=0
status after rebuild:
 M Worlds/Development/Development.hwld
(git -C data/client checkout -- Worlds/Development/Development.hwld)
status after restore:
(empty)
```
Nothing else changed, so the oasis output itself (tile, entities, foliage, .hpcg) stays deterministic.
The working tree is clean again.

Commits in this round: main 3d7bf811, 73fe1d03; data 7479a225. Nothing pushed.
