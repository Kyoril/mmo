---
name: terrain-author
description: Author, modify, and inspect 3D terrain zones (heightmaps) for this MMO's world. Use when creating a new zone or area terrain (like an Elwynn Forest or Westfall style zone), reshaping existing terrain at zone scale, importing or exporting heightmaps, or diagnosing terrain page (.tile) issues. Combines procedural heightmap generation (tools/terrain_gen Python), visual preview inspection, and the terrain_tool CLI for engine .tile page import/export.
---

# Terrain Author

Terrain zones are authored as **whole-zone heightmap images**, never as per-vertex edits.
The loop is: generate procedurally → render a preview PNG → **look at it with the Read
tool** → adjust parameters → repeat until the layout is right → import into the engine →
verify with a second preview rendered from the actual page files.

Terrain data lives in per-page binary files: `data/client/Worlds/{W}/{W}/Terrain/{x}_{z}.tile`.
Never write those bytes by hand — `terrain_tool` produces them via the same serialization
code (`src/shared/terrain_io/`) the editor uses.

## Workflow

1. **Plan the zone on paper first**: page rect, base height, landmarks (river course,
   town sites, roads) as normalized zone coordinates. See
   [references/zone-recipes.md](references/zone-recipes.md) for Elwynn/Westfall-style
   blueprints, [references/scale-cheatsheet.md](references/scale-cheatsheet.md) for
   world-unit scale intuition, and
   [references/measured-elwynn-dna.md](references/measured-elwynn-dna.md) for terrain
   character measured from real classic-WoW Elwynn heightmaps (roughness spectrum,
   slope stats, finger-ridge walls, and how to import reference heightmaps directly).
2. **Generate — layout-driven, not frame-driven**: for any real zone, write a custom
   Python script (start from
   [references/example-layout-zone.py](references/example-layout-zone.py)) that builds
   the zone from **organic mountain-mass blobs** (`mountain_mass` / `blob_mask`),
   pocket valleys carved out of the masses, `terrace` cliff shelves, `lake_basin` with
   islands, rivers at a constant `bed_level`, an anti-flood clamp outside water
   corridors, and roads flattened + slightly sunken. A rectangle-hugging `edgeWall`
   recipe (`generate_heightmap.py recipe.json`) is acceptable ONLY as a rough first
   draft — it reads as an unnatural frame and always fails visual review of a final
   zone.
3. **Preview and LOOK**:
   `python tools/terrain_gen/preview.py zone.png --contours 5 --water-level <Y>`
   then Read the resulting `_preview.png`. Check: enclosed borders, river continuity,
   plateau placement, slope harshness (dense contour packing = too steep to walk).
   Iterate on the recipe until it reads like a real zone.
4. **Import**:
   `bin/Release/terrain_tool.exe import --data <repo>/data/client --heightmap zone.png --meta zone.json`
   (build once with `cmake --build build --config Release -t terrain_tool`; requires
   `-DMMO_BUILD_TOOLS=ON` at configure time).
5. **Verify from the engine files**:
   `terrain_tool preview --data ... --world <W> --pages x0,z0,x1,z1 --out imported.png`
   — this shades using the normals **stored in the .tile files**; compare against the
   Python preview (same sun-from-northwest convention). Mismatched or inverted shading
   means something went wrong. Optionally round-trip:
   `terrain_tool export ... --out exported.png` then
   `python preview.py --diff zone.png exported.png` (max error should be ≤ one
   quantization step, typically < 0.01 units).
6. **Editor check**: have the user open the world in mmo_edit (or reopen it if it was
   open — streamed pages don't hot-reload). Read
   [references/pitfalls.md](references/pitfalls.md) BEFORE the first import.

## Command reference

```
terrain_tool import  --data <data/client> --heightmap zone.png --meta zone.json [--world <name>] [--material <asset.hmat>]
terrain_tool export  --data <data/client> --world <name> --pages x0,z0,x1,z1 --out out.png [--meta-out out.json]
terrain_tool preview --data <data/client> --world <name> --pages x0,z0,x1,z1 --out out.png [--contours <units>] [--scale <n>]

python generate_heightmap.py recipe.json [--out zone.png]     # recipe format: see file docstring
python preview.py zone.png [--contours N] [--water-level Y] [--scale N]
python preview.py --diff a.png b.png                          # prints max/mean error, writes heat map
```

The metadata JSON sidecar (written by the generator, consumed by import) is:
`{ "world", "pageRect": {x0,z0,x1,z1}, "minY", "maxY", "material" }` — pixel 0 maps to
minY, pixel 65535 to maxY. Optional keys:

- `"waterLevel"`, `"waterMaterial"`, `"waterType"` (1 Water, 2 Ocean): see Water below.
- `"zoneMap"`: a 16-bit PNG with one zone (area) id per terrain tile, sized (pages x · 16) × (pages z · 16). It is resolved relative to the meta file.
- `"pages": [[x, z], ...]`: write only these pages of the rect. Use it for irregular areas such as a continent, so that open-sea pages are never created.
- `"skipExistingPages": true`: never overwrite a page that already exists. Shape the new terrain down to the old pages' border heights so that the seams match.
- `"fillExistingZones": true`: with skipExistingPages, also give the unzoned tiles of existing v2 pages their zone. This rewrites those pages.

`tools/terrain_gen/alestia_blockout.py` uses all of them: it builds the whole map 0 continent from a sketch.

## Hard rules

- **Always import the full page rect the image covers.** A partial import creates cliffs
  against untouched neighbors. Shape zone borders *inside* the image (edge walls,
  coastal falloff) instead.
- Image orientation: column → +X (east), row → +Z (south), Y is up. Lossless resolution
  for an nx×nz page rect is `(nx·128+1) × (nz·128+1)` px — generate at exactly that.
- The world (.hwld) must exist before the terrain is usable. Either the user creates it
  in the editor, or write a minimal one programmatically: MVER(v3) + TERR(uint8 1 +
  uint16-length-prefixed default material, e.g. `Models/FalwynPlains_Terrain_Inst.hmi`)
  + empty MESH chunk — chunk magic bytes are the four chars of the code literal
  reversed ('MVER' → "REVM" on disk). Verified pattern in
  `artifacts/heightmap_references/analysis/` history (ElwynnDemo).
- **Water**: put `"waterLevel"` (and optionally `"waterMaterial"`, default
  `Worlds/Water_Base.hmat`) in the meta JSON — `save_zone(..., water_level=Y)` does it.
  Import flags every 1/8-tile quad whose terrain dips below the level and writes real
  MCWQ water chunks, so rivers and lakes are wet in-game. Design rule: carve all water
  bodies to constant bed levels below the water line, then apply an anti-flood clamp
  (softly lift terrain above waterLevel + ~1.5) everywhere OUTSIDE the designed water
  corridors so random floor dips don't become swamps.
- Phase 2 (not yet authorable): texture splatting and area IDs — imported terrain is
  single-material until then, so roads/rivers read via geometry only.
