# Terrain authoring pitfalls

## Water

- Water surfaces need a material or they render as wireframe (`Page::RebuildWaterMesh`
  falls back to `Editor/Wireframe.hmat`). The import default `Worlds/Water_Base.hmat`
  exists in data/client — keep it unless there's a reason not to.
- Import flags water quads purely by "terrain below waterLevel". Without an anti-flood
  clamp in the generator, every random floor dip becomes a pond. Clamp dry land to
  waterLevel + ~1.5 outside designed water corridors (see example-layout-zone.py).
- Fords: run the road across the river AFTER the carve — `flatten_along` pulls the
  crossing up into a shallow section automatically; verify in the preview that the
  water stays continuous there (bed must remain below the water level).

## Before the first import

- **The world must exist**: the user creates it once in the editor (File → New World).
  That writes `Worlds/{W}/{W}.hwld` with terrain enabled and a **default terrain
  material** (stored in the .hwld TERR chunk). Imported pages write empty per-tile
  material names, which fall back to that default — no default = black/invisible
  terrain. `terrain_tool import` warns when the .hwld is missing.
- **Full page rect only**: pages missing from the import rect stay blank (height 0).
  If your zone's border heights don't return to something sensible inside the image,
  players see a cliff into the void at the rect edge. Always enclose the zone
  (edge walls / falloff) within the image.

## After importing

- **The editor does not hot-reload pages**: if the world is open in mmo_edit while you
  import, close and reopen the world. Pages already streamed in as blank stay blank
  until then.
- **Rerun nav_builder** after terrain changes or server-side navigation still uses the
  old shape: `nav_builder --data data/client --world <W> --out <navdir>`.
- **nav_builder filename quirk**: it probes zero-padded page names (`05_05.tile`) while
  the engine writes non-padded ones (`5_5.tile`). Identical for pages ≥ 10 — keep zones
  in pages 10–54 (you should anyway; the world center is page 32).

## Format gotchas (for debugging, not for writing by hand)

- `.tile` files are chunked binary, format version 2. Chunk ids in code read reversed
  on disk: `MakeChunkMagic('REVM')` appears as ASCII `MVER` in a hex dump.
- Legacy v1 pages (273×273 grid) still exist in old worlds. `terrain_tool` rejects
  them ("Unsupported terrain page format version 0x01") — open and re-save the world
  in the editor to convert.
- The serialization single source of truth is `src/shared/terrain_io/` (used by both
  the editor's `Page::Save` and terrain_tool). If the format ever changes, the
  golden-bytes unit test in `src/unit_tests/test_terrain_page_io.cpp` catches drift.
- Normals stored in pages use the engine's exact math, including its scaling constant
  `PageSize/129` (not the true spacing `PageSize/128`) — replicated in
  `terrain_tool`'s `ComputeOuterNormal`. Don't "fix" this; shading would pop when the
  editor recomputes normals after a sculpt.

## Reading previews

- Both previews light from the **northwest**. If the terrain_tool preview looks like
  the negative of the Python preview (valleys reading as ridges), the normals in the
  pages are wrong — reimport; don't ship it.
- Dense contour packing (5-unit interval) = slopes players can't walk. The border
  walls should be the only place contours touch.
- `preview.py --diff source.png exported.png` after a round-trip: max error above
  ~2× the quantization step means something resampled when it shouldn't (image size
  not at the lossless resolution).
