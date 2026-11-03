# Task 11 report: road section, test path and comparison

Status: DONE_WITH_CONCERNS. Commits: main `967ffbad` (gitlink + captures); data/client on `feature/pcg-oasis-poc`: `bc9d2f736` (pre-existing oasis record re-save, format v4) and `48c7d1024` (TestRoad + test path). Nothing pushed. `data/client` working tree is clean after the delete check was restored.

## Deviation from the brief: paint layer and lantern asset

- **No `stone_road` on map 0 near Briarwatch.** Every terrain page around Briarwatch/Oakenshire uses `Models/Terrain/Oakenshire_Boars_Enhanced.hmi` (the world default). A handful of tiles use `Haven_Streets`/`Briarwatch_Farmland_*`, and none of them uses the FalwynPlains materials. The design assumed `_Inst`/`_Forest`/`_Mountains` on map 0, but that is wrong. A section painted with FalwynPlains `T_stone_road` would match no layer there, so every road pixel would be skipped. The hand-made roads (Westroad, Haven Road) are painted with **layer 1, `Oakenshire_ChurnedDirt_BaseColor`**. I sampled the cross-sections: a 6–8 m bed at 245–255 that fades into grass over about 1 m. So TestRoad was created with `--new-section TestRoad --material Models/Terrain/Oakenshire_Boars_Enhanced.hmi`, and the bake logs these layer keys: 0 Grass_01, 1 ChurnedDirt, 2 T_Dirt_01_C, 3 T_GrassTerrain (from layer bindings). The rebuild reported no skipped layers.
- **Lantern:** `Models/FalwynPlains/Props/Village/Lantern.hmsh` is a 0.6 m hanging lantern with no post. The real Briarwatch road lanterns are 44 placements of `Models/FalwynPlains/Props/WoodProps/WoodLantern_A.hwmo` ("Road lantern / Briarwatch Valley"), so I used that one. This also exercises the WorldModel placement path. Fences: `SM_Fence_01.hmsh`, the 3.41 m chainable board.

## How the placeholder section was built

1. `mmo_edit --new-section TestRoad --material Models/Terrain/Oakenshire_Boars_Enhanced.hmi` created the 2×2 flat pages, the .hwld and the .hsecdef.
2. Scratch Python (`scratchpad/road/populate.py`, run with `py -3.13 -I`, not committed) patched the four `.tile` files in place. It replaced only the MCVT/MCVI/MCLY payloads, so the files keep the same size and chunk order. It also wrote 5 `.wobj` with worldkit's `wobj_bytes`, and wrote the `.hsecdef`:
   - Variants at originX **0** and **62.5**. 62.5 m is 15 outer vertices, so both beds sit on the same vertex grid and get identical profiles. length **20**, halfWidth **3**, sideFalloff **4**.
   - Bed: −0.3 m for |x| ≤ 2.6 m, smoothstep back to 0 at 4.2 m. Painted z −6..26, so the clamped template ends stay clean. Note that the terrain grid is 4.17 m, so the dip is only resolved at the centre and inner vertices.
   - Paint: layer 1 bed with a noisy verge line (±0.25 m), a layer 2 (T_Dirt) verge band at about 3.2 m, and faint layer 2 wheel ruts at ±1.1 m. Grass everywhere else, which is the default paint 0xFF.
   - Props: variant 1 has a WoodLantern_A at (+4.3, 9), with its arm toward the road, and 3 abutting SM_Fence_01 at x −4.6, z 5.71/9.12/12.53, at yaw 90. Variant 2 has a WoodLantern_A at (−4.3, 12), turned 180°. All sit at height 0, so heightAboveTerrain is 0.
   - Rules: fence keep 0.6 with yaw jitter 3°; lantern keep 0.8.
   - Normals (MCNM/MCNI) in the section pages were left flat. They only affect how the section world renders in the editor, not the bake.
3. `--bake-section Editor/Pcg/Sections/TestRoad/TestRoad.hwld` gave: "2 variant(s), 5 placement(s) captured, 0 outside every variant, 2 rule(s), 11467 bytes, source stamp 1d8c723e9ecdb07a".

## Site and path

- Site: the meadow on the open-woodland slope north-west of Briarwatch Camp, inside the Briarwatch valley. I picked it with a scratch search over terrain, splat, entities, foliage and atlas roads. It has no road paint, no water, no entities within 55 m, no trees, fences or rocks within 15 m of the centre line, and atlas roads are ≥ 73 m away. The nearest is the Woodland Trail to the west.
- Command: `--pcg-new-path Worlds/Development/Development.hwld --points "-96,604;-99,634;-109,662;-126,687;-149,706" --section Editor/Pcg/Sections/TestRoad.hsec --seed 11 --name TestRoadBriarwatch`
- Ground heights at the points: 7.19, 11.49, 13.29, 12.75, 12.68, so it **rises 6.1 m** (5.5 m start to end). It bends about 15° at each inner point, and the 4 spans are ≈30 m each, giving 2 copies per span and 8 copies in total.
- **Record id `8dbf60735bbdcf9d`**. Report: 4771 terrain samples, 10 placements created, only page 31_33 touched, no min-radius warning, no skipped paint layers.

## Determinism

I took snapshots of `git status --porcelain --untracked-files=all` plus sha1 of every changed or untracked file, after restoring `Development.hwld` each time:
- right after creation: 24 entries
- after `--pcg-rebuild` run 2: identical to creation (statuses and hashes)
- after run 3: identical to run 2

Each rebuild logged "TestRoadBriarwatch … created 0, updated 0, removed 0, kept 10" and "Korash PoC Oasis … kept 308". The oasis page 38_47 and its `.hpcg` stayed byte-identical. However, the rebuild log still prints "PCG: terrain pages changed (their .tlod bake is stale …): 38x47" for the oasis even though nothing changed on disk (see Concerns).

Moving a middle point: **skipped**. There is no unattended flag for it, and I did not drive the editor UI (no subagents or computer-use were in scope). The span-locality behaviour stays covered by the Task 7/8 unit tests only.

## Delete check

After committing the road, I ran `--pcg-delete Worlds/Development/Development.hwld --id 8dbf60735bbdcf9d`. It logged "removed 10" and exited 0. Then I restored `Development.hwld`.
- `8dbf60735bbdcf9d.hpcg` and all 10 generated `.wobj` were deleted. No untracked leftovers.
- `31_33.tile` compared against the pre-road version (`7479a225`), chunk by chunk: MCVT, MCVI, MCNM, MCLY (splat) and all other chunks are **byte-identical**. Only MCNI (inner normals) differs: 167 of 12288 values, every component by ±1 LSB of SNorm8. Two of them show as a 0x00↔0xFF byte, which is 0 ↔ −1 signed, so still 1 LSB. This is within the accepted tolerance.
- I then restored the state with `git -C data/client checkout -- .`, and the tree is clean at `48c7d1024`.

## Captures (docs/superpowers/specs/pcg-section-templates/, 1600×900, `--grass --fog --warmup 240`, time 0.42)

| File | Viewpoint (x,y,z,yaw,pitch,fov,time) | Size |
|---|---|---|
| `testroad_along.jpg` | `-92.0,12.0,588.0,164.5,-9,55,0.42` (behind the start, looking up the road) | 292 KB |
| `testroad_side.jpg` | `-50.0,40.2,690.0,61.7,-18.7,55,0.42` (elevated, from the east) | 271 KB |
| `reference_havenroad_along.jpg` | `-525.0,15.4,403.0,139.1,-9,55,0.42` (Haven Road lantern stretch, along) | 273 KB |
| `reference_havenroad_side.jpg` | `-610.0,39.7,400.0,233.5,-24.1,55,0.42` (same stretch, elevated side) | 301 KB |

The reference is the hand-made Haven Road stretch near (-555, 440). It has WoodLantern_A placements tagged "Road lantern / Briarwatch Valley", SM_Fence pieces and ChurnedDirt paint.

## Honest comparison with the hand-made road

What already reads right: the bed paint is the same ChurnedDirt material with a soft verge. The road follows the rise without steps or visible joins between copies. The lanterns stand upright on the slope with their arms over the road, on alternating sides. The terrain-material grass and flowers clear off the painted bed on their own.

What is missing or worse than the reference:
- **Too regular.** The reference meanders and varies in width (about 5–9 m). It wobbles inside the curves, and its edges fray into the grass. TestRoad has constant width and a crisp, uniform edge. The ±0.25 m noise is lost at the 0.53 m splat resolution and repeats every 20 m.
- **Fences.** The reference uses short, broken, slightly tilted pieces from several meshes (`SM_Fence_02..06`), placed irregularly on both sides and often as pairs of posts. TestRoad has 3-board runs on one side only, and they repeat every other copy. With keep 0.6, runs show up as 1–2 isolated boards, which reads like debris.
- **Lantern rhythm.** The reference spaces lanterns about 30 m apart, irregularly. TestRoad's 8 copies × keep 0.8 give a lantern roughly every 15–20 m, which is denser and more mechanical.
- **Ends.** The road starts and stops abruptly in the meadow, because caps and junctions are a non-goal of this step.
- **Dip.** The −0.3 m bed is barely visible, because the 4.17 m vertex grid is too coarse for a 6 m road. The reference roads near Haven sit noticeably lower or sit in shaped ground (the King's Road is −7 m).
- **No roadside dressing** (stones, bushes, wear patches, cart ruts) of the kind the hand-made roads have around them.

All of these are content issues for the user's hand-built section. None of them is a plumbing problem.

## Tests

`cmake --build build --config Debug -t all_tests -- /m /v:minimal` built with exit 0, and `ctest -C Debug --output-on-failure` passed 40/40. I also rebuilt `mmo_edit` first to confirm the binary was current (it was up to date).

## Concerns

1. **The design's map-0 premise is wrong.** Map 0 near Briarwatch uses `Oakenshire_Boars_Enhanced`, not the FalwynPlains materials. Sections meant for map 0 must use that material, or paint will be skipped everywhere. The user should know this before building the real road section. "New Section" defaults to `FalwynPlains_Terrain_Inst`, so it would silently produce a road whose paint never lands on map 0, and the only sign would be the skipped-layer report.
2. **CRLF and the source stamp.** `data/client` has `core.autocrlf=true` and no `.gitattributes`. The `.hsecdef` is written with LF and committed as LF, but a fresh checkout on Windows writes CRLF. `ComputeSectionSourceStamp` hashes raw bytes, so the `.hsec` will then report as stale (this is a warning, not a refusal). The same happened with the glue scene hash. Fix options: normalise line endings in the stamp, or add `*.hsecdef -text` to data/client's `.gitattributes`.
3. **Misleading log line.** The oasis rebuild logs "terrain pages changed … 38x47" even when the page is byte-identical. The .tlod staleness message probably fires on any apply, not on a real change.
4. **Moving a middle point was not exercised in the real editor.** I skipped it: there is no headless flag, and I did not drive the UI.
5. **Docs naming.** The spec says "+X right when looking along +Z". In the engine's view, +X (east) appears on the screen's left when you look along +Z (south). The transform itself is rigid (`right = up × forward`), so nothing is mirrored; only the wording is misleading for authors.
6. Existing foliage instances (shrubs, trees) are not cleared from the corridor by the generator. None stand on this test path, but a road through woodland will need exclusion handling later.
7. Generated pages need `--bake-terrain-lod` for 31_33. I did not run it; it is outside this task.
