# Terrain Brush Raycast Precision — Design

Date: 2026-08-16
Status: Approved

## Problem

Terrain mode in the world editor picks the wrong point under the cursor. Symptoms reported:

- The brush fails to land on inner terrain vertices.
- Zoomed out, the brush aim moves in coarse jumps and can teleport to a point far from the cursor.
- Smooth, continuous painting from a high camera is effectively impossible.

## Root Causes

### 1. The picking raycast approximates the surface (`Terrain::RayIntersects`)

`src/shared/terrain/terrain.cpp` runs a coarse pass that steps the outer-vertex grid by
`coarse_step = 4`, representing a **16.67 x 16.67** world-unit block with a single
four-triangle fan built from that block's four corner heights. The real cell size is
**4.1667** units (`PageSize / (OuterVerticesPerPageSide - 1)` = `533.333 / 128`).

Consequences:

- The proxy surface deviates far from the real one, so the ray misses it on terrain that
  clearly is hit, or hits it inside the wrong block.
- Only the single nearest coarse hit is refined, and only within +/- 4 cells of it. When the
  proxy hit is in the wrong block the true hit falls outside the refinement window, so the
  result is either no hit at all or a hit far from the cursor. This is the observed
  teleporting.
- A grazing ray (a far camera) crosses many coarse blocks, which is where the proxy error is
  largest. The failure therefore worsens exactly as the user zooms out.
- The `vx + coarse_step >= OuterVerticesPerPageSide -> continue` guard silently drops the
  last up-to-4-cell band of every page, so a strip of terrain is never testable.

### 2. The raycast fabricates the inner vertex height

Inner vertices are real, independently stored, independently deformed heights:

- The rendered mesh reads the stored value (`tile.cpp` -> `Page::GetInnerHeightAt`).
- `Terrain::Deform` moves inner vertices with their own brush factor
  (`SetInnerHeightAt(ix, iz, GetInnerHeightAt(ix, iz) + heightScale * factor)`).
- `Page::m_innerHeightmapFromFile` marks pages whose inner heights came from the IVCM chunk
  rather than being derived.

Both raycasts nevertheless synthesise the fan centre as `(h00 + h10 + h01 + h11) * 0.25`:
`terrain.cpp` (picking) and `tile.cpp` (collision). The surface that is picked is therefore
not the surface that is drawn, everywhere the inner vertex differs from the corner average.
A near-grazing ray converts that small vertical error into a large horizontal one, which is
the second reason precision collapses as the camera pulls back.

### 3. `Terrain::GetAt` dereferences a possibly-null page

`GetAt` calls `GetPage(pageX, pageY)` and immediately dereferences the result with no null
check. `GetPageAndLocalVertex` clamps the page index to 63, which stays inside the page grid
only while the terrain is the full 64 pages wide, as both the editor and the client
construct it today. On any smaller terrain a valid vertex index resolves to a page past the
end and `GetPage` returns null. Latent rather than live, but the new traversal walks the
whole grid, so the trap is worth removing. (Residency is already handled: `Page::GetHeightAt`
returns 0 when the page is not prepared.)

### 3b. Every viewport pick is offset by the window's title bar

`ViewportPanel::Draw` renders the 3-D image at `GetWindowPos() + GetWindowContentRegionMin()`,
but `HandleViewportInteractions` stored `m_lastContentRectMin = GetWindowPos()`. Every picking
ray in the world editor is built as
`(mousePos - m_lastContentRectMin) / m_lastAvailViewportSize`, so all of them were offset by
the content-region inset — the title bar plus padding, roughly 27 px vertically.

This is a constant screen-space error, so it reads as slight mis-aiming up close and as a
large world-space error from a far camera, where those same pixels cover far more ground. It
affected terrain, foliage, entity, spawn and area-trigger picking alike.

### 4. The editor keeps painting at a stale position

`TerrainEditMode::OnMouseHold` applies the brush at `m_brushPosition` every frame without
consulting `m_brushPositionValid`. A missed raycast during a stroke keeps hammering the last
good position instead of pausing.

### 5. The stroke is a sequence of points, not a line

The brush is applied once per frame at a single point. A fast drag, or any drag from a far
camera where a small mouse movement covers a large world distance, leaves spaced blobs
rather than a continuous stroke.

Substepping — stamping the brush N times along the segment — only moves the problem: N has to
be capped, because every application rebuilds the tile meshes or coverage under its footprint,
and any fixed cap breaks above some cursor speed. From a far camera a frame can cover hundreds
of world units, so a capped substep count paints a short streak and then a gap, which is what
the aliased stroke in the reported screenshot actually is.

## Approaches Considered

**A. Exact DDA grid walk (chosen).** The outer-vertex lattice is uniform
(`worldX = gvx * cellSize - worldCentre`), so world-to-grid inverts in closed form. Walk
exactly the cells the ray crosses, front to back, test the real four-triangle fan using the
stored inner height, and stop at the first hit. Exact at any distance and angle. Cost is
proportional to the cells actually crossed, and a per-cell height-range reject makes most of
those nearly free, so it is also faster than the current 32x32 coarse scan per candidate
page.

**B. Keep coarse/refine, add a min/max height pyramid.** Make the coarse level a
conservative bound instead of a fake surface and refine every candidate in order. Correct,
but it needs a pyramid maintained and invalidated on every deform, stamp, undo and page
load. More persistent state, more ways to go stale, no better result.

**C. GPU depth readback.** Unproject the depth buffer at the cursor. Pixel-exact and O(1),
but adds a frame of latency, needs a readback path, and returns whatever is at the pixel
(a tree, a rock) rather than the terrain.

A was chosen because it removes the approximation rather than tightening it, introduces no
persistent state to invalidate, and is testable headless.

## Design

### 1. `src/shared/terrain/terrain_raycast.h`

New header-only helper depending only on `math` and `base`, matching the existing
`terrain_region_math.h` pattern so it is testable in the headless `terrain_tests` suite.

```cpp
struct GridRaycastParams
{
    float cellSize;
    float originX;      // world X of global outer vertex 0
    float originZ;      // world Z of global outer vertex 0
    int32 cellCountX;
    int32 cellCountZ;
};

struct GridRaycastResult
{
    bool hit = false;
    float distance = 0.0f;
    Vector3 position = Vector3::Zero;
    int32 cellX = 0;
    int32 cellZ = 0;
};

/// sampler(cx, cz, float (&corners)[4], float& innerHeight) -> bool
/// Returns false when the cell is not testable (page not resident).
/// corners are ordered (x,z), (x+1,z), (x,z+1), (x+1,z+1).
template <typename CellSampler>
GridRaycastResult RaycastHeightGrid(const Ray& ray, const GridRaycastParams& params,
                                    CellSampler&& sampler);
```

The sampler is a template parameter, not a `std::function`, so the per-cell call inlines.

Algorithm:

1. Clip the ray to the grid rectangle in XZ, producing `[tEnter, tExit]` clamped to
   `[0, ray length]`. An empty interval is a miss.
2. Seed an Amanatides-Woo traversal at the entry cell: `stepX/stepZ`, `tMaxX/tMaxZ`,
   `tDeltaX/tDeltaZ`, with the axis-parallel cases (`dir.x == 0` or `dir.z == 0`) handled by
   an infinite `tMax`/`tDelta`.
3. For each visited cell, the ray's parametric span inside it is
   `[tCell, min(tMaxX, tMaxZ, tExit)]`. Compute the ray Y at both ends. If that Y span lies
   entirely above `max(corners, inner)` or entirely below `min(corners, inner)`, skip the
   cell without touching a triangle.
4. Otherwise build the fan (four corners plus the stored centre) and test the four
   triangles, keeping the nearest. Because traversal is front to back and a cell's triangles
   are confined to that cell's XZ extent, the first cell that yields a hit is final — return
   immediately.
5. Bound the loop at `2 * (cellCountX + cellCountZ)` iterations as a guard against a
   degenerate direction.

Triangle intersection reuses the same Moller-Trumbore form already in `Terrain`, moved into
this header as a free function so the header has no dependency on `Terrain`.

### 2. `Terrain::RayIntersects` becomes an adapter

The entire coarse/refine block is deleted, along with its `O(width * height)` reverse search
for a page's own index. The sampler subsumes the page broad phase:

- It caches the resolved page, since 128 consecutive cells along an axis share one.
- On each page change it evaluates residency *and* `ray.IntersectsAABB(page bounding box)`,
  so a page the ray misses rejects all 128 of its cells without a single height fetch. This
  keeps the broad phase the old implementation ran up front, just folded into the walk.
- Heights are read **page-local** (`Page::GetHeightAt` with local indices), not through the
  global vertex grid. A page stores 129 vertices per side, so a cell's far corners at local
  index + 1 still land inside it. Going through the global grid would instead resolve a seam
  vertex to the *next* page, which returns 0 when that page is not resident and presents the
  walk with a cliff down to y=0 that is not there. Page-local reads are also what the
  renderer does, so the surface tested is the surface drawn.
- It reads the fan centre from `GetInnerHeightAt`, so picking matches rendering.

`hitPoint != Vector3::Zero` as a success test is replaced by the explicit result flag. The
page and tile of the hit are derived from the hit cell index rather than from a world-position
round trip, guarded by a `static_assert` that the tile and page cell counts agree.

Callers are unchanged: `entity_edit_mode`, `foliage_edit_mode`, `selection_raycaster` and
`world_editor_instance` all keep the same signature and all benefit.

**Behaviour change — ray length is now honoured.** The old `RayTriangleIntersection` checked
only `t > epsilon` and ignored the ray's length entirely, so picking had unlimited reach. The
walk clips to `ray.GetLength()`, which is more correct but would make a shallow pick from a
far camera newly miss on the editor's 10000-unit picking rays. The two brushes that are
painted from arbitrary camera distances — terrain (`WorldEditorInstance::OnTerrainMouseMoved`)
and foliage — therefore cast 50000 units, spanning the world diagonal. The click-to-place
tools keep their 10000.

### 3. `Terrain::GetAt` null guard

Return `0.0f` when `GetPage` yields null instead of dereferencing.

### 4. Tile collision centre height

Both `Tile::TestRayCollision` and `Tile::TestCapsuleCollision` use the stored inner vertex
height instead of the corner average, so client-side collision agrees with what is drawn.
Fixing only the ray path would have been worse than fixing neither: the capsule path is the
one that actually carries the player, and leaving it averaging would put the client's two
collision paths in disagreement with each other.

The capsule path additionally builds its per-cell rejection box from the four corners only. A
sculpted spike or pit at the inner vertex reaches past every corner, so that box culls the
cell before its triangles are tested; the box now spans the inner vertex too.

### 5. Editor stroke stability

**The viewport picking origin.** `m_lastContentRectMin` becomes
`GetWindowPos() + GetWindowContentRegionMin()`, matching where `Draw` places the image. This
alone removes the constant aiming offset from every viewport raycast in the world editor.

**A swept brush instead of a stamped one.** `BrushStroke` (new
`src/shared/terrain/brush_stroke.h`, header-only, float math only) describes a footprint swept
from one world position to another, with `DistanceTo` returning the distance to the segment
rather than to a point. `TerrainVertexBrush` and `TerrainPixelBrush` take a stroke, derive
their index range from the segment's bounds expanded by the outer radius, and weigh each
vertex or pixel by its distance to the segment. A stationary brush is the degenerate
zero-length case, for which the swept distance is identical to the old radial distance, so
every non-stroke caller — `Stamp`, the region operations, the water tools — is unaffected.

The public operations `Deform`, `Smooth`, `Flatten`, `ApplyNoise`, `Paint`, `Color` and
`PaintHoles` take a `BrushStroke` in place of a brush centre. `TerrainEditMode::OnMouseHold`
builds one stroke per frame from the previous applied position to the current brush position
and applies each operation exactly once. Coverage is then gap-free at any cursor speed, the
cost is the swept area rather than the area of every sample along it, and each frame issues a
single tile update.

Two operations cannot be swept and keep a capped stamp walk instead:

- **Masked painting.** A brush mask anchors its UVs to one footprint, so sweeping it would
  smear the pattern along the stroke rather than repeating it. Its stamps divide the frame's
  strength between them.
- **Area IDs**, which are set per tile rather than through a falloff, so the segment is walked
  at half-tile spacing.

Note the strength semantics this settles: a swept application deposits a full frame's worth of
`power * deltaSeconds` along the whole segment, so a stroke has the same strength however fast
it is drawn. The substep approach divided that strength between the substeps, which made a fast
drag deposit a fainter line — the opposite of what a paint tool should do.

**The remaining stroke-state fixes:**

- `OnMouseHold` returns early when `!m_brushPositionValid`, so a missed ray pauses the stroke
  rather than deforming the stale position for another frame.
- `WorldEditMode::OnStrokeInterrupted`, called by `WorldEditorInstance` on any frame where it
  does not call `OnMouseHold`. Leaving the viewport with the button held stops `OnMouseHold`
  without firing `OnMouseUp`, so re-entering elsewhere would otherwise look identical to the
  cursor having dragged between the two points, and the sweep would paint a line the user
  never travelled.
- The two modifier-held sampling operations (ctrl-Flatten's height picker, alt-Area's
  eyedropper) run once per frame outside the stroke path; they are not strokes.
- The tracked position resets on mouse up and when a stroke begins, so the first frame of a
  stroke sweeps a zero-length segment.

### 6. Tests

New `src/tests/terrain_tests/test_terrain_raycast.cpp`, headless, driving
`RaycastHeightGrid` over a synthetic height grid:

- Sub-cell precision: the hit point lies on the analytic fan surface.
- An inner vertex displaced off the corner average is hit at its real height, both raised and
  depressed (regression for root cause 2).
- A ridge pattern that a 4x4 corner proxy would miss is hit (regression for root cause 1).
- A hit in the final cell strip of the grid (regression for the dropped edge band).
- Front-to-back ordering: a ray crossing a near ridge and a far one hits the near ridge.
- Grazing rays at ~85 degrees from far away resolve to the correct cell.
- Traversal in -X and in -Z, and along Z with `dir.x == 0`. These cover the other half of the
  DDA seeding, which picks a cell's lower border rather than its upper one when the step is
  negative, and the axis-parallel sentinel path.
- A grid with a large negative origin (~-17066, as the real terrain has), so the
  world-to-grid conversion is exercised with a real offset rather than 0.
- A hit from below, since sculpting can put the camera under an overhanging lip.
- Cells the sampler rejects are passed through rather than terminating the walk.
- The ray's length is respected: a surface beyond it is not hit.
- Each crossed cell is visited exactly once (a revisited cell means a stuck traversal).
- Miss cases: ray above all terrain, ray pointing away, ray outside the grid, empty grid.

## Out of Scope

- **Hole awareness in picking.** The renderer and collision skip holes; the picking raycast
  does not, so the brush can still pick a surface through a hole. Deliberately deferred.
- **The nav mesh builder's fabricated centre vertices.** `nav_build/map.cpp` averages the four
  corners the same way the collision paths did. Correcting it is not a code-only change: every
  map's nav mesh would have to be rebuilt, so it needs to be scheduled as its own piece of
  work. Until then the server's nav mesh can disagree with client collision wherever inner
  vertices were sculpted independently — which `Deform` and `ApplyNoise` do, while `Smooth`,
  `Flatten` and the Coons patch re-derive them.
- **Batching tile rebuilds across substeps.** Each brush application ends in `UpdateTiles`
  over its footprint, so a 16-substep frame rebuilds the same tiles 16 times. The radius-based
  spacing keeps this bounded — a large brush gets few substeps — but a proper fix would thread
  an `updateTiles = false` flag through the public brush operations and issue one rebuild over
  the union bounds. That changes several public signatures and is left for later.
- Any change to how the brush overlay is drawn.
- The GPU readback picking path (approach C).

## Verification

`tools/gate/verify.ps1` via `/gate`: Debug build, unit tests including the new suite cases,
and E2E. Manual check in the editor: sculpt from a high, shallow camera and confirm the
brush tracks the cursor continuously and lands on the drawn surface.
