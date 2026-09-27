# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Terrain page (.tile) parser, mirroring src/shared/terrain_io/page_io.cpp and the legacy v1
path in src/shared/terrain/page.cpp.

v2 pages store 129x129 outer heights plus 128x128 inner (cell centre) heights. v1 pages store
273x273 heights only; they are resampled with the engine's legacy index mapping, and the inner
vertex is derived by averaging the cell's four corners (what the engine does when no MCVI chunk
exists).
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from ..constants import INNER_PER_PAGE, LEGACY_OUTER_PER_PAGE, OUTER_PER_PAGE, PIXELS_PER_PAGE, TILES_PER_PAGE
from .chunks import Cursor, FormatError, iter_chunks

TILE_COUNT = TILES_PER_PAGE * TILES_PER_PAGE

# Chunks the engine writes. Rendering-only chunks (normals, vertex colours) are recognised and
# skipped. Anything else is an unknown format change and is rejected, never guessed around.
_IGNORED = {b"MCNM", b"MCNI", b"MCVS", b"MCSI"}
_KNOWN = {b"MVER", b"MCMT", b"MCVT", b"MCVI", b"MCLY", b"MCAR", b"MHOL", b"MCWQ"} | _IGNORED


@dataclass
class TilePage:
    """One parsed terrain page. All grids are indexed [z, x]."""

    page_x: int
    page_z: int
    version: int
    path: Path
    outer: np.ndarray            # (129, 129) float32 outer vertex heights
    inner: np.ndarray            # (128, 128) float32 inner (cell centre) heights
    inner_from_file: bool        # False for v1 pages (inner derived from corners)
    areas: np.ndarray            # (16, 16) uint32 area/zone id per tile
    holes: np.ndarray            # (16, 16) uint64 per-tile hole bits (bit = ix + iz * 8)
    water_mask: np.ndarray       # (16, 16) uint64 per-tile water quad bits (bit = qx + qz * 8)
    water_type: np.ndarray       # (16, 16) uint8 terrain::WaterType per tile
    water_heights: np.ndarray    # (129, 129) float32 water surface per outer vertex
    materials: list[str]         # 256 per-tile material names; "" = the world's default material
    layers: np.ndarray           # (1009, 1009) uint32 splat pixels, layer 0 in the lowest byte

    def hole_cells(self) -> np.ndarray:
        """(128, 128) bool, True where the cell is a terrain hole."""
        return expand_tile_bits(self.holes)

    def water_cells(self) -> np.ndarray:
        """(128, 128) bool, True where the cell has a water quad."""
        return expand_tile_bits(self.water_mask)


def expand_tile_bits(masks: np.ndarray) -> np.ndarray:
    """Expands per-tile 8x8 bit masks (bit = ix + iz * 8) to a (128, 128) per-cell bool grid [z, x]."""
    bits = np.arange(64, dtype=np.uint64)
    flags = ((masks[..., None] >> bits) & np.uint64(1)).astype(bool)   # [tz, tx, iz*8+ix]
    flags = flags.reshape(TILES_PER_PAGE, TILES_PER_PAGE, 8, 8)          # [tz, tx, iz, ix]
    return flags.transpose(0, 2, 1, 3).reshape(INNER_PER_PAGE, INNER_PER_PAGE)


def _legacy_index(new_idx: int, new_max: int, old_max: int) -> int:
    mapped = int(float(new_idx) / float(new_max) * float(old_max) + 0.5)
    return min(mapped, old_max)


def _resample_legacy(heights: np.ndarray) -> np.ndarray:
    idx = np.array([_legacy_index(i, OUTER_PER_PAGE - 1, LEGACY_OUTER_PER_PAGE - 1) for i in range(OUTER_PER_PAGE)])
    return heights[np.ix_(idx, idx)].astype(np.float32)


def _average_inner(outer: np.ndarray) -> np.ndarray:
    return ((outer[:-1, :-1] + outer[:-1, 1:] + outer[1:, :-1] + outer[1:, 1:]) * 0.25).astype(np.float32)


def _expect_size(source: str, magic: bytes, payload: memoryview, size: int) -> None:
    if len(payload) != size:
        raise FormatError(f"{source}: chunk {magic!r} has {len(payload)} bytes, expected {size}")


def parse_tile(path: Path) -> TilePage:
    """Parses a .tile file named '<page_x>_<page_z>.tile'."""
    path = Path(path)
    source = str(path)
    try:
        page_x, page_z = (int(part) for part in path.stem.split("_", 1))
    except ValueError as exc:
        raise FormatError(f"{source}: file name must be '<page_x>_<page_z>.tile'") from exc

    chunks = iter_chunks(path.read_bytes(), source)
    if not chunks or chunks[0][0] != b"MVER":
        raise FormatError(f"{source}: first chunk must be MVER")
    _expect_size(source, b"MVER", chunks[0][1], 4)
    version = Cursor(chunks[0][1], source, b"MVER").u32()
    if version not in (1, 2):
        raise FormatError(f"{source}: unsupported terrain page version {version} (known: 1, 2)")

    outer = None
    inner = None
    areas = np.zeros((TILES_PER_PAGE, TILES_PER_PAGE), np.uint32)
    holes = np.zeros((TILES_PER_PAGE, TILES_PER_PAGE), np.uint64)
    water_mask = np.zeros((TILES_PER_PAGE, TILES_PER_PAGE), np.uint64)
    water_type = np.zeros((TILES_PER_PAGE, TILES_PER_PAGE), np.uint8)
    water_heights = np.zeros((OUTER_PER_PAGE, OUTER_PER_PAGE), np.float32)
    materials = [""] * TILE_COUNT
    layers = None

    for magic, payload in chunks[1:]:
        if magic not in _KNOWN:
            raise FormatError(f"{source}: unknown chunk {magic!r} (legacy MLCW water or a new engine chunk?)")
        if magic in _IGNORED:
            continue
        cur = Cursor(payload, source, magic)
        if magic == b"MCVT":
            side = OUTER_PER_PAGE if version == 2 else LEGACY_OUTER_PER_PAGE
            _expect_size(source, magic, payload, side * side * 4)
            heights = cur.floats(side * side).reshape(side, side)
            outer = heights if version == 2 else _resample_legacy(heights)
        elif magic == b"MCVI":
            _expect_size(source, magic, payload, INNER_PER_PAGE * INNER_PER_PAGE * 4)
            inner = cur.floats(INNER_PER_PAGE * INNER_PER_PAGE).reshape(INNER_PER_PAGE, INNER_PER_PAGE)
        elif magic == b"MCAR":
            _expect_size(source, magic, payload, TILE_COUNT * 4)
            areas = np.frombuffer(payload, dtype="<u4").astype(np.uint32).reshape(TILES_PER_PAGE, TILES_PER_PAGE)
        elif magic == b"MCLY":
            _expect_size(source, magic, payload, PIXELS_PER_PAGE * PIXELS_PER_PAGE * 4)
            layers = np.frombuffer(payload, dtype="<u4").reshape(PIXELS_PER_PAGE, PIXELS_PER_PAGE)
        elif magic == b"MCMT":
            count = cur.u16()
            if count > TILE_COUNT:
                raise FormatError(f"{source}: MCMT lists {count} materials, more than {TILE_COUNT} tiles")
            for index in range(count):
                materials[index] = cur.str16()
            if not cur.done():
                raise FormatError(f"{source}: MCMT has {cur.remaining()} trailing bytes")
        elif magic == b"MHOL":
            count = cur.u16()
            _expect_size(source, magic, payload, 2 + count * 10)
            for _ in range(count):
                tile_index = cur.u16()
                mask = cur.u64()
                if tile_index >= TILE_COUNT:
                    raise FormatError(f"{source}: MHOL tile index {tile_index} out of range")
                holes[tile_index // TILES_PER_PAGE, tile_index % TILES_PER_PAGE] = mask
        elif magic == b"MCWQ":
            count = cur.u16()
            for _ in range(count):
                tile_index = cur.u16()
                kind = cur.u8()
                mask = cur.u64()
                if tile_index >= TILE_COUNT:
                    raise FormatError(f"{source}: MCWQ tile index {tile_index} out of range")
                water_mask[tile_index // TILES_PER_PAGE, tile_index % TILES_PER_PAGE] = mask
                water_type[tile_index // TILES_PER_PAGE, tile_index % TILES_PER_PAGE] = kind
            water_heights = cur.floats(OUTER_PER_PAGE * OUTER_PER_PAGE).reshape(OUTER_PER_PAGE, OUTER_PER_PAGE)
            if cur.remaining():
                cur.str16()  # optional water material name, unused by worldkit
            if not cur.done():
                raise FormatError(f"{source}: MCWQ has {cur.remaining()} trailing bytes")

    if outer is None:
        raise FormatError(f"{source}: missing MCVT height chunk")
    if layers is None:
        raise FormatError(f"{source}: missing MCLY layer chunk")
    inner_from_file = inner is not None
    if inner is None:
        inner = _average_inner(outer)

    return TilePage(page_x, page_z, version, path, outer, inner, inner_from_file, areas, holes,
                    water_mask, water_type, water_heights, materials, layers)
