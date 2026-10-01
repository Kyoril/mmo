# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Terrain constants mirrored from src/shared/terrain/constants.h."""

from __future__ import annotations

import math
import struct

TILES_PER_PAGE = 16
CELLS_PER_TILE = 8
OUTER_PER_PAGE = 129          # (9 - 1) * 16 + 1 outer vertices per page side
INNER_PER_PAGE = 128          # one inner (centre) vertex per cell
LEGACY_OUTER_PER_PAGE = 273   # v1 pages
PIXELS_PER_PAGE = 1009        # (64 - 1) * 16 + 1 splat pixels per page side
WORLD_CENTER_PAGE = 32
MAX_PAGES = 64

# The engine declares `constexpr double TileSize = 33.33333f;` - a float literal widened to
# double. Use the same widened value so page and cell boundaries match the engine exactly.
TILE_SIZE = struct.unpack("<f", struct.pack("<f", 33.33333))[0]
PAGE_SIZE = TILE_SIZE * TILES_PER_PAGE
CELL_SIZE = PAGE_SIZE / INNER_PER_PAGE
PIXEL_SIZE = PAGE_SIZE / (PIXELS_PER_PAGE - 1)


def page_of(x: float, z: float) -> tuple[int, int]:
    """Returns the (page_x, page_z) index of the page containing world point (x, z)."""
    return int(math.floor(x / PAGE_SIZE)) + WORLD_CENTER_PAGE, int(math.floor(z / PAGE_SIZE)) + WORLD_CENTER_PAGE


def page_origin(page_x: int, page_z: int) -> tuple[float, float]:
    """Returns the world (x, z) of a page's minimum corner."""
    return (page_x - WORLD_CENTER_PAGE) * PAGE_SIZE, (page_z - WORLD_CENTER_PAGE) * PAGE_SIZE


def entity_page_index(x: float, z: float) -> int:
    """Folder index of the page holding world point (x, z): (page_x << 8) | page_z, as mmo_edit names them."""
    page_x, page_z = page_of(x, z)
    return (page_x << 8) | page_z
