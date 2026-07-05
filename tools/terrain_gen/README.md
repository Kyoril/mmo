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
