# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Procedural heightmap primitives for authoring MMO terrain zones.

All height fields are float32 numpy arrays of shape (height, width) holding world
units (Y-up). Array axes map to the world as: column -> +X, row -> +Z.

The engine terrain grid facts these helpers are built around:
  - One terrain page is 533.33333 world units squared.
  - A rect of nx x nz pages has a lossless outer-vertex resolution of
    (nx*128+1) x (nz*128+1) pixels (use `resolution_for_pages`).
  - Heightmaps are exchanged as 16-bit grayscale PNG plus a JSON sidecar holding
    the world height range (`save_zone`).
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from PIL import Image

PAGE_SIZE = 533.33333
VERTS_PER_PAGE_EDGE = 128


def resolution_for_pages(pages_x: int, pages_z: int) -> tuple[int, int]:
    """Return the (width, height) in pixels for a lossless zone image."""
    return pages_x * VERTS_PER_PAGE_EDGE + 1, pages_z * VERTS_PER_PAGE_EDGE + 1


def units_per_pixel(shape: tuple[int, int], pages_x: int) -> float:
    """World units covered by one pixel step."""
    return pages_x * PAGE_SIZE / (shape[1] - 1)


# ---------------------------------------------------------------------------
# Noise
# ---------------------------------------------------------------------------

def _value_noise_octave(shape: tuple[int, int], cells: int, rng: np.random.Generator) -> np.ndarray:
    """Smooth value noise in [-1, 1] with roughly `cells` features across the image width.

    Cell counts are derived per axis so features stay isotropic on non-square zones.
    """
    cells_x = max(2, int(cells))
    cells_z = max(2, int(round(cells * shape[0] / max(shape[1], 1))))
    coarse = rng.uniform(-1.0, 1.0, size=(cells_z + 1, cells_x + 1)).astype(np.float32)
    img = Image.fromarray(coarse, mode="F").resize((shape[1], shape[0]), Image.BICUBIC)
    return np.asarray(img, dtype=np.float32)


def fbm(shape: tuple[int, int], feature_size_px: float, octaves: int = 5,
        gain: float = 0.5, lacunarity: float = 2.0, seed: int = 0) -> np.ndarray:
    """Fractal brownian motion noise in roughly [-1, 1].

    feature_size_px: approximate size (in pixels) of the largest features.
    """
    rng = np.random.default_rng(seed)
    result = np.zeros(shape, dtype=np.float32)
    amplitude = 1.0
    total = 0.0
    cells = max(2.0, shape[1] / max(4.0, feature_size_px))
    for _ in range(octaves):
        result += amplitude * _value_noise_octave(shape, int(cells), rng)
        total += amplitude
        amplitude *= gain
        cells *= lacunarity
    return result / total


def ridged(shape: tuple[int, int], feature_size_px: float, octaves: int = 5,
           gain: float = 0.5, lacunarity: float = 2.0, seed: int = 0) -> np.ndarray:
    """Ridged multifractal noise in [0, 1] (sharp crests, good for mountain walls)."""
    rng = np.random.default_rng(seed)
    result = np.zeros(shape, dtype=np.float32)
    amplitude = 1.0
    total = 0.0
    cells = max(2.0, shape[1] / max(4.0, feature_size_px))
    for _ in range(octaves):
        octave = 1.0 - np.abs(_value_noise_octave(shape, int(cells), rng))
        result += amplitude * octave
        total += amplitude
        amplitude *= gain
        cells *= lacunarity
    return result / total


def _hash_noise(ix: np.ndarray, iz: np.ndarray, seed: int) -> np.ndarray:
    """Deterministic pseudo-random lattice values in [0, 1]."""
    h = np.sin(ix * 127.1 + iz * 311.7 + seed * 74.7) * 43758.5453
    return h - np.floor(h)


def noise_at(coords_x: np.ndarray, coords_z: np.ndarray, seed: int = 0, octaves: int = 3) -> np.ndarray:
    """Smooth value noise in [0, 1] evaluated at arbitrary (already scaled) coordinates.

    One noise feature is roughly one coordinate unit; pass transformed/stretched
    coordinate fields for anisotropic patterns.
    """
    total = np.zeros_like(coords_x, dtype=np.float32)
    amplitude, norm = 1.0, 0.0
    for o in range(octaves):
        x = coords_x * (2.0 ** o)
        z = coords_z * (2.0 ** o)
        ix, iz = np.floor(x), np.floor(z)
        fx, fz = x - ix, z - iz
        fx = fx * fx * (3 - 2 * fx)
        fz = fz * fz * (3 - 2 * fz)
        n00 = _hash_noise(ix, iz, seed + o)
        n10 = _hash_noise(ix + 1, iz, seed + o)
        n01 = _hash_noise(ix, iz + 1, seed + o)
        n11 = _hash_noise(ix + 1, iz + 1, seed + o)
        total += amplitude * ((n00 * (1 - fx) + n10 * fx) * (1 - fz) + (n01 * (1 - fx) + n11 * fx) * fz)
        norm += amplitude
        amplitude *= 0.5
    return (total / norm).astype(np.float32)


def domain_warp(field: np.ndarray, strength_px: float, scale_px: float, seed: int = 0) -> np.ndarray:
    """Distort a field by offsetting sample positions with noise (breaks up grid patterns)."""
    h, w = field.shape
    dx = fbm(field.shape, scale_px, octaves=2, seed=seed) * strength_px
    dz = fbm(field.shape, scale_px, octaves=2, seed=seed + 1) * strength_px
    cols, rows = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    return _sample_bilinear(field, cols + dx, rows + dz)


def _sample_bilinear(field: np.ndarray, cols: np.ndarray, rows: np.ndarray) -> np.ndarray:
    h, w = field.shape
    cols = np.clip(cols, 0, w - 1)
    rows = np.clip(rows, 0, h - 1)
    c0 = np.floor(cols).astype(np.int32)
    r0 = np.floor(rows).astype(np.int32)
    c1 = np.minimum(c0 + 1, w - 1)
    r1 = np.minimum(r0 + 1, h - 1)
    fc = cols - c0
    fr = rows - r0
    top = field[r0, c0] * (1 - fc) + field[r0, c1] * fc
    bottom = field[r1, c0] * (1 - fc) + field[r1, c1] * fc
    return (top * (1 - fr) + bottom * fr).astype(np.float32)


# ---------------------------------------------------------------------------
# Filtering
# ---------------------------------------------------------------------------

def gaussian_blur(field: np.ndarray, sigma_px: float) -> np.ndarray:
    """Gaussian blur via FFT (no scipy dependency)."""
    if sigma_px <= 0:
        return field
    h, w = field.shape
    fz = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :]
    kernel = np.exp(-2.0 * (np.pi ** 2) * (sigma_px ** 2) * (fz ** 2 + fx ** 2))
    return np.fft.irfft2(np.fft.rfft2(field) * kernel, s=field.shape).astype(np.float32)


def thermal_erode(field: np.ndarray, iterations: int = 30, talus: float = 1.2,
                  amount: float = 0.25) -> np.ndarray:
    """Simple thermal erosion: material slides off slopes steeper than `talus` units/pixel."""
    result = field.copy()
    for _ in range(iterations):
        for axis, shift in ((0, 1), (0, -1), (1, 1), (1, -1)):
            neighbor = np.roll(result, shift, axis=axis)
            diff = result - neighbor
            move = np.where(diff > talus, (diff - talus) * amount * 0.25, 0.0)
            # Don't wrap around the borders
            if axis == 0 and shift == 1:
                move[0, :] = 0
            elif axis == 0 and shift == -1:
                move[-1, :] = 0
            elif axis == 1 and shift == 1:
                move[:, 0] = 0
            else:
                move[:, -1] = 0
            result -= move
            result += np.roll(move, -shift, axis=axis)
    return result.astype(np.float32)


def smoothstep(t: np.ndarray) -> np.ndarray:
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------------------
# Layout-driven shaping (organic masses instead of parametric frames)
# ---------------------------------------------------------------------------

def blob_mask(shape: tuple[int, int], circles: list[tuple[float, float, float]],
              edge_uv: float = 0.05, smooth_k_uv: float = 0.06,
              wobble_uv: float = 0.02, seed: int = 0) -> np.ndarray:
    """Organic region mask in [0, 1] from a smooth union of circles (metaball style).

    circles: list of (u, v, radius) in normalized zone coordinates; positions run 0..1
    per axis, while radii/edges are fractions of the ZONE HEIGHT (v extent) — on
    non-square zones distances are aspect-corrected so circles stay circular.
    Overlapping circles fuse smoothly (smooth-min with strength smooth_k_uv), the
    boundary is softened over edge_uv and perturbed by noise (wobble_uv) so nothing
    reads as a geometric primitive. This is the building block for mountain masses,
    valley pockets, lakes and terraces — irregular painterly shapes, not rectangles.
    """
    h, w = shape
    aspect = (w - 1) / max(h - 1, 1)
    cols, rows = np.meshgrid(np.linspace(0, 1, w, dtype=np.float32), np.linspace(0, 1, h, dtype=np.float32))

    k = max(smooth_k_uv, 1e-4)
    d = np.full(shape, 1e9, dtype=np.float32)
    for (cu, cv, r) in circles:
        di = np.sqrt(((cols - cu) * aspect) ** 2 + (rows - cv) ** 2) - r
        # polynomial smooth-min keeps fused boundaries round
        hmix = np.clip(0.5 + 0.5 * (di - d) / k, 0.0, 1.0)
        d = di * (1 - hmix) + d * hmix - k * hmix * (1 - hmix)

    if wobble_uv > 0:
        d = d + (fbm(shape, w * 0.12, octaves=3, seed=seed) * wobble_uv).astype(np.float32)

    return (1.0 - smoothstep(d / max(edge_uv, 1e-6) + 0.5)).astype(np.float32)


def flow_ridges(shape: tuple[int, int], guide_mask: np.ndarray, spacing_px: float,
                stretch: float = 4.0, seed: int = 0) -> np.ndarray:
    """Ridge pattern in [0, 1] whose crests run along the local gradient of guide_mask
    (i.e. perpendicular to the mask boundary) — the hand-carved mountain-spur look.

    Seam-free implementation: stripe noise is generated for four fixed orientations
    and blended by how well each aligns with the local slope direction.
    """
    h, w = shape
    g = gaussian_blur(guide_mask.astype(np.float32), spacing_px)
    gz, gx = np.gradient(g)
    theta = np.arctan2(gz, gx)

    px_cols, px_rows = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    result = np.zeros(shape, dtype=np.float32)
    weight_sum = np.full(shape, 1e-6, dtype=np.float32)
    for i, phi in enumerate((0.0, np.pi / 4, np.pi / 2, 3 * np.pi / 4)):
        # stripes vary across direction phi, elongated along it
        across = (px_cols * np.cos(phi + np.pi / 2) + px_rows * np.sin(phi + np.pi / 2)) / spacing_px
        along = (px_cols * np.cos(phi) + px_rows * np.sin(phi)) / (spacing_px * stretch)
        stripe = 1.0 - np.abs(2.0 * noise_at(across, along, seed=seed + i) - 1.0)
        weight = np.cos(2.0 * (theta - phi)) ** 2
        weight = np.where(np.cos(2.0 * (theta - phi)) > 0, weight, 0.0)
        result += stripe * weight
        weight_sum += weight

    return (result / weight_sum).astype(np.float32)


def load_relief_sample(name: str = "ref_mountain_relief") -> tuple[np.ndarray, float]:
    """Load a relief detail sample (extracted from real reference terrain) in world units.

    Returns (relief array float32 [h, w] in units, units_per_pixel of the sample).
    """
    data_dir = Path(__file__).parent / "data"
    meta = json.loads((data_dir / f"{name}.json").read_text(encoding="utf-8"))
    img = np.asarray(Image.open(data_dir / f"{name}.png"), dtype=np.float32)
    relief = (img - 32768.0) / 32767.0 * meta["unitsAtFullScale"]
    return relief.astype(np.float32), float(meta["unitsPerPixel"])


def tile_sample(sample: np.ndarray, shape: tuple[int, int], seed: int = 0) -> np.ndarray:
    """Cover `shape` with a sample using mirror tiling, a seed-based offset and a
    seed-based rotation (0/90/180/270 + optional mirror) so repeats don't align."""
    rng = np.random.default_rng(seed)
    s = np.rot90(sample, k=int(rng.integers(0, 4)))
    if rng.random() < 0.5:
        s = s[:, ::-1]
    sh, sw = s.shape
    oz, ox = int(rng.integers(0, sh)), int(rng.integers(0, sw))
    rows = (np.arange(shape[0]) + oz)
    cols = (np.arange(shape[1]) + ox)
    # mirror (reflect) indexing avoids seams at tile borders
    period_r, period_c = 2 * sh - 2, 2 * sw - 2
    rr = rows % period_r
    rr = np.where(rr >= sh, period_r - rr, rr)
    cc = cols % period_c
    cc = np.where(cc >= sw, period_c - cc, cc)
    return s[np.ix_(rr, cc)].astype(np.float32)


def synth_from_sample(sample: np.ndarray, shape: tuple[int, int], patch_px: int = 56,
                      seed: int = 0, min_std_frac: float = 0.6) -> np.ndarray:
    """Texture-synthesize a field of `shape` from a relief sample by quilting random
    rotated/flipped patches with 50% overlap and cosine feathering.

    Only patches whose local variance is at least min_std_frac of the sample's overall
    std are used, so smooth valley areas of the sample don't produce dead flat spots.
    Avoids the kaleidoscope artifacts of mirror tiling entirely.
    """
    rng = np.random.default_rng(seed)
    sh, sw = sample.shape
    patch_px = int(min(patch_px, sh - 1, sw - 1))
    half = patch_px // 2

    # Candidate patch origins with enough detail
    global_std = float(sample.std())
    candidates = []
    for r in range(0, sh - patch_px, max(4, half // 4)):
        for c in range(0, sw - patch_px, max(4, half // 4)):
            if sample[r:r+patch_px, c:c+patch_px].std() >= min_std_frac * global_std:
                candidates.append((r, c))
    if not candidates:
        candidates = [(0, 0)]

    window = np.outer(np.hanning(patch_px), np.hanning(patch_px)).astype(np.float32) + 1e-4
    out = np.zeros(shape, dtype=np.float32)
    weight = np.zeros(shape, dtype=np.float32)

    for z0 in range(-half, shape[0], half):
        for x0 in range(-half, shape[1], half):
            r, c = candidates[int(rng.integers(0, len(candidates)))]
            patch = sample[r:r+patch_px, c:c+patch_px]
            patch = np.rot90(patch, k=int(rng.integers(0, 4)))
            if rng.random() < 0.5:
                patch = patch[:, ::-1]

            pz0, px0 = max(0, z0), max(0, x0)
            pz1, px1 = min(shape[0], z0 + patch_px), min(shape[1], x0 + patch_px)
            sz0, sx0 = pz0 - z0, px0 - x0
            sz1, sx1 = sz0 + (pz1 - pz0), sx0 + (px1 - px0)
            out[pz0:pz1, px0:px1] += patch[sz0:sz1, sx0:sx1] * window[sz0:sz1, sx0:sx1]
            weight[pz0:pz1, px0:px1] += window[sz0:sz1, sx0:sx1]

    return (out / weight).astype(np.float32)


def mountain_mass_ref(shape: tuple[int, int], circles: list[tuple[float, float, float]],
                      crest_height: float, relief_gain: float = 1.0, edge_uv: float = 0.09,
                      seed: int = 0, sample: np.ndarray = None) -> np.ndarray:
    """Mountain mass whose shape detail is borrowed from real reference terrain.

    The blob layout decides WHERE the mass is; the reference relief sample decides
    WHAT the flanks and crests look like (compound dome clusters, grass saddles,
    knobby feet). This replaces the smooth `mask^p * height` ramp that reads as a
    flat artificial mountain front in-engine.
    """
    if sample is None:
        sample, _ = load_relief_sample()
    mask = blob_mask(shape, circles, edge_uv=edge_uv, smooth_k_uv=edge_uv, seed=seed)
    relief = synth_from_sample(sample, shape, patch_px=56, seed=seed + 50)

    profile = mask ** 1.3
    # The borrowed relief MODULATES the profile (multiplicative) so the mass stays
    # sealed: saddles dip to ~(1-variation) of the crest, dome clusters rise above it.
    # A weaker additive term roughens the flanks and feet so the boundary is knobby
    # instead of a perfect falloff skirt.
    variation = np.clip(relief_gain * 0.7, 0.0, 0.85)
    sigma = float(relief.std()) + 1e-6
    rel_norm = np.clip(relief / (2.0 * sigma), -1.2, 1.2)
    height = profile * crest_height * (1.0 + variation * rel_norm)
    foot_gate = smoothstep(mask / 0.30) * (1.0 - profile * 0.6)
    relief_capped = np.clip(relief, -2.0 * sigma, 2.0 * sigma)
    height += relief_capped * 0.3 * relief_gain * foot_gate
    return np.maximum(height, np.minimum(0.0, relief_capped * 0.1)).astype(np.float32)


def scatter_knolls(field: np.ndarray, region_mask: np.ndarray, count: int,
                   radius_px: tuple[float, float], height: tuple[float, float],
                   seed: int = 0) -> np.ndarray:
    """Sprinkle rocky knolls (steep-sided dome bumps) where region_mask is high —
    foothill outcrops and freestanding rock mounds like the reference's plains."""
    rng = np.random.default_rng(seed)
    h, w = field.shape
    prob = np.clip(region_mask.astype(np.float64).ravel(), 0, None)
    if prob.sum() <= 0:
        return field
    prob /= prob.sum()
    picks = rng.choice(h * w, size=count, replace=False, p=prob)

    result = field.copy()
    for pick in picks:
        cz, cx = divmod(int(pick), w)
        r = float(rng.uniform(*radius_px))
        peak = float(rng.uniform(*height))
        z0, z1 = max(0, int(cz - 2 * r)), min(h, int(cz + 2 * r) + 1)
        x0, x1 = max(0, int(cx - 2 * r)), min(w, int(cx + 2 * r) + 1)
        zz, xx = np.meshgrid(np.arange(z0, z1), np.arange(x0, x1), indexing="ij")
        d = np.sqrt((zz - cz) ** 2 + (xx - cx) ** 2) / max(r, 1e-3)
        # slightly irregular outline
        d = d + (_hash_noise(xx * 0.7, zz * 0.7, seed) - 0.5) * 0.35
        result[z0:z1, x0:x1] += peak * (1.0 - smoothstep(d)) ** 1.5
    return result.astype(np.float32)


def mountain_mass(shape: tuple[int, int], circles: list[tuple[float, float, float]],
                  height: float, edge_uv: float = 0.07, spur_spacing_px: float = 20.0,
                  spur_amount: float = 0.5, peak_noise: float = 0.25, seed: int = 0) -> np.ndarray:
    """Height contribution of one organic mountain mass.

    The mass profile rises over edge_uv from the blob boundary; spurs (flow_ridges)
    carve the flanks perpendicular to the boundary; low-frequency noise varies the
    crest height so it never reads as a wall of constant height.
    """
    mask = blob_mask(shape, circles, edge_uv=edge_uv, smooth_k_uv=edge_uv, seed=seed)
    spurs = flow_ridges(shape, mask, spur_spacing_px, seed=seed + 100)
    crest = 1.0 + peak_noise * (fbm(shape, shape[1] * 0.25, octaves=3, seed=seed + 200) )
    profile = (mask ** 1.6) * (1.0 - spur_amount + spur_amount * spurs) * crest
    return (profile * height).astype(np.float32)


def terrace(field: np.ndarray, circles: list[tuple[float, float, float]], height: float,
            edge_uv: float = 0.015, wobble_uv: float = 0.012, seed: int = 0) -> np.ndarray:
    """Raise a blob-shaped region by `height` with a steep (but not vertical) cliff
    edge — the classic WoW two-level terrain step. Run roads across it AFTER this op:
    flatten_along pulls the crossing into a walkable ramp automatically."""
    mask = blob_mask(field.shape, circles, edge_uv=edge_uv, smooth_k_uv=edge_uv * 2,
                     wobble_uv=wobble_uv, seed=seed)
    return (field + mask * height).astype(np.float32)


def lake_basin(field: np.ndarray, circles: list[tuple[float, float, float]], bed_level: float,
               edge_uv: float = 0.03, islands: list[tuple[float, float, float, float]] = None,
               seed: int = 0) -> np.ndarray:
    """Carve a lake basin: inside the blob the terrain sinks to bed_level with shores
    blending over edge_uv. islands: list of (u, v, radius_uv, top_height) bumps rising
    from the bed (set top_height above your water level for a visible island)."""
    mask = blob_mask(field.shape, circles, edge_uv=edge_uv, smooth_k_uv=edge_uv * 1.5, seed=seed)
    bed = np.full_like(field, bed_level)
    if islands:
        for (iu, iv, ir, itop) in islands:
            bump = blob_mask(field.shape, [(iu, iv, ir)], edge_uv=ir * 1.2, smooth_k_uv=ir * 0.5,
                             wobble_uv=ir * 0.3, seed=seed + 1)
            bed = np.maximum(bed, bed_level + bump * (itop - bed_level))
    carved = field * (1.0 - mask) + np.minimum(field, bed) * mask
    return carved.astype(np.float32)


# ---------------------------------------------------------------------------
# Shaping
# ---------------------------------------------------------------------------

def radial_falloff(shape: tuple[int, int], center_uv: tuple[float, float] = (0.5, 0.5),
                   inner_radius: float = 0.25, outer_radius: float = 0.5) -> np.ndarray:
    """Mask in [0, 1]: 1 inside inner_radius, fading to 0 at outer_radius.

    Radii and center are normalized to the image diagonal/extents (uv space).
    """
    h, w = shape
    cols, rows = np.meshgrid(np.linspace(0, 1, w, dtype=np.float32), np.linspace(0, 1, h, dtype=np.float32))
    dist = np.sqrt((cols - center_uv[0]) ** 2 + (rows - center_uv[1]) ** 2)
    return 1.0 - smoothstep((dist - inner_radius) / max(outer_radius - inner_radius, 1e-6))


def edge_wall(shape: tuple[int, int], height: float, width_uv: float = 0.08,
              noise_amount: float = 0.35, seed: int = 0, fingers: bool = False,
              finger_spacing_px: float = None, finger_stretch: float = 4.0) -> np.ndarray:
    """Mountain rim along all four zone borders (classic WoW zone enclosure).

    width_uv: rim thickness normalized to the smaller image extent.
    fingers: carve the rim into parallel spur ridges running perpendicular to the
             border (measured Blizzard signature — spurs every ~60-120 yd pointing
             into the zone) instead of isotropic bumps.
    """
    h, w = shape
    cols, rows = np.meshgrid(np.linspace(0, 1, w, dtype=np.float32), np.linspace(0, 1, h, dtype=np.float32))
    border = np.minimum(np.minimum(cols, 1 - cols), np.minimum(rows, 1 - rows))
    rim = 1.0 - smoothstep(border / max(width_uv, 1e-6))

    if fingers:
        if finger_spacing_px is None:
            finger_spacing_px = min(shape) * width_uv * 0.35
        px_cols, px_rows = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
        # Ridge pattern varying ALONG each border, elongated ALONG the border normal:
        # vertical borders (west/east) vary with the row, horizontal ones with the column.
        vert = noise_at(px_rows / finger_spacing_px, px_cols / (finger_spacing_px * finger_stretch), seed=seed)
        horz = noise_at(px_cols / finger_spacing_px, px_rows / (finger_spacing_px * finger_stretch), seed=seed + 1)
        vert = 1.0 - np.abs(2.0 * vert - 1.0)
        horz = 1.0 - np.abs(2.0 * horz - 1.0)
        nearest_is_vertical = np.minimum(cols, 1 - cols) < np.minimum(rows, 1 - rows)
        ridge = np.where(nearest_is_vertical, vert, horz)
    else:
        ridge = ridged(shape, shape[1] * width_uv, octaves=4, seed=seed)

    profile = rim ** 1.5 * (1.0 - noise_amount + noise_amount * ridge)
    return (profile * height).astype(np.float32)


def catmull_rom(points_uv: list[tuple[float, float]], samples: int = 200) -> np.ndarray:
    """Smooth polyline through control points (uv space), returned as (samples, 2) array."""
    pts = np.asarray(points_uv, dtype=np.float32)
    if len(pts) < 2:
        raise ValueError("catmull_rom needs at least 2 control points")
    # Duplicate endpoints for the boundary segments
    ext = np.vstack([pts[0], pts, pts[-1]])
    result = []
    segments = len(pts) - 1
    per_segment = max(2, samples // segments)
    for i in range(segments):
        p0, p1, p2, p3 = ext[i], ext[i + 1], ext[i + 2], ext[i + 3]
        t = np.linspace(0, 1, per_segment, endpoint=(i == segments - 1), dtype=np.float32)[:, None]
        result.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t ** 2 +
                             (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3))
    return np.vstack(result)


def dist_to_polyline(shape: tuple[int, int], polyline_uv: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Distance (in pixels) from every pixel to a polyline, plus the arc-length parameter
    (0..1) of the closest point. Both as (h, w) float32 arrays."""
    h, w = shape
    pts = np.asarray(polyline_uv, dtype=np.float32) * np.array([w - 1, h - 1], dtype=np.float32)
    cols, rows = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    px = cols.ravel()
    pz = rows.ravel()

    seg_a = pts[:-1]
    seg_b = pts[1:]
    seg_len = np.sqrt(((seg_b - seg_a) ** 2).sum(axis=1))
    arc = np.concatenate([[0], np.cumsum(seg_len)])
    total_len = max(arc[-1], 1e-6)

    best_dist = np.full(px.shape, np.inf, dtype=np.float32)
    best_arc = np.zeros(px.shape, dtype=np.float32)
    for i in range(len(seg_a)):
        ax, az = seg_a[i]
        bx, bz = seg_b[i]
        abx, abz = bx - ax, bz - az
        denom = max(abx * abx + abz * abz, 1e-9)
        t = np.clip(((px - ax) * abx + (pz - az) * abz) / denom, 0.0, 1.0)
        dx = px - (ax + t * abx)
        dz = pz - (az + t * abz)
        dist = np.sqrt(dx * dx + dz * dz)
        closer = dist < best_dist
        best_dist = np.where(closer, dist, best_dist)
        best_arc = np.where(closer, (arc[i] + t * seg_len[i]) / total_len, best_arc)

    return best_dist.reshape(shape).astype(np.float32), best_arc.reshape(shape).astype(np.float32)


def meander_path(shape: tuple[int, int], polyline_uv, meander_px: float,
                 wavelength_px: float = 100.0, seed: int = 0) -> np.ndarray:
    """Resample a control polyline and push it sideways with smooth noise so it
    wanders naturally instead of running straight between control points."""
    path = catmull_rom(polyline_uv) if not isinstance(polyline_uv, np.ndarray) else polyline_uv
    h, w = shape
    px = path * np.array([w - 1, h - 1], dtype=np.float32)

    tangent = np.gradient(px, axis=0)
    seg_len = np.sqrt((tangent ** 2).sum(axis=1)) + 1e-6
    normal = np.stack([-tangent[:, 1] / seg_len, tangent[:, 0] / seg_len], axis=1)

    arc = np.concatenate([[0], np.cumsum(seg_len[1:])])
    coord = arc / max(wavelength_px, 1e-3)
    offset = (2.0 * noise_at(coord, np.full_like(coord, 3.7), seed=seed, octaves=2) - 1.0) * meander_px
    # pin the endpoints so entries/exits stay where the layout wants them
    fade = smoothstep(np.minimum(arc, arc[-1] - arc) / (0.12 * arc[-1] + 1e-6))
    px = px + normal * (offset * fade)[:, None]
    return (px / np.array([w - 1, h - 1], dtype=np.float32)).astype(np.float32)


def carve_channel(field: np.ndarray, polyline_uv, width_px: float, depth: float,
                  bank_px: float = None, bed_level: float = None,
                  meander_px: float = 0.0, meander_wavelength_px: float = 100.0,
                  width_noise: float = 0.0, bank_ragged_px: float = 0.0,
                  seed: int = 0) -> np.ndarray:
    """Carve a river channel along a smooth path.

    The bed is lowered by `depth` below the (path-smoothed) surrounding terrain,
    or down to the absolute `bed_level` if given. Banks blend over `bank_px`.
    meander_px adds natural wandering to the path, width_noise (0..~0.5) varies the
    channel width along its course, bank_ragged_px roughens the shoreline — without
    these the channel reads as a perfectly lined canal.
    """
    width_base = float(width_px)
    if bank_px is None:
        bank_px = width_base * 1.5
    if meander_px > 0:
        path = meander_path(field.shape, polyline_uv, meander_px, meander_wavelength_px, seed=seed)
    else:
        path = catmull_rom(polyline_uv) if not isinstance(polyline_uv, np.ndarray) else polyline_uv
    dist, arc = dist_to_polyline(field.shape, path)

    if bank_ragged_px > 0:
        h, w = field.shape
        cols, rows = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
        dist = dist + (2.0 * noise_at(cols / 5.0, rows / 5.0, seed=seed + 3, octaves=2) - 1.0) * bank_ragged_px

    if width_noise > 0:
        width_px = width_px * (1.0 + width_noise * (2.0 * noise_at(arc * 8.0, np.full_like(arc, 1.3), seed=seed + 7, octaves=2) - 1.0))

    # Sample terrain height along the path and smooth it so the bed flows downhill
    # gently instead of copying every bump of the surrounding terrain.
    samples = 256
    arc_bins = np.clip((arc * (samples - 1)).astype(np.int32), 0, samples - 1)
    near = dist < max(width_base, float(bank_px))
    path_height = np.zeros(samples, dtype=np.float32)
    fallback = float(field.mean())
    for i in range(samples):
        mask = near & (arc_bins == i)
        path_height[i] = field[mask].mean() if mask.any() else np.nan
    # Fill gaps and smooth along the path
    valid = ~np.isnan(path_height)
    if valid.any():
        path_height = np.interp(np.arange(samples), np.flatnonzero(valid), path_height[valid])
    else:
        path_height[:] = fallback
    kernel = np.ones(15, dtype=np.float32) / 15.0
    path_height = np.convolve(np.pad(path_height, 7, mode="edge"), kernel, mode="valid")

    local_surface = path_height[arc_bins]
    bed = (local_surface - depth) if bed_level is None else np.full_like(field, bed_level)

    t = smoothstep((dist - width_px * 0.5) / max(bank_px, 1e-6))
    carved = bed * (1.0 - t) + np.maximum(field, bed) * t
    return np.minimum(field, carved).astype(np.float32)


def flatten_along(field: np.ndarray, polyline_uv, width_px: float, blend_px: float = None) -> np.ndarray:
    """Flatten terrain along a path (roads): pulls heights toward the path-smoothed level."""
    if blend_px is None:
        blend_px = width_px
    path = catmull_rom(polyline_uv) if not isinstance(polyline_uv, np.ndarray) else polyline_uv
    dist, arc = dist_to_polyline(field.shape, path)

    samples = 256
    arc_bins = np.clip((arc * (samples - 1)).astype(np.int32), 0, samples - 1)
    near = dist < width_px + blend_px
    path_height = np.zeros(samples, dtype=np.float32)
    for i in range(samples):
        mask = near & (arc_bins == i)
        path_height[i] = field[mask].mean() if mask.any() else np.nan
    valid = ~np.isnan(path_height)
    if valid.any():
        path_height = np.interp(np.arange(samples), np.flatnonzero(valid), path_height[valid])
    else:
        return field
    kernel = np.ones(21, dtype=np.float32) / 21.0
    path_height = np.convolve(np.pad(path_height, 10, mode="edge"), kernel, mode="valid")

    target = path_height[arc_bins]
    t = 1.0 - smoothstep((dist - width_px * 0.5) / max(blend_px, 1e-6))
    return (field * (1.0 - t) + target * t).astype(np.float32)


def stamp_plateau(field: np.ndarray, center_uv: tuple[float, float], radius_uv: float,
                  level: float = None, blend_uv: float = None) -> np.ndarray:
    """Flatten a circular area (town/camp site) to `level` (default: current center height).

    radius_uv/blend_uv are fractions of the zone height (aspect-corrected on
    non-square zones, matching blob_mask conventions)."""
    h, w = field.shape
    aspect = (w - 1) / max(h - 1, 1)
    if blend_uv is None:
        blend_uv = radius_uv * 0.75
    cols, rows = np.meshgrid(np.linspace(0, 1, w, dtype=np.float32), np.linspace(0, 1, h, dtype=np.float32))
    dist = np.sqrt(((cols - center_uv[0]) * aspect) ** 2 + (rows - center_uv[1]) ** 2)
    if level is None:
        level = float(field[int(center_uv[1] * (h - 1)), int(center_uv[0] * (w - 1))])
    t = 1.0 - smoothstep((dist - radius_uv) / max(blend_uv, 1e-6))
    return (field * (1.0 - t) + level * t).astype(np.float32)


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def save_zone(field: np.ndarray, png_path, json_path, world: str,
              page_rect: tuple[int, int, int, int], material: str = "",
              pad: float = 2.0, water_level: float = None, water_material: str = "") -> dict:
    """Quantize a height field to 16-bit PNG plus metadata JSON for terrain_tool import.

    page_rect: (x0, z0, x1, z1), inclusive page indices.
    pad: extra world units added above/below the height range (headroom for later edits).
    water_level: if set, terrain_tool import flags water quads wherever the terrain
    sits below this height (rivers/lakes become real water in-game).
    Returns the metadata dict.
    """
    field = np.asarray(field, dtype=np.float32)
    x0, z0, x1, z1 = page_rect
    expected = resolution_for_pages(x1 - x0 + 1, z1 - z0 + 1)
    if (field.shape[1], field.shape[0]) != expected:
        print(f"note: field is {field.shape[1]}x{field.shape[0]}, lossless resolution for "
              f"this page rect is {expected[0]}x{expected[1]} (terrain_tool will resample)")

    min_y = float(field.min()) - pad
    max_y = float(field.max()) + pad
    scaled = np.clip((field - min_y) / (max_y - min_y) * 65535.0 + 0.5, 0, 65535).astype(np.uint16)

    Image.fromarray(scaled).save(str(png_path))

    meta = {
        "world": world,
        "pageRect": {"x0": x0, "z0": z0, "x1": x1, "z1": z1},
        "minY": min_y,
        "maxY": max_y,
        "material": material,
    }
    if water_level is not None:
        meta["waterLevel"] = float(water_level)
        if water_material:
            meta["waterMaterial"] = water_material
    Path(json_path).write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    return meta


def load_zone(png_path, json_path) -> tuple[np.ndarray, dict]:
    """Load a 16-bit PNG + JSON pair back into a float32 height field in world units."""
    meta = json.loads(Path(json_path).read_text(encoding="utf-8"))
    img = np.asarray(Image.open(str(png_path)), dtype=np.float32)
    if img.ndim == 3:
        img = img[..., 0]
    # Pillow loads I;16 as 16-bit; 8-bit fallbacks get rescaled
    scale = 65535.0 if img.max() > 255 else 255.0
    field = meta["minY"] + img / scale * (meta["maxY"] - meta["minY"])
    return field.astype(np.float32), meta
