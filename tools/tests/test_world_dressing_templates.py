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
from worldkit.geometry import quat_to_matrix  # noqa: E402
from worldkit.prop_lint import PropContext, lint_props, props_from_entities  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.templates import item_rotation, item_to_prop, load_template, place, settle  # noqa: E402
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
		(repo / "data" / "client" / "Models" / "Test" / "Rock_Big.hmsh").write_bytes(fx.cube_hmsh(collision=False, size=2.5))
		snapshot = build_snapshot("L", repo=repo)
		cls.query = WorldQuery(snapshot, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
		cls.catalog = build_catalog(repo)
		cls.poi = {"id": "site", "center": [50.0, 60.0], "radius": 30.0}
		cls.context = PropContext(cls.query, cls.catalog, RULES, props_from_entities(snapshot.entities), [], cls.poi, [])
		cls.folder = repo / "templates"
		cls.folder.mkdir()
		(cls.folder / "t.json").write_text(json.dumps(TEMPLATE), encoding="utf-8")
		cls.template = load_template("t", cls.folder)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def run_place(self, seed=4):
		return place(self.template, (50.0, 60.0), 30.0, self.context, seed)

	def make(self, roles, clear=(), entry="anchor", name="x"):
		(self.folder / f"{name}.json").write_text(json.dumps({"version": 1, "name": name, "entry": entry, "clear": list(clear), "roles": roles}), encoding="utf-8")
		return load_template(name, self.folder)

	@staticmethod
	def role(name, tag, rule, count=(4, 4), spacing=0.0, store="wobj", **extra):
		return {"name": name, "query": {"tags": [tag]}, "count": list(count), "spacing": spacing, "store": store, "rule": rule, **extra}

	def site(self, x, z, radius):
		"""A context whose place is centred on the given site."""
		poi = {"id": "site", "center": [x, z], "radius": radius}
		return PropContext(self.query, self.catalog, RULES, self.context.existing, [], poi, [])

	def corners(self, item, half):
		"""World xz of the footprint corners of a 2*half square item."""
		rotation = quat_to_matrix(item_rotation(item))
		scale = item["scale"]
		result = []
		for lx, lz in ((-half, -half), (half, -half), (half, half), (-half, half)):
			wx, _, wz = rotation @ [lx * scale, 0.0, lz * scale]
			result.append((item["position"][0] + float(wx), item["position"][2] + float(wz)))
		return result

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

	def test_space_gap(self):
		template = self.make([self.role("far", "crate", {"type": "scatter"}, count=(3, 3), spacing=100.0)])
		result = place(template, (50.0, 60.0), 30.0, self.context, 1)
		self.assertEqual(len(result.items), 1)
		self.assertEqual(result.gaps, [{"role": "far", "kind": "space", "wanted": 3, "placed": 1}])

	def test_keep_away_covers_all_roles_and_footprints(self):
		template = self.make([self.role("rocks", "rock", {"type": "scatter"}, count=(12, 12))])
		result = place(template, (50.0, 60.0), 30.0, self.context, 3, keep_away=[(50.0, 60.0, 24.0)])
		self.assertTrue(result.items)
		for item in result.items:
			distance = math.hypot(item["position"][0] - 50.0, item["position"][2] - 60.0)
			self.assertGreaterEqual(distance, 24.0 + math.hypot(1.0, 1.0) * item["scale"] - 0.01)
			for x, z in self.corners(item, 1.0):
				self.assertGreaterEqual(math.hypot(x - 50.0, z - 60.0), 24.0)
		# keep_away also binds at_anchor roles; the template's own clear radii do not
		fire = self.make([self.role("fire", "crate", {"type": "at_anchor"}, count=(1, 1))], clear=[3.0])
		self.assertEqual(len(place(fire, (50.0, 60.0), 30.0, self.context, 1).items), 1)
		blocked = place(fire, (50.0, 60.0), 30.0, self.context, 1, keep_away=[(50.0, 60.0, 1.0)])
		self.assertEqual(blocked.items, [])
		self.assertEqual(blocked.gaps, [{"role": "fire", "kind": "space", "wanted": 1, "placed": 0}])

	def test_clear_circle_is_footprint_aware(self):
		big = {**self.role("big", "rock", {"type": "ring", "r1": 3.5, "r2": 12.0}, count=(5, 5)),
			   "query": {"tags": ["rock"], "assets": ["Models/Test/Rock_Big.hmsh"]}}
		result = place(self.make([big], clear=[3.0]), (50.0, 60.0), 30.0, self.context, 2)
		self.assertTrue(result.items)
		for item in result.items:
			self.assertEqual(item["asset"], "Models/Test/Rock_Big.hmsh")
			for x, z in self.corners(item, 2.5):
				self.assertGreaterEqual(math.hypot(x - 50.0, z - 60.0), 3.0)

	def test_against_cliff(self):
		template = self.make([self.role("rocks", "rock", {"type": "against_cliff"}, count=(5, 5), spacing=1.0)])
		result = place(template, (296.0, 200.0), 12.0, self.site(296.0, 200.0, 12.0), 6)
		self.assertTrue(result.items)
		for item in result.items:
			x, z = item["position"][0], item["position"][2]
			self.assertLessEqual(self.query.slope_at(x, z), 35.0)
			self.assertGreater(x, 290.0)
			self.assertLess(abs(item["yaw"] - 270.0), 25.0)

	def test_along_path(self):
		template = self.make([self.role("props", "crate", {"type": "along_path", "offset": [4.0, 9.0]}, count=(5, 5), spacing=2.0)])
		result = place(template, (105.0, 60.0), 20.0, self.site(105.0, 60.0, 20.0), 8)
		self.assertTrue(result.items)
		for item in result.items:
			x = item["position"][0]
			self.assertLessEqual(max(100.0 - x, x - 110.0, 0.0), 9.0 + 1.0 + 0.01)

	def test_ring_and_cluster_ranges(self):
		template = self.make([self.role("ring", "crate", {"type": "ring", "r1": 8.0, "r2": 12.0}, count=(4, 4), spacing=3.0),
							  self.role("pile", "rock", {"type": "cluster", "spread": 4.0}, count=(4, 4), spacing=0.0)])
		result = place(template, (50.0, 60.0), 30.0, self.context, 9)
		ring = [i for i in result.items if i["role"] == "ring"]
		pile = [i for i in result.items if i["role"] == "pile"]
		self.assertTrue(ring and len(pile) >= 2)
		for item in ring:
			self.assertTrue(7.99 <= math.hypot(item["position"][0] - 50.0, item["position"][2] - 60.0) <= 12.01)
		for a in pile:
			for b in pile:
				self.assertLessEqual(math.hypot(a["position"][0] - b["position"][0], a["position"][2] - b["position"][2]), 8.02)

	def test_settle_aligns_to_the_ramp(self):
		info = self.catalog["Models/Test/Rock.hmsh"]
		tags = RULES.for_asset("Models/Test/Rock.hmsh")
		h = self.query.height_at
		for yaw in (0.0, 37.0, 200.0):
			y, pitch, roll = settle(info, tags, self.query, 330.0, 80.0, yaw, 1.0)
			item = {"yaw": yaw, "tilt": [pitch, roll], "scale": 1.0, "position": [330.0, y, 80.0]}
			rotation = quat_to_matrix(item_rotation(item))
			up = rotation @ [0.0, 1.0, 0.0]
			normal = [-(h(331.0, 80.0) - h(329.0, 80.0)) / 2.0, 1.0, -(h(330.0, 81.0) - h(330.0, 79.0)) / 2.0]
			norm = math.sqrt(sum(c * c for c in normal))
			self.assertGreater(sum(float(u) * c / norm for u, c in zip(up, normal)), 0.999, yaw)
			for lx, lz in ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)):
				wx, wy, wz = rotation @ [lx, 0.0, lz]
				ground = h(330.0 + float(wx), 80.0 + float(wz))
				self.assertLess(abs(y + float(wy) - ground), 0.25, (yaw, lx, lz))

	def test_load_validation(self):
		def bad(roles, entry="anchor"):
			with self.assertRaises(ValueError):
				self.make(roles, entry=entry, name="bad")
		crate = self.role("a", "crate", {"type": "scatter"})
		bad([crate, crate])
		bad([self.role("n", "crate", {"type": "near_role", "role": "a"}), crate])
		bad([self.role("n", "crate", {"type": "near_role", "role": "missing"})])
		bad([crate], entry="missing")
		bad([self.role("r", "crate", {"type": "ring", "r1": 3.0})])
		self.make([crate, self.role("n", "crate", {"type": "near_role", "role": "a"})], entry="n", name="good")

	def test_role_sink_overrides_the_tag_sink(self):
		info = self.catalog["Models/Test/Cube.hmsh"]
		template = self.make([self.role("flat", "crate", {"type": "scatter", "radius_fraction": 0.3}, count=(4, 4), spacing=3.0, sink=0.0, scale=[0.5, 1.5])])
		self.assertEqual(template.roles[0].sink, 0.0)
		result = place(template, (50.0, 60.0), 30.0, self.context, 2)
		self.assertTrue(result.items)
		for item in result.items:
			x, _, z = item["position"]
			bottom = item["position"][1] + info.bounds_min[1] * item["scale"]
			self.assertAlmostEqual(bottom, self.query.height_at(x, z), delta=0.01)
		# without a role sink the tag's sink applies and the template field stays unset
		plain = self.make([self.role("flat", "crate", {"type": "scatter"})], name="plain")
		self.assertIsNone(plain.roles[0].sink)

	def test_negative_role_sink_is_rejected(self):
		with self.assertRaises(ValueError):
			self.make([self.role("n", "crate", {"type": "scatter"}, sink=-0.1)], name="negsink")

	def test_shipped_templates_load(self):
		for name in ("quarry", "cave_mouth", "waterfall_basin", "hunting_camp", "abbey_surround"):
			self.assertTrue(load_template(name).roles, name)


if __name__ == "__main__":
	unittest.main()
