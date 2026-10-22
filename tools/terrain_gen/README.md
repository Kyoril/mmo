# terrain_gen — procedural zone heightmap toolkit

Generates whole-zone heightmaps as 16-bit grayscale PNGs (plus a JSON metadata
sidecar) which `terrain_tool import` converts into engine terrain pages
(`.tile` files). Companion of the `terrain-author` Claude skill.

## Requirements

```
pip install -r requirements.txt   # numpy + Pillow
```

## Workflow

```
# 1. Generate from a recipe (or write a custom script using terrain_lib)
python generate_heightmap.py my_zone_recipe.json --out zone.png

# 2. Inspect before touching engine data
python preview.py zone.png --contours 5 --water-level 98

# 3. Import into the game (terrain_tool is built with -DMMO_BUILD_TOOLS=ON)
../../bin/Release/terrain_tool.exe import --data ../../data/client --heightmap zone.png --meta zone.json

# 4. Verify what actually landed in the .tile files
../../bin/Release/terrain_tool.exe preview --data ../../data/client --world MyZone --pages 30,30,33,33 --out imported.png
../../bin/Release/terrain_tool.exe export  --data ../../data/client --world MyZone --pages 30,30,33,33 --out exported.png
python preview.py --diff zone.png exported.png
```

## Conventions

- Heights are world units, Y-up. Image column → +X (east), image row → +Z (south).
- One terrain page is 533.33 × 533.33 world units; the world is a 64×64 page
  grid centered at page (32,32): `worldX = (pageX − 32) · 533.33`.
- Lossless image resolution for an `nx × nz` page rect: `(nx·128+1) × (nz·128+1)`
  pixels (`terrain_lib.resolution_for_pages`). Other resolutions are resampled.
- The JSON sidecar carries the height range (`minY`/`maxY`) that pixel values
  0/65535 map to. 16-bit precision over a 200-unit range ≈ 3 mm steps.

See `generate_heightmap.py --help` for the recipe format and
`terrain_lib.py` for the full primitive library (fbm/ridged noise, domain warp,
edge walls, river carving, road flattening, plateaus, erosion, blur).

## Optional splat import

`terrain_tool import ... --splat layers.png` accepts an 8-bit RGBA PNG over the
same full page rectangle. Its exact resolution is `(nx*1008+1, nz*1008+1)`.
R/G/B/A store layers 0/1/2/3; alpha is a weight, not transparency. Each pixel
must sum to 255. Invalid dimensions, channel counts or sums fail before writing.
Shared page-edge pixels are copied from the same source column/row. Without
`--splat`, import retains the original all-layer-0 default. Import regenerates
whole pages; it is not a paint-only patch for existing terrain.

## Reshaping existing pages (delta import)

A plain import regenerates whole pages, so it wipes painted layers, holes, zones and water.
To reshape terrain that already exists, set `"applyAsDelta": true` in the metadata JSON:
the heightmap then holds height *offsets* (`minY`..`maxY`) that are added to the existing
pages, and everything else in them is kept. Every page of the rect must exist.

- Outer and inner heights move by the offset (inner vertices by the mean of their quad's
  corners, so sculpted inner detail survives). Normals are recomputed only where heights
  changed; untouched vertices stay bit for bit. Keep the offset zero at the rect border.
- `"waterLevel"`: reshaped quads get water exactly where they now dip below the level
  (and lose it where they no longer do). Quads the delta did not touch keep their water.
- `"setWaterMaterial": true` gives every written page with water the `waterMaterial`.
- `"clearWaterMask"`: 16-bit grey PNG with one pixel per water quad of the rect
  (`nx·128 × nz·128`); white quads lose their water.
- `"paint": [{"mask": "m.png", "layer": 1, "strength": 1.0}, ...]`: 16-bit grey masks at
  splat resolution (`nx·1008+1`) blended into the existing layer weights, in order.

- `"paint"` entries with `"mode": "remove"` move that layer's weight back to layer 0 instead.
- `"tileMaterials": {"mask": "tiles.png", "materials": ["Models/Terrain/X.hmi", ...]}`: 16-bit grey
  mask with one pixel per terrain tile (`nx·16 × nz·16`); value k > 0 gives the tile `materials[k-1]`.
  Use it for material variants that swap layer textures (e.g. `Briarwatch_Farmland_*.hmi`).
- `"waterFlow": {"path": [[x, z, halfWidth], ...], "uvScale": 0.0625, "falloff": 60, "minSpeed": 0.08}`:
  river flow, downstream order. Every written page with water gets the optional `WFLW` chunk: per
  water vertex a flow-aligned UV (u = distance downstream, v = across) and a speed that falls off
  beyond the channel. `Worlds/Water_River.hmat` (`tools/water_gen/build_ocean_material.py --variant
  river`) pans its normals and foam streaks along it. Pages without the chunk keep world UVs.
  The flow only shows on pages whose water material is `Water_River.hmat`.
  Easier: the world editor's Terrain → Water → Brush Mode **Flow** draws river paths in the viewport
  (click to add points, drag to move, Shift+wheel for width), shows the stored flow as arrows and
  writes the same chunk (and the river material) on its own. Its paths are kept in `<world>.hflow`
  next to the world file, in the same `[x, z, halfWidth]` format.

A zero image with only `paint`, `tileMaterials`, `waterFlow` or `clearWaterMask` is a data-only patch. Rebake the distant
terrain afterwards (`mmo_edit --bake-terrain-lod <world.hwld>`), rebuild the nav mesh, and move
placements (trees, props, spawns) by the same offsets.

To judge the result in context, `mmo_edit --capture-view <world.hwld> --view
"x,y,z,yaw,pitch,fov,time" --out shot.png [--size WxH] [--fog] [--grass] [--far-radius N]`
renders a viewpoint to PNG; the World Settings panel's View section copies the current one.
`mmo_edit --import-texture <asset folder> <image>... [--normal | --grayscale] [--compress] [--height-map]`
imports textures the same way the editor's import dialog does.

`collision_rock_poc.py` generates the single-page Collision rock study in
`generated/terrain/collision-rock-poc/`. Its README records assets, import and
review details. The script writes source images/instance JSON only; importing
the HMI and page is an explicit separate step.
