# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placed world entity (.wobj) parser, mirroring src/shared/game_common/world_entity_loader.cpp.

Layout: REVW (uint32 version 1..3), then exactly one of
  HSMW mesh:  u64 id, str16 mesh, 3f pos, 4f rot (w,x,y,z), 3f scale, u8 n + n*(u8 idx, str16 mat),
              version > 1: str8 name, str16 category
  OMWW world model: u64 id, str16 hwmo, 3f pos, 4f rot, 3f scale, version >= 3: str8 name, str16 category
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .chunks import Cursor, FormatError, iter_chunks


@dataclass(frozen=True)
class WorldEntity:
    unique_id: int
    kind: str                                    # "mesh" | "wmo"
    asset: str                                   # .hmsh or .hwmo path relative to data/client
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]  # (w, x, y, z)
    scale: tuple[float, float, float]
    name: str
    category: str
    material_overrides: tuple[tuple[int, str], ...]
    path: str


def parse_wobj(path: Path) -> WorldEntity:
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    if len(chunks) != 2 or chunks[0][0] != b"REVW":
        raise FormatError(f"{source}: expected REVW followed by one entity chunk, found {[m for m, _ in chunks]}")
    version = Cursor(chunks[0][1], source, b"REVW").u32()
    if not 1 <= version <= 3:
        raise FormatError(f"{source}: unsupported world entity version {version} (known: 1..3)")

    magic, payload = chunks[1]
    if magic not in (b"HSMW", b"OMWW"):
        raise FormatError(f"{source}: unknown entity chunk {magic!r}")
    cur = Cursor(payload, source, magic)
    unique_id = cur.u64()
    asset = cur.str16()
    position = (cur.f32(), cur.f32(), cur.f32())
    rotation = (cur.f32(), cur.f32(), cur.f32(), cur.f32())
    scale = (cur.f32(), cur.f32(), cur.f32())
    overrides: list[tuple[int, str]] = []
    name = category = ""
    if magic == b"HSMW":
        for _ in range(cur.u8()):
            overrides.append((cur.u8(), cur.str16()))
        if version > 1:
            name, category = cur.str8(), cur.str16()
    elif version >= 3:
        name, category = cur.str8(), cur.str16()
    if not cur.done():
        raise FormatError(f"{source}: {cur.remaining()} trailing bytes in {magic!r}")

    return WorldEntity(unique_id, "mesh" if magic == b"HSMW" else "wmo", asset, position, rotation, scale,
                       name, category, tuple(overrides), source)
