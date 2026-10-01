#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Byte builders for synthetic world files used by the worldkit tests.

They mirror the writers in src/shared/terrain_io/page_io.cpp (SavePage) and
src/mmo_edit/editors/world_editor/world_editor_instance.cpp (.wobj save), so a parser test
exercises the same layout the engine writes. Not a production writer.

Importing this module also puts tools/world on sys.path so tests can `import worldkit`.
"""

import struct
import sys
import unittest
from pathlib import Path

try:
	import numpy as np
	import PIL  # noqa: F401
	import google.protobuf  # noqa: F401
except ImportError as exc:  # the gate interpreter without tools/world/requirements.txt installed
	raise unittest.SkipTest(f"worldkit tests need tools/world/requirements.txt ({exc})")

REPO = Path(__file__).resolve().parents[2]
LIVE_DATA = (REPO / "data" / "client" / "Worlds" / "Development").is_dir() and (REPO / "data" / "editor" / "data" / "units.data").is_file()


def requires_live_data(test):
	"""Skips tests that read the shipped world when the data submodules are not checked out."""
	return unittest.skipUnless(LIVE_DATA, "data/client and data/editor submodules are not checked out")(test)
if str(REPO / "tools" / "world") not in sys.path:
	sys.path.insert(0, str(REPO / "tools" / "world"))

PAGE_SIZE = struct.unpack("<f", struct.pack("<f", 33.33333))[0] * 16


def chunk(magic: bytes, payload: bytes) -> bytes:
	return magic + struct.pack("<I", len(payload)) + payload


def str16(text: str) -> bytes:
	raw = text.encode("utf-8")
	return struct.pack("<H", len(raw)) + raw


def str8(text: str) -> bytes:
	raw = text.encode("utf-8")
	return struct.pack("<B", len(raw)) + raw


def tile_bytes(
	outer=None, inner=None, areas=None, materials=None, layers=None,
	holes=None, water=None, water_heights=None, version=2, extra_chunks=b"", legacy_water=None,
) -> bytes:
	"""Builds a .tile file. Arrays default to a flat page at height 0.

	holes: dict {tile_index: uint64 mask}; water: dict {tile_index: (type, uint64 mask)};
	legacy_water: dict {tile_index: (height, type)} written as the legacy MLCW chunk.
	"""
	parts = [chunk(b"MVER", struct.pack("<I", version))]
	names = materials if materials is not None else [""] * 256
	parts.append(chunk(b"MCMT", struct.pack("<H", len(names)) + b"".join(str16(n) for n in names)))
	if version == 2:
		o = np.zeros((129, 129), np.float32) if outer is None else np.asarray(outer, np.float32)
		i = np.zeros((128, 128), np.float32) if inner is None else np.asarray(inner, np.float32)
		parts.append(chunk(b"MCVT", o.astype("<f4").tobytes()))
		parts.append(chunk(b"MCVI", i.astype("<f4").tobytes()))
	else:
		o = np.zeros((273, 273), np.float32) if outer is None else np.asarray(outer, np.float32)
		parts.append(chunk(b"MCVT", o.astype("<f4").tobytes()))
	parts.append(chunk(b"MCNM", b"\x00\x7f\x00" * (129 * 129)))
	lay = np.full((1009, 1009), 0x000000FF, np.uint32) if layers is None else np.asarray(layers, np.uint32)
	parts.append(chunk(b"MCLY", lay.astype("<u4").tobytes()))
	if holes:
		payload = struct.pack("<H", len(holes)) + b"".join(struct.pack("<HQ", k, v) for k, v in sorted(holes.items()))
		parts.append(chunk(b"MHOL", payload))
	if water:
		wh = np.zeros((129, 129), np.float32) if water_heights is None else np.asarray(water_heights, np.float32)
		payload = struct.pack("<H", len(water))
		payload += b"".join(struct.pack("<HBQ", k, t, m) for k, (t, m) in sorted(water.items()))
		payload += wh.astype("<f4").tobytes() + str16("")
		parts.append(chunk(b"MCWQ", payload))
	if legacy_water:
		payload = struct.pack("<H", len(legacy_water))
		payload += b"".join(struct.pack("<HfB", k, h, t) for k, (h, t) in sorted(legacy_water.items()))
		parts.append(chunk(b"MLCW", payload))
	a = np.zeros((16, 16), np.uint32) if areas is None else np.asarray(areas, np.uint32)
	parts.append(chunk(b"MCAR", a.astype("<u4").tobytes()))
	return b"".join(parts) + extra_chunks


def wobj_bytes(kind="mesh", version=3, unique_id=7, asset="Models/Test/Crate.hmsh",
			   position=(1.0, 2.0, 3.0), rotation=(1.0, 0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0),
			   name="Crate", category="Props", overrides=()) -> bytes:
	body = struct.pack("<Q", unique_id) + str16(asset)
	body += struct.pack("<3f", *position) + struct.pack("<4f", *rotation) + struct.pack("<3f", *scale)
	if kind == "mesh":
		body += struct.pack("<B", len(overrides)) + b"".join(struct.pack("<B", i) + str16(m) for i, m in overrides)
		if version > 1:
			body += str8(name) + str16(category)
		magic = b"HSMW"
	else:
		if version >= 3:
			body += str8(name) + str16(category)
		magic = b"OMWW"
	return chunk(b"REVW", struct.pack("<I", version)) + chunk(magic, body)


def hwld_bytes(version=3, has_terrain=True, default_material="Models/Terrain/Default.hmi", meshes=("Models/A.hmsh",)) -> bytes:
	parts = [chunk(b"REVM", struct.pack("<I", version))]
	parts.append(chunk(b"TERR", struct.pack("<B", 1 if has_terrain else 0) + str16(default_material)))
	parts.append(chunk(b"HSEM", b"".join(m.encode() + b"\x00" for m in meshes)))
	return b"".join(parts)


def hfol_bytes(meshes, instances, version=2) -> bytes:
	"""Builds a .hfol file. instances: iterable of (unique_id, mesh_index, position, rotation(w,x,y,z), scale, collides)."""
	names = b"".join(str16(m) for m in meshes)
	body = struct.pack("<I", len(instances))
	for unique_id, index, position, rotation, scale, collides in instances:
		body += struct.pack("<QI", unique_id, index) + struct.pack("<3f", *position) + struct.pack("<4f", *rotation)
		body += struct.pack("<3f", *scale)
		if version >= 2:
			body += struct.pack("<B", 1 if collides else 0)
	return chunk(b"REVF", struct.pack("<I", version)) + chunk(b"HSMF", names) + chunk(b"SNIF", body)


def vertex_block(positions, uvs=None) -> bytes:
	"""u32 count + 64-byte vertices: pos 3f, colour u32, uv 2f, w f, normal 3f, binormal 3f, tangent 3f."""
	data = b""
	for i, p in enumerate(positions):
		uv = uvs[i] if uvs is not None else (0.0, 0.0)
		data += struct.pack("<3fI2ff3f3f3f", *p, 0xFFFFFFFF, *uv, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)
	return struct.pack("<I", len(positions)) + data


def hmsh_bytes(submeshes, version=0x301, collision=True) -> bytes:
	"""submeshes: iterable of (name, material, positions, uvs, indices, index32). Mirrors MeshSerializer (v3 SUBM)."""
	parts = [chunk(b"MESH", struct.pack("<I", version))]
	if collision:
		parts.append(chunk(b"LLOC", b"1HVB" + b"\x00" * 12))
	for name, material, positions, uvs, indices, index32 in submeshes:
		body = str8(name) + str16(material) + struct.pack("<B", 0)
		if version >= 0x301:
			body += struct.pack("<B", 1)
		body += vertex_block(positions, uvs)
		body += struct.pack("<BIB", 1, len(indices), 1 if index32 else 0)
		body += struct.pack(f"<{len(indices)}{'I' if index32 else 'H'}", *indices)
		body += struct.pack("<I", 0)
		parts.append(chunk(b"SUBM", body))
	return b"".join(parts)


def legacy_hmsh_bytes(positions, indices, material) -> bytes:
	"""Version 0x200: shared VERT, INDX (flag 1 = 16-bit) and an index-range SUBM."""
	return (chunk(b"MESH", struct.pack("<I", 0x200)) + chunk(b"VERT", vertex_block(positions))
			+ chunk(b"INDX", struct.pack("<IB", len(indices), 1) + struct.pack(f"<{len(indices)}H", *indices))
			+ chunk(b"SUBM", str16(material) + struct.pack("<II", 0, len(indices))))


def strz32(text: str) -> bytes:
	raw = text.encode("utf-8")
	return struct.pack("<I", len(raw)) + raw + b"\x00"


def hwmo_bytes(bounds_min, bounds_max, refs) -> bytes:
	"""refs: iterable of (mesh, position, rotation(w,x,y,z), scale). One group with one FRMM."""
	header = struct.pack("<8I", 0, 1, 0, 0, 0, 0, 0, 0) + struct.pack("<3f", *bounds_min) + struct.pack("<3f", *bounds_max)
	header += struct.pack("<II", 0, 0)
	frmm = struct.pack("<I", len(refs))
	for mesh, position, rotation, scale in refs:
		frmm += strz32(mesh) + strz32("") + strz32("") + struct.pack("<3f4f3fB", *position, *rotation, *scale, 1)
	group = b"\x00" * 68 + chunk(b"MNGM", b"Group_01\x00") + chunk(b"FRMM", frmm)
	return chunk(b"REVM", struct.pack("<I", 0x200)) + chunk(b"DHOM", header) + chunk(b"PGOM", group)


CUBE_POSITIONS = [(x, y, z) for x in (-1.0, 1.0) for y in (0.0, 2.0) for z in (-1.0, 1.0)]
CUBE_INDICES = [0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3]


def cube_hmsh(material="Textures/Test/Cube.hmi", collision=True, size=1.0) -> bytes:
	"""A 2x2x2 cube (scaled by size) standing on y = 0, one submesh."""
	positions = [(x * size, y * size, z * size) for x, y, z in CUBE_POSITIONS]
	return hmsh_bytes([("Cube0", material, positions, None, CUBE_INDICES, False)], collision=collision)


def make_world(repo: Path, directory: str, pages: dict, entities=(), default_material="Models/Terrain/Default.hmi") -> None:
	"""Writes a synthetic world under <repo>/data/client/Worlds/<directory>/.

	pages: {(page_x, page_z): tile_bytes kwargs}; entities: iterable of wobj_bytes kwargs.
	"""
	world = repo / "data" / "client" / "Worlds" / directory
	terrain = world / directory / "Terrain"
	terrain.mkdir(parents=True, exist_ok=True)
	(world / f"{directory}.hwld").write_bytes(hwld_bytes(default_material=default_material))
	for (page_x, page_z), kwargs in pages.items():
		(terrain / f"{page_x}_{page_z}.tile").write_bytes(tile_bytes(**kwargs))
	for index, kwargs in enumerate(entities):
		x, _, z = kwargs.get("position", (1.0, 2.0, 3.0))
		page_x = int(np.floor(x / PAGE_SIZE)) + 32
		page_z = int(np.floor(z / PAGE_SIZE)) + 32
		folder = world / directory / "Entities" / str((page_x << 8) | page_z)
		folder.mkdir(parents=True, exist_ok=True)
		(folder / f"{kwargs.get('unique_id', index + 1)}.wobj").write_bytes(wobj_bytes(**kwargs))
