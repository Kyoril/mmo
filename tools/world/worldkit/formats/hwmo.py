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

from .chunks import Cursor, FormatError, chunk_bytes, iter_chunks

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


# --- Rooms: groups, portals, portal references and containment volumes ------------------------------
#
# MOGP header (68 bytes): i32 name, i32 descriptive name, u32 flags, 6f AABB, u16 portal ref start,
# u16 portal ref count, then batch counts, fog, liquid and padding this code never touches. Sub-chunks
# that matter here: MNGM (MGNM) name, FRMM (MMRF) mesh references, PVCM (MCVP) containment volumes:
# u32 count, per volume strz32 name, 6f AABB, u32 plane count, planes as 4f (normal, d) where a point
# is inside when normal.p + d <= 0 for every plane. Portals: VPOM (MOPV) u32 count + 3f vertices,
# TPOM (MOPT) u32 count + 44-byte infos (u16 start, u16 count, ...), RPOM (MOPR) u32 count + 8-byte refs
# (u16 portal, u16 target group, i16 side, u16 filler). All coordinates are model space.

GROUP_INTERIOR = 0x2000
GROUP_EXTERIOR = 0x0008


@dataclass
class ContainmentVolume:
    name: str
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]
    planes: list[tuple[float, float, float, float]]

    @staticmethod
    def box(name: str, lo, hi) -> "ContainmentVolume":
        """The engine's ContainmentVolume::FromAABB: six outward planes."""
        lo = tuple(float(v) for v in lo)
        hi = tuple(float(v) for v in hi)
        planes = [(-1.0, 0.0, 0.0, lo[0]), (1.0, 0.0, 0.0, -hi[0]),
                  (0.0, -1.0, 0.0, lo[1]), (0.0, 1.0, 0.0, -hi[1]),
                  (0.0, 0.0, -1.0, lo[2]), (0.0, 0.0, 1.0, -hi[2])]
        return ContainmentVolume(name, lo, hi, planes)

    def contains(self, p) -> bool:
        if any(p[k] < self.bounds_min[k] or p[k] > self.bounds_max[k] for k in range(3)):
            return False
        return all(n0 * p[0] + n1 * p[1] + n2 * p[2] + d <= 0.0001 for n0, n1, n2, d in self.planes)


@dataclass
class PortalRef:
    portal: int
    group: int
    side: int


@dataclass
class RoomGroup:
    name: str
    flags: int
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]
    mesh_refs: list[MeshRef]
    portal_refs: list[PortalRef]
    volumes: list[ContainmentVolume]

    def contains(self, p) -> bool:
        """WorldModelGroup::ContainsPoint: the volumes if there are any, else the AABB."""
        if self.volumes:
            return any(v.contains(p) for v in self.volumes)
        return all(self.bounds_min[k] <= p[k] <= self.bounds_max[k] for k in range(3))


@dataclass
class WorldModelRooms:
    groups: list[RoomGroup]
    portals: list[list[tuple[float, float, float]]]   # model-space vertices per portal


def _volumes(payload: memoryview, source: str) -> list[ContainmentVolume]:
    cur = Cursor(payload, source, b"PVCM")
    volumes = []
    for _ in range(cur.u32()):
        name = cur.strz32()
        lo = (cur.f32(), cur.f32(), cur.f32())
        hi = (cur.f32(), cur.f32(), cur.f32())
        planes = [(cur.f32(), cur.f32(), cur.f32(), cur.f32()) for _ in range(cur.u32())]
        volumes.append(ContainmentVolume(name, lo, hi, planes))
    return volumes


def parse_hwmo_rooms(path: Path) -> WorldModelRooms:
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    if not chunks or chunks[0][0] != b"REVM":
        raise FormatError(f"{source}: expected a REVM version chunk first")
    vertices: list[tuple[float, float, float]] = []
    infos: list[tuple[int, int]] = []
    refs: list[PortalRef] = []
    groups: list[RoomGroup] = []
    ranges: list[tuple[int, int]] = []
    for magic, payload in chunks[1:]:
        if magic == b"VPOM":
            cur = Cursor(payload, source, magic)
            vertices = [(cur.f32(), cur.f32(), cur.f32()) for _ in range(cur.u32())]
        elif magic == b"TPOM":
            count = struct.unpack_from("<I", payload, 0)[0]
            infos = [struct.unpack_from("<HH", payload, 4 + i * 44) for i in range(count)]
        elif magic == b"RPOM":
            count = struct.unpack_from("<I", payload, 0)[0]
            for i in range(count):
                portal, group, side, _ = struct.unpack_from("<HHhH", payload, 4 + i * 8)
                refs.append(PortalRef(portal, group, side))
        elif magic == b"PGOM":
            if len(payload) < _GROUP_HEADER:
                raise FormatError(f"{source}: group chunk shorter than its header")
            flags = struct.unpack_from("<I", payload, 8)[0]
            box = struct.unpack_from("<6f", payload, 12)
            ranges.append(struct.unpack_from("<HH", payload, 36))
            group = RoomGroup("", flags, box[:3], box[3:], [], [], [])
            for sub_magic, sub in iter_chunks(bytes(payload[_GROUP_HEADER:]), source):
                if sub_magic == b"MNGM":
                    group.name = bytes(sub).split(b"\0")[0].decode("utf-8")
                elif sub_magic == b"FRMM":
                    group.mesh_refs = _mesh_refs(sub, source)
                elif sub_magic == b"PVCM":
                    group.volumes = _volumes(sub, source)
            groups.append(group)
    for group, (start, count) in zip(groups, ranges):
        group.portal_refs = [PortalRef(r.portal, r.group, r.side) for r in refs[start:start + count]]
    portals = [vertices[start:start + count] for start, count in infos]
    return WorldModelRooms(groups, portals)


def _volumes_bytes(volumes: list[ContainmentVolume]) -> bytes:
    out = bytearray(struct.pack("<I", len(volumes)))
    for v in volumes:
        raw = v.name.encode("utf-8")
        out += struct.pack("<I", len(raw)) + raw + b"\0"
        out += struct.pack("<6f", *v.bounds_min, *v.bounds_max)
        out += struct.pack("<I", len(v.planes))
        for plane in v.planes:
            out += struct.pack("<4f", *plane)
    return bytes(out)


def write_hwmo_rooms(path: Path, rooms: WorldModelRooms) -> bytes:
    """The file at `path` with its portal references and containment volumes replaced by `rooms`.

    Every other chunk and sub-chunk is copied byte for byte, so this cannot disturb geometry, lights
    or doodads; groups and portals themselves must match the file one to one.
    """
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    group_count = sum(1 for magic, _ in chunks if magic == b"PGOM")
    if group_count != len(rooms.groups):
        raise FormatError(f"{source}: {group_count} groups on disk, {len(rooms.groups)} given")

    all_refs = bytearray()
    ranges = []
    for group in rooms.groups:
        ranges.append((len(all_refs) // 8, len(group.portal_refs)))
        for ref in group.portal_refs:
            if not 0 <= ref.portal < len(rooms.portals) or not 0 <= ref.group < len(rooms.groups):
                raise FormatError(f"{source}: portal ref {ref} out of range")
            all_refs += struct.pack("<HHhH", ref.portal, ref.group, ref.side, 0)
    mopr = chunk_bytes(b"RPOM", struct.pack("<I", len(all_refs) // 8) + bytes(all_refs))

    out = bytearray()
    wrote_refs = False
    group_index = 0
    for magic, payload in chunks:
        if magic in (b"RPOM", b"PGOM") and not wrote_refs:
            out += mopr
            wrote_refs = True
        if magic == b"RPOM":
            continue
        if magic == b"PGOM":
            group = rooms.groups[group_index]
            header = bytearray(payload[:_GROUP_HEADER])
            struct.pack_into("<HH", header, 36, *ranges[group_index])
            body = bytearray()
            for sub_magic, sub in iter_chunks(bytes(payload[_GROUP_HEADER:]), source):
                if sub_magic != b"PVCM":
                    body += chunk_bytes(sub_magic, bytes(sub))
            if group.volumes:
                body += chunk_bytes(b"PVCM", _volumes_bytes(group.volumes))
            out += chunk_bytes(b"PGOM", bytes(header) + bytes(body))
            group_index += 1
            continue
        out += chunk_bytes(magic, bytes(payload))
    return bytes(out)
