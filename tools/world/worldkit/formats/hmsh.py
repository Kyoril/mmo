# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Static mesh (.hmsh) geometry, mirroring src/shared/scene_graph/mesh_serializer.cpp.

Chunks (magic as on disk): MESH (u32 version 0x100/0x200/0x300/0x301), VERT (shared vertices),
INDX (legacy shared indices, version < 0x300; a non-zero flag means 16-bit), LEKS (skeleton name),
LLOC (collision BVH: only its presence matters here), SUBM (one per submesh), TAGS.
Vertices are 64 bytes from version 0x200 (position 3f @0, colour u32 @12, uv 2f @16, w @24, normal,
binormal, tangent) and 40 bytes in 0x100. A v3 SUBM is: str8 name, str16 material, u8 shared,
(0x301: u8 visible), own vertex block unless shared, u8 has indices [u32 count, u8 size (0 = 16-bit,
1 = 32-bit), indices], u32 bone assignments x 10 bytes. Bounds are not stored; they come from vertices.
Pre-chunk legacy files (e.g. Models/Stump_03) raise FormatError.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .chunks import Cursor, FormatError, iter_chunks

VERSIONS = (0x100, 0x200, 0x300, 0x301)


@dataclass
class Submesh:
    name: str
    material: str
    positions: np.ndarray   # (n, 3) float32, model space
    uvs: np.ndarray         # (n, 2) float32
    indices: np.ndarray     # (m,) uint32 triangle list into positions


@dataclass
class MeshData:
    version: int
    submeshes: list[Submesh]
    has_collision: bool

    @property
    def bounds(self) -> tuple[np.ndarray, np.ndarray]:
        points = [s.positions for s in self.submeshes if len(s.positions)]
        if not points:
            return np.zeros(3, np.float32), np.zeros(3, np.float32)
        everything = np.concatenate(points)
        return everything.min(axis=0), everything.max(axis=0)


def _vertices(cur: Cursor, stride: int) -> tuple[np.ndarray, np.ndarray]:
    count = cur.u32()
    raw = np.frombuffer(cur.raw(count * stride), dtype="<f4").reshape(count, stride // 4)
    return raw[:, 0:3].astype(np.float32), raw[:, 4:6].astype(np.float32)


def _indices(cur: Cursor, count: int, sixteen_bit: bool) -> np.ndarray:
    width = 2 if sixteen_bit else 4
    return np.frombuffer(cur.raw(count * width), dtype="<u2" if sixteen_bit else "<u4").astype(np.uint32)


def _check_indices(source: str, sub: Submesh) -> Submesh:
    if len(sub.indices) and int(sub.indices.max()) >= len(sub.positions):
        raise FormatError(f"{source}: submesh {sub.name!r} index {int(sub.indices.max())} >= {len(sub.positions)} vertices")
    return sub


def _submesh_v3(cur: Cursor, version: int, stride: int, shared: tuple[np.ndarray, np.ndarray], source: str) -> Submesh:
    name = cur.str8()
    material = cur.str16()
    uses_shared = cur.u8() != 0
    if version >= 0x301:
        cur.u8()   # visible by default
    positions, uvs = shared if uses_shared else _vertices(cur, stride)
    indices = np.zeros(0, np.uint32)
    if cur.u8():
        count = cur.u32()
        size = cur.u8()
        if size not in (0, 1):
            raise FormatError(f"{source}: submesh {name!r} has unknown index size {size}")
        indices = _indices(cur, count, size == 0)
    cur.raw(cur.u32() * 10)   # bone assignments: u32 vertex, u16 bone, f32 weight
    if not cur.done():
        raise FormatError(f"{source}: {cur.remaining()} trailing bytes in submesh {name!r}")
    return _check_indices(source, Submesh(name, material, positions, uvs, indices))


def parse_hmsh(path: Path) -> MeshData:
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    if not chunks or chunks[0][0] != b"MESH":
        raise FormatError(f"{source}: not a chunked mesh")
    version = Cursor(chunks[0][1], source, b"MESH").u32()
    if version not in VERSIONS:
        raise FormatError(f"{source}: unsupported mesh version 0x{version:x}")
    stride = 40 if version < 0x200 else 64
    shared = (np.zeros((0, 3), np.float32), np.zeros((0, 2), np.float32))
    shared_indices = np.zeros(0, np.uint32)
    has_collision = False
    submeshes: list[Submesh] = []
    for magic, payload in chunks[1:]:
        cur = Cursor(payload, source, magic)
        if magic == b"VERT":
            shared = _vertices(cur, stride)
        elif magic == b"INDX":
            count = cur.u32()
            shared_indices = _indices(cur, count, cur.u8() != 0)
        elif magic == b"LLOC":
            has_collision = True
        elif magic == b"SUBM":
            if version < 0x300:
                material = cur.str16()
                start, end = cur.u32(), cur.u32()
                sub = Submesh(f"submesh{len(submeshes)}", material, shared[0], shared[1], shared_indices[start:end])
                submeshes.append(_check_indices(source, sub))
            else:
                submeshes.append(_submesh_v3(cur, version, stride, shared, source))
    return MeshData(version, submeshes, has_collision)
