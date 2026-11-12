# PCG field control — design

Date: 2026-11-12
Branch: `feature/pcg-field-control`

## Intent

Falwyn Forest and Oakenshire are being rebuilt with PCG controls. Roads, rivers, lakes, forest areas, valley
walls and exclusion areas exist as control types; farm fields do not. Today a field (a rectangle of ploughed
ground, fenced, with bushes along the fence and a gap as the way in) has to be painted and dressed by hand.

The goal is a control the author drags out as a rectangle. Its area is painted with the field terrain layer, and
a hand-built edge section (fence, bushes, the soft transition from soil to grass) is laid around it — the same
idea as a road section, only around a rectangle instead of along a spline.

While this is being built, a second gap gets closed: a placed control can only be edited point by point. The
author wants to move, rotate and scale any control as a whole.

## What the author decided

- The field **lies on the terrain**. It does not level or reshape the ground.
- The edge is a **section** (authored in a section world like a road section), laid around the rectangle.
- The control is a **rectangle whose sides can be dragged**.
- **Gaps in the fence come from exclusion areas**, and this is a general rule: exclusion applies to every
  section, not only to fields.
- Furrows stay **world-aligned for now**. The field texture is a terrain layer, so on a rotated field the furrows
  run at an angle to the fence. Accepted; the author aligns fields to the axes where it matters.
- **Every control can be moved, rotated and scaled as a whole.**

Everything below that is not in this list is the designer's call and can be overruled.

## Goals

1. A `Field` control type for Falwyn Forest that produces a painted, fenced field from one rectangle.
2. A rectangle control shape, usable by any control type, edited by dragging its sides.
3. Sections honour exclusion areas (and road clearance) for their props.
4. Sections can be laid without changing terrain height.
5. Whole-control move / rotate / scale for every placed control shape.

## Non-goals

- Furrows that rotate with the field (needs a rotated texture projection in the terrain material).
- Crops or other props inside the field.
- Non-rectangular fields as a control shape. (The graph part works on any Area item, so a closed-loop field
  type could be added later as pure data.)
- Anything at runtime. This is editor and bake-time only: no client, server, protocol or material compiler
  change.

## Anti-goals

- A field-specific generator next to the graph system. The field must be a control type plus graph nodes, like
  `Road` and `Lake`.
- Changing what existing graphs build. A graph that does not use the new inputs and settings must produce
  byte-identical output (same placement keys, same terrain).
- Fence pieces that shift along the edge when an exclusion area is added. Cutting a gap must only remove pieces;
  every other piece keeps its position and key.

## Current state (what the builder builds on)

- Control types are data (`.hctl`, `src/shared/pcg/graph/controls.h`) with a `ControlShape`:
  `ClosedLoop`, `OpenSpline`, `Volume`, `Point`. Placed controls are `.hpcgc`.
- `GetControls` (`nodes_input.cpp`) turns controls into items: loops into Area items, a `Volume` already into an
  Area item with a closed `Linear` four-point spline (`VolumeFootprint`).
- `LaySectionAlongLoop` / `LaySectionAlongSpline` / `StampSectionAtPoints` (`nodes_section.cpp`) emit Terrain
  items carrying a `SectionPath` op; `terrain_compose.cpp` prepares them and calls
  `pcg::BuildSectionPlacements`. Closed paths, `floorInside`, corner variants and runs that close around a loop
  already work (`section_path.h`).
- `PaintSplat` paints an Area with a texture, weight and edge falloff.
- `Difference` (`nodes_filter.cpp`) removes points inside Areas and within `distance + clearance` of Splines.
  `Exclusion.hctl` is a closed loop tagged `exclusion`; `Road.hctl` is tagged `exclusion` and `road` and carries
  `clearance`.
- The editor side is `procedural_edit_mode.cpp` (spline point editing, `SelectedControlTransform` gizmo for
  Volume and Point controls) and `graph_instance_builder.cpp`.
- Existing content: `data/client/Editor/Pcg/Biomes/FalwynForest/` (`Controls/*.hctl`,
  `Graphs/FalwynValley.hpcgg`).
- Earlier designs: `docs/superpowers/specs/2026-10-25-pcg-graphs-design.md`,
  `2026-10-25-pcg-section-templates-design.md`.

## Design

### 1. Rectangle control shape

A new `ControlShape::Rectangle` (`"rectangle"` in `.hctl`).

- **Storage:** reuses the instance's `position`, `rotation` and `halfExtents`. Only the yaw of the rotation and
  `halfExtents.x` / `.z` matter; no new fields in `.hpcgc`. `ResolveControl` accepts it like a Volume.
- **In the graph:** `GetControls` emits an Area item with a closed `Linear` spline of the four corners, in a
  fixed order and winding, with fixed point ids 1..4. The ids must not depend on size or rotation, so section
  span keys (and with them placement keys) stay stable when a side is dragged. All four corners carry
  `position.y` (ground-snapped at placement). `GetControls` must not gain a terrain dependency for this: it
  would re-run every `GetControls` node on any terrain change, and the field branch (`keepGround`) never reads
  the corner heights.
- **Difference from Volume:** no height (`volumeHeight` attribute is not written), and its own handles in the
  editor. A Volume stays what it is.
- **Editing (designer's call on the details):**
  - placed with one click at the centre, like a Volume, with a default size (20 m x 20 m);
  - one handle in the middle of each side: dragging moves that side along its normal, the opposite side stays
    where it is (centre and half extent change together);
  - one handle per corner: moves both adjacent sides, the opposite corner stays;
  - a minimum size of 1 m per axis;
  - the outline is drawn on the ground like loop controls;
  - move / rotate / scale as a whole through part 5.
- The control type editor (`pcg_control_type_editor.cpp`) offers the new shape.
- `ControlsHash` already covers shape, transform and half extents; verify the new shape is included.

### 2. Exclusion for sections

All three section nodes get an optional input **`Exclude`** (Areas and Splines), with the settings `distance`
and `clearanceAttribute` and the exact inside / near test of `Difference`: inside any Area (even-odd), or
within `distance + clearance` of any Spline.

- A **single placement** of a section copy is dropped if its final position (after jitter) is excluded.
- A **run piece** (fence board) is dropped if the midpoint of its footprint is excluded. The run is laid out
  exactly as without exclusion and pieces are removed afterwards, so the remaining pieces keep position and key.
- Terrain (height and paint) of the section is **not** cut. Only props.
- The exclusion shapes travel with the Terrain item to the terrain stage, where the placements are built. How
  they are carried (on the item, on the op) is the builder's choice; they must be part of the item's hash so a
  moved exclusion area triggers a rebuild.
- With nothing connected to `Exclude` the nodes behave exactly as today.

A road crossing a field's fence therefore opens the fence over the road's clearance without any extra control.

### 3. Sections that leave the height alone

A new `SectionHeightMode` value **`keepGround`** (`"keepGround"` for `SetSectionPathParam`).

- The section contributes no height: `heightWeight` is 0 everywhere. Its variants' height grids are ignored.
- Paint works as today.
- Props stand on the final ground at their position plus their baked `heightAboveTerrain`.
- The mass layer is off in this mode (a mass without a centre line height makes no sense); `massEnabled` with
  `keepGround` is a node error, not a silent ignore.
- If the legacy section path record (`SectionPathRecord`, PCGP) stores the height mode as a number, the new
  value is appended so old files read unchanged. Whether the legacy section path panel offers the new mode is
  the builder's call.

### 4. The Field control type and the graph

**`Controls/Field.hctl`** — shape `rectangle`, default tags `field` and `exclusion` (so forest does not grow in
a field), one property group:

| Property | Type | Default | Meaning |
|---|---|---|---|
| `section` | asset | `Editor/Pcg/Sections/FieldEdge.hsec` | Edge section laid around the field |
| `tileMaterial` | asset | `Models/Terrain/Briarwatch_Farmland_Plowed.hmi` | Tile material that carries the soil layer (see "Soil layer on the tiles") |
| `texture` | asset | `Textures/Tilesets/T_Field_Plowed_C.htex` | Terrain layer the area is painted with |
| `falloff` | float | 1.5 | Distance (m) over which the soil fades out at the edge |

The property is named `falloff` because `PaintSplat` takes per-item overrides only from an attribute with the
setting's own name. There is deliberately no `propDensity` property: a run gets one keep roll per chain
(`keepChance * propDensity`), and on a closed loop the fence is a single chain, so any value below 1 would make
a field's whole fence vanish at random. Bush density is authored in the section's prop rules.

**`Graphs/FalwynValley.hpcgg`** gains a field branch:

```
Get Controls(field) ──┬─> Paint Splat (texture, falloff from the control)
                      └─> Lay Section Along Loop (section from the control,
                              heightMode keepGround, floorInside true)
                                  ^ Exclude
Get Controls(exclusion) ─> Filter by Tag (excludeTags: field) ─┘
```

- The filter matters: fields are tagged `exclusion` themselves, and a field must not cut its own fence (nor a
  neighbouring field's).
- The paint is stacked before the section, so the section's own edge paint lies on top of the soil.
- Existing branches that subtract `exclusion` controls (forest scatter) pick fields up automatically.

**Soil layer on the tiles (author's decision: the field provides the layer itself).** Terrain tiles store one
material name per tile and four splat weights; the layer textures live in the material. `PaintSplat` paints only
where the tile's material has the texture as one of its four layers, otherwise it paints nothing and notes a
missing layer. The existing convention for farmland is a tile material instance of the same base material with
one layer slot swapped: `Models/Terrain/Briarwatch_Farmland_Plowed.hmi` (also `_Sprouts`, `_Stubble`) is
`Oakenshire_BoarTerrain.hmat` with the `T_GrassTerrain_01_01` slot replaced by `T_Field_Plowed`. Only tiles
`30_32` and `30_33` of `Worlds/Development` use it today.

`PaintSplat` therefore gets an optional setting **`tileMaterial`** (String, overridable per item like every
other setting; empty = today's behaviour):

- Every tile on which the op paints with a weight above 0 is switched to that material, through the existing
  tile material channel (`TerrainTarget::setMaterial`, `TerrainContribution::materials`), so removing or moving
  the field restores the previous material.
- The texture lookup for the paint runs against the new material.
- A tile has one material, so the switch affects the whole tile: whatever was painted in the swapped slot
  elsewhere on that tile now shows soil. The build reports (as a warning on the instance, not an error) every
  switched tile where a layer slot whose texture differs between old and new material has weight outside the
  painted area.
- Two paint ops asking for different materials on one tile cannot both win (for example a Plowed and a Sprouts
  field sharing a tile). The op stacked later wins; the other paints nothing there and the build reports it.
- A tile that already has the requested material is not touched.

**Existing section nodes.** Because exclusion is a general rule, the road and the valley wall section nodes of
`FalwynValley.hpcgg` get `Exclude` wired as well, each with a self-filter: the road node receives the
`exclusion` controls without those tagged `road` or `field` (a road is tagged `exclusion` itself and its
clearance is wider than its section, so unfiltered it would strip its own props; two crossing roads do not cut
each other in this step). The valley wall receives the `exclusion` controls without `field`. The waterfall
stamp is left alone. This changes what existing Falwyn content builds only where an Exclusion area overlaps a
road or wall, which is the intended effect.

**`Editor/Pcg/Sections/FieldEdge.hsec`** (next to `TestRoad.hsec`) — a section authored in a section world with the floor side towards the field:
a fence run, bushes on the outside, and a paint transition. It should have an `InnerCorner` variant so the fence
meets cleanly at the four 90° corners; without one the section bends around the corner, which works but leaves
the corner to chance. The builder makes a first version from the fence and bush meshes of the existing
hand-built field (the one on tiles `30_32` / `30_33` of `Worlds/Development`, the only tiles using the farmland
material); the author refines the look in the section world afterwards. The existing hand-built
field in the Falwyn Forest world stays untouched — replacing it is the author's step.

### 5. Whole-control transform

Every placed control can be moved, rotated and scaled as a whole with the editor's transform gizmo.

- **Volume, Point, Rectangle:** the gizmo acts on `position` / `rotation` / `halfExtents` (Volume and Point
  have this today; Rectangle joins them; scale changes the half extents).
- **Closed loop and open spline:** a "whole control" selection next to the existing point selection. The pivot
  is the centroid of the control points (x/z). Move translates every point; rotate turns the points about the
  pivot around world up and turns Catmull-Rom tangent handles with them; scale scales point offsets from the
  pivot in x/z (and tangent handles accordingly). Point heights follow whatever a single dragged point does
  today (ground snap or kept height) — the same rule, applied to all points.
- Point ids never change under a whole-control transform.
- One whole-control drag is one undoable step if the mode has undo for point edits, and triggers one rebuild
  (live rebuild behaves as for a point drag).
- How the author switches between point editing and whole-control editing (a toggle in the details panel, a
  click on the outline versus on a point, a hotkey) is the builder's call; it must be discoverable in the
  panel and must not change how single points are selected today.
- Scope is the placed controls of the graph system (`.hpcgc`). The legacy controls of the same mode (oasis,
  legacy section paths) are included only if they share the code path at no extra cost.

## Constraints

- Code style, error handling and file conventions per `CLAUDE.md`. Editor tooling stays in C++ (`mmo_edit`).
- The shared `pcg` library must keep building on the headless Linux server build; only `mmo_edit` code may use
  graphics or ImGui.
- Determinism: the same controls and graph produce the same bytes. No iteration over unordered containers in
  anything that reaches output or hashes.
- No temporary scripts in `tools/`.

## Verification

- `pcg_tests`: `.hctl` / `.hpcgc` round trip of the rectangle shape; `GetControls` on a rectangle (corner order,
  ids stable under resize and rotation); section placements with exclusion (pieces removed, the others
  identical to the run without exclusion, by key and position); `keepGround` (no height contribution, props on
  the ground); `PaintSplat` with `tileMaterial` (tile switched and restored on unapply, conflict and
  side-effect reports); the existing acceptance tests unchanged.
- An acceptance test for the field branch in the style of `test_graph_acceptance.cpp`: a field, an exclusion
  area on one edge, a road through another edge.
- In the editor on the Falwyn Forest world: place a field, drag sides and corners, rotate / move / scale it as
  a whole, do the same with a road and a forest area, cut a gate with an exclusion area. A screenshot of the
  built field goes back to the author.
- Fast gate (`/gate`) green before `/ship`.
- Authoring note with the capture and the author's checklist: [docs/pcg-fields.md](../../pcg-fields.md).

## Left to the builder

- How exclusion shapes are carried to the terrain stage.
- Handle look, hit sizes and the point / whole-control switch.
- Whether `keepGround` appears in the legacy section path panel.
- File layout of new editor code inside `world_editor/`.
