#!/usr/bin/env python3
"""Inspect terrain pages, zone bindings, and terrain height for spawn placement.

Thin CLI over tools/world/worldkit, the single terrain parser in this repository. Heights come from
the rendered surface (the four-triangle fan around each cell's stored inner vertex), so they can
differ from older bilinear results where inner vertices were sculpted. Also reports slope, water,
holes, terrain kind and whether the spot is placeable.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from npc_catalog_lib import find_project_root, load_catalog_bundle

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "world"))

from worldkit.atlas import load_atlas_or_none  # noqa: E402
from worldkit.constants import CELLS_PER_TILE, INNER_PER_PAGE, MAX_PAGES, PAGE_SIZE, TILE_SIZE, TILES_PER_PAGE, page_of, page_origin  # noqa: E402
from worldkit.paths import atlas_path, terrain_dir  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import load_snapshot  # noqa: E402
from worldkit.terrain_kinds import load_kinds  # noqa: E402

WORLD_HALF_SIZE = MAX_PAGES * PAGE_SIZE * 0.5


def tile_bounds_from_global(global_tile_x: int, global_tile_z: int) -> dict[str, float]:
    min_x = global_tile_x * TILE_SIZE - WORLD_HALF_SIZE
    min_z = global_tile_z * TILE_SIZE - WORLD_HALF_SIZE
    return {"min_x": min_x, "max_x": min_x + TILE_SIZE, "min_z": min_z, "max_z": min_z + TILE_SIZE,
            "center_x": min_x + TILE_SIZE * 0.5, "center_z": min_z + TILE_SIZE * 0.5}


def map_block(map_entry) -> dict:
    return {"id": map_entry.id, "name": map_entry.name, "directory": map_entry.directory}


def inspect_world_position(project_root: Path, map_entry, zone_index, world_x: float, world_z: float) -> dict:
    snapshot = load_snapshot(map_entry.directory, repo=project_root)
    query = WorldQuery(snapshot, load_atlas_or_none(atlas_path(map_entry.id, project_root)), load_kinds(),
                       {zone_id: zone.name for zone_id, zone in zone_index.items()})
    page_x, page_z = page_of(world_x, world_z)
    tile_path = terrain_dir(map_entry.directory, project_root) / f"{page_x}_{page_z}.tile"
    located = snapshot.page_local(world_x, world_z)
    if located is None:
        raise SystemExit(f"Terrain page does not exist: {tile_path}")
    slot = located[0]
    height = query.height_at(world_x, world_z)
    ok, reasons = query.placeable(world_x, world_z)
    global_tile_x = int((world_x + WORLD_HALF_SIZE) / TILE_SIZE)
    global_tile_z = int((world_z + WORLD_HALF_SIZE) / TILE_SIZE)
    area_id = query.area_at(world_x, world_z) or 0
    zone_entry = zone_index.get(area_id)
    origin_x, origin_z = page_origin(page_x, page_z)
    return {
        "map": map_block(map_entry),
        "query": {"world_x": world_x, "world_z": world_z},
        "terrain_page": {"page_x": page_x, "page_z": page_z, "origin_x": origin_x, "origin_z": origin_z,
                         "path": str(tile_path), "version": int(snapshot.page_versions[slot])},
        "terrain_sample": {"height_y": height, "suggested_spawn_y": height,
                           "slope_deg": query.slope_at(world_x, world_z),
                           "water_depth": query.water_depth_at(world_x, world_z),
                           "hole": query.hole_at(world_x, world_z),
                           "terrain_kind": query.terrain_kind_at(world_x, world_z)},
        "placement": {"ok": ok, "reasons": reasons,
                      "pois": [p["name"] for p in (query.atlas.pois_at(world_x, world_z) if query.atlas else [])]},
        "tile": {"global_tile_x": global_tile_x, "global_tile_z": global_tile_z,
                 "local_tile_x": global_tile_x % TILES_PER_PAGE, "local_tile_z": global_tile_z % TILES_PER_PAGE,
                 "bounds": tile_bounds_from_global(global_tile_x, global_tile_z)},
        "area": {"id": area_id, "name": zone_entry.name if zone_entry else None,
                 "parentzone": zone_entry.parentzone if zone_entry and zone_entry.HasField("parentzone") else None},
    }


def resolve_zone_ids(indexes, map_id: int, zone_id: int | None, zone_name: str | None) -> list[int]:
    zones = indexes["zones"]
    if zone_id is not None:
        return [zone_id] if zone_id in zones else []
    if not zone_name:
        return []
    folded = zone_name.casefold()
    candidates = [e for e in zones.values() if not e.HasField("map") or e.map == map_id]
    exact = sorted(e.id for e in candidates if e.name.casefold() == folded)
    return exact or sorted(e.id for e in candidates if folded in e.name.casefold())


def inspect_zone_coverage(project_root: Path, map_entry, indexes, target_zone_ids: list[int]) -> dict:
    result = {"map": map_block(map_entry), "matched_zone_ids": target_zone_ids,
              "matched_zone_names": [indexes["zones"][z].name for z in target_zone_ids],
              "tile_count": 0, "pages": [], "world_bounds": None, "sample_tile_centers": []}
    if not target_zone_ids:
        return result
    snapshot = load_snapshot(map_entry.directory, repo=project_root)
    pages = set()
    tiles = []
    for pz in range(snapshot.pages_z):
        for px in range(snapshot.pages_x):
            if snapshot.page_slot[pz, px] < 0:
                continue
            page_x, page_z = snapshot.page_min_x + px, snapshot.page_min_z + pz
            rows = slice(pz * INNER_PER_PAGE, (pz + 1) * INNER_PER_PAGE, CELLS_PER_TILE)
            cols = slice(px * INNER_PER_PAGE, (px + 1) * INNER_PER_PAGE, CELLS_PER_TILE)
            tile_areas = snapshot.area[rows, cols]
            for local_z in range(TILES_PER_PAGE):
                for local_x in range(TILES_PER_PAGE):
                    if int(tile_areas[local_z, local_x]) in target_zone_ids:
                        pages.add((page_x, page_z))
                        tiles.append((page_x, page_z, local_x, local_z,
                                      page_x * TILES_PER_PAGE + local_x, page_z * TILES_PER_PAGE + local_z))
    result["tile_count"] = len(tiles)
    result["pages"] = [{"page_x": x, "page_z": z} for x, z in sorted(pages)]
    if tiles:
        lo = tile_bounds_from_global(min(t[4] for t in tiles), min(t[5] for t in tiles))
        hi = tile_bounds_from_global(max(t[4] for t in tiles), max(t[5] for t in tiles))
        result["world_bounds"] = {"min_x": lo["min_x"], "max_x": hi["max_x"], "min_z": lo["min_z"], "max_z": hi["max_z"]}
        for page_x, page_z, local_x, local_z, gx, gz in tiles[:12]:
            bounds = tile_bounds_from_global(gx, gz)
            result["sample_tile_centers"].append({"page_x": page_x, "page_z": page_z, "local_tile_x": local_x,
                                                  "local_tile_z": local_z, "world_x": bounds["center_x"],
                                                  "world_z": bounds["center_z"]})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--map-id", type=int, required=True)
    parser.add_argument("--world-x", type=float)
    parser.add_argument("--world-z", type=float)
    parser.add_argument("--zone-id", type=int)
    parser.add_argument("--zone-name")
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    if (args.world_x is None) != (args.world_z is None):
        parser.error("--world-x and --world-z must be provided together")
    if args.world_x is None and args.zone_id is None and not args.zone_name:
        parser.error("provide either --world-x/--world-z, --zone-id, or --zone-name")

    project_root = find_project_root(args.project_root)
    _, _, indexes = load_catalog_bundle(project_root)
    map_entry = indexes["maps"].get(args.map_id)
    if not map_entry:
        raise SystemExit(f"Unknown map id {args.map_id}")

    result = {}
    if args.world_x is not None:
        result["world_position"] = inspect_world_position(project_root, map_entry, indexes["zones"], args.world_x, args.world_z)
    if args.zone_id is not None or args.zone_name:
        result["zone_coverage"] = inspect_zone_coverage(project_root, map_entry, indexes,
                                                        resolve_zone_ids(indexes, args.map_id, args.zone_id, args.zone_name))
    print(json.dumps(result, indent=2 if args.pretty else None))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
