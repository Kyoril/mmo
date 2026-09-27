#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.query.WorldQuery on a synthetic world, plus a consistency check against the
old bilinear-corner height on the real Development world.

	python tools/tests/test_world_query.py
"""

import random
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit.constants import CELL_SIZE, OUTER_PER_PAGE, PAGE_SIZE  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402


def steep_outer():
	outer = np.zeros((129, 129), np.float32)
	outer[:, 64:] = np.arange(65, dtype=np.float32) * CELL_SIZE * 2.0   # 63 degree ramp in the east half
	return outer


class QueryTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		outer = steep_outer()
		inner = ((outer[:-1, :-1] + outer[:-1, 1:] + outer[1:, :-1] + outer[1:, 1:]) * 0.25).astype(np.float32)
		water_heights = np.full((129, 129), 2.0, np.float32)
		fx.make_world(repo, "W", {
			(32, 32): dict(outer=outer, inner=inner, holes={2: 0xFFFFFFFFFFFFFFFF}, water={1: (1, 0xFFFFFFFFFFFFFFFF)}, water_heights=water_heights),
		}, entities=[
			dict(position=(100.0, 0.0, 100.0), asset="Models/FalwynPlains/Props/Camp/SM_hc_Camptent_B.hmsh", unique_id=1),
			dict(kind="wmo", position=(200.0, 0.0, 30.0), asset="Models/Haven_001.hwmo", unique_id=2),
		])
		cls.snapshot = build_snapshot("W", repo=repo)
		atlas = empty_atlas(0)
		atlas.add_poi(name="Camp", kind="camp", center=[100.0, 100.0], radius=30.0, status="placeholder", source="agent")
		cls.query = WorldQuery(cls.snapshot, atlas=atlas)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def test_placeable_flat_ground(self):
		ok, reasons = self.query.placeable(150.0, 150.0)
		self.assertTrue(ok, reasons)

	def test_no_terrain(self):
		ok, reasons = self.query.placeable(-10.0, 10.0)
		self.assertEqual((ok, reasons), (False, ["no terrain"]))
		self.assertIsNone(self.query.height_at(-10.0, 10.0))

	def test_hole_water_slope_edge_footprint(self):
		self.assertIn("terrain hole", self.query.placeable(70.0, 5.0)[1])       # tile 2 = x 66..100, z 0..33
		self.assertTrue(any(r.startswith("water") for r in self.query.placeable(40.0, 5.0)[1]))  # tile 1
		self.assertTrue(any(r.startswith("slope") for r in self.query.placeable(400.0, 200.0)[1]))
		self.assertIn("within 5 m of the terrain edge", self.query.placeable(2.0, 200.0)[1])
		self.assertTrue(any("inside footprint" in r for r in self.query.placeable(101.0, 100.0)[1]))

	def test_structure_and_describe(self):
		self.assertEqual(self.query.structure_at(210.0, 30.0).asset, "Models/Haven_001.hwmo")
		self.assertIsNone(self.query.footprint_at(210.0, 30.0))
		info = self.query.describe(100.0, 100.0)
		self.assertEqual(info["pois"][0]["name"], "Camp")
		self.assertEqual(info["entities"][0]["asset"], "Models/FalwynPlains/Props/Camp/SM_hc_Camptent_B.hmsh")


@fx.requires_live_data
class DevelopmentConsistencyTests(unittest.TestCase):
	"""The fan surface differs from the old bilinear-corner height only where inner vertices were
	sculpted, so on the real world the two agree closely in the median."""

	def test_median_difference_is_small(self):
		snap = build_snapshot("Development")
		query = WorldQuery(snap)
		rng = random.Random(1)
		x0, z0, x1, z1 = snap.extent
		diffs = []
		while len(diffs) < 300:
			x, z = rng.uniform(x0, x1), rng.uniform(z0, z1)
			located = snap.page_local(x, z)
			if located is None:
				continue
			slot, lx, lz = located
			outer = snap.outer[slot]
			scale = PAGE_SIZE / (OUTER_PER_PAGE - 1)
			gx, gz = lx / scale, lz / scale
			xi, zi = min(int(gx), 127), min(int(gz), 127)
			u, v = gx - xi, gz - zi
			bilinear = (outer[zi, xi] * (1 - u) * (1 - v) + outer[zi, xi + 1] * u * (1 - v)
						+ outer[zi + 1, xi] * (1 - u) * v + outer[zi + 1, xi + 1] * u * v)
			diffs.append(abs(query.height_at(x, z) - bilinear))
		self.assertLess(float(np.median(diffs)), 0.1)


if __name__ == "__main__":
	unittest.main()
