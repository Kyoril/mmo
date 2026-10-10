# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Top-down survey of the Hollow Choir (map 1, Monastery_001 world model) for placing spawns.

    py -3 tools/hollow_choir/survey_layout.py [--spawns] [--out generated/hollow_choir/layout.png]

Renders the world model from above in world coordinates (floors light, walls dark, ceilings and
anything high above the floor left out), overlays the walkable navmesh as a sampled grid and a
labelled coordinate grid. With --spawns it also draws map 1's creature spawns and their aggro
circles, which is how group spacing is checked: circles of neighbouring groups must not overlap.

North is up (-Z), as on the minimap.
"""

import argparse
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/world"))
from worldkit.formats.hmsh import parse_hmsh  # noqa: E402
from worldkit.formats.hwmo import parse_hwmo  # noqa: E402
from worldkit.formats.wobj import parse_wobj  # noqa: E402
from worldkit.meshrender import Camera, DrawMesh, render  # noqa: E402
from worldkit.nav import NavQuery  # noqa: E402

CLIENT = ROOT / "data/client"
WORLD_DIR = "Test"
PIXELS_PER_UNIT = 8
# Pieces whose top is more than this above the WMO's floor level are roofs, galleries' undersides
# and the like; they would hide the floor plan.
MAX_HEIGHT = 7.0
# (height above the WMO origin, overlay colour). The first level that finds the navmesh wins.
PROBE_LEVELS = [(-3.0, (60, 120, 255, 80)), (-1.5, (60, 160, 255, 80)), (0.0, (60, 200, 90, 70)),
                (1.5, (200, 200, 60, 80)), (3.0, (240, 160, 40, 80)), (4.5, (240, 100, 40, 80)),
                (6.0, (240, 60, 160, 80)), (8.0, (200, 60, 240, 80)), (10.0, (150, 60, 240, 80))]


def quat_matrix(w, x, y, z):
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
        [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
        [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)],
    ])


def transform(position, rotation, scale):
    m = np.eye(4)
    m[:3, :3] = quat_matrix(*rotation) * np.array(scale)
    m[:3, 3] = position
    return m


def apply(points, matrix):
    return points @ matrix[:3, :3].T + matrix[:3, 3]


def piece_colour(name):
    lower = name.lower()
    if "floor" in lower or "platform" in lower:
        return (150, 150, 140)
    if "stair" in lower:
        return (120, 160, 200)
    if "column" in lower:
        return (230, 120, 90)
    return (75, 75, 80)


def world_meshes():
    entity = next(parse_wobj(p) for p in (CLIENT / "Worlds" / WORLD_DIR / WORLD_DIR / "Entities").glob("*/*.wobj"))
    root = transform(entity.position, entity.rotation, entity.scale)
    model = parse_hwmo(CLIENT / entity.asset)
    meshes, cache = [], {}
    for ref in model.mesh_refs:
        if not ref.visible or "ceiling" in ref.mesh.lower():
            continue
        if ref.mesh not in cache:
            try:
                cache[ref.mesh] = parse_hmsh(CLIENT / ref.mesh)
            except Exception:
                cache[ref.mesh] = None
        mesh = cache[ref.mesh]
        if mesh is None:
            continue
        matrix = root @ transform(ref.position, ref.rotation, ref.scale)
        for sub in mesh.submeshes:
            if not len(sub.positions):
                continue
            pos = apply(sub.positions.astype(float), matrix)
            if pos[:, 1].min() > entity.position[1] + MAX_HEIGHT:
                continue
            # Clip everything above the cut height so walls read as outlines, not roofs.
            pos[:, 1] = np.minimum(pos[:, 1], entity.position[1] + MAX_HEIGHT)
            meshes.append(DrawMesh(pos, sub.indices.reshape(-1, 3), color=piece_colour(ref.mesh)))
    return meshes, entity


def bounds_of(meshes):
    lo = np.min([m.positions.min(axis=0) for m in meshes], axis=0)
    hi = np.max([m.positions.max(axis=0) for m in meshes], axis=0)
    return lo, hi


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--spawns", action="store_true")
    parser.add_argument("--nav-step", type=float, default=1.0)
    parser.add_argument("--out", default=str(ROOT / "generated/hollow_choir/layout.png"))
    parser.add_argument("--aggro", type=float, default=0.0, help="aggro radius drawn around spawns")
    parser.add_argument("--crop", type=float, nargs=4, metavar=("X0", "X1", "Z0", "Z1"),
                        help="only this world rectangle")
    parser.add_argument("--ppu", type=int, default=PIXELS_PER_UNIT, help="pixels per world unit")
    args = parser.parse_args()

    meshes, entity = world_meshes()
    lo, hi = bounds_of(meshes)
    margin = 4.0
    x0, x1 = lo[0] - margin, hi[0] + margin
    z0, z1 = lo[2] - margin, hi[2] + margin
    if args.crop:
        x0, x1, z0, z1 = args.crop
    ppu = args.ppu
    width = int((x1 - x0) * ppu)
    height = int((z1 - z0) * ppu)
    cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
    # Looking straight down with north (-Z) up: up vector -Z.
    camera = Camera(eye=(cx, hi[1] + 50, cz), target=(cx, lo[1], cz), width=width, height=height,
                    ortho_height=(z1 - z0), up=(0.0, 0.0, -1.0))
    rgb, _ = render(meshes, camera)
    image = Image.fromarray(rgb.astype(np.uint8)).convert("RGB")
    draw = ImageDraw.Draw(image, "RGBA")

    def px(x, z):
        return ((x - x0) * ppu, (z - z0) * ppu)

    # Walkable navmesh, sampled. The floor sits at the WMO's height; probe a little above it.
    walkable = 0
    with NavQuery(ROOT / "data/editor/nav", WORLD_DIR, exe=ROOT / "bin/Release/nav_query.exe") as nav:
        step = args.nav_step
        z = z0
        while z <= z1:
            x = x0
            while x <= x1:
                # Probe several heights: podiums and galleries sit above the WMO's floor level.
                for level, colour in PROBE_LEVELS:
                    distance = nav.on_mesh((x, entity.position[1] + level, z), radius=0.6)
                    if distance is not None and distance < 0.6:
                        walkable += 1
                        a, b = px(x - step / 2, z - step / 2), px(x + step / 2, z + step / 2)
                        draw.rectangle([a, b], fill=colour)
                        break
                x += step
            z += step

    font = ImageFont.load_default()
    grid = 10 if (x1 - x0) > 40 else 2
    for gx in range(int(math.floor(x0 / grid)) * grid, int(x1) + 1, grid):
        draw.line([px(gx, z0), px(gx, z1)], fill=(255, 255, 255, 50))
        draw.text(px(gx + 0.3, z0 + 0.3), f"x{gx}", fill=(255, 255, 0), font=font)
    for gz in range(int(math.floor(z0 / grid)) * grid, int(z1) + 1, grid):
        draw.line([px(x0, gz), px(x1, gz)], fill=(255, 255, 255, 50))
        draw.text(px(x0 + 0.3, gz + 0.3), f"z{gz}", fill=(255, 255, 0), font=font)

    if args.spawns:
        sys.path.insert(0, str(ROOT / ".agents/skills/mmo-npc-designer/scripts"))
        from proto_runtime import load_modules
        mods = load_modules(ROOT)
        maps = mods["maps"].Maps()
        maps.ParseFromString((ROOT / "data/editor/data/maps.data").read_bytes())
        dungeon = next(m for m in maps.entry if m.id == 1)
        for spawn in dungeon.unitspawns:
            p = px(spawn.positionx, spawn.positionz)
            if args.aggro > 0:
                r = args.aggro * ppu
                draw.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], outline=(255, 80, 80, 120))
            draw.ellipse([p[0] - 3, p[1] - 3, p[0] + 3, p[1] + 3], fill=(255, 60, 60))
            draw.text((p[0] + 4, p[1] - 4), spawn.name or str(spawn.unitentry), fill=(255, 200, 200), font=font)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    image.save(out)
    print(f"world model at {entity.position}, bounds x {lo[0]:.1f}..{hi[0]:.1f} y {lo[1]:.1f}..{hi[1]:.1f} "
          f"z {lo[2]:.1f}..{hi[2]:.1f}; {walkable} walkable samples; wrote {out}")


if __name__ == "__main__":
    main()
