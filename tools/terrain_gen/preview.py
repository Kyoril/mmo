# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Render a shaded relief preview of a zone heightmap (16-bit PNG + JSON pair).

Usage:
  python preview.py zone.png [--out zone_preview.png] [--water-level Y] [--contours N] [--scale N]
  python preview.py --diff a.png b.png [--out diff.png]

The JSON sidecar is looked up next to each PNG (same name, .json extension).
Lighting matches `terrain_tool preview` (sun from the northwest), so the two
outputs are directly comparable.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from terrain_lib import PAGE_SIZE, load_zone

# Same hypsometric ramp as terrain_tool's preview command
HEIGHT_RAMP = [
    (0.00, (0.13, 0.35, 0.16)),
    (0.35, (0.42, 0.58, 0.28)),
    (0.60, (0.66, 0.58, 0.36)),
    (0.80, (0.52, 0.42, 0.30)),
    (1.00, (0.93, 0.93, 0.93)),
]


def sample_ramp(t: np.ndarray) -> np.ndarray:
    rgb = np.zeros(t.shape + (3,), dtype=np.float32)
    stops = HEIGHT_RAMP
    rgb[t <= stops[0][0]] = stops[0][1]
    for (t0, c0), (t1, c1) in zip(stops[:-1], stops[1:]):
        mask = (t > t0) & (t <= t1)
        f = ((t - t0) / (t1 - t0))[mask, None]
        rgb[mask] = np.array(c0, dtype=np.float32) * (1 - f) + np.array(c1, dtype=np.float32) * f
    rgb[t > stops[-1][0]] = stops[-1][1]
    return rgb


def render(field: np.ndarray, meta: dict, water_level: float | None,
           contours: float, scale: int) -> Image.Image:
    pages_x = meta["pageRect"]["x1"] - meta["pageRect"]["x0"] + 1
    spacing = pages_x * PAGE_SIZE / (field.shape[1] - 1)

    # Central-difference normals (Y-up), matching the engine's slope sense
    dz_dx = np.gradient(field, spacing, axis=1)
    dz_dz = np.gradient(field, spacing, axis=0)
    normal = np.dstack([-dz_dx, np.ones_like(field), -dz_dz])
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)

    to_sun = np.array([-1.0, 1.0, -1.0], dtype=np.float32)
    to_sun /= np.linalg.norm(to_sun)
    diffuse = np.clip((normal * to_sun).sum(axis=2), 0.0, None)
    lighting = 0.25 + 0.75 * diffuse

    span = max(float(field.max() - field.min()), 0.001)
    tint = sample_ramp((field - field.min()) / span)

    rgb = tint * lighting[..., None]

    if contours > 0:
        band = np.floor(field / contours)
        edge = (np.diff(band, axis=0, append=band[-1:, :]) != 0) | \
               (np.diff(band, axis=1, append=band[:, -1:]) != 0)
        rgb[edge] *= 0.55

    if water_level is not None:
        depth = np.clip((water_level - field) / 10.0, 0.0, 1.0)
        water = np.array([0.15, 0.35, 0.7], dtype=np.float32)
        under = field < water_level
        blend = (0.55 + 0.35 * depth)[under, None]
        rgb[under] = rgb[under] * (1 - blend) + water * blend

    img = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), mode="RGB")
    if scale > 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    return img


def diff(path_a: Path, path_b: Path, out: Path) -> int:
    field_a, meta_a = load_zone(path_a, path_a.with_suffix(".json"))
    field_b, _ = load_zone(path_b, path_b.with_suffix(".json"))
    if field_a.shape != field_b.shape:
        print(f"error: shapes differ: {field_a.shape} vs {field_b.shape}")
        return 1

    delta = field_b - field_a
    max_err = float(np.abs(delta).max())
    mean_err = float(np.abs(delta).mean())
    quant_step = (meta_a["maxY"] - meta_a["minY"]) / 65535.0
    print(f"max |diff|:  {max_err:.5f} world units")
    print(f"mean |diff|: {mean_err:.5f} world units")
    print(f"quantization step of A: {quant_step:.5f} world units")

    # Signed difference heat map: blue = B lower, white = equal, red = B higher
    limit = max(max_err, 1e-6)
    t = np.clip(delta / limit, -1.0, 1.0)
    rgb = np.ones(delta.shape + (3,), dtype=np.float32)
    pos = t > 0
    rgb[pos, 1] = 1 - t[pos]
    rgb[pos, 2] = 1 - t[pos]
    neg = t < 0
    rgb[neg, 0] = 1 + t[neg]
    rgb[neg, 1] = 1 + t[neg]
    Image.fromarray((rgb * 255).astype(np.uint8), mode="RGB").save(str(out))
    print(f"wrote {out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("png", type=Path, help="zone heightmap PNG (JSON sidecar expected next to it)")
    parser.add_argument("diff_b", nargs="?", type=Path, help="second PNG when using --diff")
    parser.add_argument("--diff", action="store_true", help="compare two heightmaps instead of rendering")
    parser.add_argument("--out", type=Path, default=None, help="output image path")
    parser.add_argument("--water-level", type=float, default=None, help="tint pixels below this world height")
    parser.add_argument("--contours", type=float, default=0.0, help="contour interval in world units")
    parser.add_argument("--scale", type=int, default=1, help="integer upscale factor")
    args = parser.parse_args()

    if args.diff:
        if not args.diff_b:
            parser.error("--diff needs two PNG paths")
        out = args.out or args.png.with_name(args.png.stem + "_diff.png")
        return diff(args.png, args.diff_b, out)

    field, meta = load_zone(args.png, args.png.with_suffix(".json"))
    out = args.out or args.png.with_name(args.png.stem + "_preview.png")
    water_level = args.water_level if args.water_level is not None else meta.get("waterLevel")
    render(field, meta, water_level, args.contours, args.scale).save(str(out))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
