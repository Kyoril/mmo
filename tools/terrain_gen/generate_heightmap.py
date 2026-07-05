# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Generate a zone heightmap from a JSON recipe.

Usage:
  python generate_heightmap.py recipe.json [--out zone.png]

Recipe format (distances in world units unless stated otherwise, positions in
normalized zone coordinates 0..1 with u -> +X / east, v -> +Z / south):

{
  "world": "MyZone",
  "pageRect": { "x0": 30, "z0": 30, "x1": 33, "z1": 33 },
  "seed": 42,
  "baseHeight": 100.0,
  "material": "",
  "ops": [
    { "op": "fbm",      "amplitude": 15, "featureSize": 400, "octaves": 5 },
    { "op": "ridged",   "amplitude": 40, "featureSize": 600, "mask": "edges" },
    { "op": "warp",     "strength": 60, "scale": 500 },
    { "op": "edgeWall", "height": 120, "width": 250 },
    { "op": "falloff",  "center": [0.5, 0.5], "inner": 0.3, "outer": 0.5, "drop": 30 },
    { "op": "river",    "points": [[0.1, 0.4], [0.5, 0.5], [0.9, 0.7]], "width": 25, "depth": 5 },
    { "op": "road",     "points": [[0.2, 0.8], [0.5, 0.5]], "width": 12 },
    { "op": "plateau",  "center": [0.5, 0.5], "radius": 80, "blend": 40 },
    { "op": "smooth",   "sigma": 8 },
    { "op": "erode",    "iterations": 25, "talus": 1.0 }
  ]
}

An op may set "mask": "edges" (only affect the zone border area) or
"mask": "center" (only the middle). For full control, write a Python script
using terrain_lib directly instead of a recipe.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

import terrain_lib as tl


def build_mask(kind: str | None, shape) -> np.ndarray | None:
    if kind == "edges":
        return 1.0 - tl.radial_falloff(shape, inner_radius=0.28, outer_radius=0.48)
    if kind == "center":
        return tl.radial_falloff(shape, inner_radius=0.28, outer_radius=0.48)
    return None


def apply_op(field: np.ndarray, op: dict, upp: float, seed: int) -> np.ndarray:
    kind = op["op"]
    shape = field.shape
    mask = build_mask(op.get("mask"), shape)
    op_seed = op.get("seed", seed) + hash(kind) % 1000

    def masked(delta: np.ndarray) -> np.ndarray:
        return field + (delta * mask if mask is not None else delta)

    if kind == "fbm":
        noise = tl.fbm(shape, op.get("featureSize", 400) / upp, octaves=op.get("octaves", 5), seed=op_seed)
        return masked(noise * op.get("amplitude", 10))

    if kind == "ridged":
        noise = tl.ridged(shape, op.get("featureSize", 500) / upp, octaves=op.get("octaves", 5), seed=op_seed)
        return masked(noise * op.get("amplitude", 30))

    if kind == "warp":
        return tl.domain_warp(field, op.get("strength", 50) / upp, op.get("scale", 400) / upp, seed=op_seed)

    if kind == "edgeWall":
        width_uv = op.get("width", 250) / upp / min(shape)
        spacing = op.get("fingerSpacing", 90) / upp
        return field + tl.edge_wall(shape, op.get("height", 100), width_uv=width_uv,
                                    noise_amount=op.get("noise", 0.35), seed=op_seed,
                                    fingers=op.get("fingers", False), finger_spacing_px=spacing)

    if kind == "falloff":
        falloff = tl.radial_falloff(shape, tuple(op.get("center", [0.5, 0.5])),
                                    op.get("inner", 0.3), op.get("outer", 0.5))
        return field - (1.0 - falloff) * op.get("drop", 20)

    if kind == "river":
        return tl.carve_channel(field, op["points"], op.get("width", 20) / upp,
                                op.get("depth", 4), op.get("bank", op.get("width", 20) * 1.5) / upp,
                                bed_level=op.get("bedLevel"))

    if kind == "road":
        return tl.flatten_along(field, op["points"], op.get("width", 10) / upp,
                                op.get("blend", op.get("width", 10)) / upp)

    if kind == "plateau":
        radius_uv = op.get("radius", 60) / upp / min(shape)
        blend_uv = op.get("blend", op.get("radius", 60) * 0.6) / upp / min(shape)
        return tl.stamp_plateau(field, tuple(op["center"]), radius_uv, op.get("level"), blend_uv)

    if kind == "smooth":
        return tl.gaussian_blur(field, op.get("sigma", 5) / upp)

    if kind == "erode":
        return tl.thermal_erode(field, op.get("iterations", 25),
                                op.get("talus", 1.0), op.get("amount", 0.25))

    raise ValueError(f"unknown op: {kind}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("recipe", type=Path)
    parser.add_argument("--out", type=Path, default=None, help="output PNG path (default: recipe name .png)")
    args = parser.parse_args()

    recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
    rect = recipe["pageRect"]
    pages_x = rect["x1"] - rect["x0"] + 1
    pages_z = rect["z1"] - rect["z0"] + 1
    width, height = tl.resolution_for_pages(pages_x, pages_z)
    upp = tl.units_per_pixel((height, width), pages_x)
    seed = recipe.get("seed", 0)

    print(f"generating {width}x{height} px ({pages_x}x{pages_z} pages, {upp:.3f} units/px, seed {seed})")

    field = np.full((height, width), float(recipe.get("baseHeight", 0.0)), dtype=np.float32)
    for op in recipe.get("ops", []):
        print(f"  applying {op['op']}...")
        field = apply_op(field, op, upp, seed)

    out_png = args.out or args.recipe.with_suffix(".png")
    out_json = out_png.with_suffix(".json")
    if out_json.resolve() == args.recipe.resolve():
        print("error: the metadata sidecar would overwrite the recipe file itself - "
              "pass an --out name that differs from the recipe name (e.g. zone.png for recipe zone_recipe.json)")
        return 1
    meta = tl.save_zone(field, out_png, out_json, recipe["world"],
                        (rect["x0"], rect["z0"], rect["x1"], rect["z1"]),
                        material=recipe.get("material", ""))
    print(f"wrote {out_png} and {out_json} (height range {meta['minY']:.1f} to {meta['maxY']:.1f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
