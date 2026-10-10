# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""World model (.hwmo) bounds and mesh references, mirroring src/shared/scene_graph/world_model_serializer.cpp.

Every magic is written byte-reversed (REVM, DHOM, PGOM, ...). DHOM (MOHD) is 64 bytes with the model
AABB at offset 32 (min 3f, max 3f). A PGOM (MOGP) group payload is a 68-byte header followed by
sub-chunks; its FRMM (MMRF) sub-chunk lists the group's mesh references: u32 count, then per ref
strz32 mesh, strz32 name, strz32 material override, 3f position, 4f rotation (w,x,y,z), 3f scale,
u8 visible. Doodads (MODD) and child models (RWCM) are not read: previews draw group meshes only.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from .chunks import Cursor, FormatError, iter_chunks

VERSIONS = (0x200, 0x201)
_GROUP_HEADER = 68


@dataclass(frozen=True)
class MeshRef:
    mesh: str
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]   # (w, x, y, z)
    scale: tuple[float, float, float]
    visible: bool


@dataclass
class WorldModelData:
    version: int
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]
    mesh_refs: list[MeshRef]


def _mesh_refs(payload: memoryview, source: str) -> list[MeshRef]:
    cur = Cursor(payload, source, b"FRMM")
    refs = []
    for _ in range(cur.u32()):
        mesh = cur.strz32()
        cur.strz32()   # instance name
        cur.strz32()   # material override
        position = (cur.f32(), cur.f32(), cur.f32())
        rotation = (cur.f32(), cur.f32(), cur.f32(), cur.f32())
        scale = (cur.f32(), cur.f32(), cur.f32())
        refs.append(MeshRef(mesh, position, rotation, scale, cur.u8() != 0))
    if not cur.done():
        raise FormatError(f"{source}: {cur.remaining()} trailing bytes in FRMM")
    return refs


def parse_hwmo(path: Path) -> WorldModelData:
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    if not chunks or chunks[0][0] != b"REVM":
        raise FormatError(f"{source}: expected a REVM version chunk first")
    version = Cursor(chunks[0][1], source, b"REVM").u32()
    if version not in VERSIONS:
        raise FormatError(f"{source}: unsupported world model version 0x{version:x}")
    bounds = None
    refs: list[MeshRef] = []
    for magic, payload in chunks[1:]:
        if magic == b"DHOM":
            if len(payload) != 64:
                raise FormatError(f"{source}: MOHD header is {len(payload)} bytes, expected 64")
            bounds = (struct.unpack_from("<3f", payload, 32), struct.unpack_from("<3f", payload, 44))
        elif magic == b"PGOM":
            if len(payload) < _GROUP_HEADER:
                raise FormatError(f"{source}: group chunk shorter than its header")
            for sub_magic, sub in iter_chunks(bytes(payload[_GROUP_HEADER:]), source):
                if sub_magic == b"FRMM":
                    refs.extend(_mesh_refs(sub, source))
    if bounds is None:
        raise FormatError(f"{source}: no MOHD header")
    return WorldModelData(version, tuple(bounds[0]), tuple(bounds[1]), refs)
