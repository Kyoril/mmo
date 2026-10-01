#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the nav_query client and the walkability checks.

	python tools/tests/test_world_dressing_nav.py
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.nav import NavQuery, Route, load_routes, save_routes, walkability_violations  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.spawns import SpawnRecord, spawn_key  # noqa: E402


class FakeProcess:
	"""Stands in for nav_query: answers from a function."""

	def __init__(self, respond):
		self.respond = respond
		self.lines = [json.dumps({"ready": True, "pages": 1}) + "\n"]
		outer = self

		class In:
			def write(self, text):
				outer.lines.append(json.dumps(outer.respond(json.loads(text))) + "\n")

			def flush(self):
				pass

			def close(self):
				pass

		class Out:
			def readline(self):
				return outer.lines.pop(0) if outer.lines else ""

			def close(self):
				pass

		self.stdin, self.stdout = In(), Out()

	def wait(self, timeout=None):
		return 0


class FakeNav:
	"""Straight-line paths except across a wall at x = 500; nothing is on the mesh near (0, 0)."""

	def path(self, a, b):
		if (a[0] < 500) != (b[0] < 500):
			return None
		return ((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5

	def on_mesh(self, p, radius=2.0):
		return None if abs(p[0]) < 5 and abs(p[2]) < 5 else 0.2


def spawn(x, z):
	return SpawnRecord(spawn_key("unit", 0, 1, "S", x, z), 0, "unit", 1, 0, 0, "S", x, 0.0, z, True, 0, 30000, ())


class NavTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		fx.make_world(Path(cls._tmp.name), "N", {(31, 31): {}, (32, 31): {}, (31, 32): {}, (32, 32): {}})
		cls.query = WorldQuery(build_snapshot("N", repo=Path(cls._tmp.name)))

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def test_client_protocol(self):
		answers = {"path": {"ok": True, "length": 12.5, "points": []}, "on_mesh": {"ok": True, "nearest": [0, 0, 0], "distance": 0.4}}
		nav = NavQuery(Path("nav"), "W", popen=lambda *a, **k: FakeProcess(lambda r: answers[r["op"]]), exe=Path("nav_query"))
		self.assertEqual(nav.path((0, 0, 0), (1, 0, 1)), 12.5)
		self.assertEqual(nav.on_mesh((0, 0, 0)), 0.4)
		nav.close()
		missing = NavQuery(Path("nav"), "W", popen=lambda *a, **k: FakeProcess(lambda r: {"ok": False}), exe=Path("nav_query"))
		self.assertIsNone(missing.path((0, 0, 0), (1, 0, 1)))

	def test_routes_sites_and_spawns(self):
		routes = [Route("ok", "fine", [[100.0, 100.0], [200.0, 100.0]], 100.0),
				  Route("cut", "crosses the wall", [[400.0, 100.0], [600.0, 100.0]], None),
				  Route("long", "got longer", [[100.0, 200.0], [150.0, 200.0]], 30.0)]
		sites = [{"name": "near", "x": 120.0, "z": 120.0, "radius": 30.0},
				 {"name": "behind_wall", "x": 520.0, "z": 300.0, "radius": 30.0}]
		violations = walkability_violations(FakeNav(), self.query, routes, sites, [spawn(130.0, 125.0), spawn(-1.0, 1.0)])
		found = {(v.rule, v.subject) for v in violations}
		self.assertIn(("route_broken", "route:cut"), found)
		self.assertIn(("route_longer", "route:long"), found)
		self.assertIn(("site_unreachable", "site:behind_wall"), found)
		self.assertNotIn(("site_unreachable", "site:near"), found)
		self.assertNotIn("route:ok", {v.subject for v in violations})

	def test_routes_file_round_trip(self):
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "routes.json"
			save_routes([Route("a", "A", [[1.0, 2.0], [3.0, 4.0]], 2.83)], path)
			self.assertEqual(load_routes(path)[0].baseline_length, 2.83)

	def test_shipped_routes_load(self):
		self.assertTrue(load_routes())


from worldkit.nav import scratch_nav_root, tool_exe  # noqa: E402


@fx.requires_live_data
class LiveNavTests(unittest.TestCase):
	def test_town_hall_to_west_gate(self):
		nav_dir = scratch_nav_root() / "nav"
		try:
			tool_exe("nav_query")
		except Exception:
			self.skipTest("nav_query is not built")
		if not (nav_dir / "Development.map").is_file():
			self.skipTest("no scratch navmesh: run nav_builder -o generated/world")
		with NavQuery(nav_dir, "Development") as nav:
			length = nav.path((300.0, 8.0, 560.0), (100.0, 4.0, 505.0))
		self.assertIsNotNone(length)
		self.assertLess(length, 600.0)


if __name__ == "__main__":
	unittest.main()
