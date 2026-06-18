#!/usr/bin/env python3
"""Inspect terrain pages, zone bindings, and terrain height for spawn placement."""

from __future__ import annotations

import argparse
import json
import math
import struct
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from npc_catalog_lib import find_project_root, load_catalog_bundle


TILES_PER_PAGE = 16
OUTER_VERTICES_PER_PAGE_SIDE = 129
LEGACY_VERTICES_PER_PAGE_SIDE = 273
PAGE_SIZE = 33.33333 * TILES_PER_PAGE
TILE_SIZE = 33.33333
WORLD_PAGES_PER_SIDE = 64
WORLD_CENTER_PAGE = WORLD_PAGES_PER_SIDE // 2
WORLD_HALF_SIZE = (WORLD_PAGES_PER_SIDE * PAGE_SIZE) * 0.5

CHUNK_VERSION = b"MVER"
CHUNK_VERTICES = b"MCVT"
CHUNK_AREAS = b"MCAR"


@dataclass(frozen=True)
class TerrainPageData:
    page_x: int
    page_z: int
    version: int
    heights: tuple[float, ...]
    areas: tuple[int, ...]
    path: Path


def map_legacy_index(new_idx: int, new_max: int, old_max: int) -> int:
    if new_max == 0:
        return 0
    t = float(new_idx) / float(new_max)
    mapped = t * float(old_max) + 0.5
    idx = int(mapped)
    return min(idx, old_max)


def world_to_page_index(x: float, z: float) -> tuple[int, int]:
    page_x = math.floor(x / PAGE_SIZE) + WORLD_CENTER_PAGE
    page_z = math.floor(z / PAGE_SIZE) + WORLD_CENTER_PAGE
    return int(page_x), int(page_z)


def page_origin(page_x: int, page_z: int) -> tuple[float, float]:
    return (page_x - WORLD_CENTER_PAGE) * PAGE_SIZE, (page_z - WORLD_CENTER_PAGE) * PAGE_SIZE


def world_to_global_tile(x: float, z: float) -> tuple[int, int] | None:
    tile_x = int((x + WORLD_HALF_SIZE) / TILE_SIZE)
    tile_z = int((z + WORLD_HALF_SIZE) / TILE_SIZE)
    if tile_x < 0 or tile_z < 0 or tile_x >= WORLD_PAGES_PER_SIDE * TILES_PER_PAGE or tile_z >= WORLD_PAGES_PER_SIDE * TILES_PER_PAGE:
        return None
    return tile_x, tile_z


def tile_bounds_from_global(global_tile_x: int, global_tile_z: int) -> dict[str, float]:
    min_x = global_tile_x * TILE_SIZE - WORLD_HALF_SIZE
    min_z = global_tile_z * TILE_SIZE - WORLD_HALF_SIZE
    return {
        "min_x": min_x,
        "max_x": min_x + TILE_SIZE,
        "min_z": min_z,
        "max_z": min_z + TILE_SIZE,
        "center_x": min_x + (TILE_SIZE * 0.5),
        "center_z": min_z + (TILE_SIZE * 0.5),
    }


def terrain_root_for_map(project_root: Path, map_directory: str) -> Path:
    return project_root / "data" / "client" / "Worlds" / map_directory / map_directory / "Terrain"


def page_path(project_root: Path, map_directory: str, page_x: int, page_z: int) -> Path:
    return terrain_root_for_map(project_root, map_directory) / f"{page_x}_{page_z}.tile"


def parse_tile_chunks(data: bytes) -> dict[bytes, bytes]:
    offset = 0
    chunks: dict[bytes, bytes] = {}
    while offset + 8 <= len(data):
        magic = data[offset : offset + 4]
        size = struct.unpack_from("<I", data, offset + 4)[0]
        start = offset + 8
        end = start + size
        if end > len(data):
            raise ValueError(f"Chunk {magic!r} exceeds file bounds")
        chunks[magic] = data[start:end]
        offset = end
    return chunks


def parse_heights(vertex_chunk: bytes, version: int) -> tuple[float, ...]:
    if version == 2:
        count = OUTER_VERTICES_PER_PAGE_SIDE * OUTER_VERTICES_PER_PAGE_SIDE
        fmt = f"<{count}f"
        expected = struct.calcsize(fmt)
        if len(vertex_chunk) != expected:
            raise ValueError(f"Unexpected v2 height chunk size: expected {expected}, got {len(vertex_chunk)}")
        return struct.unpack(fmt, vertex_chunk)

    if version == 1:
        legacy_count = LEGACY_VERTICES_PER_PAGE_SIDE * LEGACY_VERTICES_PER_PAGE_SIDE
        fmt = f"<{legacy_count}f"
        expected = struct.calcsize(fmt)
        if len(vertex_chunk) != expected:
            raise ValueError(f"Unexpected v1 height chunk size: expected {expected}, got {len(vertex_chunk)}")
        legacy = struct.unpack(fmt, vertex_chunk)
        resampled: list[float] = []
        for z in range(OUTER_VERTICES_PER_PAGE_SIDE):
            legacy_z = map_legacy_index(z, OUTER_VERTICES_PER_PAGE_SIDE - 1, LEGACY_VERTICES_PER_PAGE_SIDE - 1)
            for x in range(OUTER_VERTICES_PER_PAGE_SIDE):
                legacy_x = map_legacy_index(x, OUTER_VERTICES_PER_PAGE_SIDE - 1, LEGACY_VERTICES_PER_PAGE_SIDE - 1)
                resampled.append(legacy[legacy_x + legacy_z * LEGACY_VERTICES_PER_PAGE_SIDE])
        return tuple(resampled)

    raise ValueError(f"Unsupported terrain page version {version}")


def parse_areas(area_chunk: bytes) -> tuple[int, ...]:
    count = TILES_PER_PAGE * TILES_PER_PAGE
    fmt = f"<{count}I"
    expected = struct.calcsize(fmt)
    if len(area_chunk) != expected:
        raise ValueError(f"Unexpected area chunk size: expected {expected}, got {len(area_chunk)}")
    return struct.unpack(fmt, area_chunk)


@lru_cache(maxsize=512)
def load_terrain_page(project_root_str: str, map_directory: str, page_x: int, page_z: int) -> TerrainPageData:
    project_root = Path(project_root_str)
    tile_path = page_path(project_root, map_directory, page_x, page_z)
    if not tile_path.is_file():
        raise FileNotFoundError(f"Terrain page does not exist: {tile_path}")

    chunks = parse_tile_chunks(tile_path.read_bytes())
    if CHUNK_VERSION not in chunks:
        raise ValueError(f"Terrain page {tile_path} is missing {CHUNK_VERSION.decode('ascii')} chunk")

    version_chunk = chunks[CHUNK_VERSION]
    if len(version_chunk) < 4:
        raise ValueError(f"Terrain page {tile_path} has truncated version chunk")
    version = struct.unpack_from("<I", version_chunk, 0)[0]

    if CHUNK_VERTICES not in chunks:
        raise ValueError(f"Terrain page {tile_path} is missing {CHUNK_VERTICES.decode('ascii')} chunk")
    heights = parse_heights(chunks[CHUNK_VERTICES], version)

    if CHUNK_AREAS in chunks:
        areas = parse_areas(chunks[CHUNK_AREAS])
    else:
        areas = tuple(0 for _ in range(TILES_PER_PAGE * TILES_PER_PAGE))

    return TerrainPageData(page_x=page_x, page_z=page_z, version=version, heights=heights, areas=areas, path=tile_path)


def sample_height(page: TerrainPageData, world_x: float, world_z: float) -> float:
    origin_x, origin_z = page_origin(page.page_x, page.page_z)
    local_x = world_x - origin_x
    local_z = world_z - origin_z

    scale = PAGE_SIZE / float(OUTER_VERTICES_PER_PAGE_SIDE - 1)
    scaled_x = local_x / scale
    scaled_z = local_z / scale

    xi = int(scaled_x)
    zi = int(scaled_z)
    xpct = scaled_x - xi
    zpct = scaled_z - zi

    if xi < 0 or zi < 0:
        raise ValueError("World location falls before page start")

    if xi >= OUTER_VERTICES_PER_PAGE_SIDE - 1:
        xi = OUTER_VERTICES_PER_PAGE_SIDE - 2
        xpct = 1.0
    if zi >= OUTER_VERTICES_PER_PAGE_SIDE - 1:
        zi = OUTER_VERTICES_PER_PAGE_SIDE - 2
        zpct = 1.0

    side = OUTER_VERTICES_PER_PAGE_SIDE
    h00 = page.heights[xi + zi * side]
    h01 = page.heights[xi + (zi + 1) * side]
    h10 = page.heights[(xi + 1) + zi * side]
    h11 = page.heights[(xi + 1) + (zi + 1) * side]

    w00 = (1.0 - xpct) * (1.0 - zpct)
    w01 = (1.0 - xpct) * zpct
    w10 = xpct * (1.0 - zpct)
    w11 = xpct * zpct
    return (w00 * h00) + (w01 * h01) + (w10 * h10) + (w11 * h11)


def inspect_world_position(project_root: Path, map_entry, zone_index: dict[int, object], world_x: float, world_z: float) -> dict:
    page_x, page_z = world_to_page_index(world_x, world_z)
    page = load_terrain_page(str(project_root), map_entry.directory, page_x, page_z)
    height = sample_height(page, world_x, world_z)

    global_tile = world_to_global_tile(world_x, world_z)
    area_id = 0
    global_tile_x = None
    global_tile_z = None
    local_tile_x = None
    local_tile_z = None
    tile_bounds = None
    if global_tile is not None:
        global_tile_x, global_tile_z = global_tile
        local_tile_x = global_tile_x % TILES_PER_PAGE
        local_tile_z = global_tile_z % TILES_PER_PAGE
        area_id = page.areas[local_tile_x + local_tile_z * TILES_PER_PAGE]
        tile_bounds = tile_bounds_from_global(global_tile_x, global_tile_z)

    zone_entry = zone_index.get(area_id)
    origin_x, origin_z = page_origin(page_x, page_z)
    return {
        "map": {
            "id": map_entry.id,
            "name": map_entry.name,
            "directory": map_entry.directory,
        },
        "query": {
            "world_x": world_x,
            "world_z": world_z,
        },
        "terrain_page": {
            "page_x": page_x,
            "page_z": page_z,
            "origin_x": origin_x,
            "origin_z": origin_z,
            "path": str(page.path),
            "version": page.version,
        },
        "terrain_sample": {
            "height_y": height,
            "suggested_spawn_y": height,
        },
        "tile": {
            "global_tile_x": global_tile_x,
            "global_tile_z": global_tile_z,
            "local_tile_x": local_tile_x,
            "local_tile_z": local_tile_z,
            "bounds": tile_bounds,
        },
        "area": {
            "id": area_id,
            "name": zone_entry.name if zone_entry else None,
            "parentzone": zone_entry.parentzone if zone_entry and zone_entry.HasField("parentzone") else None,
        },
    }


def resolve_zone_ids(indexes: dict[str, dict[int, object]], map_id: int, zone_id: int | None, zone_name: str | None) -> list[int]:
    zones = indexes["zones"]
    if zone_id is not None:
        return [zone_id] if zone_id in zones else []

    if not zone_name:
        return []

    folded = zone_name.casefold()
    exact = [
        entry.id
        for entry in zones.values()
        if entry.name.casefold() == folded and (not entry.HasField("map") or entry.map == map_id)
    ]
    if exact:
        return sorted(exact)

    partial = [
        entry.id
        for entry in zones.values()
        if folded in entry.name.casefold() and (not entry.HasField("map") or entry.map == map_id)
    ]
    return sorted(partial)


def inspect_zone_coverage(project_root: Path, map_entry, indexes: dict[str, dict[int, object]], target_zone_ids: list[int]) -> dict:
    if not target_zone_ids:
        return {
            "map": {
                "id": map_entry.id,
                "name": map_entry.name,
                "directory": map_entry.directory,
            },
            "matched_zone_ids": [],
            "matched_zone_names": [],
            "tile_count": 0,
            "pages": [],
            "world_bounds": None,
            "sample_tile_centers": [],
        }

    terrain_dir = terrain_root_for_map(project_root, map_entry.directory)
    sample_tile_centers: list[dict[str, float | int]] = []
    pages: set[tuple[int, int]] = set()
    min_tile_x = None
    min_tile_z = None
    max_tile_x = None
    max_tile_z = None
    tile_count = 0

    for tile_file in sorted(terrain_dir.glob("*.tile")):
        page_name = tile_file.stem
        try:
            page_x_str, page_z_str = page_name.split("_", 1)
            page_x = int(page_x_str)
            page_z = int(page_z_str)
        except ValueError:
            continue

        page = load_terrain_page(str(project_root), map_entry.directory, page_x, page_z)
        for local_tile_z in range(TILES_PER_PAGE):
            for local_tile_x in range(TILES_PER_PAGE):
                area_id = page.areas[local_tile_x + local_tile_z * TILES_PER_PAGE]
                if area_id not in target_zone_ids:
                    continue

                global_tile_x = page_x * TILES_PER_PAGE + local_tile_x
                global_tile_z = page_z * TILES_PER_PAGE + local_tile_z
                tile_count += 1
                pages.add((page_x, page_z))

                min_tile_x = global_tile_x if min_tile_x is None else min(min_tile_x, global_tile_x)
                min_tile_z = global_tile_z if min_tile_z is None else min(min_tile_z, global_tile_z)
                max_tile_x = global_tile_x if max_tile_x is None else max(max_tile_x, global_tile_x)
                max_tile_z = global_tile_z if max_tile_z is None else max(max_tile_z, global_tile_z)

                if len(sample_tile_centers) < 12:
                    bounds = tile_bounds_from_global(global_tile_x, global_tile_z)
                    sample_tile_centers.append(
                        {
                            "page_x": page_x,
                            "page_z": page_z,
                            "local_tile_x": local_tile_x,
                            "local_tile_z": local_tile_z,
                            "world_x": bounds["center_x"],
                            "world_z": bounds["center_z"],
                        }
                    )

    world_bounds = None
    if min_tile_x is not None and min_tile_z is not None and max_tile_x is not None and max_tile_z is not None:
        min_bounds = tile_bounds_from_global(min_tile_x, min_tile_z)
        max_bounds = tile_bounds_from_global(max_tile_x, max_tile_z)
        world_bounds = {
            "min_x": min_bounds["min_x"],
            "max_x": max_bounds["max_x"],
            "min_z": min_bounds["min_z"],
            "max_z": max_bounds["max_z"],
        }

    return {
        "map": {
            "id": map_entry.id,
            "name": map_entry.name,
            "directory": map_entry.directory,
        },
        "matched_zone_ids": target_zone_ids,
        "matched_zone_names": [indexes["zones"][zone_id].name for zone_id in target_zone_ids],
        "tile_count": tile_count,
        "pages": [{"page_x": page_x, "page_z": page_z} for page_x, page_z in sorted(pages)],
        "world_bounds": world_bounds,
        "sample_tile_centers": sample_tile_centers,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--map-id", type=int, required=True)
    parser.add_argument("--world-x", type=float)
    parser.add_argument("--world-z", type=float)
    parser.add_argument("--zone-id", type=int)
    parser.add_argument("--zone-name")
    parser.add_argument("--pretty", action="store_true")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if (args.world_x is None) != (args.world_z is None):
        parser.error("--world-x and --world-z must be provided together")
    if args.world_x is None and args.zone_id is None and not args.zone_name:
        parser.error("provide either --world-x/--world-z, --zone-id, or --zone-name")

    project_root = find_project_root(args.project_root)
    _, catalogs, indexes = load_catalog_bundle(project_root)

    map_entry = indexes["maps"].get(args.map_id)
    if not map_entry:
        raise SystemExit(f"Unknown map id {args.map_id}")

    result: dict[str, object] = {}

    if args.world_x is not None and args.world_z is not None:
        result["world_position"] = inspect_world_position(project_root, map_entry, indexes["zones"], args.world_x, args.world_z)

    if args.zone_id is not None or args.zone_name:
        target_zone_ids = resolve_zone_ids(indexes, args.map_id, args.zone_id, args.zone_name)
        result["zone_coverage"] = inspect_zone_coverage(project_root, map_entry, indexes, target_zone_ids)

    if args.pretty:
        print(json.dumps(result, indent=2, sort_keys=False))
    else:
        print(json.dumps(result))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
