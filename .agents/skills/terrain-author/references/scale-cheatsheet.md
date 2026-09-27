# Terrain scale cheatsheet

## Grid facts

| Quantity | Value |
|---|---|
| Terrain page | 533.33 × 533.33 world units |
| Page grid | 64 × 64 pages, world-centered at page (32,32) |
| World position | `worldX = (pageX − 32) · 533.33`, same for Z |
| Outer vertex spacing | 533.33 / 128 ≈ 4.17 units |
| Lossless image size | `(pagesX·128+1) × (pagesZ·128+1)` px |
| Height precision | (maxY − minY) / 65535 — a 200-unit range ≈ 3 mm |

Stay in pages ≥ 10: nav_builder probes zero-padded page filenames and would miss
pages 0–9 (the canonical names are non-padded). All real content lives around
page 32 anyway.

## Zone sizing

- A classic starter zone (Elwynn, Westfall feel): **3×3 to 4×4 pages** (~1.6–2.1 km
  across). Bigger reads empty without content density.
- A player runs ~7 units/second — 4 pages ≈ 5 minutes of travel.

## Feature heights (world units, relative to zone base)

Values cross-checked against measured classic-Elwynn data
([measured-elwynn-dna.md](measured-elwynn-dna.md)): real forest floor has local relief
of ~10–35 units per 100-yd window (median 23), median slope 12°, and border massifs
up to ~220 units.

| Feature | Amplitude |
|---|---|
| Rolling forest hills | 8–25 (local relief 10–35 per 100 yd) |
| Farmland (Westfall-flat) | 3–8 |
| River bed below banks | 5–8 (water surface ~1–2 above bed; must exceed floor noise swing) |
| Lake depression | 5–15 |
| Border mountain walls | 80–150 tall, 200–350 units thick, finger-ridged (spacing ~90) |
| Cliffs / shelf steps | 10–30 per step |

## Slope rules of thumb

- Walkable slope limit ≈ 50–60°. At 4.17-unit vertex spacing that's ~5–6 units of
  height change per vertex step. In preview contours (5-unit interval): adjacent
  contour lines touching = unwalkable.
- Roads: ≤ 10° along the direction of travel; flatten with `road`/`flatten_along`
  (width 8–14, blend 12–20).
- Town plateaus: radius 60–120 units, blend 40–80. Buildings need genuinely flat
  ground — don't rely on "almost flat".

## Feature widths

| Feature | Width |
|---|---|
| Footpath / road | 8–14 units |
| Stream | 10–18 units |
| River | 20–40 units |
| Valley floor between hills | 100–300 units |
