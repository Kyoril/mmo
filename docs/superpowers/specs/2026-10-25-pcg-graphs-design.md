# PCG Graphs, Control Types and Biomes — Design (phase 3)

Status: design, awaiting approval (2026-10-25)
Builds on: `2026-10-24-pcg-oasis-poc-design.md`, `2026-10-25-pcg-section-templates-design.md` (branch `feature/pcg-oasis-poc`)
Research: `docs/superpowers/research/2026-10-25-unreal-pcg-architecture.md` (Unreal 5.8 PCG, node catalogue),
`docs/superpowers/research/2026-10-25-korash-decomposition.md`

## Intent

The proofs so far showed the pieces work: non-destructive terrain contributions, ownership and
reconcile, seeded determinism, shared 3D splines, hand-built section templates laid along splines,
connected prop runs. But every rule is still C++ (the oasis) or one fixed recipe (section paths).

The user's model for the real system:

> Graphs are basically my "tilesets" for different areas. Once I have such a tileset working, we can
> use it to easily and extremely quickly build huge maps out of these, and also iterate over map areas
> extremely fast.

A **graph** (UE5-PCG-style visual node graph) holds the rules of a landscape type; **controls**
(loops, splines, volumes, points with a type, tags and properties) are cheap to place by hand or by
AI; **sections** are hand-built pieces the graph stamps. Quality comes from hand-built graphs and
sections; scale comes from placing controls.

## User decisions (this conversation)

- **Visual node editor** to build graphs, with **debug capabilities**: per node, toggle a debug view
  that draws the node's output (coloured cubes for spawn points etc.) into the world. The graph is
  "just another editor instance" of a file asset in the existing editor system, in its own dockable
  (fullscreen-able) window next to a world view.
- **Graphs are human-built (or heavily human-reviewed) tilesets.** AI places controls; humans own graphs.
- **One graph covers a region** and reads all controls in it (Electric Dreams model), settling their
  interactions (roads clear forest, exclusions block everything, rivers feed lakes).
- **Control types are data**: a control type declares its shape, default tags and property groups;
  placed controls override properties per instance. **Tags** are how graphs find controls (an
  exclusion is anything tagged `exclusion`; a road can also carry `exclusion`). **Graph parameters**
  are region-wide knobs (seed, density, …).
- **Biomes own their assets**: control types (and graphs, sections) live in per-biome folders, so a
  Falwyn "Forest Area" never affects Korash; editing one control type has no hidden side effects
  elsewhere.
- **Graph assets, control types, sections are editor-only** (under `data/client/Editor/`, not shipped).
- **Terrain**: the user blocks out large shapes by hand with the normal terrain tools (e.g. keeping
  the land high behind Oakenshire's mountains); controls build **on top of** that and a rebuild must
  never wipe the hand edits.
- **Control kinds wanted for Falwyn/Oakenshire**: valley (closed loop → mountain wall around it),
  mountain ridge (open spline → mountains along it, raising the spline raises the ridge), forest
  area (closed loop → trees and foliage with minimum spacing for playspace, plus gentle terrain
  roughness), exclusion areas (village, bandit camps, boar mud field — hand-built areas), roads
  (spline → road sections; also clears forest on/near it), lakes (closed loop → water), rivers
  (spline → bed, automatic water flow).
- **Waterfalls** and similar "smart" placements are graph logic (e.g. a node path that finds river
  ends near other, lower water within a range and height difference and emits a spawn point for a
  waterfall section), not hard-coded features.
- **Mountain walls** are built from hand-made cliff sections stamped around/along the control; how
  sections "work together" at corners is an open quality risk to prove early.
- **First real tileset: Falwyn forest / Oakenshire**, rebuilt in **world 2** (`Worlds/Collision`,
  the user's collision test level) on **unused pages** — **map 0 must not be modified for tests**.
  The result does not need to match 1:1 but must be **recognisable, playable and feel well polished**.
- Carried over: editor-only, deterministic with seeds, everything owned and removable, C++ tooling
  with unattended flags and Catch2 tests, nothing reaches develop before the user has judged it.

## Goals

1. **Graph asset + graph editor**: create/open/save a graph (`.hpcgg`) in its own editor instance
   with an imgui-node-editor canvas (as the material editor does), node palette, pins, links,
   per-node settings panel, graph parameters panel.
2. **Debug view per node** in the world: toggling a node draws its output in the open world viewport
   (points as coloured cubes oriented by their rotation, scaled by density/size; splines and areas as
   lines; terrain targets as a coloured height overlay or sample dots). Debug output is never saved
   or added to the world. While a graph is open with debug on, edits to node settings, parameters or
   controls re-evaluate the affected nodes (cached) and refresh the debug view without writing
   terrain or placements.
3. **Control types** (`.hctl`): shape (closed loop | open spline | volume | point), default tags,
   property groups with typed fields (float, int, bool, enum, string, asset reference, weighted asset
   list, colour) and defaults. Created and edited in their own small editor instance.
4. **Placed controls**: in the Procedural mode, place a control of a type from the active biome;
   edit its shape with the existing 3D spline tools (or a volume/point gizmo), its tags and its
   property overrides in the details panel. Stored per world like today's records.
5. **Graph instances**: a world object that applies one graph to a region: an area (closed loop or
   box), the graph asset, the biome, seed and parameter overrides. **Rebuild** evaluates the graph
   over its region and applies everything through the existing core (`ApplyGeneratedContent`): one
   owned content per graph instance; **Delete** removes all of it.
6. **Node set v1** (enough for Falwyn/Oakenshire, see below), deterministic, each node unit-tested.
7. **Terrain builds on the hand-made base**: all terrain nodes write *relative* targets (base +
   delta, or blend toward a target expressed relative to base) so hand sculpting under and around
   generated content survives rebuilds; the oasis' "owns the basin height" ruling (R7) is retired for
   graph content.
8. **Falwyn forest biome + Oakenshire rebuild in world 2**: control types, sections (cliff wall
   pieces, road pieces), and a region graph that produce a recognisable, playable, polished
   Oakenshire on unused pages of `Worlds/Collision`.

## Non-goals

- Korash desert biome (plateau steps, desert rivers/waterfalls, oasis as graph) — next biome.
- Runtime/in-client generation, partitioned or hierarchical generation, streaming generation.
- Navmesh and minimap/far-LOD regeneration (reported, run by hand as today).
- Graph-in-graph (subgraphs) and loops in v1 (may come with the second biome).
- Replacing the existing oasis and Section Path controls; they keep working. Their logic becomes
  available as nodes ("Lay Section Along Spline", the oasis rules are not ported in v1).

## Anti-goals

- A graph system that is pleasant on paper but slow to iterate with: debug feedback for a region
  must be quick enough to tune by eye (target: seconds, not minutes, for an Oakenshire-sized region).
- Hidden cross-biome side effects.
- Rebuilds that destroy hand-sculpted terrain or hand-placed content outside generated ownership.
- Anything shipping to clients; any change to map 0.

## Concepts and data model

### Folders (all editor-only)

```
data/client/Editor/Pcg/Biomes/<Biome>/
    Graphs/<Name>.hpcgg        graph assets ("tilesets")
    Controls/<Name>.hctl       control types
    Sections/<Name>/...        section worlds (existing format) and <Name>.hsec
data/client/Editor/Pcg/Worlds/<world>/<id>.hpcg   placed controls and graph instances (records)
```

Existing section worlds/assets move under biomes when used by a biome; existing oasis/path records
stay where they are.

### Data flowing through a graph (Unreal-style, simplified)

A pin carries a **collection** of tagged data items. Item kinds in v1:

- **Points**: position, rotation, scale, density (0–1), seed, bounds, plus named attributes
  (float, int, vector, string) — Unreal's point data + metadata.
- **Splines**: a `PathSpline` (3D, tangents) plus attributes (copied from the control's properties).
- **Areas**: a closed loop (or box) plus attributes.
- **Terrain**: a read view of the base terrain (height, normal/slope, splat weights, water) for the
  region — Unreal's landscape data.
- **Params**: attribute sets (graph parameters, control properties).
- **Terrain targets**: deferred terrain writes (height delta/blend, splat, water) produced by terrain
  nodes, merged in node order into the instance's `TerrainContribution` at Rebuild.
- **Spawns**: deferred placements (entity, world model, foliage, section stamp) — merged and
  reconciled at Rebuild.

Every item carries **tags** (from the control or added by nodes). Control properties arrive as
attributes on the item (e.g. `density` on a forest area).

### Determinism

Seed = Hash(instance seed, node seed, item/point seed); point seeds derive from position (grid cell)
or from the producing control's stable id, as in Unreal and as the PoC already does. Same graph +
same controls + same base terrain → identical files.

### Execution

- A graph instance's **Rebuild** evaluates the graph topologically over its region, then hands the
  merged terrain targets and spawns to `ApplyGeneratedContent` (page readiness, drift guard, unapply,
  apply, reconcile — unchanged).
- **Preview/debug** evaluates the same graph without applying; node outputs are cached by a hash of
  (node settings, input hashes, seed); changing a control or a setting re-evaluates only downstream
  nodes.
- Terrain nodes in preview read the base terrain *as it would be after Unapply* of the instance's
  current contribution, so previews match what a rebuild produces.

### Terrain stacking

Graph terrain nodes produce targets of three kinds: **add** (delta on top of base — roughness,
ridge raise, river bed carve), **blend-to** (move toward a height relative to base, with weight —
flattening a road bed relative to the local base), **paint** (splat). Hand edits made after a build
stay because `base' = current − (written − base)` (existing contribution logic) and targets are
relative to `base'`.

## Node set v1

Grouped as in Unreal; names are ours. Each with settings, deterministic, unit-tested on synthetic
data, and a debug drawing.

- **Inputs**: *Get Controls* (by tag and/or control type, inside the instance region) → splines /
  areas / points with their properties; *Get Terrain*; *Graph Parameters*.
- **Samplers**: *Sample Spline* (points along at spacing, or along offset lines); *Sample Area*
  (points inside a loop: grid + jitter or Poisson-disk with minimum spacing); *Sample Terrain
  Surface* (points on terrain over an area at density).
- **Filters / spatial**: *Filter by Tag/Attribute*; *Filter by Terrain* (slope, height, splat
  layer, water); *Density Filter*; *Self Prune* (minimum spacing, priority by density or seed);
  *Difference* (remove points inside areas / within a distance of splines; also area − area);
  *Intersection*; *Distance To* (writes distance to nearest spline/area edge as an attribute).
- **Point operations**: *Transform Points* (offset, rotation, scale ranges, align to terrain normal
  or upright); *Density Noise*; *Attribute Math* (basic arithmetic/remap on attributes); *Copy
  Attributes from Control*.
- **Terrain**: *Terrain Noise* (add, inside areas, falloff — forest roughness); *Raise Along
  Spline* (mountain ridge profile, height from the spline's height above base, falloff); *Carve
  Along Spline* (river bed / road bed, relative); *Paint Splat* (layer by texture key, inside areas /
  along splines / at points with radius); *Water Area* (lake fill from a loop, level from control);
  *River* (bed + water heights + flow from a spline).
- **Spawners**: *Spawn Meshes* (weighted asset list → entities or foliage, per-point); *Stamp
  Section at Points* (a section variant at each point, oriented by point rotation); *Lay Section Along
  Spline* (today's section path as a node, incl. connected prop runs); *Lay Section Along Loop*
  (cliff/wall sections around a closed loop, inward-facing, corners handled by the join rules).
- **Debug**: every node draws its output when its debug toggle is on; a *Debug Probe* node for
  inspecting attributes of a selected point.

The waterfall rule from the user is expressible with these nodes (river spline end points → Distance
To other water → Filter by distance and height drop → Stamp Section at Points with a waterfall
section) and is the acceptance example for the node set's expressiveness.

## Editor

- **Graph editor instance** (like the material editor): canvas with nodes/pins/links, node palette
  (searchable, grouped), details panel for the selected node's settings, graph parameter list, and a
  "target world / instance" selector naming which open world and graph instance the debug view uses.
- **World view**: the existing world editor viewport; debug draw layer per graph editor; a legend.
- **Control type editor instance**: shape, default tags, property groups and fields with defaults.
- **Procedural mode** additions: biome selector; "New Control" (types of the active biome); "New
  Graph Instance" (area + graph + params); list shows controls and instances; Rebuild / Rebuild All /
  Delete as today.
- Unattended flags for AI and tests: create controls and instances, rebuild instances, evaluate a
  graph and dump node outputs (for tests and agents).

## First tileset: Falwyn forest, Oakenshire in world 2

- **Site**: unused pages of `Worlds/Collision` (currently pages x 29–36 / z 30–33 are used; pick a
  free block of the right size away from them). Base terrain blocked out by hand-equivalent means
  (terrain tools / terrain_tool import of a rough height shape) — the "hand base".
- **Control types** (biome `FalwynForest`): Valley (closed loop), Ridge (open spline), Forest Area
  (closed loop: density, min spacing, species set, roughness), Exclusion (closed loop; tag
  `exclusion`), Road (open spline; tags `road`, `exclusion`; section, width), Lake (closed loop;
  level), River (open spline; width, depth).
- **Sections**: cliff/mountain wall pieces (incl. corner variants) and road pieces built in section
  worlds from existing Falwyn/Oakenshire assets; the user refines them by hand.
- **Region graph** "Falwyn Valley": valley walls from Lay Section Along Loop + Raise; forests from
  Sample Area → Difference (exclusions, roads, water) → Self Prune → Filter by Terrain → Spawn
  Meshes, plus Terrain Noise; roads via Lay Section Along Spline; lakes and rivers via Water Area and
  River.
- **Acceptance**: the user's hand-built bandit camp inside an exclusion survives every regeneration
  untouched; the user judges it recognisable, playable (walkable, roads connect, no trees in
  the way of the playspace) and polished; rebuild twice → no file changes; hand terrain edits
  survive a rebuild; delete removes all generated content; map 0 untouched.

## Phasing (one plan per phase)

0. Wall spike: cliff section with a corner variant around a valley loop (see "Resolved" item 2).
1. Graph core (pure lib): data items, attributes, evaluation, caching, determinism; nodes: inputs,
   samplers, filters, point ops; control types and placed controls (records); unit tests.
2. Editors: graph editor instance, control type editor, debug draw layer, Procedural mode controls
   and graph instances; Rebuild through the core.
3. Terrain, water and section nodes (relative terrain stacking, Lay Section Along Loop with corners,
   river flow).
4. Falwyn forest biome content and the Oakenshire rebuild in world 2, iterated with the user.

## Resolved after the builder check (designer's calls)

1. **Section heights in graphs.** Sections are applied *relative to a smoothed base*: the centre-line
   height is the hand base (`base'`) under the spline/loop, low-pass filtered along it (so bumps are
   levelled but hand-made large-scale shape is followed), plus the section's baked heights. Within
   the section's width the section owns the height (it must level a road bed); hand sculpting inside
   that corridor is overwritten on rebuild, hand sculpting of the large shape is followed. The
   existing `FollowSpline` mode stays available as a node setting (absolute heights from the spline,
   used where the designer wants an exact grade). Cliff/wall sections: baked heights are relative to
   the floor side (0 at the loop); a control property `wallHeightScale` scales them.
2. **Corners and an early wall prototype.** Walls along loops use bending/stretching like roads,
   plus optional corner variants in a section, tagged `corner.inner` / `corner.outer`, chosen at
   control points whose turn angle exceeds a section setting (and blended with join cross-fades). The
   `.hsec` format gains a per-variant role (straight / inner corner / outer corner). Because corners
   are the main quality risk, **phase 0** is a quick wall spike: one hand-built cliff section with a
   corner variant laid around a valley loop with the existing section-path code, judged by the user
   before the graph core is built.
3. **Regions.** v1: one graph instance per region; regions may touch but not overlap (refused as
   today). Graph output (terrain targets and spawns) is clipped to the region's area; terrain falloff
   fades to zero at the region border. Controls crossing a border are read whole by every region
   they touch but only produce output inside each region. Per-sample ownership across overlapping
   regions is a later step.
4. **Locality.** Required where cheap: samplers and Self Prune use world-anchored cell hashing
   (Poisson-like via per-cell candidates with hashed priorities, as the oasis already does), so
   moving one control changes output only near it; section layout stays per control-point span.
   Determinism is required everywhere.

## User answers after the builder check

- **Base terrain in world 2:** the builder blocks out a rough Oakenshire-like shape (valley floor,
  higher land behind the surrounding mountains, gate opening) with the terrain tools / terrain_tool —
  not a copy of map 0.
- **Hand-built areas:** the user builds the bandit camps (and other hand-made areas) in world 2
  themselves, inside exclusion controls. This is an explicit acceptance test: regenerating the
  region must not override the user's hand-built camp.
- **Map 0:** the existing test content there (oasis, TestRoad) may stay. **No new PCG output on
  map 0**, and no changes to its terrain, placements or NPC placements from now on.

## Calls made by the designer (check these)

- Work continues on `feature/pcg-oasis-poc` (it already carries the PCG foundation this builds on).

- Graph file extension `.hpcgg`, control type `.hctl`; biome folder layout above.
- A graph instance is a separate world object (area + graph + params) rather than a component of a
  control; it is the unit of ownership/rebuild.
- Data model simplified from Unreal: points/splines/areas/terrain/params plus deferred terrain targets
  and spawns; no spatial-data hierarchy, no partitioning.
- Terrain nodes write relative to the hand-made base; the oasis' absolute-basin rule is not used for
  graph content.
- Existing oasis and Section Path controls stay; the section path becomes a node; the oasis is not
  ported in v1.
- Debug preview never writes the world; only Rebuild does.

## Decided while building phase 1 (graph core)

- **Region clipping happens when content is applied, not during evaluation.** Evaluation output is
  deliberately unclipped, so spacing near a region border is computed from the whole neighbourhood
  and stays consistent between neighbouring regions. The Rebuild/apply step (phases 2–3) clips to
  the region's area, as "Resolved" item 3 requires.
- **Graph parameters drive node settings through `$name` bindings.** Any setting value `"$name"` is
  resolved from the merged graph parameters before the node runs (`"$$"` escapes a literal `$`), and
  the resolved value is part of the cache key. A binding to an undeclared parameter is reported by
  validation.
- **Per-part context dependencies.** Each node declares which parts of the evaluation context it
  reads (parameters, region, controls, terrain, point limit). Its cache key includes only those parts,
  so dragging a control or changing a parameter re-runs only the nodes that depend on it.
- **Distance To writes `distanceTo` by default.** Copy Attributes from Control also reads values from
  area edges within `distance`; the waterfall rule uses this to give river ends the lake's `level`.
- **Ties between points that are too close keep the lower seed** in both Sample Area's built-in prune
  and Self Prune (`priority = seed`).
- **Phase 2 notes from the final review:** outputs should be shared rather than copied before the
  editor keeps per-node debug output; Evaluate needs cancellation and progress hooks; terrain-writing
  nodes (phase 3) need an output item kind, because the terrain sampler is read-only.

## Carried into phase 3 (from the phase 2 final review)

- **Evaluate on the base terrain.** Rebuild evaluates the graph before `ApplyGeneratedContent` undoes the
  instance's previous terrain contribution. Once graphs write terrain, terrain nodes would sample their own
  earlier output and heights would compound on every rebuild. Evaluate after Unapply (inside `prepare`) or
  against a base-terrain view, and ground placements on the terrain after the new write.
- **Terrain overlap for graph instances.** Graph instances are left out of the oasis/section terrain overlap
  check while they write no terrain (ruling R16). Add them once they do.
- **Explicit "undo, write nothing".** Today an empty `content.terrain` keeps a recorded contribution, so a graph
  that drops its terrain nodes would leave terrain behind until Delete. `ApplyGeneratedContent` needs an explicit
  mode that undoes the old contribution and writes nothing.
- **Order of Rebuild All.** Graph instances are rebuilt after terrain writers. Keep that order when graph instances
  start writing terrain themselves, and decide how instances that both read and write terrain are ordered.

## Decided while building phase 3b

From the rulings made while the phase was executed:

- **Prepare's seam `neighbours` parameter defaults to empty.** Existing callers and tests stay unchanged; the
  editor passes the neighbours explicitly.
- **A river is not the water it falls into.** The waterfall rule's Distance To and Copy Attributes from Control
  read Get Controls(`water`, excluding `river`); a river falling into another river gets no waterfall in v1.
- **The basin rim check runs only when `depth` > 0,** so flat depth-0 lakes keep their phase 3a output without
  warnings.
- **Stamp length 0 is resolved when the terrain is prepared** (marker params `stampLength`/`stampAnchor`), because
  nodes have no asset access; an explicit length emits a plain SectionPath op.
- **A missing `section` on Stamp Section at Points is a node error,** as for Lay Section, even with no points.
- **Inside `width` the river owns the bed height** (surface − depth, even where the ground is lower), like a section
  corridor; a river across a deep hollow builds a raised bed there.
- **A river has a flat downstream end** (the upstream end closes like the banks, see "River banks" below). Past the last point (projected along the end segment) there is no
  water and no owned bed; only the bank closes the bed there, by the distance to the bed's end edge, and it only
  lowers. Round caps put a sheet of river water up to `width` + `waterMargin` past the end, floating over a waterfall
  pool.
- **Flow paths end exactly at the river's last point** (the last gap may be shorter than 4 m).
- **A closed River spline is skipped with a warning;** rivers flow one way.
- **`waterMargin` defaults to 3.** 2 left a thin dry band below the waterline at bank 4 / depth 1.5. River `depth` is
  clamped to >= 0 and WaterArea `depth` to <= 200 when the terrain is prepared.
- **Records always write version 2;** an existing record grows by 4 bytes on its next rebuild.
- **The world's flow file is `<world folder>/<world>.hflow`** (world 2: `Worlds/Collision/Collision.hflow`).
- **A rebuild shares flow with the open editor's hand rivers** through an `IWorldEditor` hook (pending flow edits
  are flushed first); the `.hflow` on disk is read only when no Water mode exists.
- **Seam neighbours are found by `CollectSeamNeighbours`:** an instance counts when it wrote terrain or its graph
  has a node with a Terrain pin (the R11 rule).

Task 7 (Falwyn stream and waterfall):

- **River controls carry a `clearance` (10 m = bed + bank + 2 m).** Difference clears points around a spline only by
  `distance` + the spline's `clearance`; the `exclusion` tag alone removes nothing.
- **Lakes are Get Controls(`water`, excluding `river`)** for Water Area too: Water Area takes areas, and the lake
  basin (`depth` 3, `shoreWidth` 6) must not see the river spline.
- **Waterfall section:** L 12, halfWidth 6, sideFalloff 4. The lip (local z 0–3) is rock at 0 beside a 9 m spill
  channel at −1.9, the river bed depth (1.5 m under the water surface −0.4) carried to the edge; the face drops to a
  plunge pool at −6.9…−7.5. `SM_Waterfall2` is a map entity (foliage draws its translucent material as white
  fragments), scale (1.0, 1.38, 1.1), at z 4. Section placements stand on the final ground under them, so its height
  is tuned to the world 2 site (ground 20.62 under the pivot): foam at 21.55 on the lake (21.5), top at 26.79 on the
  river surface (26.73). Other drops need their own tuning or a per-point scale. Six `SM_stone_06` rocks, no paint,
  no prop rules.
- **The drop comes from a hand base.** World 2's valley floor is flat (~20.5 m) and lower than the lake level 21.5.
  A terrace east of MeadowLake (+5.8 m scarp 2–7 m from the shore, rising to +10.2 m upstream) gives the river a
  5.65 m drop. A berm around the lake (ground lifted to 22.8 m within 3 m of the shore, fading out by 12 m) fixes
  the rim warning (base rim was 20.24 m).
- **Moving a control point unattended means editing the control file.** Delete and create again would sample the
  point heights from the already carved terrain.
- **Re-creating an instance reproduces its terrain byte for byte, but not its placement ids.** Created placements get
  new unique ids, so the committed state is the original instance (its record id kept), not the re-created one.
- **River materials win page-wide, inside the region only.** A page where the instance writes water gets the River
  op's material (`Water_River.hmat`) if it has none yet, so a page with a river renders all its water, the lake
  included, with it. The flow update (`Terrain::ApplyWaterFlow`) assigns no material: it would reach every page with
  water near any path, hand rivers and pages outside the region included, and Delete could not take it back.
- **The waterfall's water is the mesh alone.** With flat end caps the river water stops at the lip; the curtain's top
  sheet carries it over the slope (terrain cells are ~4.2 m, so the face is a slope, not a wall). MeadowLake has a
  lobe (two points at x 334) that brings the lake to the foot of the fall, so the plunge pool is lake water.
- **Translucent meshes must be entities, not foliage.** As a foliage instance `SM_Waterfall2` draws only as white
  fragments (the same mesh renders correctly as an entity, as in Korash). An engine bug in the foliage path, not fixed
  here.
- **River banks rise above the water (2026-11-03, after the user saw stepped river edges).** Water is per terrain quad
  (~4.2 m), decided at the quad centre, so wherever the ground at the mask's edge was below the surface the water
  ended on quad edges. 58 of 105 edge vertices of the MeadowStream were below it (up to 1.4 m: the stream runs across
  the terrace's slope, and the upstream end had no water past it but a lowered bed). Now the bank climbs from the
  bed to max(ground, surface + 0.25) over `bank`, that top holds to the end of the water (width + bank +
  `waterMargin`, >= a quad's half diagonal) and fades back over another `bank`; low ground gets a small levee. The
  visible shoreline is where the bank crosses the surface. The upstream end closes the same way by the distance to
  the bed's end edge; the downstream end stays flat (it flows into a lake or a fall). 4 edge vertices remain within
  0.24 m (the region fade and the fall's section own the ground there). Capture: `pcg-graphs/phase3b-river-banks.png`.
- **Rebuild speed (2026-11-03, the user found rebuilds very slow).** FalwynValley took 21.4 s in the Debug editor:
  14.6 s splat pass (4.4 M pixels, 13.9 s of it SectionPath: ValleyWall projection and mass noise), 2 x 1.75 s
  `BuildNearestGrid` for the ValleyWall mass (every cell against every chunk, dry run and base run), 1.2 s page
  loading (CLI only) and ~0.8 s section asset loading per rebuild. Now: `pcg::Apply` takes one target per worker
  thread and evaluates the height and splat samples in parallel before writing them in the old order (output byte
  for byte the same; the editor gives each worker a copy of the warmed texture layer cache and its own notes);
  `BuildNearestGrid` builds its rows in parallel (`pcg::ParallelFor`, plain threads: the editor and tools do not
  initialise the TaskSystem); rebuilds keep section assets loaded (`DropChanged` rereads changed files). 5.0 s in
  the CLI (Debug), of which ~2 s are the one-time page and asset loads an open editor does not repeat.
- **Background rebuilds and live rebuild (2026-11-03, user request: non-blocking rebuilds, rebuild on release).** A
  graph instance rebuild is a `GraphRebuildJob` in three steps. Start (main thread): refusals that need no evaluation,
  page loading, copies of the record, graph, placed controls and tile materials, then `BeginContentRebuild` takes the
  old contribution out of the terrain data (normals included) with the GPU updates deferred, so the world keeps showing
  the old build. Run (worker thread): evaluate on that base terrain, prepare the ops, `pcg::ComputeTerrainSamples` (only
  reads the terrain; section assets from the cache only, a miss is loaded on the main thread and Run repeats). Commit
  (main thread): the remaining refusals (put the old build back exactly), ocean materialisation (then Run again on the
  new base), `WriteTerrainSamples` (refuses if a base height changed), one GPU update, placements, flow, seams. The
  synchronous rebuild (CLI, Rebuild All) runs the same steps inline, so both write the same bytes (checked on
  FalwynValley: sync, `--pcg-rebuild --background` and the committed data are identical). No dry run on the live
  terrain any more: everything is computed once, on the base. Safety rules: one job at a time; every other terrain
  write of the Procedural mode (Rebuild, Rebuild All, Delete, leaving the mode, Load) waits for it first; streaming only
  unloads tiles, never the arrays the job reads; the job never logs and never touches materials or the asset registry.
  Live rebuild (checkbox, off by default): a graph instance whose inputs hash changed (record without its content, graph
  revision, control library revision, the placed controls whose bounds may reach the area) is queued when no edit is in
  progress (no mouse button, drag, gizmo or active widget); a request for the instance being rebuilt cancels that job
  (old build back) and starts again. Turning it on counts the current state as built. Main thread cost per background
  rebuild (Debug): Start ~0.25 s (+ first page/asset loads), Commit ~0.7 s; the rest (~2-3 s) runs in the background.
