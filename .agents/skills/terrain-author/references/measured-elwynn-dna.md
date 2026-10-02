# Measured terrain DNA — classic Elwynn Forest references

Source: 16-bit heightmap exports of 10 classic-WoW Elwynn ADT tiles plus minimaps in
`artifacts/heightmap_references/` (`heightmap_XX_YY.png`, 4096² px per 533.33-yd tile,
`mapXX_YY.png` minimaps). Measured 2026-07 (analysis lives in this file so numbers
survive; re-derive only if new references arrive).

## Reference file facts

- All tiles share **one global 16-bit height mapping** (edge-continuity verified,
  neighbor mismatch ≈ quantization noise).
- Stitch layout rule (empirical): **second filename index = column** (ascending →
  image right), **first index = row ascending upward** (e.g. tile 33_49 sits above
  32_49, and 32_50 right of 32_49).
- The 4096² images are upsampled from WoW's real vertex grid (~16-px blocks visible)
  — actual information density equals our engine grid (~4.17 yd/vertex). Importing at
  our lossless resolution loses nothing.
- **Height scale estimate: global range ≈ 300 yd** (0..65535). Calibrated by slope
  plausibility: at 300 yd the forest core is 99% walkable, median slope 11.6°; at
  200 yd everything is implausibly flat, at 480+ yd implausibly steep. Confirm with
  the exporter's actual range if available.

## Terrain character (at the 300-yd scale)

| Metric | Value |
|---|---|
| Forest-floor slope (core, no walls) | p50 ≈ 12°, p90 ≈ 37°, ~99% < 50° |
| Local relief per 100-yd window | p25 ≈ 10, **p50 ≈ 23**, p75 ≈ 35 units |
| Border-massif total relief | ≈ 220 units (Stormwind massif; playable walls less) |
| Floor : massif relief ratio | ≈ 1 : 10 |

## Roughness spectrum (the key insight)

RMS height energy by wavelength band, forest tile 32_49:

| Wavelength | RMS (units) | |
|---|---|---|
| 250–2000 yd | ~7 | dominant — broad swells |
| 120–250 yd | ~6 | equally strong |
| 60–120 yd | ~2.2 | falling fast |
| 30–60 yd | ~1.5 | |
| 15–30 yd | ~0.8 | |
| 5–15 yd | **~0.16** | essentially zero |

**Blizzard's floor is broad smooth undulation with virtually no noise below ~30 yd.**
Recipe translation: two roughly equal-amplitude fbm layers at featureSize ~420 and
~170, a small (~25%) layer at ~90, then a smooth pass of sigma ≈ 8–10 units. Do not
add high-frequency detail noise.

## Border mountains are finger-ridges, not noise

The zone-edge walls are **anisotropic parallel spurs** running perpendicular to the
border, pointing into the zone — spacing ~60–120 yd, spur length 150–300 yd. Use
`edge_wall(..., fingers=True, finger_spacing_px=90/upp)` (recipe:
`"fingers": true, "fingerSpacing": 90`) — isotropic ridged noise looks wrong in
side-by-side comparison.

## Importing reference heightmaps directly

A WoW ADT and an engine page are both 533.33 units — **1 ADT = 1 page**. To bring
reference terrain in: stitch the tiles per the layout rule above, multiply raw values
by `300/65535`, downsample to `(pages·128+1)²` with **area averaging** (PIL
`Image.BOX`), write 16-bit PNG + meta JSON, and run `terrain_tool import`. Worked
example: world `ElwynnRef` (pages 31,31–32,32) built from tiles 32/33 × 49/50.
