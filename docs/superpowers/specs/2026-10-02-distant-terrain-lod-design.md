# Distant terrain LOD — design

Date: 2026-10-02. Status: approved by the user in chat ("Ja, passt so, leg los").

## Problem

Map 0 is now a 26 × 17.5 km continent, but the client only streams the 3 × 3 pages around the
camera, and the player camera's far clip is the `Camera` default of 1000 units. The world ends a
few hundred metres away. Loading more full pages is not an option: a page is a 4.4 MB `.tile`
with 256 collision tiles.

## Decisions (user)

- Distant pages come from **baked LOD data**, not from the live `.tile` files.
- Default reach **9 × 9 pages** (radius 4, ≈ 2.1–2.4 km), configurable.

## Data — baked per page, next to the `.tile`

- `Worlds/<map>/<map>/Terrain/<x>_<z>.tlod` (`terrain_io/page_lod.h`): MVER, LHGT (33 × 33
  page-local heights — every 4th outer vertex, so borders coincide with the full page and with
  neighbouring LOD pages), LNRM (33 × 33 SNorm8 normals, mean of the full-resolution normals within
  half a LOD cell; on the page border only along the border, whose vertices the neighbouring page
  shares, so the lighting does not crease at page seams). A sample touching a water quad is raised to the water surface (never lowered)
  with an up normal, so distant seas show a surface, not their floor. Water presence is the quad
  mask, never the height.
- `<x>_<z>_lod.htex`: 256 × 256 DXT1 with mips, sRGB-encoded **unlit** base colour (G-buffer
  albedo + emissive of a top-down orthographic render of the page; emissive catches the unlit
  minimap water material). The far mesh is lit by the deferred renderer like everything else, so
  sun, time of day, fog and atmosphere apply. Terrain and water only — no WMOs, doodads or foliage.

## Bake (mmo_edit)

- "Generate Terrain LOD" next to "Generate Minimaps" in the world editor viewport.
- Visits every page that has a `.tile`; skips pages whose `.tlod`/`_lod.htex` are newer than the
  `.tile` (staleness by file time). Loads a page synchronously if it is not loaded, bakes, and
  unloads only pages it loaded itself (never pages with unsaved changes).
- Render: a dedicated 256² `DeferredRenderer`, orthographic top-down camera, everything but the
  terrain hidden, minimap water mode on. Albedo + emissive are read back from the RGBA16F G-buffer
  targets (readback had to learn RowPitch and 8-byte pixels), converted to sRGB8 and DXT1 (stb_dxt).
- Command line: `mmo_edit --bake-terrain-lod Worlds/<n>/<n>.hwld [--force]` opens the world, bakes all
  stale pages (all pages with `--force`) and exits with 1 if any page failed. Pages the editor's
  streaming is preparing are waited for (its dispatcher is pumped).
  Bakes right after opening (not from the paint handler, so it works with a hidden window —
  a *minimised* one fails to create its swap chain) and logs to `terrain_lod_bake.log` in the working directory (normally `bin/<cfg>`).
- Traps found while building it: the bake must `gx.Reset()` + `SetViewport` before rendering like the
  viewport does (otherwise the first, pre-viewport bake leaves the G-buffer empty); each page is rendered
  twice because fresh tiles create their index buffers in `PreRender`, after the scene captured their
  render operation; DeferredRenderer's shadow camera names had to become per-instance.

## Runtime (client)

- `terrain::FarTerrain` owns one `FarPage` (MovableObject + Renderable, TileVertex layout, shared
  static 16-bit index buffer, `MaterialInstance` with the page's albedo) per selected page. The
  material is the generic `Models/AlbedoNormal_Opaque_Base.hmat` with the flat
  `Textures/Character/BaseFlattenNormalMap.htex` as normal map, so no new HMAT had to be compiled. Render queue `TerrainGeometry`, no shadow casting, no collision,
  query flags 0.
- Selection (`terrain/far_terrain_selection.h`): pages within `radius` on both axes whose nearest
  point is within `radius × PageSize`, i.e. a round area; a page whose full-resolution version is
  loaded (`Page::IsLoaded`) is hidden.
- Streaming: `.tlod` parsed on the streaming thread, buffers, texture and material created on the
  main thread via the dispatcher. Pages that leave the area are destroyed.
- Hand-over: on the client (batch rendering) tiles of a page that is still loading are excluded
  from rendering; the batches appear when the page is complete, in the same frame the far page
  hides. No holes, no z-fighting during the load.
- Seams: 25 unit skirts.
- Cvar `TerrainFarRadius` (0 = off, default 4). While at least one stand-in is streamed in, the
  player camera's far clip becomes `(radius + 1.5) × PageSize` (a selected page starts within
  `radius` pages, so its far corner reaches further); otherwise — terrain-less maps, maps without
  baked data — it stays 1000. Shadow cascades are already capped separately (`maxShadowDistance`).
  The froxel fog's `SkyDistance` (2000) follows the far clip so distant terrain is never fogged more
  than the sky behind it. The editor viewport keeps its own camera, so its sky fog (2000 m) no longer
  matches the client's exactly when previewing zone environments.

## Out of scope

Distant WMOs/doodads/trees, distant terrain in the editor viewport, several LOD levels, horizon
fade (only if the fogged edge proves visible).

## Testing

- `terrain_io_tests [page_lod]`: grid, sampling, water lift rules, normal filtering, round trip,
  truncated data.
- `terrain_tests [far_terrain]`: mesh extent, skirt, winding, round selection, map bounds.
- `scene_graph_tests`: readback of a float render target is not unit-testable headless (null
  device); verified by the bake output.
- Manual: bake map 0, run the client far from the centre, compare horizon and seams.

## Related fix shipped on the same branch

Falling through the ground at page borders far from the centre: octree AABB/ray queries tested
tight octant boxes in a loose octree (`5c7f6ec6`).
