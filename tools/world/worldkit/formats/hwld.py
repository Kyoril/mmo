# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""World header (.hwld) parser, mirroring src/mmo_client/world_deserializer.cpp.

REVM: uint32 version. TERR: u8 has_terrain + str16 default terrain material. HSEM: NUL-terminated
mesh names. TNEM: legacy (v1/v2) inline entity records, superseded by .wobj files; recognised
and skipped.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .chunks import Cursor, FormatError, iter_chunks


@dataclass
class WorldHeader:
    version: int
    has_terrain: bool = False
    default_material: str = ""
    mesh_names: list[str] = field(default_factory=list)


def parse_hwld(path: Path) -> WorldHeader:
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    if not chunks or chunks[0][0] != b"REVM":
        raise FormatError(f"{source}: first chunk must be REVM")
    header = WorldHeader(version=Cursor(chunks[0][1], source, b"REVM").u32())
    if header.version < 1:
        raise FormatError(f"{source}: unsupported world version {header.version}")
    for magic, payload in chunks[1:]:
        if magic == b"TERR":
            cur = Cursor(payload, source, magic)
            header.has_terrain = cur.u8() != 0
            header.default_material = cur.str16()
        elif magic == b"HSEM":
            header.mesh_names = [name.decode("utf-8") for name in bytes(payload).split(b"\x00") if name]
        elif magic == b"TNEM":
            continue
        else:
            raise FormatError(f"{source}: unknown chunk {magic!r}")
    return header
