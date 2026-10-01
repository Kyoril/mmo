#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the nav_query client and the walkability checks.

	python tools/tests/test_world_dressing_nav.py
"""

import json
import os
import queue
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.nav import NavError, NavQuery, Route, build_nav, tool_exe, load_routes, save_routes, walkability_violations  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.spawns import SpawnRecord, spawn_key  # noqa: E402


class FakeProcess:
	"""Stands in for nav_query: answers from a function (returning None means "never answers").

	Answers go through a blocking queue like a real pipe, so a silent child blocks readline.
	"""

	def __init__(self, respond, ready=None, alive_for=None, die_on="read", exit_code=0, hang_on_wait=False):
		self.respond = respond
		self.alive_for = alive_for      # answers served before the child "dies" (None: never)
		self.die_on = die_on            # "read": the write works, the pipe then hits EOF; "write": the write raises
		self.exit_code = exit_code
		self.hang_on_wait = hang_on_wait
		self.served = 0
		self.terminated = False
		self.killed = False
		self.queue = queue.Queue()
		self.queue.put(json.dumps({"ready": True, "pages": 1}) + "\n" if ready is None else ready)
		outer = self

		class In:
			def write(self, text):
				if outer.alive_for is not None and outer.served >= outer.alive_for:
					if outer.die_on == "write":
						raise BrokenPipeError("pipe closed")
					outer.queue.put("")
					return
				answer = outer.respond(json.loads(text))
				outer.served += 1
				if answer is not None:
					outer.queue.put(json.dumps(answer) + "\n")

			def flush(self):
				pass

			def close(self):
				pass

		class Out:
			def readline(self):
				return outer.queue.get()

			def close(self):
				pass

		self.stdin, self.stdout = In(), Out()

	def wait(self, timeout=None):
		if self.hang_on_wait and not self.killed and not self.terminated:
			raise subprocess.TimeoutExpired("nav_query", timeout)
		return self.exit_code

	def terminate(self):
		self.terminated = True
		self.queue.put("")

	def kill(self):
		self.killed = True
		self.queue.put("")


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
				 {"name": "behind_wall", "x": 520.0, "z": 300.0, "radius": 30.0},
				 {"name": "origin", "x": 3.0, "z": 3.0, "radius": 10.0},
				 {"name": "origin_too", "x": 0.0, "z": 0.0, "radius": 10.0}]
		on_mesh, off_mesh = spawn(130.0, 125.0), spawn(-1.0, 1.0)
		violations = walkability_violations(FakeNav(), self.query, routes, sites, [on_mesh, off_mesh])
		found = {(v.rule, v.subject) for v in violations}
		self.assertIn(("route_broken", "route:cut"), found)
		self.assertIn(("route_longer", "route:long"), found)
		self.assertIn(("site_unreachable", "site:behind_wall"), found)
		self.assertNotIn(("site_unreachable", "site:near"), found)
		self.assertNotIn("route:ok", {v.subject for v in violations})
		self.assertIn(("spawn_off_mesh", off_mesh.key), found)
		self.assertNotIn(("spawn_off_mesh", on_mesh.key), found)
		# The off-mesh spawn lies inside two overlapping site radii but is reported once.
		self.assertEqual(sum(1 for v in violations if v.subject == off_mesh.key), 1)

	def test_error_answers_raise(self):
		nav = NavQuery(Path("nav"), "W", popen=lambda *a, **k: FakeProcess(lambda r: {"ok": False, "error": "bad op"}), exe=Path("nav_query"))
		with self.assertRaisesRegex(NavError, "bad op"):
			nav.path((0, 0, 0), (1, 0, 1))
		with self.assertRaisesRegex(NavError, "bad op"):
			nav.on_mesh((0, 0, 0))
		nav.close()

	def test_failed_start_terminates_the_child(self):
		for ready in (json.dumps({"ready": False}) + "\n", "this is not json\n", ""):
			spawned = []

			def popen(*a, _ready=ready, **k):
				spawned.append(FakeProcess(lambda r: {"ok": True}, ready=_ready))
				return spawned[0]

			with self.assertRaises(NavError):
				NavQuery(Path("nav"), "W", popen=popen, exe=Path("nav_query"))
			self.assertTrue(spawned[0].terminated, repr(ready))

	def test_dead_child_raises(self):
		for mode in ("read", "write"):
			proc = FakeProcess(lambda r: {"ok": True, "length": 1.0}, alive_for=1, die_on=mode, exit_code=3)
			nav = NavQuery(Path("nav"), "W", popen=lambda *a, **k: proc, exe=Path("nav_query"))
			self.assertEqual(nav.path((0, 0, 0), (1, 0, 1)), 1.0)
			with self.assertRaisesRegex(NavError, "exit code 3"):
				nav.path((0, 0, 0), (1, 0, 1))
			with self.assertRaisesRegex(NavError, "exited"):
				nav.on_mesh((0, 0, 0))
			nav.close()

	def test_silent_child_times_out(self):
		proc = FakeProcess(lambda r: None)
		nav = NavQuery(Path("nav"), "W", popen=lambda *a, **k: proc, exe=Path("nav_query"), timeout=0.2)
		with self.assertRaisesRegex(NavError, "did not answer"):
			nav.path((0, 0, 0), (1, 0, 1))
		self.assertTrue(proc.terminated)

	def test_close_kills_a_child_that_will_not_exit(self):
		proc = FakeProcess(lambda r: {"ok": True}, hang_on_wait=True)
		nav = NavQuery(Path("nav"), "W", popen=lambda *a, **k: proc, exe=Path("nav_query"))
		nav.close()
		self.assertTrue(proc.killed)

	def test_build_nav_timeout(self):
		def runner(cmd, **kwargs):
			self.assertEqual(kwargs["timeout"], 5)
			raise subprocess.TimeoutExpired(cmd, kwargs["timeout"])

		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			(repo / "bin" / "Release").mkdir(parents=True)
			(repo / "bin" / "Release" / "nav_builder.exe").write_bytes(b"")
			(repo / "bin" / "Release" / "nav_builder").write_bytes(b"")
			with self.assertRaisesRegex(NavError, "took too long"):
				build_nav("W", repo / "out", repo=repo, runner=runner, timeout=5)

	def test_tool_exe_finds_single_config_builds(self):
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			(repo / "bin").mkdir()
			with self.assertRaises(NavError):
				tool_exe("nav_query", repo)
			path = repo / "bin" / ("nav_query.exe" if os.name == "nt" else "nav_query")
			path.write_bytes(b"")
			self.assertEqual(tool_exe("nav_query", repo), path)

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
