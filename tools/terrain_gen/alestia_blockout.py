# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Rough blockout of the Alestia continent on map 0 (Development world).

Reads the user's continent sketch (docs/world/reference/alestia-known-lands.webp), lays it over
the 64x64 page grid anchored at Oakenshire, and writes everything terrain_tool import needs:

- a lossless 16-bit heightmap of the whole page rect (land masses, mountains, sea floor),
- a 16-bit zone map with one zone id per terrain tile,
- the metadata JSON (ocean water, page list, keep existing pages),
- review renders (zones over the page grid, shaded relief).

This is a scale study, not a finished landscape: heights follow per-region profiles plus noise,
coastlines follow the sketch. Pages that already exist (Oakenshire and its surroundings) are never
overwritten; the new land is feathered down to their flat height-0 border so the seams match, and
their unzoned tiles receive a zone from the zone map.

    py -3.14 tools/terrain_gen/alestia_blockout.py [--out generated/terrain/alestia] [--plan-only] [--write-zones]

--write-zones adds the zone rows this blockout introduces (ids 14+) to data/editor/data/zones.data
and data/client/ClientDB/zones.data. Rows that already exist are never changed.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_lib as tl  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SKETCH = REPO / "docs" / "world" / "reference" / "alestia-known-lands.webp"
WORLD = "Development"
TERRAIN_DIR = REPO / "data" / "client" / "Worlds" / WORLD / WORLD / "Terrain"

PAGE = 533.3333333
HALF_WORLD = PAGE * 32.0
TILES_PER_PAGE = 16
TILE = PAGE / TILES_PER_PAGE
VERTS_PER_TILE = 8

# --- Anchor: the sketch's Oakenshire pin sits on the in-game Oakenshire town hall -----------------
# Pin tips measured on the 1536x1024 sketch; world positions are the canon atlas pins.
OAKENSHIRE_PX = (607.0, 436.25)
OAKENSHIRE_WORLD = (341.8, 546.5)      # atlas oakenshire_hub
HAVEN_PX = (623.0, 496.25)
HAVEN_WORLD = (-780.6, 1249.1)         # atlas haven
HAVEN_RADIUS = 311.5

SEA_LEVEL = -0.5        # ocean surface; existing flat land sits at 0 and must stay dry
OCEAN_RING_PAGES = 1    # ocean pages written around every land page
EXISTING_FEATHER = 1500.0

# --- Zones ---------------------------------------------------------------------------------------
# id, name, profile (base height, relief amplitude, ridged share), seeds in sketch pixels.
# Existing zones keep their ids (1 Falwyn Forest for the unlabelled heartland, 3 Haven).
ZONES = [
    (1, "Falwyn Forest", (8.0, 14.0, 0.25), [(607, 436), (623, 496), (560, 470), (690, 455), (655, 520), (575, 405)]),
    (14, "Whispering Woods", (22.0, 30.0, 0.35), [(600, 290), (700, 300), (780, 250), (470, 230), (545, 340), (735, 365)]),
    (15, "Frostward Peaks", (170.0, 260.0, 0.85), [(520, 95), (640, 60), (760, 95), (850, 60), (430, 150)]),
    (16, "Stonehelm Clans", (110.0, 200.0, 0.8), [(1000, 110), (1080, 180), (1150, 245), (950, 55), (1060, 60)]),
    (17, "Ironspine Wastes", (55.0, 90.0, 0.7), [(900, 300), (975, 360), (860, 245), (1040, 300)]),
    (18, "Dawnbreak Plains", (12.0, 10.0, 0.15), [(900, 485), (1000, 450), (820, 560), (1080, 545), (800, 430)]),
    (19, "Emberreach", (60.0, 170.0, 0.8), [(1380, 330), (1300, 450), (1225, 525), (1445, 250), (1350, 230)]),
    (20, "The Blackmoors", (6.0, 12.0, 0.2), [(920, 690), (1020, 700), (850, 745), (1100, 640), (990, 610)]),
    (21, "Silvermere", (10.0, 16.0, 0.25), [(720, 615), (680, 665), (760, 570)]),
    (22, "Sands of Korash", (24.0, 40.0, 0.45), [(720, 880), (880, 900), (615, 905), (1000, 860), (800, 965), (650, 820)]),
    (23, "Greenvale Forest", (15.0, 24.0, 0.3), [(530, 720), (450, 645), (420, 780), (600, 770), (410, 590)]),
    (24, "Valemarch", (20.0, 38.0, 0.4), [(400, 460), (300, 420), (480, 520), (240, 370), (380, 360)]),
    (25, "Westerfell", (40.0, 80.0, 0.55), [(180, 620), (110, 560), (270, 640), (200, 705), (75, 470)]),
]
HAVEN_ZONE = 3
NEW_ZONE_IDS = [zone_id for zone_id, *_ in ZONES if zone_id >= 14]
ZONE_FLAGS_ALLOW_DUELING = 1 << 1

PALETTE = {
    1: (126, 170, 84), 3: (230, 230, 230), 14: (46, 110, 60), 15: (220, 232, 245), 16: (150, 125, 105),
    17: (176, 120, 70), 18: (222, 196, 110), 19: (205, 70, 40), 20: (85, 80, 100), 21: (120, 180, 200),
    22: (232, 190, 120), 23: (70, 150, 80), 24: (160, 175, 95), 25: (120, 140, 130),
}


# --------------------------------------------------------------------------------------------------
# Sketch analysis
# --------------------------------------------------------------------------------------------------

def load_sketch() -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Returns (rgb, soft land [0..1], snow [0..1]) at sketch resolution.

    Mountains are not read from the sketch: its painted shading is indistinguishable from forest
    stipple and label cartouches by colour statistics, so relief comes from the zone profiles.
    """
    image = Image.open(SKETCH).convert("RGB")
    blurred = np.asarray(image.filter(ImageFilter.GaussianBlur(2)), dtype=np.float32)
    r, g, b = blurred[..., 0], blurred[..., 1], blurred[..., 2]
    land = ~((b > r + 8) & (b >= g - 12))

    # Map furniture is drawn over the sea: frame, title cartouche, legend, compass rose, scale bar.
    land[:20, :] = False
    land[-20:, :] = False
    land[:, :20] = False
    land[:, -20:] = False
    land[20:300, 20:290] = False
    land[785:990, 15:190] = False
    land[690:1000, 1170:1510] = False

    # Drop specks (label shadows, foam) with an open, then soften the coast.
    mask = Image.fromarray((land * 255).astype(np.uint8))
    mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))
    soft = np.asarray(mask.filter(ImageFilter.GaussianBlur(1.5)), dtype=np.float32) / 255.0

    raw = np.asarray(image, dtype=np.float32)
    bright = raw.mean(axis=2)
    spread = raw.max(axis=2) - raw.min(axis=2)
    snow = ((bright > 165) & (spread < 45)).astype(np.float32)
    snow[:, :] *= land
    snow = tl.gaussian_blur(snow, 6.0)
    snow = np.clip(snow * 2.0, 0.0, 1.0)
    return raw, soft, snow


def derive_scale(land: np.ndarray) -> tuple[float, float]:
    """Returns (scale actually used, scale derived from Oakenshire->Haven) in metres per sketch pixel."""
    derived = float(np.hypot(HAVEN_WORLD[0] - OAKENSHIRE_WORLD[0], HAVEN_WORLD[1] - OAKENSHIRE_WORLD[1]) /
                    np.hypot(HAVEN_PX[0] - OAKENSHIRE_PX[0], HAVEN_PX[1] - OAKENSHIRE_PX[1]))
    rows, cols = np.nonzero(land > 0.5)
    margin = PAGE * (OCEAN_RING_PAGES + 0.25)
    limits = []
    for extent_px, anchor_px, anchor_world, sign in (
            (cols.max(), OAKENSHIRE_PX[0], OAKENSHIRE_WORLD[0], 1), (cols.min(), OAKENSHIRE_PX[0], OAKENSHIRE_WORLD[0], -1),
            (rows.max(), OAKENSHIRE_PX[1], OAKENSHIRE_WORLD[1], 1), (rows.min(), OAKENSHIRE_PX[1], OAKENSHIRE_WORLD[1], -1)):
        room = HALF_WORLD - margin - sign * anchor_world
        limits.append(room / abs(extent_px - anchor_px))
    return min(derived, min(limits)), derived


def to_sketch(wx: np.ndarray, wz: np.ndarray, scale: float) -> tuple[np.ndarray, np.ndarray]:
    return OAKENSHIRE_PX[0] + (wx - OAKENSHIRE_WORLD[0]) / scale, OAKENSHIRE_PX[1] + (wz - OAKENSHIRE_WORLD[1]) / scale


def sample(field: np.ndarray, px: np.ndarray, py: np.ndarray) -> np.ndarray:
    return tl._sample_bilinear(field.astype(np.float32), px.astype(np.float32), py.astype(np.float32))


# --------------------------------------------------------------------------------------------------
# Grid helpers
# --------------------------------------------------------------------------------------------------

def existing_pages() -> set[tuple[int, int]]:
    pages = set()
    for path in TERRAIN_DIR.glob("*.tile"):
        x, z = path.stem.split("_")
        pages.add((int(x), int(z)))
    return pages


def distance_steps(seed: np.ndarray, steps: int) -> tuple[np.ndarray, np.ndarray]:
    """Grows a bool seed by 8-neighbour dilation. Returns (approximate distance in cells, index of
    the seed cell each cell was reached from, as a flat index)."""
    h, w = seed.shape
    dist = np.where(seed, 0.0, np.inf).astype(np.float32)
    source = np.where(seed, np.arange(h * w).reshape(h, w), -1)
    for _ in range(steps):
        grown = False
        for dz, dx, cost in ((0, 1, 1.0), (0, -1, 1.0), (1, 0, 1.0), (-1, 0, 1.0),
                             (1, 1, 1.4142), (1, -1, 1.4142), (-1, 1, 1.4142), (-1, -1, 1.4142)):
            shifted = np.full_like(dist, np.inf)
            shifted_src = np.full_like(source, -1)
            zs = slice(max(dz, 0), h + min(dz, 0))
            zd = slice(max(-dz, 0), h + min(-dz, 0))
            xs = slice(max(dx, 0), w + min(dx, 0))
            xd = slice(max(-dx, 0), w + min(-dx, 0))
            shifted[zs, xs] = dist[zd, xd] + cost
            shifted_src[zs, xs] = source[zd, xd]
            better = shifted < dist
            if better.any():
                grown = True
                dist[better] = shifted[better]
                source[better] = shifted_src[better]
        if not grown:
            break
    return dist, source


def smoothstep01(t: np.ndarray) -> np.ndarray:
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def resize(field: np.ndarray, width: int, height: int) -> np.ndarray:
    """Corner-aligned bicubic resize (sample i of the output sits at i/(n-1) of the input span)."""
    h, w = field.shape
    cols = np.linspace(0.0, w - 1.0, width, dtype=np.float32)
    rows = np.linspace(0.0, h - 1.0, height, dtype=np.float32)
    # Separable cubic (Catmull-Rom) along x, then z.
    def cubic_axis(data: np.ndarray, coords: np.ndarray, axis: int) -> np.ndarray:
        n = data.shape[axis]
        i1 = np.floor(coords).astype(np.int64)
        t = (coords - i1).astype(np.float32)
        idx = [np.clip(i1 + k, 0, n - 1) for k in (-1, 0, 1, 2)]
        p = [np.take(data, ix, axis=axis) for ix in idx]
        shape = [1, 1]
        shape[axis] = -1
        t = t.reshape(shape)
        return (0.5 * ((2 * p[1]) + (-p[0] + p[2]) * t + (2 * p[0] - 5 * p[1] + 4 * p[2] - p[3]) * t * t +
                       (-p[0] + 3 * p[1] - 3 * p[2] + p[3]) * t * t * t)).astype(np.float32)
    return cubic_axis(cubic_axis(field.astype(np.float32), cols, 1), rows, 0)


# --------------------------------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(REPO / "generated" / "terrain" / "alestia"))
    parser.add_argument("--plan-only", action="store_true", help="only write the zone map and review renders")
    parser.add_argument("--write-zones", action="store_true", help="add the new zone rows to the game data and exit")
    args = parser.parse_args()
    if args.write_zones:
        return write_zones()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    rgb, land_px, snow_px = load_sketch()
    scale, derived = derive_scale(land_px)
    print(f"scale: {scale:.2f} m per sketch pixel (Oakenshire->Haven implies {derived:.2f}; capped by the 64-page grid)")

    # Page rect = every page touching land, grown by the ocean ring.
    rows, cols = np.nonzero(land_px > 0.5)
    wx = OAKENSHIRE_WORLD[0] + (cols - OAKENSHIRE_PX[0]) * scale
    wz = OAKENSHIRE_WORLD[1] + (rows - OAKENSHIRE_PX[1]) * scale
    page_x = np.floor((wx + HALF_WORLD) / PAGE).astype(int)
    page_z = np.floor((wz + HALF_WORLD) / PAGE).astype(int)
    x0, x1 = page_x.min() - OCEAN_RING_PAGES, page_x.max() + OCEAN_RING_PAGES
    z0, z1 = page_z.min() - OCEAN_RING_PAGES, page_z.max() + OCEAN_RING_PAGES
    x0, z0, x1, z1 = max(x0, 0), max(z0, 0), min(x1, 63), min(z1, 63)
    pages_x, pages_z = x1 - x0 + 1, z1 - z0 + 1
    print(f"page rect: {x0},{z0} - {x1},{z1} ({pages_x}x{pages_z} pages, {pages_x * PAGE / 1000:.1f} x {pages_z * PAGE / 1000:.1f} km)")

    # ---- Tile grid: one cell per terrain tile (33.3 m), cell centres ----
    tiles_x, tiles_z = pages_x * TILES_PER_PAGE, pages_z * TILES_PER_PAGE
    origin_x = x0 * PAGE - HALF_WORLD
    origin_z = z0 * PAGE - HALF_WORLD
    tile_wx = origin_x + (np.arange(tiles_x) + 0.5) * TILE
    tile_wz = origin_z + (np.arange(tiles_z) + 0.5) * TILE
    grid_wx, grid_wz = np.meshgrid(tile_wx, tile_wz)
    spx, spy = to_sketch(grid_wx, grid_wz, scale)

    # Fractal coastline: warp the sketch lookup by a few hundred metres of noise.
    warp = 260.0 / scale
    shape = (tiles_z, tiles_x)
    wpx = spx + tl.fbm(shape, 40, octaves=4, seed=11) * warp
    wpy = spy + tl.fbm(shape, 40, octaves=4, seed=12) * warp
    land = sample(land_px, wpx, wpy) > 0.5
    snow = sample(snow_px, spx, spy)

    # Existing pages (never overwritten) count as land: the sketch has Oakenshire inland.
    existing = existing_pages()
    existing_tiles = np.zeros(shape, bool)
    for (px_, pz_) in existing:
        if x0 <= px_ <= x1 and z0 <= pz_ <= z1:
            tx, tz = (px_ - x0) * TILES_PER_PAGE, (pz_ - z0) * TILES_PER_PAGE
            existing_tiles[tz:tz + TILES_PER_PAGE, tx:tx + TILES_PER_PAGE] = True
    land |= existing_tiles

    # ---- Zones: nearest seed, measured in warped sketch space so borders meander ----
    border_warp = 900.0 / scale
    zpx = spx + tl.fbm(shape, 60, octaves=4, seed=21) * border_warp
    zpy = spy + tl.fbm(shape, 60, octaves=4, seed=22) * border_warp
    best = np.full(shape, np.inf, np.float32)
    region = np.zeros(shape, np.int32)
    for zone_id, _, _, seeds in ZONES:
        for sx, sy in seeds:
            d = (zpx - sx) ** 2 + (zpy - sy) ** 2
            closer = d < best
            best[closer] = d[closer]
            region[closer] = zone_id

    # Sea takes the zone of the nearest land within ~1 km; open sea has no zone.
    sea_dist, sea_src = distance_steps(land, 40)
    sea_dist_full, _ = distance_steps(land, 64)
    land_dist, _ = distance_steps(~land, 64)
    zones = np.where(land, region, 0)
    near_sea = (~land) & np.isfinite(sea_dist)
    zones[near_sea] = region.reshape(-1)[sea_src[near_sea]]
    haven = (grid_wx - HAVEN_WORLD[0]) ** 2 + (grid_wz - HAVEN_WORLD[1]) ** 2 < HAVEN_RADIUS ** 2
    zones[haven] = HAVEN_ZONE

    # ---- Pages to write: land pages plus the ocean ring, minus existing pages ----
    land_pages = set()
    for tz, tx in zip(*np.nonzero(land & ~existing_tiles)):
        land_pages.add((x0 + tx // TILES_PER_PAGE, z0 + tz // TILES_PER_PAGE))
    ring = set()
    for (px_, pz_) in land_pages | {p for p in existing if x0 <= p[0] <= x1 and z0 <= p[1] <= z1}:
        for dx in range(-OCEAN_RING_PAGES, OCEAN_RING_PAGES + 1):
            for dz in range(-OCEAN_RING_PAGES, OCEAN_RING_PAGES + 1):
                if x0 <= px_ + dx <= x1 and z0 <= pz_ + dz <= z1:
                    ring.add((px_ + dx, pz_ + dz))
    # Existing pages are listed too: import keeps them and fills their unzoned tiles.
    write_pages = sorted(ring | land_pages)
    new_pages = [p for p in write_pages if p not in existing]
    print(f"pages: {len(land_pages)} new land pages, {len(new_pages) - len(land_pages)} ocean pages, "
          f"{len(existing)} existing pages kept")

    zone_png = out / "alestia_zones.png"
    Image.fromarray(zones.astype(np.uint16)).save(zone_png)

    # ---- Heights at tile resolution ----
    profile_base = np.zeros(shape, np.float32)
    profile_amp = np.zeros(shape, np.float32)
    profile_ridge = np.zeros(shape, np.float32)
    for zone_id, _, (base, amp, ridge), _ in ZONES:
        sel = region == zone_id
        profile_base[sel], profile_amp[sel], profile_ridge[sel] = base, amp, ridge
    sigma = 600.0 / TILE
    profile_base = tl.gaussian_blur(profile_base, sigma)
    profile_amp = tl.gaussian_blur(profile_amp, sigma)
    profile_ridge = tl.gaussian_blur(profile_ridge, sigma)

    hills = tl.fbm(shape, 1600.0 / TILE, octaves=5, seed=31) * 0.5 + 0.5
    crests = tl.ridged(shape, 2600.0 / TILE, octaves=5, seed=32)
    crests = np.clip((crests - 0.45) / 0.55, 0.0, 1.0) ** 1.6
    relief = profile_amp * ((1.0 - profile_ridge) * hills + profile_ridge * crests * 1.4)
    land_h = profile_base * (0.6 + 0.4 * hills) + relief + snow * 160.0 * crests

    # Both surfaces are defined everywhere (land at the coast is 1.5 m, sea at the coast -2 m) and
    # blended at vertex resolution by a finer coastline, so the shore is not stair-stepped per tile.
    land_dist = tl.gaussian_blur(np.minimum(land_dist, 64.0), 1.5)
    sea_dist_full = tl.gaussian_blur(np.minimum(sea_dist_full, 64.0), 2.5)
    coast_ramp = smoothstep01(land_dist / (1100.0 / TILE))
    land_h = (1.5 + land_h * coast_ramp).astype(np.float32)
    sea_h = (-(2.0 + 46.0 * smoothstep01((sea_dist_full - 0.5) / (1400.0 / TILE)))).astype(np.float32)

    review_zones(out, rgb, zones, region, land, existing_tiles, (x0, z0, x1, z1), set(write_pages), existing, scale)
    if args.plan_only:
        return 0

    # ---- Vertex resolution ----
    width, height = pages_x * 128 + 1, pages_z * 128 + 1
    print(f"heightmap: {width}x{height}")
    def to_vertices(tile_field: np.ndarray) -> np.ndarray:
        # Tile centres sit half a tile inside the rect; pad by edge replication so corners align.
        # Padded cell i centre = origin + (i - 0.5) * TILE and vertex v = origin + v * TILE / 8,
        # so vertex v samples padded position v / 8 + 0.5 cells, which is resize output index v + 4.
        up = resize(np.pad(tile_field, 1, mode="edge"), (tiles_x + 1) * VERTS_PER_TILE + 1, (tiles_z + 1) * VERTS_PER_TILE + 1)
        return up[4:4 + height, 4:4 + width]

    vx = origin_x + np.arange(width, dtype=np.float64) * (TILE / VERTS_PER_TILE)
    vz = origin_z + np.arange(height, dtype=np.float64) * (TILE / VERTS_PER_TILE)
    land_v = to_vertices(land_h)
    sea_v = to_vertices(sea_h)
    # Fine coastline: the same warped sketch lookup as the tile grid, at vertex resolution, plus a
    # second smaller warp octave for coves and points.
    vshape = (height, width)
    fine_x = vx[None, :].astype(np.float32)
    fine_z = vz[:, None].astype(np.float32)
    vpx, vpy = to_sketch(fine_x, fine_z, scale)
    vpx = vpx + to_vertices(tl.fbm(shape, 40, octaves=4, seed=11)) * warp + tl.fbm(vshape, 900, octaves=3, seed=13) * (70.0 / scale)
    vpy = vpy + to_vertices(tl.fbm(shape, 40, octaves=4, seed=12)) * warp + tl.fbm(vshape, 900, octaves=3, seed=14) * (70.0 / scale)
    coast = smoothstep01((sample(land_px, vpx, vpy) - 0.35) / 0.3)
    del vpx, vpy
    coast = np.maximum(coast, to_vertices(existing_tiles.astype(np.float32)) > 0.5)
    vertex_h = (sea_v * (1.0 - coast) + land_v * coast).astype(np.float32)
    del land_v, sea_v, coast

    # Fine detail on land only, scaled with the local relief.
    amp_v = to_vertices(profile_amp)
    detail = tl.fbm((height, width), 220.0 / (TILE / VERTS_PER_TILE), octaves=4, seed=41)
    above = smoothstep01((vertex_h - 2.0) / 6.0)
    vertex_h += detail * (1.0 + 0.06 * amp_v) * above
    del amp_v, detail, above

    # Feather down to height 0 at existing pages so their flat borders meet the new land exactly.
    dist2 = np.full((height, width), np.inf, np.float32)
    for (px_, pz_) in existing:
        if not (x0 <= px_ <= x1 and z0 <= pz_ <= z1):
            continue
        ex0, ex1 = px_ * PAGE - HALF_WORLD, (px_ + 1) * PAGE - HALF_WORLD
        ez0, ez1 = pz_ * PAGE - HALF_WORLD, (pz_ + 1) * PAGE - HALF_WORLD
        dx = np.maximum(np.maximum(ex0 - vx, vx - ex1), 0.0).astype(np.float32)
        dz = np.maximum(np.maximum(ez0 - vz, vz - ez1), 0.0).astype(np.float32)
        np.minimum(dist2, dz[:, None] ** 2 + dx[None, :] ** 2, out=dist2)
    feather = smoothstep01(np.sqrt(dist2) / EXISTING_FEATHER)
    del dist2
    vertex_h = np.where(feather > 0.0, vertex_h * feather, 0.0).astype(np.float32)
    del feather

    # Quantize with an exact step so height 0 is an exact pixel value (seams against existing pages).
    step = 0.01
    min_y = -60.0
    max_y = min_y + step * 65535.0
    if vertex_h.max() > max_y - 1.0:
        raise SystemExit(f"terrain reaches {vertex_h.max():.1f} m, above the encodable {max_y:.1f} m")
    pixels = np.clip(np.rint((vertex_h - min_y) / step), 0, 65535).astype(np.uint16)
    height_png = out / "alestia_height.png"
    Image.fromarray(pixels).save(height_png)
    print(f"heights: {vertex_h.min():.1f} .. {vertex_h.max():.1f} m (sea level {SEA_LEVEL})")

    meta = {
        "world": WORLD,
        "pageRect": {"x0": int(x0), "z0": int(z0), "x1": int(x1), "z1": int(z1)},
        "minY": min_y,
        "maxY": max_y,
        "material": "",
        "waterLevel": SEA_LEVEL,
        "waterMaterial": "Worlds/Water_Ocean.hmat",
        "waterType": 2,
        "zoneMap": zone_png.name,
        "skipExistingPages": True,
        "pages": [[int(x), int(z)] for x, z in write_pages],
    }
    (out / "alestia_height.json").write_text(json.dumps(meta, indent=1) + "\n", encoding="utf-8")
    review_relief(out, vertex_h)
    return 0


def write_zones() -> int:
    """Adds the blockout's new top-level zones to the editor data and the client's ClientDB copy."""
    sys.path.insert(0, str(REPO / "tools" / "world"))
    from worldkit.data import load_proto_modules
    zones_pb2 = load_proto_modules()["zones"]
    for path in (REPO / "data" / "editor" / "data" / "zones.data", REPO / "data" / "client" / "ClientDB" / "zones.data"):
        catalog = zones_pb2.Zones()
        catalog.ParseFromString(path.read_bytes())
        known = {entry.id: entry.name for entry in catalog.entry}
        added = []
        for zone_id, name, _, _ in ZONES:
            if zone_id in known:
                if known[zone_id] != name:
                    print(f"{path.name}: zone {zone_id} already exists as '{known[zone_id]}', left unchanged")
                continue
            entry = catalog.entry.add()
            entry.id = zone_id
            entry.name = name
            entry.parentzone = 0
            entry.map = 0
            entry.flags = ZONE_FLAGS_ALLOW_DUELING
            added.append(zone_id)
        path.write_bytes(catalog.SerializeToString())
        print(f"{path.relative_to(REPO)}: added zones {added}")
    return 0


# --------------------------------------------------------------------------------------------------
# Review renders
# --------------------------------------------------------------------------------------------------

def _font(size: int):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def review_zones(out: Path, rgb: np.ndarray, zones: np.ndarray, region: np.ndarray, land: np.ndarray,
                 existing_tiles: np.ndarray, rect, write_pages: set, existing: set, scale: float) -> None:
    x0, z0, x1, z1 = rect
    cell = 2  # px per tile
    h, w = zones.shape
    img = np.zeros((h, w, 3), np.uint8)
    img[:] = (28, 52, 78)
    sea_blue = np.array([28, 52, 78], np.float32)
    for zone_id, color in PALETTE.items():
        sel = zones == zone_id
        tint = np.where(land[sel][:, None], 1.0, 0.22)
        img[sel] = (np.array(color, np.float32)[None, :] * tint + sea_blue[None, :] * (1.0 - tint)).astype(np.uint8)
    img[existing_tiles] = (img[existing_tiles] * 0.6 + np.array([255, 255, 255]) * 0.4).astype(np.uint8)
    canvas = Image.fromarray(img).resize((w * cell, h * cell), Image.NEAREST)
    draw = ImageDraw.Draw(canvas)
    page_px = TILES_PER_PAGE * cell
    for px_ in range(x0, x1 + 2):
        draw.line([((px_ - x0) * page_px, 0), ((px_ - x0) * page_px, h * cell)], fill=(0, 0, 0), width=1)
    for pz_ in range(z0, z1 + 2):
        draw.line([(0, (pz_ - z0) * page_px), (w * cell, (pz_ - z0) * page_px)], fill=(0, 0, 0), width=1)
    for (px_, pz_) in write_pages:
        if (px_, pz_) not in existing:
            continue
        bx, bz = (px_ - x0) * page_px, (pz_ - z0) * page_px
        draw.rectangle([bx, bz, bx + page_px, bz + page_px], outline=(255, 255, 255), width=2)
    font = _font(15)
    for zone_id, name, _, _ in ZONES:
        sel = np.nonzero((region == zone_id) & land)
        if len(sel[0]) == 0:
            continue
        cz, cx = np.median(sel[0]) * cell, np.median(sel[1]) * cell
        text = f"{name} ({zone_id})"
        tw = draw.textlength(text, font=font)
        draw.text((cx - tw / 2 + 1, cz + 1), text, fill=(0, 0, 0), font=font)
        draw.text((cx - tw / 2, cz), text, fill=(255, 255, 255), font=font)
    # Scale bar: 5 km
    bar = 5000.0 / TILE * cell
    draw.rectangle([20, h * cell - 30, 20 + bar, h * cell - 24], fill=(255, 255, 255))
    draw.text((20, h * cell - 50), "5 km", fill=(255, 255, 255), font=font)
    draw.text((20, 12), f"Map 0 - Alestia blockout - pages {x0},{z0}-{x1},{z1} - {scale:.1f} m per sketch px - north up",
              fill=(255, 255, 255), font=font)
    canvas.save(out / "review_zones.png")


def review_relief(out: Path, vertex_h: np.ndarray) -> None:
    step = 4
    small = vertex_h[::step, ::step]
    spacing = (TILE / VERTS_PER_TILE) * step
    gz, gx = np.gradient(small, spacing)
    # sun from the north-west (matches terrain_tool preview)
    shade = np.clip(0.55 + (-gx * -0.7 + -gz * -0.7) * 1.2, 0.15, 1.0)
    color = np.zeros(small.shape + (3,), np.float32)
    sea = small < SEA_LEVEL
    t = np.clip(small / 300.0, 0.0, 1.0)[..., None]
    low = np.array([96, 140, 70], np.float32)
    mid = np.array([150, 130, 95], np.float32)
    high = np.array([240, 240, 245], np.float32)
    land_color = np.where(t < 0.5, low + (mid - low) * (t / 0.5), mid + (high - mid) * ((t - 0.5) / 0.5))
    color[:] = land_color * shade[..., None]
    depth = np.clip(-small / 48.0, 0.0, 1.0)[..., None]
    sea_color = np.array([70, 140, 170], np.float32) * (1 - depth) + np.array([20, 50, 85], np.float32) * depth
    color[sea] = sea_color[sea]
    Image.fromarray(np.clip(color, 0, 255).astype(np.uint8)).save(out / "review_relief.png")


if __name__ == "__main__":
    raise SystemExit(main())
