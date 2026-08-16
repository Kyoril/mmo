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
or `IsPrepared` check. Any raycast that reaches a non-resident page crashes the editor.

### 4. The editor keeps painting at a stale position

`TerrainEditMode::OnMouseHold` applies the brush at `m_brushPosition` every frame without
consulting `m_brushPositionValid`. A missed raycast during a stroke keeps hammering the last
good position instead of pausing.

### 5. The stroke is a sequence of points, not a line

The brush is applied once per frame at a single point. A fast drag, or any drag from a far
camera where a small mouse movement covers a large world distance, leaves spaced blobs
rather than a continuous stroke.

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

The entire coarse/refine block is deleted, along with the page broad phase and its
`O(width * height)` reverse search for a page's own index. The sampler subsumes both:

- It caches the last page pointer, since 128 consecutive cells along an axis share one page.
- It rejects non-resident or unprepared pages before fetching any height.
- It reads the fan centre from `GetInnerHeightAt`, so picking matches rendering.
- Corner heights come from `GetHeightAt` on global indices, which already resolves the
  vertices shared across a page seam.

`hitPoint != Vector3::Zero` as a success test is replaced by the explicit result flag. The
existing tile lookup for the returned `RayIntersectsResult` is kept.

Callers are unchanged: `entity_edit_mode`, `foliage_edit_mode`, `selection_raycaster` and
`world_editor_instance` all keep the same signature and all benefit.

### 3. `Terrain::GetAt` null guard

Return `0.0f` when `GetPage` yields null or an unprepared page, instead of dereferencing.

### 4. `Tile::RayIntersects` centre height

Use the stored inner vertex height instead of the corner average, so client-side collision
agrees with what is drawn. Same one-line class of fix as the picking path.

### 5. Editor stroke stability (`TerrainEditMode`)

- `OnMouseHold` returns early when `!m_brushPositionValid`, so a missed ray pauses the
  stroke rather than deforming the stale position for another frame.
- Stroke interpolation. Track the previously applied position. When the brush has moved
  further than `min(cellSize, outerRadius * 0.25)` since the last application, subdivide the
  segment (capped at 32 substeps) and apply along it.
  - Time-integrated operations (Sculpt, Smooth, Noise, Paint, VertexShading) divide
    `power * deltaSeconds` across the substeps, so total applied strength is unchanged and
    only the coverage becomes continuous.
  - Set-style operations (Holes, Area) apply their full effect at each substep, since they
    are idempotent. This also closes the gaps left by fast hole painting.
  - The tracked position resets on mouse up and when a stroke begins, so the first frame of
    a stroke applies a single step.

### 6. Tests

New `src/tests/terrain_tests/test_terrain_raycast.cpp`, headless, driving
`RaycastHeightGrid` over a synthetic height grid:

- Sub-cell precision: the hit point lies on the analytic fan surface.
- An inner vertex displaced off the corner average is hit at its real height (regression for
  root cause 2).
- A ridge pattern that a 4x4 corner proxy would miss is hit (regression for root cause 1).
- A hit in the final cell strip of the grid (regression for the dropped edge band).
- Front-to-back ordering: a ray crossing a near ridge and a far one hits the near ridge.
- Grazing rays at ~85 degrees from far away resolve to the correct cell.
- Cells the sampler rejects are passed through rather than terminating the walk.
- The ray's length is respected: a surface beyond it is not hit.
- Miss cases: ray above all terrain, ray pointing away, ray outside the grid.

## Out of Scope

- **Hole awareness in picking.** The renderer and collision skip holes; the picking raycast
  does not, so the brush can still pick a surface through a hole. Deliberately deferred.
- Any change to how the brush overlay is drawn.
- The GPU readback picking path (approach C).

## Verification

`tools/gate/verify.ps1` via `/gate`: Debug build, unit tests including the new suite cases,
and E2E. Manual check in the editor: sculpt from a high, shallow camera and confirm the
brush tracks the cursor continuously and lands on the drawn surface.
