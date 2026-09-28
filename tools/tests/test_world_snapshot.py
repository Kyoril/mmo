#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.snapshot, worldkit.entities and worldkit.terrain_kinds.

Synthetic worlds are written to a temp "repo" so the tests never touch data/client or generated/.
One test builds the real Development world without the cache as an end-to-end check.

	python tools/tests/test_world_snapshot.py
"""

import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit import paths  # noqa: E402
from worldkit.constants import CELL_SIZE, PAGE_SIZE  # noqa: E402
from worldkit.entities import footprint_radius, structure_radius  # noqa: E402
from worldkit.formats.wobj import WorldEntity  # noqa: E402
from worldkit.snapshot import NoTerrainError, build_snapshot, load_snapshot  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds, load_kinds  # noqa: E402


def two_page_world(repo: Path):
	areas = np.zeros((16, 16), np.uint32)
	areas[:, :] = 8
	layers = np.full((1009, 1009), 0x000000FF, np.uint32)
	layers[:, :] = 0x0000FF00  # layer 1 dominant everywhere on page 33_32
	inner = np.zeros((128, 128), np.float32)
	water_heights = np.full((129, 129), 3.0, np.float32)
	fx.make_world(repo, "W", {
		(32, 32): dict(areas=areas, holes={0: 1}, water={1: (1, 1)}, water_heights=water_heights, inner=inner),
		(33, 32): dict(layers=layers, materials=["Models/Terrain/B.hmi"] * 256),
	}, entities=[dict(position=(10.0, 0.0, 10.0), unique_id=5)])


class SnapshotTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.repo = Path(self._tmp.name)
		two_page_world(self.repo)

	def tearDown(self):
		self._tmp.cleanup()

	def test_grid_layout(self):
		snap = build_snapshot("W", repo=self.repo)
		self.assertEqual((snap.page_min_x, snap.page_min_z, snap.pages_x, snap.pages_z), (32, 32, 2, 1))
		self.assertEqual(snap.height.shape, (128, 256))
		self.assertEqual(snap.origin, (0.0, 0.0))
		self.assertAlmostEqual(snap.extent[2], 2 * PAGE_SIZE, places=3)
		self.assertEqual(int(snap.area[0, 0]), 8)
		self.assertEqual(int(snap.area[0, 200]), 0)
		self.assertTrue(snap.hole[0, 0])
		self.assertFalse(snap.hole[0, 1])
		self.assertAlmostEqual(float(snap.water_depth[0, 8]), 3.0)   # tile 1 -> cell cx 8
		self.assertEqual(float(snap.water_depth[0, 9]), 0.0)
		self.assertEqual(int(snap.layer[5, 5]), 0)
		self.assertEqual(int(snap.layer[5, 200]), 1)
		self.assertEqual(snap.material_name(int(snap.material[0, 0])), "Models/Terrain/Default.hmi")
		self.assertEqual(snap.material_name(int(snap.material[0, 200])), "Models/Terrain/B.hmi")
		self.assertEqual(len(snap.entities), 1)

	def test_lookups(self):
		snap = build_snapshot("W", repo=self.repo)
		self.assertEqual(snap.cell_index(CELL_SIZE * 3.5, CELL_SIZE * 2.5), (2, 3))
		self.assertIsNone(snap.cell_index(-1.0, 0.0))
		slot, lx, lz = snap.page_local(PAGE_SIZE + 5.0, 7.0)
		self.assertEqual(snap.page_slot[0, 1], slot)
		self.assertAlmostEqual(lx, 5.0, places=3)
		self.assertAlmostEqual(lz, 7.0, places=3)
		self.assertIsNone(snap.page_local(0.0, PAGE_SIZE + 1.0))

	def test_cache_roundtrip_and_invalidation(self):
		first = load_snapshot("W", repo=self.repo)
		meta = paths.cache_dir("W", self.repo) / "snapshot.json"
		self.assertTrue(meta.is_file())
		second = load_snapshot("W", repo=self.repo)
		self.assertTrue(np.array_equal(first.slope, second.slope))
		self.assertEqual(second.entities, first.entities)
		stamp = meta.stat().st_mtime_ns
		tile = paths.terrain_dir("W", self.repo) / "32_32.tile"
		os.utime(tile, ns=(tile.stat().st_atime_ns, tile.stat().st_mtime_ns + 10_000_000_000))
		load_snapshot("W", repo=self.repo)
		self.assertNotEqual(meta.stat().st_mtime_ns, stamp)

	def test_world_without_terrain(self):
		(self.repo / "data" / "client" / "Worlds" / "Empty" / "Empty" / "Terrain").mkdir(parents=True)
		with self.assertRaises(NoTerrainError):
			build_snapshot("Empty", repo=self.repo)


class FootprintTests(unittest.TestCase):
	def entity(self, asset, kind="mesh", scale=(1.0, 1.0, 1.0)):
		return WorldEntity(1, kind, asset, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0), scale, "", "", (), "x")

	def test_heuristics(self):
		self.assertEqual(footprint_radius(self.entity("Models/X/Haven_001.hwmo", kind="wmo")), 0.0)
		self.assertEqual(structure_radius(self.entity("Models/X/Haven_001.hwmo", kind="wmo")), 25.0)
		self.assertEqual(footprint_radius(self.entity("Models/FalwynPlains/Props/Camp/SM_hc_Camptent_B.hmsh")), 3.0)
		self.assertEqual(footprint_radius(self.entity("Models/FalwynPlains/Props/Furniture/SM_hc_Wagon_Big.hmsh", scale=(2.0, 1.0, 1.0))), 5.0)
		self.assertEqual(footprint_radius(self.entity("Models/FalwynPlains/Buildings/Fortress_Arch_01.hmsh")), 0.0)
		self.assertEqual(structure_radius(self.entity("Models/FalwynPlains/Buildings/Floor_01.hmsh")), 8.0)
		self.assertEqual(structure_radius(self.entity("Models/Trees/Tree_02.hmsh")), 0.0)


class TerrainKindTests(unittest.TestCase):
	def test_lookup_and_road_mask(self):
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			two_page_world(repo)
			snap = build_snapshot("W", repo=repo)
		kinds = TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]})
		self.assertEqual(kinds.kind("Models/Terrain/B.hmi", 1), "road")
		self.assertEqual(kinds.kind("Models/Terrain/Unknown.hmi", 1), "unknown")
		mask = kinds.road_mask(snap)
		self.assertTrue(mask[5, 200])   # page 33_32: material B, layer 1 dominant
		self.assertFalse(mask[5, 5])    # page 32_32: default material, not in the table
		# A painted riverbed under water is not a road; a ford a few centimetres deep still is.
		snap.water_depth[5, 200] = 7.0
		snap.water_depth[5, 201] = 0.2
		mask = kinds.road_mask(snap)
		self.assertFalse(mask[5, 200])
		self.assertTrue(mask[5, 201])

	def test_shipped_table_is_valid(self):
		kinds = load_kinds()
		for material, layers in kinds.table.items():
			self.assertEqual(len(layers), 4, material)


@fx.requires_live_data
class RealWorldTests(unittest.TestCase):
	def test_development_world_builds(self):
		snap = build_snapshot("Development")
		self.assertGreaterEqual(len(snap.entities), 100)
		areas = set(np.unique(snap.area).tolist())
		self.assertTrue({1, 2, 8}.issubset(areas), areas)
		valid = ~np.isnan(snap.height)
		self.assertGreater(int(valid.sum()), 60 * 128 * 128)


if __name__ == "__main__":
	unittest.main()
