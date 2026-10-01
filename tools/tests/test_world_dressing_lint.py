#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the prop placement checks.

	python tools/tests/test_world_dressing_lint.py
"""

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.assets import build_catalog  # noqa: E402
from worldkit.constants import CELL_SIZE, PIXEL_SIZE  # noqa: E402
from worldkit.geometry import quat_from_yaw_tilt  # noqa: E402
from worldkit.prop_lint import PlacedProp, PropContext, _inside_quad, lint_props, lint_world_props, props_from_entities  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.spawns import SpawnRecord, spawn_key  # noqa: E402
from worldkit.tags import TagRules  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402

RULES = TagRules([
	{"match": "models/test/cube*", "tags": ["crate"], "max_slope": 15, "sink": 0.05},
	{"match": "models/test/rock*", "tags": ["rock"], "max_slope": 90, "sink": 0.4, "align_to_slope": True, "may_overlap": ["rock"]},
])


def lint_world(repo: Path):
	"""Page 32_32: flat at 0 with a 45 degree ramp for x >= 300, water in tile (6, 6), a hole in tile (8, 0),
	a painted road strip x 100..110, and an existing cube at (150, 150)."""
	outer = np.zeros((129, 129), np.float32)
	outer[:, 72:] = (np.arange(57, dtype=np.float32) * CELL_SIZE)[None, :]
	inner = ((outer[:-1, :-1] + outer[:-1, 1:] + outer[1:, :-1] + outer[1:, 1:]) * 0.25).astype(np.float32)
	layers = np.full((1009, 1009), 0x000000FF, np.uint32)
	layers[:, int(100 / PIXEL_SIZE):int(110 / PIXEL_SIZE)] = 0x0000FF00
	fx.make_world(repo, "L", {(32, 32): dict(outer=outer, inner=inner, layers=layers, materials=["Models/Terrain/B.hmi"] * 256,
										  holes={8: 0xFFFFFFFFFFFFFFFF}, water={102: (1, 0xFFFFFFFFFFFFFFFF)},
										  water_heights=np.full((129, 129), 2.0, np.float32))},
				  entities=[dict(asset="Models/Test/Cube.hmsh", position=(150.0, 0.0, 150.0), unique_id=5)])
	models = repo / "data" / "client" / "Models" / "Test"
	models.mkdir(parents=True)
	(models / "Cube.hmsh").write_bytes(fx.cube_hmsh())
	(models / "Rock.hmsh").write_bytes(fx.cube_hmsh(collision=False))


def spawn(x, z):
	return SpawnRecord(spawn_key("unit", 0, 1, "S", x, z), 0, "unit", 1, 0, 0, "S", x, 0.0, z, True, 0, 30000, ())


def prop(x, z, y=0.0, asset="Models/Test/Cube.hmsh", collides=True, yaw=0.0, key="new"):
	return PlacedProp(key, asset, "wobj", (x, y, z), quat_from_yaw_tilt(yaw), 1.0, collides)


class PropLintTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		lint_world(repo)
		snapshot = build_snapshot("L", repo=repo)
		cls.query = WorldQuery(snapshot, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
		cls.catalog = build_catalog(repo)
		cls.existing = props_from_entities(snapshot.entities)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def context(self, poi=None, roads=(), spawns=()):
		return PropContext(self.query, self.catalog, RULES, self.existing, list(spawns), poi, list(roads))

	def rules(self, props, **kwargs):
		return {(v.rule, v.severity) for v in lint_props(props, self.context(**kwargs))}

	def test_clean(self):
		self.assertEqual(self.rules([prop(50.0, 50.0)]), set())

	def test_floating_and_buried(self):
		self.assertIn(("prop_floating", "error"), self.rules([prop(50.0, 50.0, y=1.0)]))
		self.assertIn(("prop_buried", "error"), self.rules([prop(50.0, 50.0, y=-1.0)]))

	def test_slope_allows_aligned_rocks(self):
		self.assertIn(("prop_slope", "error"), self.rules([prop(320.0, 50.0, y=self.query.height_at(320.0, 50.0))]))
		rock = prop(320.0, 80.0, asset="Models/Test/Rock.hmsh", y=self.query.height_at(320.0, 80.0))
		self.assertNotIn(("prop_slope", "error"), self.rules([rock]))

	def test_overlap(self):
		self.assertIn(("prop_overlap", "error"), self.rules([prop(150.5, 150.0)]))
		rocks = [prop(60.0, 60.0, asset="Models/Test/Rock.hmsh", key="a"), prop(60.5, 60.0, asset="Models/Test/Rock.hmsh", key="b")]
		self.assertNotIn(("prop_overlap", "error"), self.rules(rocks))

	def test_overlap_uses_the_rotated_footprint(self):
		# The existing cube covers x 149..151; a neighbour 2.2 m away clears it unrotated (gap 0.2 m) but its
		# 45 degree diamond reaches 1.41 m towards it and pokes in.
		self.assertNotIn(("prop_overlap", "error"), self.rules([prop(152.2, 150.0)]))
		self.assertIn(("prop_overlap", "error"), self.rules([prop(152.2, 150.0, yaw=45.0)]))

	def test_roads(self):
		self.assertIn(("prop_on_road", "error"), self.rules([prop(105.0, 300.0)]))
		self.assertIn(("prop_on_road", "warning"), self.rules([prop(105.0, 300.0, collides=False)]))
		self.assertIn(("prop_on_road", "error"), self.rules([prop(50.0, 401.5)], roads=[[[0.0, 400.0], [90.0, 400.0]]]))

	def test_water_hole_edge(self):
		self.assertIn(("prop_in_water", "error"), self.rules([prop(216.0, 216.0)]))
		self.assertIn(("prop_over_hole", "error"), self.rules([prop(283.0, 16.0)]))
		self.assertIn(("prop_terrain_edge", "error"), self.rules([prop(2.0, 250.0)]))

	def test_spawn_poi_grid_unknown(self):
		self.assertIn(("prop_on_spawn", "warning"), self.rules([prop(60.0, 70.0)], spawns=[spawn(60.0, 70.0)]))
		poi = {"id": "p", "center": [50.0, 50.0], "radius": 20.0}
		self.assertIn(("prop_outside_poi", "warning"), self.rules([prop(50.0, 90.0)], poi=poi))
		grid = [prop(160.0 + i * 14.0, 200.0 + j * 14.0, key=f"g{i}{j}") for i in range(3) for j in range(3)]
		self.assertIn(("prop_grid_pattern", "warning"), self.rules(grid))
		self.assertIn(("prop_unknown_asset", "error"), self.rules([prop(50.0, 50.0, asset="Models/Nope.hmsh")]))

	def test_on_spawn_edges(self):
		self.assertIn(("prop_on_spawn", "warning"), self.rules([prop(60.0, 70.0)], spawns=[spawn(60.9, 70.0)]))
		self.assertNotIn(("prop_on_spawn", "warning"), self.rules([prop(60.0, 70.0)], spawns=[spawn(61.2, 70.0)]))
		self.assertNotIn(("prop_on_spawn", "warning"), self.rules([prop(60.0, 70.0)], spawns=[spawn(300.0, 400.0)]))

	def test_degenerate_quad_contains_nothing(self):
		flat = np.array([[5.0, 5.0], [5.0, 5.0], [5.0, 5.0], [5.0, 5.0]])
		self.assertFalse(_inside_quad((5.0, 5.0), flat))
		line = np.array([[0.0, 0.0], [2.0, 0.0], [2.0, 0.0], [0.0, 0.0]])
		self.assertFalse(_inside_quad((1.0, 0.0), line))
		square = np.array([[0.0, 0.0], [2.0, 0.0], [2.0, 2.0], [0.0, 2.0]])
		self.assertTrue(_inside_quad((1.0, 1.0), square))
		self.assertFalse(_inside_quad((3.0, 1.0), square))

	def test_tags_resolved_once_per_asset(self):
		props = [prop(40.0 + i * 30.0, 40.0, key=f"c{i}") for i in range(4)]
		props += [prop(40.0 + i * 30.0, 100.0, asset="Models/Test/Rock.hmsh", key=f"r{i}") for i in range(3)]
		real = TagRules.for_asset
		with mock.patch.object(TagRules, "for_asset", autospec=True, side_effect=real) as counter:
			lint_props(props, self.context())
		assets = {p.asset for p in props} | {p.asset for p in self.existing}
		self.assertGreater(counter.call_count, 0)
		self.assertLessEqual(counter.call_count, len(assets))

	def test_world_lint_covers_existing_props(self):
		violations = lint_world_props(self.context())
		self.assertEqual([v for v in violations if v.severity == "error"], [])


@fx.requires_live_data
class PropLintCliTests(unittest.TestCase):
	def test_shipped_world_is_clean_against_the_baseline(self):
		sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "world"))
		import prop_lint  # noqa: E402
		self.assertEqual(prop_lint.main(["--map", "0"]), 0)


if __name__ == "__main__":
	unittest.main()
