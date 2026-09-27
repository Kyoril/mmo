#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Render a top-down review map of a world (or of one zone / place).

    python tools/world/render_map.py --map 0 --out generated/world/review/map_0.png
    python tools/world/render_map.py --map 0 --zone "Briarwatch March" --out generated/world/review/briarwatch.png
    python tools/world/render_map.py --map 0 --poi barrowfield --margin 150 --dump-state before.json --out before.png
    python tools/world/render_map.py --map 0 --poi barrowfield --margin 150 --diff before.json --out after.png
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

from worldkit.cli_query import open_map
from worldkit.constants import CELL_SIZE
from worldkit.render import ALL_LAYERS, RenderOptions, dump_state, render_map
from worldkit.report import quest_links
from worldkit.spawns import object_spawn_records, unit_spawn_records


def zone_bbox(query, data, zone: str, margin: float):
    area_id = int(zone) if zone.isdigit() else next((z.id for z in data.zones.values() if z.name.casefold() == zone.casefold()), None)
    if area_id is None:
        raise SystemExit(f"unknown zone '{zone}'")
    rows, cols = np.nonzero(query.snapshot.area == area_id)
    if rows.size == 0:
        raise SystemExit(f"zone '{zone}' (area {area_id}) has no terrain on this map")
    ox, oz = query.snapshot.origin
    return (ox + cols.min() * CELL_SIZE - margin, oz + rows.min() * CELL_SIZE - margin,
            ox + (cols.max() + 1) * CELL_SIZE + margin, oz + (rows.max() + 1) * CELL_SIZE + margin)


def poi_bbox(query, poi_id: str, margin: float):
    poi = query.atlas.poi(poi_id) if query.atlas else None
    if poi is None:
        raise SystemExit(f"unknown place '{poi_id}' (is data/world/atlas/map_<id>.json present?)")
    if "radius" in poi:
        (cx, cz), r = poi["center"], poi["radius"]
        return cx - r - margin, cz - r - margin, cx + r + margin, cz + r + margin
    xs = [p[0] for p in poi["polygon"]]
    zs = [p[1] for p in poi["polygon"]]
    return min(xs) - margin, min(zs) - margin, max(xs) + margin, max(zs) + margin


def content_bbox(query, spawns, margin: float):
    """Frame what exists (zoned terrain, props, spawns, atlas places) instead of every terrain page."""
    xs, zs = [], []
    rows, cols = np.nonzero(query.snapshot.area > 0)
    if rows.size:
        ox, oz = query.snapshot.origin
        xs += [ox + cols.min() * CELL_SIZE, ox + (cols.max() + 1) * CELL_SIZE]
        zs += [oz + rows.min() * CELL_SIZE, oz + (rows.max() + 1) * CELL_SIZE]
    xs += [e.position[0] for e in query.snapshot.entities] + [s.x for s in spawns]
    zs += [e.position[2] for e in query.snapshot.entities] + [s.z for s in spawns]
    if query.atlas:
        xs += [p["center"][0] for p in query.atlas.pois]
        zs += [p["center"][1] for p in query.atlas.pois]
    if not xs:
        return None
    return min(xs) - margin, min(zs) - margin, max(xs) + margin, max(zs) + margin


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    area = parser.add_mutually_exclusive_group()
    area.add_argument("--zone", help="zone name or area id")
    area.add_argument("--poi", help="atlas place id")
    area.add_argument("--bbox", type=float, nargs=4, metavar=("X0", "Z0", "X1", "Z1"))
    area.add_argument("--full", action="store_true", help="every terrain page instead of the content extent")
    parser.add_argument("--margin", type=float, default=50.0)
    parser.add_argument("--px-per-m", type=float)
    parser.add_argument("--layers", default=",".join(ALL_LAYERS))
    parser.add_argument("--color-by", choices=("level", "faction"), default="level")
    parser.add_argument("--diff", type=Path, help="state JSON from an earlier --dump-state")
    parser.add_argument("--dump-state", type=Path, help="write spawn/entity positions for a later --diff")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)

    layers = frozenset(l.strip() for l in args.layers.split(",") if l.strip())
    unknown = layers - set(ALL_LAYERS)
    if unknown:
        parser.error(f"unknown layers {sorted(unknown)}; choose from {ALL_LAYERS}")

    data, map_entry, query = open_map(args.map)
    spawns = unit_spawn_records(map_entry) + object_spawn_records(map_entry)
    bbox = tuple(args.bbox) if args.bbox else None
    if args.zone:
        bbox = zone_bbox(query, data, args.zone, args.margin)
    elif args.poi:
        bbox = poi_bbox(query, args.poi, args.margin)
    elif not args.bbox and not args.full:
        bbox = content_bbox(query, spawns, max(args.margin, 150.0))

    title = map_entry.name + (f" - {args.zone or args.poi}" if (args.zone or args.poi) else "")
    image = render_map(query.snapshot, spawns=spawns, unit_levels=data.unit_levels(),
                       unit_factions={uid: u.factionTemplate for uid, u in data.units.items()}, atlas=query.atlas,
                       kinds=query.kinds, quest_arrows=quest_links(data, map_entry),
                       options=RenderOptions(bbox=bbox, px_per_m=args.px_per_m, layers=layers, color_by=args.color_by, title=title),
                       diff_state=json.loads(args.diff.read_text(encoding="utf-8")) if args.diff else None)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.out)
    print(f"wrote {args.out} ({image.width}x{image.height})")
    if args.dump_state:
        args.dump_state.parent.mkdir(parents=True, exist_ok=True)
        args.dump_state.write_text(json.dumps(dump_state(query.snapshot, spawns)), encoding="utf-8")
        print(f"wrote {args.dump_state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
