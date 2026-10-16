# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placed world entity (.wobj) parser and writer, mirroring src/shared/game_common/world_entity_loader.cpp and mmo_edit's Save.

Layout: REVW (uint32 version 1..3), then exactly one of
  HSMW mesh:  u64 id, str16 mesh, 3f pos, 4f rot (w,x,y,z), 3f scale, u8 n + n*(u8 idx, str16 mat),
              version > 1: str8 name, str16 category
  OMWW world model: u64 id, str16 hwmo, 3f pos, 4f rot, 3f scale, version >= 3: str8 name, str16 category
  TIKW (optional, world model only, after OMWW): u8 count, count * str16 prop kit path (.hkit)
"""

from __future__ import annotations

import random
import struct
import time
from dataclasses import dataclass
from pathlib import Path

from ..constants import entity_page_index
from ..paths import REPO, entities_dir
from .chunks import Cursor, FormatError, chunk_bytes, iter_chunks, str8_bytes, str16_bytes


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
    kits: tuple[str, ...] = ()                   # ordered prop kit paths (world models only)


def parse_wobj(path: Path) -> WorldEntity:
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    if len(chunks) not in (2, 3) or chunks[0][0] != b"REVW":
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

    kits: list[str] = []
    if len(chunks) == 3:
        kit_magic, kit_payload = chunks[2]
        if kit_magic != b"TIKW" or magic != b"OMWW":
            raise FormatError(f"{source}: unexpected third chunk {kit_magic!r} after {magic!r}")
        kit_cur = Cursor(kit_payload, source, kit_magic)
        for _ in range(kit_cur.u8()):
            kits.append(kit_cur.str16())
        if not kit_cur.done():
            raise FormatError(f"{source}: {kit_cur.remaining()} trailing bytes in {kit_magic!r}")

    return WorldEntity(unique_id, "mesh" if magic == b"HSMW" else "wmo", asset, position, rotation, scale,
                       name, category, tuple(overrides), source, tuple(kits))


def wobj_bytes(*, kind: str, unique_id: int, asset: str, position, rotation, scale, name: str = "",
               category: str = "", kits: tuple[str, ...] = ()) -> bytes:
    """A version-3 entity file exactly as mmo_edit writes it (no material overrides). `kits` (world models
    only) appends the optional prop kit chunk."""
    if kind not in ("mesh", "wmo"):
        raise ValueError(f"unknown entity kind {kind!r}")
    body = struct.pack("<Q", unique_id) + str16_bytes(asset)
    body += struct.pack("<3f4f3f", *position, *rotation, *scale)
    if kind == "mesh":
        body += struct.pack("<B", 0)
    body += str8_bytes(name) + str16_bytes(category)
    out = chunk_bytes(b"REVW", struct.pack("<I", 3)) + chunk_bytes(b"HSMW" if kind == "mesh" else b"OMWW", body)
    if kits:
        if kind != "wmo":
            raise ValueError("prop kits are only valid on world model placements")
        if len(kits) > 255:
            raise ValueError("at most 255 prop kits per placement")
        out += chunk_bytes(b"TIKW", struct.pack("<B", len(kits)) + b"".join(str16_bytes(k) for k in kits))
    return out


def entity_file(directory: str, unique_id: int, x: float, z: float, repo: Path = REPO) -> Path:
    """Entities/<pageIndex>/<uniqueId>.wobj; the folder is the page of the entity's position."""
    return entities_dir(directory, repo) / str(entity_page_index(x, z)) / f"{unique_id}.wobj"


def new_unique_id(taken: set[int], rng: random.Random) -> int:
    """mmo_edit's scheme (EntityFactory::GenerateUniqueId): 16 bits of the ms clock over 48 random bits.
    The id is added to `taken`."""
    while True:
        value = ((int(time.time() * 1000) & 0xFFFF) << 48) | rng.getrandbits(48)
        if value and value not in taken:
            taken.add(value)
            return value
