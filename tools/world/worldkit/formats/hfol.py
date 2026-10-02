# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Per-page foliage instances (.hfol), mirroring src/shared/game_common/world_foliage.cpp.

Layout (magics as stored on disk; the C++ MakeChunkMagic literals appear byte-reversed):
  REVF  u32 version (1 or 2)
  HSMF  mesh names: str16 repeated until the chunk ends (no count)
  SNIF  u32 n, then n x (u64 id, u32 mesh index, 3f position, 4f rotation (w,x,y,z), 3f scale,
        version 2 only: u8 collides)
mmo_edit writes version 2 with the mesh-name table in first-use order of the instances, so appending
new instances (and new names) at the end produces exactly the file the editor itself would write.
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

from .chunks import Cursor, FormatError, chunk_bytes, iter_chunks, str16_bytes

VERSION = 2
_INSTANCE = struct.Struct("<QI3f4f3fB")


@dataclass(frozen=True)
class FoliageInstance:
    unique_id: int
    mesh: str
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]   # (w, x, y, z)
    scale: tuple[float, float, float]
    collides: bool = True


@dataclass
class FoliageFile:
    version: int
    meshes: list[str]
    instances: list[FoliageInstance]


def parse_hfol(data: bytes, source: str = "<hfol>") -> FoliageFile:
    chunks = iter_chunks(data, source)
    if not chunks or chunks[0][0] != b"REVF":
        raise FormatError(f"{source}: expected a REVF version chunk first")
    version = Cursor(chunks[0][1], source, b"REVF").u32()
    if version not in (1, 2):
        raise FormatError(f"{source}: unsupported foliage version {version} (known: 1, 2)")
    meshes: list[str] = []
    instances: list[FoliageInstance] = []
    for magic, payload in chunks[1:]:
        cur = Cursor(payload, source, magic)
        if magic == b"HSMF":
            while not cur.done():
                meshes.append(cur.str16())
        elif magic == b"SNIF":
            for _ in range(cur.u32()):
                unique_id, index = cur.u64(), cur.u32()
                position = (cur.f32(), cur.f32(), cur.f32())
                rotation = (cur.f32(), cur.f32(), cur.f32(), cur.f32())
                scale = (cur.f32(), cur.f32(), cur.f32())
                collides = cur.u8() != 0 if version >= 2 else True
                if index >= len(meshes):
                    raise FormatError(f"{source}: instance {unique_id} uses mesh index {index} of {len(meshes)}")
                instances.append(FoliageInstance(unique_id, meshes[index], position, rotation, scale, collides))
            if not cur.done():
                raise FormatError(f"{source}: {cur.remaining()} trailing bytes in SNIF")
        # Unknown chunks are skipped, like the engine's loader (m_ignoreUnhandledChunks).
    return FoliageFile(version, meshes, instances)


def write_hfol(ff: FoliageFile) -> bytes:
    """Writes version 2. Every instance's mesh must be in ff.meshes."""
    index = {name: i for i, name in enumerate(ff.meshes)}
    names = b"".join(str16_bytes(name) for name in ff.meshes)
    body = struct.pack("<I", len(ff.instances))
    for inst in ff.instances:
        body += _INSTANCE.pack(inst.unique_id, index[inst.mesh], *inst.position, *inst.rotation, *inst.scale,
                               1 if inst.collides else 0)
    return chunk_bytes(b"REVF", struct.pack("<I", VERSION)) + chunk_bytes(b"HSMF", names) + chunk_bytes(b"SNIF", body)


def append_instances(ff: FoliageFile, new: list[FoliageInstance]) -> FoliageFile:
    taken = {i.unique_id for i in ff.instances}
    meshes = list(ff.meshes)
    for inst in new:
        if inst.unique_id in taken:
            raise ValueError(f"foliage instance id {inst.unique_id} already exists")
        taken.add(inst.unique_id)
        if inst.mesh not in meshes:
            meshes.append(inst.mesh)
    return FoliageFile(VERSION, meshes, [*ff.instances, *new])


def remove_instances(ff: FoliageFile, ids: set[int]) -> FoliageFile:
    """Drops the given instances; a mesh name goes only when a removed instance used it and none is left."""
    kept = [i for i in ff.instances if i.unique_id not in ids]
    removed_meshes = {i.mesh for i in ff.instances if i.unique_id in ids}
    still_used = {i.mesh for i in kept}
    meshes = [m for m in ff.meshes if m not in removed_meshes or m in still_used]
    return FoliageFile(VERSION, meshes, kept)


def load_hfol(path: Path) -> FoliageFile:
    """The page's foliage, or an empty version-2 file when the page has none yet."""
    path = Path(path)
    if not path.is_file():
        return FoliageFile(VERSION, [], [])
    return parse_hfol(path.read_bytes(), str(path))


def instance_hash(inst: FoliageInstance) -> str:
    """Identity of an instance's placement at float32 precision (what the file stores)."""
    packed = _INSTANCE.pack(inst.unique_id, 0, *inst.position, *inst.rotation, *inst.scale, 1 if inst.collides else 0)
    return hashlib.sha1(inst.mesh.encode("utf-8") + packed).hexdigest()
