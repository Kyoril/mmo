# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Room volumes and portal links for a world model (.hwmo), derived from its geometry.

    py -3 tools/world/wmo_rooms.py Models/Dungeon/Monastery_001.hwmo            # report only
    py -3 tools/world/wmo_rooms.py Models/Dungeon/Monastery_001.hwmo --write    # rewrite the file
    py -3 tools/world/wmo_rooms.py Models/Dungeon/Monastery_001.hwmo --image out.png

Portal culling starts in the room (group) that contains the camera. A group without containment
volumes falls back to its AABB, and the AABB of a modular room includes every wall, window and arch
assigned to it, so neighbouring rooms' boxes overlap and the camera is often credited to the wrong
room - whose portals then hide the room it is really in.

This derives each group's containment volumes from its walkable pieces (floors, platforms, stairs):
their footprints are rasterised on a grid and merged into as few boxes as possible, each reaching
from just below the floor to the group's ceiling. It then checks every portal: the group on either
side of it (sampled a little in front and behind the portal's centre) must be the pair the portal
references link. Mislinked portals are reported and, with --write, relinked.

Only portal references and containment volumes are rewritten; every other byte of the file is kept.
Re-run it after editing rooms in the world model editor.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/world"))
from worldkit.formats.hmsh import parse_hmsh  # noqa: E402
from worldkit.formats.hwmo import (ContainmentVolume, PortalRef, WorldModelRooms,  # noqa: E402
                                   parse_hwmo_rooms, write_hwmo_rooms)

CLIENT = ROOT / "data/client"
# Pieces a player can stand on. Their footprints are the rooms.
WALKABLE = ("floor", "platform", "stair", "ramp", "bridge")
CELL = 0.5
# Volumes start this far below the lowest floor of their cells, so a camera dipping under a floor
# edge (stairs, uneven tiles) still counts as inside.
BELOW_FLOOR = 1.5
# How far in front of and behind a portal the two linked rooms are looked for.
PORTAL_PROBE = (0.75, 1.5, 2.5)


def quat_matrix(w, x, y, z):
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
        [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
        [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)],
    ])


def piece_bounds(ref, cache):
    """Model-space AABB of one mesh reference, or None if its mesh cannot be read."""
    if ref.mesh not in cache:
        try:
            mesh = parse_hmsh(CLIENT / ref.mesh)
            points = [s.positions for s in mesh.submeshes if len(s.positions)]
            cache[ref.mesh] = np.concatenate(points).astype(float) if points else None
        except Exception as error:  # noqa: BLE001 - a missing piece only weakens the footprint
            print(f"  warning: cannot read {ref.mesh}: {error}")
            cache[ref.mesh] = None
    points = cache[ref.mesh]
    if points is None:
        return None
    matrix = quat_matrix(*ref.rotation) * np.array(ref.scale)
    world = points @ matrix.T + np.array(ref.position)
    return world.min(0), world.max(0)


def is_walkable(mesh: str) -> bool:
    lower = Path(mesh).stem.lower()
    return any(key in lower for key in WALKABLE)


def merge_cells(occupied: np.ndarray) -> list[tuple[int, int, int, int]]:
    """Greedy rectangle cover of a boolean grid: (i0, j0, i1, j1) half-open, rows first."""
    grid = occupied.copy()
    rects = []
    rows, cols = grid.shape
    for i in range(rows):
        j = 0
        while j < cols:
            if not grid[i, j]:
                j += 1
                continue
            j1 = j
            while j1 < cols and grid[i, j1]:
                j1 += 1
            i1 = i + 1
            while i1 < rows and grid[i1, j:j1].all():
                i1 += 1
            grid[i:i1, j:j1] = False
            rects.append((i, j, i1, j1))
            j = j1
    return rects


def derive_volumes(rooms: WorldModelRooms) -> list[list[ContainmentVolume]]:
    cache: dict = {}
    result = []
    for index, group in enumerate(rooms.groups):
        pieces = [b for b in (piece_bounds(r, cache) for r in group.mesh_refs if r.visible and is_walkable(r.mesh)) if b]
        if not pieces:
            print(f"  group {index} {group.name!r}: no walkable pieces, keeping its AABB")
            result.append([])
            continue
        lo = np.min([p[0] for p in pieces], axis=0)
        hi = np.max([p[1] for p in pieces], axis=0)
        x0, z0 = math.floor(lo[0] / CELL) * CELL, math.floor(lo[2] / CELL) * CELL
        cols = int(math.ceil((hi[0] - x0) / CELL))
        rows = int(math.ceil((hi[2] - z0) / CELL))
        floor_y = np.full((rows, cols), np.inf)
        for plo, phi in pieces:
            j0 = int(round((plo[0] - x0) / CELL))
            j1 = int(round((phi[0] - x0) / CELL))
            i0 = int(round((plo[2] - z0) / CELL))
            i1 = int(round((phi[2] - z0) / CELL))
            cell = floor_y[i0:max(i1, i0 + 1), j0:max(j1, j0 + 1)]
            np.minimum(cell, plo[1], out=cell)
        ceiling = max(group.bounds_max[1], hi[1]) + 0.5
        volumes = []
        for i0, j0, i1, j1 in merge_cells(np.isfinite(floor_y)):
            bottom = float(floor_y[i0:i1, j0:j1].min()) - BELOW_FLOOR
            box_lo = (x0 + j0 * CELL, bottom, z0 + i0 * CELL)
            box_hi = (x0 + j1 * CELL, ceiling, z0 + i1 * CELL)
            volumes.append(ContainmentVolume.box(f"{group.name or 'Group'} floor {len(volumes) + 1}", box_lo, box_hi))
        result.append(volumes)
    return result


def portal_frame(vertices):
    v = np.array(vertices, float)
    centre = v.mean(0)
    normal = np.cross(v[1] - v[0], v[2] - v[0])
    normal /= np.linalg.norm(normal)
    return centre, normal


def groups_at(rooms: WorldModelRooms, point) -> list[int]:
    return [i for i, g in enumerate(rooms.groups) if g.contains(point)]


def side_groups(rooms: WorldModelRooms, centre, normal, sign) -> set[int]:
    for distance in PORTAL_PROBE:
        # Probe at the portal centre's height and a little above the bottom edge: a tall portal's
        # centre can sit above a low room's ceiling.
        found = set()
        for lift in (0.0, -0.25):
            found.update(groups_at(rooms, centre + normal * distance * sign + np.array([0.0, lift, 0.0])))
        if found:
            return found
    return set()


def relink_portals(rooms: WorldModelRooms) -> list[str]:
    """Checks every portal's links against the rooms on its two sides and fixes the mislinked ones."""
    notes = []
    for index, vertices in enumerate(rooms.portals):
        linked = sorted({g for g, group in enumerate(rooms.groups) for r in group.portal_refs if r.portal == index})
        centre, normal = portal_frame(vertices)
        front = side_groups(rooms, centre, normal, 1.0)
        back = side_groups(rooms, centre, normal, -1.0)
        if len(front) != 1 or len(back) != 1 or front == back:
            notes.append(f"portal {index}: rooms in front {sorted(front)}, behind {sorted(back)} - "
                         f"cannot decide, keeping links {linked}")
            continue
        pair = sorted(front | back)
        if pair == linked:
            continue
        notes.append(f"portal {index}: linked {linked}, geometry says {pair} - relinked")
        for group in rooms.groups:
            group.portal_refs = [r for r in group.portal_refs if r.portal != index]
        a, b = pair
        rooms.groups[a].portal_refs.append(PortalRef(index, b, 1))
        rooms.groups[b].portal_refs.append(PortalRef(index, a, -1))
    for group in rooms.groups:
        group.portal_refs.sort(key=lambda r: r.portal)
    return notes


def ambiguity_report(rooms: WorldModelRooms) -> list[str]:
    """Walkable spots claimed by more than one room (harmless, but worth knowing) per group pair."""
    pairs: dict[tuple[int, int], int] = {}
    for g, group in enumerate(rooms.groups):
        for volume in group.volumes:
            lo, hi = np.array(volume.bounds_min), np.array(volume.bounds_max)
            for x in np.arange(lo[0] + CELL / 2, hi[0], CELL * 2):
                for z in np.arange(lo[2] + CELL / 2, hi[2], CELL * 2):
                    p = (x, lo[1] + BELOW_FLOOR + 1.5, z)
                    for other in groups_at(rooms, p):
                        if other > g:
                            pairs[(g, other)] = pairs.get((g, other), 0) + 1
    return [f"groups {a} and {b} share ~{n * (CELL * 2) ** 2:.0f} square units of floor" for (a, b), n in sorted(pairs.items())]


def draw(rooms: WorldModelRooms, path: Path):
    from PIL import Image, ImageDraw
    colours = [(230, 80, 80), (80, 200, 80), (80, 120, 255), (230, 200, 60), (200, 80, 230), (60, 220, 220),
               (240, 140, 60), (150, 150, 255), (120, 230, 160), (230, 120, 180)]
    boxes = [(g, v) for g, group in enumerate(rooms.groups) for v in group.volumes]
    if not boxes:
        return
    lo = np.min([v.bounds_min for _, v in boxes], axis=0) - 4
    hi = np.max([v.bounds_max for _, v in boxes], axis=0) + 4
    ppu = 6
    image = Image.new("RGB", (int((hi[0] - lo[0]) * ppu), int((hi[2] - lo[2]) * ppu)), (20, 20, 24))
    canvas = ImageDraw.Draw(image, "RGBA")

    def px(x, z):
        return (x - lo[0]) * ppu, (z - lo[2]) * ppu

    for g, volume in boxes:
        colour = colours[g % len(colours)]
        canvas.rectangle([px(volume.bounds_min[0], volume.bounds_min[2]), px(volume.bounds_max[0], volume.bounds_max[2])],
                         fill=colour + (90,), outline=colour + (255,))
    for g, group in enumerate(rooms.groups):
        if group.volumes:
            centre = np.mean([np.add(v.bounds_min, v.bounds_max) / 2 for v in group.volumes], axis=0)
            canvas.text(px(centre[0], centre[2]), f"{g} {group.name}", fill=(255, 255, 255))
    for index, vertices in enumerate(rooms.portals):
        v = np.array(vertices)
        canvas.line([px(v[:, 0].min(), v[:, 2].min()), px(v[:, 0].max(), v[:, 2].max())], fill=(255, 255, 255), width=3)
        canvas.text(px(v[:, 0].mean() + 0.5, v[:, 2].mean() + 0.5), f"P{index}", fill=(255, 255, 0))
    image.save(path)
    print(f"wrote {path} (model space, +X right, +Z down)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("model", help="world model path relative to data/client")
    parser.add_argument("--write", action="store_true", help="rewrite the file with the derived volumes and links")
    parser.add_argument("--image", type=Path, help="top-down picture of the derived volumes and portals")
    args = parser.parse_args()

    path = CLIENT / args.model
    rooms = parse_hwmo_rooms(path)
    print(f"{args.model}: {len(rooms.groups)} groups, {len(rooms.portals)} portals")

    for group, volumes in zip(rooms.groups, derive_volumes(rooms)):
        if volumes:
            group.volumes = volumes
    for index, group in enumerate(rooms.groups):
        print(f"  group {index} {group.name!r}: {len(group.volumes)} volume(s), "
              f"links {[(r.portal, r.group) for r in group.portal_refs]}")

    notes = relink_portals(rooms)
    for note in notes or ["all portal links match the geometry"]:
        print("  " + note)
    for note in ambiguity_report(rooms):
        print("  " + note)

    if args.image:
        draw(rooms, args.image)
    if args.write:
        path.write_bytes(write_hwmo_rooms(path, rooms))
        print(f"wrote {path}")
    else:
        print("dry run - pass --write to rewrite the file")


if __name__ == "__main__":
    main()
