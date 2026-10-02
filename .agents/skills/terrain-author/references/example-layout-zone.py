"""ElwynnDemo v5 - expanded 3x2 zone with gold-mine mountain, river removed.

Changes:
- Zone expanded east: pages 31,31 - 33,32 (3x2). The former east border becomes an
  INTERIOR dividing ridge with a pass at the east road -> two connected valley rooms.
- River removed entirely. The lake stays (closed lake, fed by the pocket brook).
- New east room: gold-mine mountain attached to the NE rim, with an excavated
  entrance bowl facing southwest, a flat mine apron (for the entrance + buildings),
  tailing mounds, and a dead-end mine road spur off the east road.
- Upland bench relocated to the new SE rim.

Coordinates: u,v in 0..1 per axis (u -> east over 1600 units, v -> south over 1066);
blob radii are fractions of the ZONE HEIGHT.
"""
import argparse
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'terrain_gen'))

import numpy as np
import terrain_lib as tl

_parser = argparse.ArgumentParser()
_parser.add_argument('--out-dir', required=True, help='where to write the generated layout (e.g. your scratchpad)')
OUT = str(Path(_parser.parse_args().out_dir))
RECT = (31, 31, 33, 32)                       # 3 pages wide, 2 pages tall
PAGES_X, PAGES_Z = 3, 2
W, H = tl.resolution_for_pages(PAGES_X, PAGES_Z)   # 385 x 257
SHAPE = (H, W)
UPP = tl.units_per_pixel(SHAPE, PAGES_X)
BASE = 62.0
WATER = 54.0
MATERIAL = "Models/Terrain/Oakenshire_Boars_Enhanced.hmi"

def u2px(units):
    return units / UPP

mountain_sample, _ = tl.load_relief_sample('ref_mountain_relief')
floor_sample, _ = tl.load_relief_sample('ref_floor_relief')

# ------------------------------------------- forest floor from real Elwynn relief
field = np.full(SHAPE, BASE, dtype=np.float32)
field += tl.fbm(SHAPE, u2px(420), octaves=2, seed=1) * 3.0
field += tl.synth_from_sample(floor_sample, SHAPE, patch_px=48, seed=8, min_std_frac=0.4) * 0.7
field = tl.gaussian_blur(field, u2px(5))

# ------------------------------------------------------------- mountain masses
# West room rims = v4 layout with u rescaled by 2/3; new N/E/S rim segments cover the
# eastern expansion. The former east border masses (now at u~0.61-0.71) stay as the
# interior dividing ridge, with a pass at the east road (v~0.5).
masses = [
    # west + northwest
    (-0.04, 0.02, 0.22), (0.05, -0.05, 0.24), (0.01, 0.18, 0.14), (-0.03, 0.30, 0.10),
    (-0.05, 0.40, 0.09), (-0.07, 0.52, 0.10),
    (-0.04, 0.68, 0.16), (0.03, 0.86, 0.18), (-0.01, 1.02, 0.22), (0.11, 1.00, 0.15),
    # north rim (west room)
    (0.23, -0.08, 0.18), (0.31, -0.04, 0.13), (0.28, 0.06, 0.09),
    # pocket ring / north-center
    (0.41, -0.06, 0.16), (0.52, -0.08, 0.18), (0.65, -0.02, 0.20), (0.70, 0.16, 0.16),
    # interior dividing ridge (former east border), pass gap at v~0.5
    (0.68, 0.34, 0.12), (0.61, 0.42, 0.09), (0.39, 0.12, 0.09),
    (0.71, 0.58, 0.13), (0.69, 0.72, 0.14), (0.68, 0.86, 0.12),
    # south rim
    (0.08, 1.04, 0.14), (0.19, 1.06, 0.13),
    (0.37, 1.06, 0.13), (0.43, 1.02, 0.09),
    (0.52, 1.06, 0.12), (0.61, 1.04, 0.14), (0.69, 0.94, 0.14),
    # eastern expansion: north rim, east rim, south rim
    (0.78, -0.06, 0.16), (0.90, -0.08, 0.20), (1.03, -0.02, 0.18),
    (1.05, 0.25, 0.14), (1.06, 0.45, 0.13), (1.05, 0.65, 0.14), (1.03, 0.85, 0.15),
    (0.78, 1.05, 0.14), (0.89, 1.06, 0.15), (1.00, 1.04, 0.14),
    # gold-mine mountain, attached to the NE rim
    (0.86, 0.24, 0.095), (0.92, 0.16, 0.09),
]
mass_field = tl.mountain_mass_ref(SHAPE, masses, crest_height=75.0, relief_gain=0.85,
                                  edge_uv=0.10, seed=42, sample=mountain_sample)

# Clearings carved out of the mass: NE pocket valley + the mine's entrance bowl
pocket_clear = tl.blob_mask(SHAPE, [(0.53, 0.18, 0.085), (0.49, 0.26, 0.05), (0.47, 0.31, 0.045)],
                            edge_uv=0.05, smooth_k_uv=0.05, seed=6)
mine_clear = tl.blob_mask(SHAPE, [(0.828, 0.32, 0.038), (0.845, 0.27, 0.028)],
                          edge_uv=0.030, smooth_k_uv=0.03, wobble_uv=0.012, seed=77)
mass_field *= (1.0 - pocket_clear) * (1.0 - mine_clear * 0.92)
field += mass_field

# Rocky knolls on foothills, a few freestanding
foot_mask = tl.blob_mask(SHAPE, masses, edge_uv=0.16, smooth_k_uv=0.10, seed=42)
foothill_band = np.clip(foot_mask * (1.0 - foot_mask) * 4.0, 0.0, 1.0) ** 2
field = tl.scatter_knolls(field, foothill_band, count=16, radius_px=(u2px(18), u2px(45)),
                          height=(3.0, 8.0), seed=17)
plain_band = np.clip((1.0 - foot_mask) - 0.55, 0.0, 1.0)
field = tl.scatter_knolls(field, plain_band, count=6, radius_px=(u2px(14), u2px(28)),
                          height=(2.0, 4.5), seed=23)

# ------------------------------------------------- NE pocket valley (Northshire)
pocket = tl.blob_mask(SHAPE, [(0.53, 0.18, 0.10), (0.49, 0.24, 0.07)], edge_uv=0.06,
                      smooth_k_uv=0.05, seed=5)
field += pocket * 9.0
field = tl.stamp_plateau(field, (0.54, 0.16), 0.055, level=None, blend_uv=0.045)

# ------------------------------------------------------------- gold mine site
# Flat apron inside the excavated bowl (mine entrance + buildings go here), and
# tailing mounds spilling out toward the road spur.
field = tl.stamp_plateau(field, (0.828, 0.325), 0.032, level=None, blend_uv=0.026)
tailings_zone = tl.blob_mask(SHAPE, [(0.822, 0.40, 0.045)], edge_uv=0.03, seed=78)
field = tl.scatter_knolls(field, tailings_zone, count=5, radius_px=(u2px(8), u2px(16)),
                          height=(1.5, 3.5), seed=79)

# --------------------------- upland bench attached to the new SE rim (irregular)
field = tl.terrace(field, [(1.02, 0.55, 0.11), (0.97, 0.65, 0.09), (1.01, 0.78, 0.11), (0.96, 0.86, 0.08)],
                   height=8.0, edge_uv=0.012, wobble_uv=0.022, seed=9)

# Town plateau BEFORE the water carves, so the brook cuts through its rim instead of
# the plateau lifting the brook mouth dry afterwards.
town = (0.333, 0.44)
field = tl.stamp_plateau(field, town, 0.05, level=None, blend_uv=0.04)

# ------------------------------------------------------------- lake (no river)
field = tl.lake_basin(field, [(0.24, 0.50, 0.075), (0.287, 0.55, 0.05)], bed_level=48.0,
                      edge_uv=0.035, islands=[(0.25, 0.51, 0.022, 58.0)], seed=13)

brook_pts = [(0.52, 0.22), (0.48, 0.30), (0.44, 0.38), (0.39, 0.46), (0.293, 0.545)]
brook_path = tl.meander_path(SHAPE, brook_pts, meander_px=u2px(11), wavelength_px=u2px(110), seed=29)
field = tl.carve_channel(field, brook_path, width_px=u2px(20), depth=0, bank_px=u2px(42),
                         bed_level=58.0, width_noise=0.25, bank_ragged_px=u2px(5), seed=29)
field = tl.carve_channel(field, brook_path, width_px=u2px(7), depth=0, bank_px=u2px(14),
                         bed_level=52.5, width_noise=0.25, bank_ragged_px=u2px(4), seed=30)

# Anti-flood clamp, feathered, hard minimum
brook_dist, _ = tl.dist_to_polyline(SHAPE, brook_path)
lake_zone = tl.blob_mask(SHAPE, [(0.24, 0.50, 0.10), (0.287, 0.55, 0.075)], edge_uv=0.02, seed=13)
w_brook = tl.smoothstep((brook_dist - u2px(9)) / u2px(8))
dry_w = w_brook * (1.0 - tl.smoothstep((lake_zone - 0.15) / 0.3))
dry_min = WATER + 1.2
dry_floor = np.where(field < dry_min, dry_min + (field - dry_min) * 0.1, field)
field = (field * (1.0 - dry_w) + dry_floor * dry_w).astype(np.float32)

# ------------------------------------------------------------------------ roads
road_w = [(0.013, 0.47), (0.107, 0.46), (0.213, 0.44), town]
road_e = [town, (0.40, 0.47), (0.52, 0.50), (0.66, 0.50), (0.74, 0.46)]      # through the pass
road_mine = [(0.74, 0.46), (0.79, 0.42), (0.822, 0.375), (0.828, 0.335)]     # spur to the mine apron
road_s = [town, (0.32, 0.56), (0.313, 0.66), (0.293, 0.80), (0.28, 0.97)]
road_n = [town, (0.373, 0.36), (0.413, 0.30), (0.467, 0.26), (0.52, 0.20)]
for i, road in enumerate((road_w, road_e, road_mine, road_s, road_n)):
    path = tl.meander_path(SHAPE, road, meander_px=u2px(7), wavelength_px=u2px(140), seed=40 + i)
    field = tl.flatten_along(field, path, width_px=u2px(11), blend_px=u2px(16))
    dist, _ = tl.dist_to_polyline(SHAPE, path)
    field -= (1.0 - tl.smoothstep(dist / u2px(8))) * 0.8

# --------------------------------------------------------------------- finalize
field = tl.gaussian_blur(field, u2px(3))
meta = tl.save_zone(field, f'{OUT}\\elwynndemo5.png', f'{OUT}\\elwynndemo5.json',
                    'ElwynnDemo', RECT, material=MATERIAL, water_level=WATER)
print(f"range {meta['minY']:.1f}..{meta['maxY']:.1f}, water {WATER}")
