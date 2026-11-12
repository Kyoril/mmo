# Procedural Content Generation — Oasis Proof of Concept

Status: design, awaiting approval (2026-10-24)

## Intent

The Sands of Korash demo (map 0, area 22, pages x 37-39 / z 49-51) shows the look we want for the
whole desert zone, but it was imported from an Unreal demo level and hand-placed content does not
scale to the ~210 pages of the zone. Asking an AI to place thousands of assets one by one does not
reach the quality bar either.

The long-term goal is an editor-only procedural system inspired by Unreal Engine 5's PCG framework
(see the Electric Dreams PCG write-up): hand-made **rule sets** per biome (our "tilesets") that know
how to build an oasis, a canyon ditch, a path, a river bank, driven by cheap **control objects**
(closed splines, open splines, volumes) that a human or an AI places. The rule set reads the
terrain and the control objects and produces terrain edits plus placed assets, deterministically.

This is experimental and the user does not yet know whether it can reach the quality bar. This
document describes **only a proof of concept**: one control object type (an oasis), one hard-coded
rule set, no node graph. Its purpose is to answer two questions cheaply:

1. **Plumbing:** can a generator own terrain edits and placed assets non-destructively — rebuild
   gives identical results, local changes only change the local result, removing the generator
   restores the world?
2. **Quality:** does a rule-driven oasis built from the existing Korash assets look as good as the
   hand-imported demo?

The second question is the real risk. Plumbing that works but produces ugly oases means the idea
needs rethinking before any graph editor is built.

## Constraints (user decisions)

- **Editor-only.** Nothing of the generator ships with the client or runs at runtime. The editor
  produces ordinary terrain pages, entity placements and foliage files that the client loads like
  any hand-made content. The generator's own source data (control objects, recorded ownership,
  terrain bookkeeping) must not be packed into client archives.
- **Ownership.** Everything a generator produced is linked to it. Deleting the generator removes
  all of it, including its terrain changes.
- **Determinism.** Same inputs + same seed = same output, bit for bit. Changing the seed reshuffles
  placement. Changing the terrain or moving a control point changes the result only where the
  change has influence.
- **Terrain is a primary output.** We are low-poly with no Nanite; we cannot build rock formations
  from thousands of meshes everywhere. Terrain heights, splat painting and water carry much of the
  look, and the generator must write them, not only read them.
- **Nothing reaches develop until the user has judged the result.** Code (spline refactor, PCG
  library, Procedural mode) and the generated test oasis (pages, entities, foliage, `.hpcg`, data
  submodule commits) all stay on the feature branch `feature/pcg-oasis-poc`; no `/ship`, no push.
  Data submodule commits for the test oasis are made on a branch in the submodule as well, never on
  its `master`. Since nothing ships, navmesh and server collision are not a concern for the PoC.
- **Tooling lives in C++** (`mmo_edit` UI + unattended `--flag` where needed), with Catch2 tests.
  No new committed Python tools (standing user rule).

## Findings that shape the design

From a survey of the engine (2026-10-24):

- **The Korash canyon look is mostly meshes.** The demo holds ~1,050 rock pieces (Cliff A/B/C/Half,
  Shelf, Hoodoo, Clump, Layered, Platform) — later baked into one WMO `Korash_Rocks.hwmo` — plus
  ~7,900 foliage instances and 20 water planes. Terrain supplies dunes and basin floors. So an oasis
  is terrain shaping *and* a ring of rock pieces.
- **Grass is already procedural at runtime.** Terrain materials carry `MaterialFoliageEntry` lists
  driven by splat-layer coverage (`Desert_Terrain.hmat`: R sand, G dirt, B desert grass, A grass).
  Painting the grass layer makes grass appear; the generator must not place grass instances.
- **Terrain pages** (`src/shared/terrain_io`): 533.33 units per page, 129×129 outer + 128×128 inner
  heights, 1009×1009 splat pixels (4 × 8-bit weights summing to 255), per-tile water mask + type,
  129×129 water heights per page, per-tile material. Seam samples are duplicated in neighbours and
  `Terrain::SetHeightAt` etc. mirror them.
- **`terrain_tool` delta import** (`src/terrain_tool/import_command.cpp`) already applies a height
  offset grid plus splat/water masks on top of existing pages and recomputes only affected normals —
  the closest existing thing to "apply a generator's terrain contribution".
- **Placements:** mesh/WMO entities are per-file `.wobj` with a 64-bit unique id, a name and a
  category string; trees are `InstancedFoliage` in per-page `.hfol` files with unique ids. No tag
  system, no general undo, noise in `math/noise.h` has no seed.
- **River splines already exist.** The Water mode's flow brush edits river paths
  (`terrain_io::water_flow::FlowPath` = points `{x, z, halfWidth}` in downstream order), stored per
  world in a `.hflow` file next to the `.hworld` (`WorldEditorInstance::GetFlowPathsFileName`), with
  viewport overlay drawing, point/segment hit testing, dragging, Alt+click insert and delete, all
  implemented inside `WaterEditMode` (`edit_modes/water_edit_mode.{h,cpp}`). Segments are straight
  polylines; the flow is computed from them into the pages' WFLW chunk.
- **Editor-only data folder:** `data/client/Editor/` is excluded from shipping
  (`tools/tests/test_client_data_packing.py`, `NOT_SHIPPED`).

## Goals

1. A new world-editor mode **"Procedural"** where the user can create an **Oasis** control object:
   a closed spline of control points lying on the terrain, which can be moved, inserted and removed
   in the viewport, with a seed and a small set of rule-set parameters in the details panel.
2. **Rebuild** (button, per oasis and "rebuild all") runs the oasis rule set and applies:
   - terrain height edits (basin floor, stylized rim falloff),
   - splat painting (grass inside, dirt/sand transition band at the rim),
   - water (a pool in the basin, using the per-tile water mask + water heights),
   - rock pieces on the rim (Korash cliff/shelf/clump meshes as map entities),
   - trees and bushes (palms, bushes as `InstancedFoliage`).
3. **Delete** an oasis removes every entity and foliage instance it placed and reverts its terrain
   contribution.
4. **Determinism and locality,** verified by unit tests and by hand:
   - Rebuild twice → identical pages, entity files and foliage files.
   - Move one spline point → only placements near that part of the rim change.
   - Sculpt the terrain under/around the oasis by hand, rebuild → the oasis adapts to the new ground.
   - Change the seed → different placement, same overall character.
5. Saved with the world (Ctrl+S) and restored on reload: control objects and ownership survive an
   editor restart; the generated data is ordinary world data.
6. One oasis built on a flat or dune stretch of area 22 **outside** the hand-imported 3×3 demo pages,
   for the user to compare side by side with the demo oasis.

## Non-goals (for the PoC)

- Node graph editor, graph assets, user-authored rule sets. The oasis rule set is C++ code with
  exposed parameters.
- Other control objects (open-spline paths, opening/exclusion volumes, rivers, canyon ditches).
- Tags on arbitrary actors / scene queries over tagged objects.
- Automatic rebuild on edit (rebuild is a button).
- Overlapping oases, or an oasis overlapping other generated content. Overlap is detected and
  refused with a message.
- Baking the rock ring into a WMO. Rocks are individual map entities in the PoC; WMO baking is the
  likely production path (minimap, draw calls) and is noted under follow-ups.
- Navmesh rebuild (existing nav tooling is run by hand afterwards if needed).
- General editor undo. Delete is the "undo" of a generator; ordinary terrain brushes keep their
  current (non-)undo behaviour.
- Client or server changes of any kind.

## Anti-goals

- Anything generator-related reaching the client: no PCG code linked into `mmo_client`, no PCG
  files packed into the patch archives.
- A rebuild that silently destroys hand edits made *outside* the oasis' influence area.
- Non-determinism from `std::` distributions, unordered containers, pointer ordering, time or the
  global `RandomGenerator`.
- Leaving orphans: after Delete there must be no entity, foliage instance or terrain change left
  that the oasis created.

## Design

### Shared spline editing (user request, 2026-10-24)

Splines are useful well beyond PCG: river flow paths today, PCG control objects now, NPC patrol
waypoints later. The PoC therefore does **not** write a second spline editor for the oasis. It
extracts the river path editing out of `WaterEditMode` into a reusable component and uses it in
both places:

- **Data** (`src/shared/math` or a small new place in shared, builder's call — must stay free of
  editor/render dependencies): a `Spline` = ordered control points with position (x, z, optional
  y), a `closed` flag, an interpolation mode (linear polyline, as rivers use today; Catmull-Rom for
  smooth loops), and optional per-point scalar attributes (rivers need `halfWidth`; the oasis may
  use per-point rim width). Evaluation: sample by arc length, nearest point / signed distance,
  inside test for closed loops, bounds.
- **Editor controller** (`mmo_edit`): a `SplineEditController` that owns selection of a point/
  segment, draws the overlay through `ImDrawList` (line, handles, selected point, optional width
  ribbon), hit-tests points and segments, drags points snapped to ground (or to a caller-supplied
  height function — rivers snap to the water surface), inserts (Alt+click on a segment, as today)
  and deletes points, and reports "changed" so the owner can schedule its own work (flow apply for
  rivers, nothing automatic for oases).
- **Rivers migrate onto it** with no change in behaviour or file format: `.hflow` keeps its layout,
  `FlowPath` is produced from the spline (linear mode), and the existing flow computation is
  untouched. This is a refactor with a regression check (same `.hflow` in → same WFLW out).
- **Oasis** uses the same controller with a closed Catmull-Rom spline.
- **NPC patrol waypoints** (`SpawnEditMode`, `maps.proto` waypoints) are a natural third user but
  are **out of scope** for the PoC; listed under follow-ups.

### Library split

- **`src/shared/pcg/`** — new static library, no rendering or editor dependency, linked only by
  `mmo_edit` and `pcg_tests`. Contains:
  - a small deterministic RNG (e.g. PCG32 or SplitMix64) and a stateless hash
    `Hash(seed, x, z, salt)`; seeded value/Perlin noise built on it,
  - (spline evaluation comes from the shared spline data above, not from this library),
  - the **oasis rule set** as a pure function:
    `OasisResult BuildOasis(const OasisInput&)`, where the input is the control points, seed,
    parameters and a read-only height sampler over the *base* terrain, and the output is a height
    target grid with weights, splat targets, a water region with level, and a list of placements
    (asset path, transform, kind = entity | foliage, stable local key).
  - No file IO and no `Terrain` dependency, so it is unit-testable headless (and builds on Linux,
    though nothing on the servers uses it).
- **`mmo_edit`** — the `ProceduralEditMode` (a `WorldEditMode`), the control-object storage, and
  the *applier* that turns an `OasisResult` into terrain edits, `.wobj` entities and `.hfol`
  instances through the existing `Terrain`, entity and foliage APIs.

### Determinism and locality

- All randomness comes from `Hash(oasisSeed, cellX, cellZ, salt)` evaluated on a **world-space
  grid**, never from a sequential RNG stream across the whole oasis. A placement candidate's
  existence, jitter, asset choice, rotation and scale depend only on its own cell and the seed.
  Moving one spline point therefore changes only candidates whose acceptance depends on the moved
  part of the rim.
- Rim placements (rocks) are generated by walking the spline by arc length in fixed steps; the
  random stream for each step is keyed by `Hash(seed, round(arcLengthStep), salt)`. Inserting a
  point shifts arc length downstream, so rim placements after the edit may shift — acceptable for
  the PoC, noted as a known locality limit.
- Iteration order of every container that affects output is defined (sorted vectors, no
  `unordered_*` in output paths). Float maths stays in a fixed order; the editor is Windows/MSVC
  only, so cross-compiler bit identity is not required, but the tests run on both compilers via the
  gate and must check "same build, same result".
- Each placement gets a **stable local key** (rule id + cell or arc step). The applier maps
  local keys to engine unique ids; on rebuild, placements whose key survives keep their unique id
  (and file), so unchanged rocks are not rewritten. This keeps diffs of the data repo small.

### Terrain contribution (non-destructive)

The oasis owns a contribution to the terrain inside its **influence rect** (spline bounds + rim
falloff + margin), stored editor-side so it can be removed:

- **Heights — additive delta.** The oasis stores `delta = generated − base` for every vertex it
  touched (outer and inner). Rebuild: `base' = current − delta` (this keeps any hand sculpting the
  user did since the last build), run the rule set against `base'`, write `base' + newDelta`, store
  `newDelta`. Delete: `current − delta`. This is what makes "sculpt under the oasis, rebuild, it
  follows" work.
- **Splat, water, tile material — compare-and-restore.** For each pixel/tile it changed the oasis
  stores the base value and the value it wrote. On rebuild/delete, a pixel whose current value still
  equals what the oasis wrote is restored to base; a pixel the user has repainted since is left as is
  and becomes the new base. Water heights and masks are handled per tile the same way.
- **Normals** are recomputed for all touched vertices plus a one-vertex border, using neighbour
  pages where they are loaded (editor has them).
- **Seams** go through the existing `Terrain` setters so neighbour duplicates stay identical.
- The applier only touches pages whose bounds intersect the influence rect, and marks them changed
  so the normal world save writes them.

### Oasis rule set (first version — parameters exposed, defaults tuned against Korash)

All distances in world units (1 unit = 1 m).

1. **Base sampling.** Sample `base'` heights along the spline; rim height = a low percentile of
   those samples (so a dune peak on one side doesn't lift the whole basin).
2. **Basin.** Inside the loop, target floor height = rim height − `basinDepth`. Blend from rim to
   floor over `rimWidth` with a stylized profile (smoothstep with an optional terrace step count,
   matching the soft-capped, stepped look of Korash) plus low-amplitude seeded noise on the floor.
   Outside the loop, blend back to `base'` over `outerFalloff`.
3. **Water.** Pool region = points with signed distance < −`waterInset` from the loop; water level
   = floor height + `waterDepthOffset`. Written as per-tile water mask (type Water) + water vertex
   heights, using the water material already used by Korash pools.
4. **Splat.** Grass (layer A) inside, fading to desert grass (B) then dirt (G) in a band around the
   rim, then sand (R) outside; band edges perturbed by seeded noise. Runtime grass appears from this
   by itself. Tiles inside the influence rect are set to `Desert_Terrain.hmat` if they are not
   already.
5. **Rim rocks.** Walk the loop by arc length, place cliff/shelf pieces (`SM_RockDesert_Cliff*`,
   `Shelf*`, `CliffHalf*`) facing into the basin, sitting on the outer edge of the slope, with
   clumps (`Clump*`, `Rock*`) filling gaps; random yaw jitter and scale range per family. Pieces
   snap to `base'+delta` ground height with a sink offset.
6. **Vegetation.** On the grass band between pool and rim: palms (`DesertCoconutPalm*`,
   `DesertPalmTree*`, saplings) with a minimum spacing (grid-cell Poisson-ish rejection), denser near
   the water; bushes (`DesertBush*`, `ElephantEars`, `Marshtail` near water) in the same pass. All as
   `InstancedFoliage` (trees collide, bushes don't, matching Korash).
7. **Slope/ground rejection.** Candidates on slopes steeper than per-family limits, or under water,
   are rejected.

Asset lists and parameters live in one editor data file (JSON or SFF, builder's call) under
`data/client/Editor/Pcg/`, so tuning does not need a recompile. Defaults are tuned by eye against
the Korash demo oasis visible in the user's screenshot.

### Control objects and storage

- One file per world: `data/client/Editor/Pcg/Worlds/<world>/oasis.<id>.hpcg` (or a single
  per-world file — builder's call) holding: oasis id (64-bit), name, seed, parameters, control
  points, influence rect, the height delta grid, the splat/water/material base-and-written records,
  and the local-key → unique-id map for entities and foliage instances.
- Written on world save alongside pages/entities/foliage; loaded when the world opens.
- Because `data/client/Editor/` is not shipped, nothing of this reaches players.
- Generated entities additionally carry category `Pcg` and a name prefix with the oasis id, so they
  are recognisable in the existing entity list. Ownership is defined by the `.hpcg` record, not by
  the category.

### Editor UX

- New mode **Procedural** in the world editor mode combo.
- Details panel: list of oases in the world; buttons *New Oasis* (places a default 8-point loop of
  ~40 m radius at the viewport's ground hit), *Rebuild*, *Rebuild All*, *Delete*; selected oasis'
  name, seed (with *Randomize*), and parameters.
- Viewport: the loop is edited through the shared `SplineEditController`, with the same gestures
  as river paths today (drag a handle, Alt+click a segment to insert, delete a selected point; an
  oasis keeps at least 3 points). Influence rect drawn faintly.
- Rebuild shows a short status (time taken, counts of terrain vertices / placements written / kept /
  removed).
- Generated entities are selectable like any other but a warning shows in the details panel that
  they are owned by an oasis and will be overwritten on rebuild.
- Unattended flag `mmo_edit --pcg-rebuild <world> [--oasis <id>]` that loads the world, rebuilds,
  saves and exits — for the determinism check and for agents.

### Testing

- Spline data tests (in the suite of whichever library hosts it): signed distance, inside test,
  arc length on known shapes, linear and Catmull-Rom, open and closed.
- River regression: an existing `.hflow` produces the same `FlowPath`s and the same WFLW flow
  before and after the refactor.
- `src/tests/pcg_tests/` (new suite, headless, on Linux too):
  - RNG/hash reproducibility and distribution sanity.
  - `BuildOasis` determinism: same input twice → identical result; seed change → different
    placements; moving one control point → placements far from it unchanged.
  - Height delta round trip on a synthetic height field: apply, rebuild with no change → identical;
    hand edit + rebuild keeps hand edit outside influence; delete → original field.
  - Compare-and-restore for splat and water.
- Manual acceptance in the editor: the six goals above, plus side-by-side screenshots of the PoC
  oasis and the demo oasis (`--capture-view` exists for this) for the user's quality judgement.

## Success criteria

- All determinism/locality/removal checks pass (tests + `--pcg-rebuild` twice → no file changes).
- The user judges the PoC oasis to be in the same quality league as the demo oasis, or can say
  concretely what is missing — that list is the input for deciding whether to continue.

## Calls made by the designer (not user decisions — check these)

- **Hand paint inside the oasis:** a pixel/tile the user repainted after a build becomes the new
  base, and the next rebuild paints over it again (the oasis owns the paint in its area; the hand
  paint comes back on Delete). No per-pixel "user-owned" mark in the PoC. Heights differ on purpose:
  hand sculpting is kept because the rule set adapts to the ground.
- **Existing hand-made content inside the influence rect** (entities, foliage, water) is ignored by
  the PoC: not avoided, not removed. The test site is chosen empty. Avoiding existing placements is
  the job of the exclusion volumes in the follow-ups.
- **Reference oasis:** the green basin with the pool in the centre of the 3×3 demo pages (the one in
  the user's screenshot, found by its water). Korash pools above sea level already use per-quad
  terrain water (`waterLevelMap` → water heights), so terrain water is a fair comparison; the demo's
  mesh water planes and waterfalls are not reproduced.
- **Float drift:** `current − delta` round trips are compared with a small epsilon in tests;
  "bit for bit" applies to generator output for identical inputs, not to repeated apply/unapply.
- Stale `.tlod` far-terrain bakes for touched pages are rebuilt (or flagged) after a rebuild;
  pages not loaded in the editor are loaded before applying.

## Left to the builder

- Exact parameter defaults and profile curves (tune visually).
- File format details for `.hpcg` (chunked binary like other editor formats vs SFF/JSON) as long as
  it is deterministic and diff-friendly enough.
- Which existing water material/type Korash pools use and how pools above sea level get their
  level (`waterLevelMap` → per-tile water heights).
- The test site in area 22 (any open dune/flat stretch outside the 3×3 demo pages).
- Whether placement uses `Terrain` directly or goes through `terrain_io::PageData` views.

## Follow-ups (after the PoC, if it convinces)

- Node graph: data-driven rule sets assembled from nodes (sample terrain, sample spline, filter by
  slope/height/splat, density, difference/intersection, spawn mesh/foliage/terrain op).
- More control objects: exclusion/opening volumes, open-spline paths, river splines that carve and
  write water flow, canyon ditch loops.
- Tags on editor actors and graph queries over them.
- NPC patrol waypoints edited with the shared spline controller.
- Move `.hflow` into the non-shipped editor folder (it sits next to the `.hworld` today and is
  editor-only data).
- Bake generated rocks per generator into a WMO (minimap, draw calls), as was done for Korash.
- Overlap and ordering between generators (priority / layer order).
- Auto rebuild on change, navmesh rebuild hook, agent-facing CLI for placing control objects.
