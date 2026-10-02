#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Smoke and pixel tests for worldkit.render on a synthetic world.

	python tools/tests/test_world_render.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402
from test_world_lint import rec  # noqa: E402

from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit.render import LEGEND_WIDTH, RenderOptions, dump_state, render_map, world_to_pixel  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402

BBOX = (0.0, 0.0, 400.0, 400.0)


class RenderTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		fx.make_world(repo, "R", {(32, 32): dict(water={0: (1, 0xFFFFFFFFFFFFFFFF)}, water_heights=np.full((129, 129), 3.0, np.float32))},
					  entities=[dict(position=(300.0, 0.0, 300.0))])
		cls.snapshot = build_snapshot("R", repo=repo)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def render(self, spawns=None, diff_state=None):
		atlas = empty_atlas(0)
		atlas.add_poi(name="Camp", kind="camp", center=[200.0, 200.0], radius=20.0, status="placeholder", source="agent", ask="Name it")
		options = RenderOptions(bbox=BBOX, px_per_m=1.0)
		return render_map(self.snapshot, spawns=[rec(250.0, 150.0, entry=1)] if spawns is None else spawns,
						  unit_levels={1: (5, 5)}, unit_factions={1: 3}, atlas=atlas, kinds=TerrainKinds({}),
						  quest_arrows=[(10, (200.0, 200.0), (250.0, 150.0))], options=options, diff_state=diff_state)

	def test_size_includes_legend(self):
		self.assertEqual(self.render().size, (400 + LEGEND_WIDTH, 400))

	def test_water_is_blue(self):
		image = self.render(spawns=[])
		px, py = world_to_pixel(BBOX, 1.0, 15.0, 15.0)   # tile 0 is water
		r, g, b = image.getpixel((int(px), int(py)))[:3]
		self.assertGreater(b, r + 40)

	def test_north_is_up(self):
		# The in-game minimap puts smaller world Z (north) at the top; review maps must agree.
		_, north = world_to_pixel(BBOX, 1.0, 0.0, 10.0)
		_, south = world_to_pixel(BBOX, 1.0, 0.0, 390.0)
		self.assertLess(north, south)

	def test_spawn_dot_drawn(self):
		# Away from the quest arrow, which is drawn on top of spawns.
		plain = np.asarray(self.render(spawns=[]))
		with_spawn = np.asarray(self.render(spawns=[rec(320.0, 80.0, entry=1)]))
		px, py = world_to_pixel(BBOX, 1.0, 320.0, 80.0)
		self.assertFalse(np.array_equal(plain[int(py), int(px)], with_spawn[int(py), int(px)]))

	def test_diff_state_roundtrip(self):
		before = dump_state(self.snapshot, [rec(10.0, 10.0)])
		self.assertEqual(self.render(diff_state=before).size, (400 + LEGEND_WIDTH, 400))


if __name__ == "__main__":
	unittest.main()
