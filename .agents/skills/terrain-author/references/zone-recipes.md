# Zone design recipes

Positions are normalized zone coordinates `[u, v]` (u → east, v → south).

**For final-quality zones use the layout-driven script approach**
([example-layout-zone.py](example-layout-zone.py)): mountain masses as blob unions
that bulge into the zone, pocket sub-valleys (Northshire pattern: ring of mountains
with `pocket_clear` subtracted from the mass, canyon mouth, plateau, brook), terrace
cliff shelves, lakes with islands, constant-bed rivers with an anti-flood clamp, and
flattened + slightly sunken (0.5–0.8 unit) roads. The JSON recipes below are for
quick drafts and parameter reference only — the rectangular `edgeWall` frame does not
survive visual review of a real zone.

**Mountains: always `mountain_mass_ref`** (relief borrowed from the real Elwynn massif
via `tools/terrain_gen/data/ref_mountain_relief.png` + patch quilting in
`synth_from_sample`). The purely procedural `mountain_mass` produces smooth artificial
ramp fronts — user-rejected. **Rivers: always meander** (`meander_path` +
`width_noise` + `bank_ragged_px`); straight constant-width channels read as canals.
Sprinkle `scatter_knolls` on foothills and plains for freestanding rock mounds. If
the relief sample ever produces an isolated alien dome, the sample crop leaked a
saturated cliff area — see extract_relief.py in artifacts/heightmap_references/analysis.

Further user-verified rules (feedback round 3):
- **Forest floor: quilt `ref_floor_relief`** (real Elwynn hillocks) at gain ~0.7 over a
  weak macro fbm — pure fbm floors read as artificial. Raise the base so floor lows
  stay ≥ ~1.5 units above the water level, and clamp dry land with a hard minimum
  (`where(f < WATER+1.2, WATER+1.2 + (f-(WATER+1.2))*0.1, f)`), feathered around the
  water corridors — a proportional-only clamp lets deep lows flood as pond speckles.
- **Streams crossing elevated ground: carve in TWO passes** — first a wide shallow
  vale (bed above water level, ~2.5× width, wide banks), then the channel. A single
  bed-level carve through high ground digs a uniform-width "trench tool" canyon
  (user-rejected).
- **Terraces/benches must attach to a mountain rim** with strong edge wobble.
  Free-standing terrace blobs on the plain read as literal circles from the air
  (user-rejected).
- Default per-tile material for authored zones: `Models/Terrain/Oakenshire_Boars_Enhanced.hmi`
  (set it in the meta `material` field; also used as the .hwld default).

## Elwynn-style: enclosed forest valley

Structure: mountain walls on all borders, gently rolling interior, one river crossing
the zone, 1–3 town plateaus connected by roads that follow the terrain.

Noise amplitudes/feature sizes and the finger-ridge wall below are calibrated against
measured classic-Elwynn reference data — see
[measured-elwynn-dna.md](measured-elwynn-dna.md) before changing them.

```json
{
  "world": "MyZone",
  "pageRect": { "x0": 30, "z0": 30, "x1": 33, "z1": 33 },
  "seed": 1337,
  "baseHeight": 60.0,
  "ops": [
    { "op": "fbm", "amplitude": 6, "featureSize": 420, "octaves": 3 },
    { "op": "fbm", "amplitude": 4.5, "featureSize": 170, "octaves": 3, "seed": 7 },
    { "op": "fbm", "amplitude": 1.5, "featureSize": 90, "octaves": 2, "seed": 11 },
    { "op": "warp", "strength": 70, "scale": 500 },
    { "op": "edgeWall", "height": 95, "width": 270, "noise": 0.55, "fingers": true, "fingerSpacing": 90 },
    { "op": "river", "points": [[0.05, 0.35], [0.3, 0.45], [0.55, 0.5], [0.75, 0.62], [0.95, 0.8]], "width": 24, "depth": 7, "bank": 50 },
    { "op": "plateau", "center": [0.42, 0.32], "radius": 90, "blend": 60 },
    { "op": "plateau", "center": [0.7, 0.72], "radius": 60, "blend": 45 },
    { "op": "road", "points": [[0.42, 0.32], [0.5, 0.45], [0.6, 0.55], [0.7, 0.72]], "width": 10, "blend": 14 },
    { "op": "smooth", "sigma": 9 }
  ]
}
```

Tuning notes:
- Keep the floor smooth: no noise layers below ~60-unit featureSize, and always finish
  with `smooth` sigma 8–10. The reference spectrum shows Blizzard has essentially zero
  energy below 30-yd wavelengths.
- River depth must exceed the total fbm amplitude swing near the water level, or random
  dips read as puddles competing with the river. Preview with `--water-level` ≈
  baseHeight − depth + 2 and check the river is the ONLY large water body (plus
  intended lakes).
- `"fingers": true` on edgeWall is what makes borders read as hand-carved spur ridges.
- River should exit through low border sections; use `"bedLevel"` (absolute Y) instead
  of `"depth"` when the river must hold a constant water level for a lake connection.
- Preview with `--water-level` ≈ baseHeight − depth + 1 to sanity-check continuity.
- Order matters: plateaus and roads AFTER the river so towns aren't flooded; `erode`
  and gentle `smooth` last to take the procedural edge off.

## Westfall-style: open farmland with a coast

Structure: flat, low-amplitude plains; ocean along the west edge below water level with
a cliff shelf; border walls only on the three land sides.

Recipe sketch (edgeWall has no per-side option yet — build coastal zones with a custom
script using `terrain_lib` directly):

```python
import numpy as np, terrain_lib as tl

rect = (30, 30, 33, 33)
w, h = tl.resolution_for_pages(4, 4)
upp = tl.units_per_pixel((h, w), 4)
field = np.full((h, w), 60.0, dtype=np.float32)          # low base, sea level ~50

field += tl.fbm((h, w), 500 / upp, octaves=4, seed=3) * 5  # farmland: amplitude 5
field += tl.fbm((h, w), 90 / upp, octaves=3, seed=8) * 2

# Coastal falloff: west 15% of the zone drops below sea level
u = np.linspace(0, 1, w, dtype=np.float32)[None, :]
coast = tl.smoothstep((u - 0.08) / 0.10)                   # 0 at far west -> 1 inland
field = field * coast + (42.0 + tl.fbm((h, w), 300/upp, seed=5) * 2) * (1 - coast)
field += (1 - tl.smoothstep((u - 0.16) / 0.03)) * np.where(u > 0.13, 8.0, 0.0)  # cliff lip

# Land-side border walls: mask the edge_wall so the west stays open
wall = tl.edge_wall((h, w), 90, width_uv=0.07, seed=11)
wall *= tl.smoothstep((u - 0.10) / 0.08)                   # fade wall out toward the coast
field += wall

field = tl.thermal_erode(field, 15, talus=0.8)
field = tl.gaussian_blur(field, 3 / upp)
tl.save_zone(field, "westfall.png", "westfall.json", "MyZone", rect)
```

Preview with `--water-level 50`.

## General principles (WoW-classic zone language)

- **Enclosure**: zones are rooms — borders are always impassable (mountains or ocean).
  Exits are deliberate gaps you carve later with roads through the wall.
- **Readability beats realism**: broad flat play space, distinct landmarks, gentle
  slopes. Realistic erosion detail is seasoning, not the meal.
- **Flat where gameplay happens**: quest hubs, camps and building sites on plateaus.
- **The river is a barrier and a signpost**: it divides the zone into halves the player
  crosses at readable points (fords = shallow wide sections).
