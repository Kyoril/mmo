#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for site templates and the placer.

	python tools/tests/test_world_dressing_templates.py
"""

import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402
from test_world_dressing_lint import RULES, lint_world  # noqa: E402

from worldkit.assets import build_catalog  # noqa: E402
from worldkit.prop_lint import PropContext, lint_props, props_from_entities  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.templates import item_to_prop, load_template, place, settle  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402

TEMPLATE = {
	"version": 1, "name": "t", "description": "test", "entry": "fire", "clear": [3.0], "notes": ["n"],
	"roles": [
		{"name": "fire", "query": {"tags": ["crate"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "at_anchor", "offset": [5.0, 0.0]}},
		{"name": "stones", "query": {"tags": ["rock"]}, "count": [6, 6], "spacing": 4.0, "store": "hfol", "rule": {"type": "scatter", "radius_fraction": 0.9}},
		{"name": "near", "query": {"tags": ["crate"]}, "count": [2, 2], "spacing": 3.0, "store": "wobj", "rule": {"type": "near_role", "role": "fire", "distance": [3.0, 6.0]}},
		{"name": "nothing", "query": {"tags": ["bridge"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "scatter"}},
	],
}


class TemplateTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		lint_world(repo)
		snapshot = build_snapshot("L", repo=repo)
		cls.query = WorldQuery(snapshot, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
		cls.catalog = build_catalog(repo)
		cls.poi = {"id": "site", "center": [50.0, 60.0], "radius": 30.0}
		cls.context = PropContext(cls.query, cls.catalog, RULES, props_from_entities(snapshot.entities), [], cls.poi, [])
		folder = repo / "templates"
		folder.mkdir()
		(folder / "t.json").write_text(json.dumps(TEMPLATE), encoding="utf-8")
		cls.template = load_template("t", folder)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def run_place(self, seed=4):
		return place(self.template, (50.0, 60.0), 30.0, self.context, seed)

	def test_deterministic_per_seed(self):
		self.assertEqual(self.run_place(4).items, self.run_place(4).items)
		self.assertNotEqual(self.run_place(4).items, self.run_place(5).items)

	def test_roles_rules_and_gaps(self):
		result = self.run_place()
		roles = [i["role"] for i in result.items]
		self.assertEqual(roles.count("fire"), 1)
		self.assertEqual(roles.count("stones"), 6)
		fire = next(i for i in result.items if i["role"] == "fire")
		self.assertEqual(fire["position"][0], 55.0)
		self.assertEqual(result.entry, (55.0, 60.0))
		for item in result.items:
			if item["role"] == "near":
				self.assertLessEqual(math.hypot(item["position"][0] - 55.0, item["position"][2] - 60.0), 6.01)
		stones = [i for i in result.items if i["role"] == "stones"]
		for a in stones:
			for b in stones:
				if a is not b:
					self.assertGreaterEqual(math.hypot(a["position"][0] - b["position"][0], a["position"][2] - b["position"][2]), 4.0)
		self.assertIn({"role": "nothing", "kind": "asset", "wanted": 1, "placed": 0}, result.gaps)
		self.assertTrue(all(i["store"] == "hfol" for i in stones))

	def test_clear_circle_and_lint_clean(self):
		result = self.run_place()
		for item in result.items:
			if item["role"] != "fire":
				self.assertGreaterEqual(math.hypot(item["position"][0] - 50.0, item["position"][2] - 60.0), 3.0)
		props = [item_to_prop(item, f"d:{i}") for i, item in enumerate(result.items)]
		self.assertEqual([v for v in lint_props(props, self.context) if v.severity == "error"], [])

	def test_settle_on_flat_ground(self):
		info = self.catalog["Models/Test/Cube.hmsh"]
		y, pitch, roll = settle(info, RULES.for_asset("Models/Test/Cube.hmsh"), self.query, 50.0, 50.0, 0.0, 1.0)
		self.assertAlmostEqual(y, -0.025, places=3)
		self.assertEqual((pitch, roll), (0.0, 0.0))

	def test_shipped_templates_load(self):
		for name in ("quarry", "cave_mouth", "waterfall_basin", "hunting_camp", "abbey_surround"):
			self.assertTrue(load_template(name).roles, name)


if __name__ == "__main__":
	unittest.main()
