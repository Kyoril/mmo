#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the worldkit binary format parsers (.tile, .wobj, .hwld).

Synthetic files come from world_fixtures.py, which mirrors the C++ writers. The final test class
parses every shipped world file, so a format change in the engine that the parsers do not know
about fails here instead of producing silently wrong heights.

	python tools/tests/test_world_formats.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402  (also puts tools/world on sys.path)

from worldkit import paths  # noqa: E402
from worldkit.formats import hwld, tile, wobj  # noqa: E402
from worldkit.formats.chunks import FormatError  # noqa: E402


def write(tmp: Path, name: str, data: bytes) -> Path:
	path = tmp / name
	path.write_bytes(data)
	return path


class TileParserTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.tmp = Path(self._tmp.name)

	def tearDown(self):
		self._tmp.cleanup()

	def test_v2_page_roundtrip(self):
		outer = np.arange(129 * 129, dtype=np.float32).reshape(129, 129)
		inner = np.full((128, 128), 5.0, np.float32)
		areas = np.zeros((16, 16), np.uint32)
		areas[2, 3] = 8  # tz=2, tx=3
		materials = [""] * 256
		materials[3 + 2 * 16] = "Models/Terrain/X.hmi"
		path = write(self.tmp, "33_31.tile", fx.tile_bytes(outer=outer, inner=inner, areas=areas, materials=materials))

		page = tile.parse_tile(path)

		self.assertEqual((page.page_x, page.page_z, page.version), (33, 31, 2))
		self.assertEqual(page.outer.shape, (129, 129))
		self.assertEqual(page.outer[1, 0], 129.0)  # [z, x]
		self.assertTrue(page.inner_from_file)
		self.assertEqual(page.inner[0, 0], 5.0)
		self.assertEqual(page.areas[2, 3], 8)
		self.assertEqual(page.materials[3 + 2 * 16], "Models/Terrain/X.hmi")
		self.assertEqual(page.layers.shape, (1009, 1009))

	def test_holes_and_water_expand_to_cells(self):
		# tile (tx=1, tz=0) index 1: hole bit for inner cell (ix=2, iz=3) -> cell (cx=10, cz=3)
		holes = {1: 1 << (2 + 3 * 8)}
		# tile (tx=0, tz=1) index 16: water quad (qx=0, qz=0) -> cell (cx=0, cz=8)
		water = {16: (1, 1)}
		path = write(self.tmp, "32_32.tile", fx.tile_bytes(holes=holes, water=water))
		page = tile.parse_tile(path)

		hole_cells = page.hole_cells()
		self.assertTrue(hole_cells[3, 10])
		self.assertEqual(int(hole_cells.sum()), 1)
		water_cells = page.water_cells()
		self.assertTrue(water_cells[8, 0])
		self.assertEqual(int(water_cells.sum()), 1)
		self.assertEqual(page.water_type[1, 0], 1)

	def test_v1_page_resamples_and_derives_inner(self):
		outer = np.zeros((273, 273), np.float32)
		outer[0, 272] = 10.0  # far x corner of row z=0
		path = write(self.tmp, "30_29.tile", fx.tile_bytes(outer=outer, version=1))
		page = tile.parse_tile(path)
		self.assertEqual(page.version, 1)
		self.assertEqual(page.outer.shape, (129, 129))
		self.assertEqual(page.outer[0, 128], 10.0)
		self.assertFalse(page.inner_from_file)
		# inner vertex of the last cell in row 0 averages its corners: (0 + 10 + 0 + 0) / 4
		self.assertAlmostEqual(float(page.inner[0, 127]), 2.5)

	def test_legacy_water_is_converted_like_the_engine(self):
		path = write(self.tmp, "32_32.tile", fx.tile_bytes(legacy_water={17: (4.5, 2)}))   # tile tx=1, tz=1
		page = tile.parse_tile(path)
		self.assertEqual(int(page.water_mask[1, 1]), 0xFFFFFFFFFFFFFFFF)
		self.assertEqual(int(page.water_type[1, 1]), 2)
		self.assertEqual(float(page.water_heights[8, 8]), 4.5)
		self.assertEqual(float(page.water_heights[16, 16]), 4.5)
		self.assertEqual(float(page.water_heights[17, 17]), 0.0)
		self.assertEqual(int(page.water_cells().sum()), 64)

	def test_unknown_chunk_is_rejected(self):
		path = write(self.tmp, "32_32.tile", fx.tile_bytes(extra_chunks=fx.chunk(b"ZZZZ", b"")))
		with self.assertRaises(FormatError):
			tile.parse_tile(path)

	def test_wrong_height_chunk_size_is_rejected(self):
		good = b"MCVT" + (129 * 129 * 4).to_bytes(4, "little")
		bad = b"MCVT" + (129 * 129 * 4 - 4).to_bytes(4, "little")
		path = write(self.tmp, "32_32.tile", fx.tile_bytes().replace(good, bad, 1))
		with self.assertRaises(FormatError):
			tile.parse_tile(path)

	def test_unsupported_version_is_rejected(self):
		path = write(self.tmp, "32_32.tile", fx.tile_bytes(version=3))
		with self.assertRaises(FormatError):
			tile.parse_tile(path)


class WobjParserTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.tmp = Path(self._tmp.name)

	def tearDown(self):
		self._tmp.cleanup()

	def test_mesh_v3(self):
		path = write(self.tmp, "7.wobj", fx.wobj_bytes(overrides=((1, "Models/M.hmi"),)))
		entity = wobj.parse_wobj(path)
		self.assertEqual(entity.kind, "mesh")
		self.assertEqual(entity.unique_id, 7)
		self.assertEqual(entity.asset, "Models/Test/Crate.hmsh")
		self.assertEqual(entity.position, (1.0, 2.0, 3.0))
		self.assertEqual(entity.rotation, (1.0, 0.0, 0.0, 0.0))
		self.assertEqual(entity.material_overrides, ((1, "Models/M.hmi"),))
		self.assertEqual((entity.name, entity.category), ("Crate", "Props"))

	def test_mesh_v1_has_no_name(self):
		entity = wobj.parse_wobj(write(self.tmp, "7.wobj", fx.wobj_bytes(version=1)))
		self.assertEqual((entity.name, entity.category), ("", ""))

	def test_wmo_v2_has_no_name_v3_has(self):
		self.assertEqual(wobj.parse_wobj(write(self.tmp, "a.wobj", fx.wobj_bytes(kind="wmo", version=2))).name, "")
		wmo = wobj.parse_wobj(write(self.tmp, "b.wobj", fx.wobj_bytes(kind="wmo", asset="Models/Haven_001.hwmo")))
		self.assertEqual((wmo.kind, wmo.asset, wmo.name), ("wmo", "Models/Haven_001.hwmo", "Crate"))

	def test_unknown_version_rejected(self):
		with self.assertRaises(FormatError):
			wobj.parse_wobj(write(self.tmp, "7.wobj", fx.wobj_bytes(version=4)))


class HwldParserTests(unittest.TestCase):
	def test_header(self):
		with tempfile.TemporaryDirectory() as tmp:
			path = write(Path(tmp), "W.hwld", fx.hwld_bytes(meshes=("Models/A.hmsh", "Models/B.hmsh")))
			header = hwld.parse_hwld(path)
		self.assertEqual(header.version, 3)
		self.assertTrue(header.has_terrain)
		self.assertEqual(header.default_material, "Models/Terrain/Default.hmi")
		self.assertEqual(header.mesh_names, ["Models/A.hmsh", "Models/B.hmsh"])


@fx.requires_live_data
class ShippedWorldFilesTests(unittest.TestCase):
	"""Every shipped world file must parse. This is the drift alarm for engine format changes."""

	def test_every_tile_parses(self):
		tiles = sorted((paths.REPO / "data" / "client" / "Worlds").glob("*/*/Terrain/*.tile"))
		self.assertGreater(len(tiles), 100)
		for path in tiles:
			page = tile.parse_tile(path)
			self.assertTrue(np.isfinite(page.outer).all(), path)
			self.assertTrue(np.isfinite(page.inner).all(), path)

	def test_every_wobj_parses(self):
		files = sorted((paths.REPO / "data" / "client" / "Worlds").glob("*/*/Entities/*/*.wobj"))
		self.assertGreater(len(files), 100)
		for path in files:
			entity = wobj.parse_wobj(path)
			self.assertTrue(entity.asset, path)
			# Whether the asset exists is content, not format: the props domain of the content audit reports it
			# (prop_unknown_asset), so a prop the user is still placing cannot turn the gate red.

	def test_every_hwld_parses(self):
		for path in sorted((paths.REPO / "data" / "client" / "Worlds").glob("*/*.hwld")):
			hwld.parse_hwld(path)


if __name__ == "__main__":
	unittest.main()
