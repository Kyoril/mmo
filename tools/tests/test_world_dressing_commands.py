#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""End-to-end test of a dressing pass on a synthetic world: plan, check, apply, packet, undo.

	python tools/tests/test_world_dressing_commands.py
"""

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402
from test_world_dressing_lint import RULES, lint_world  # noqa: E402
from test_world_dressing_templates import TEMPLATE  # noqa: E402

from worldkit.assets import build_catalog  # noqa: E402
from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit import dress_commands  # noqa: E402
from worldkit.dress_commands import DressSession, check_pass, nav_check, plan_pass  # noqa: E402
from worldkit.dressing import PassError, apply_pass, load_doc, save_doc, undo_pass  # noqa: E402
from worldkit.foliage import load_world_foliage  # noqa: E402
from worldkit.nav import NavError  # noqa: E402
from worldkit.packet import write_batch_index, write_packet  # noqa: E402
from worldkit.paths import manifests_dir, passes_dir  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.spawns import SpawnRecord, spawn_key  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402


class MapEntry:
	id, name, directory, instancetype = 0, "Lint World", "L", 0


class CommandTests(unittest.TestCase):
	def test_plan_check_apply_packet_undo(self):
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			lint_world(repo)
			folder = repo / "tools" / "world" / "templates"
			folder.mkdir(parents=True)
			(folder / "t.json").write_text(json.dumps(TEMPLATE), encoding="utf-8")
			atlas = empty_atlas(0)
			atlas.add_poi(id="site", name="Site", kind="camp", center=[50.0, 60.0], radius=30.0, status="canon", source="user")
			query = WorldQuery(build_snapshot("L", repo=repo), atlas=atlas, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
			session = DressSession(MapEntry(), query, build_catalog(repo), RULES, load_world_foliage("L", repo), [], repo)
			doc = plan_pass(session, "site", "t", seed=4, render=False)
			self.assertEqual(doc["status"], "planned")
			self.assertTrue(doc["items"])
			self.assertEqual([v for v in doc["checks"]["placement"] if v["severity"] == "error"], [])
			doc = check_pass(session, doc, render=False)
			doc = apply_pass(doc, repo, probe=lambda: False)
			packet = write_packet(doc, {}, repo)
			readme = (packet / "README.md").read_text(encoding="utf-8")
			self.assertIn("dress.py undo", readme)
			self.assertIn("nothing", readme)          # the asset gap is listed
			self.assertEqual(len(undo_pass(doc, repo, probe=lambda: False).removed), len(doc["items"]))


def _spawn(x, z):
	return SpawnRecord(spawn_key("unit", 0, 1, "S", x, z), 0, "unit", 1, 0, 0, "S", x, 0.0, z, True, 0, 30000, ())


def _session(repo, spawns=()):
	atlas = empty_atlas(0)
	atlas.add_poi(id="site", name="Site", kind="camp", center=[50.0, 60.0], radius=30.0, status="canon", source="user")
	query = WorldQuery(build_snapshot("L", repo=repo), atlas=atlas, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
	return DressSession(MapEntry(), query, build_catalog(repo), RULES, load_world_foliage("L", repo), list(spawns), repo)


def _template_repo(repo: Path) -> None:
	lint_world(repo)
	folder = repo / "tools" / "world" / "templates"
	folder.mkdir(parents=True)
	(folder / "t.json").write_text(json.dumps(TEMPLATE), encoding="utf-8")


class _Result:
	def __init__(self, returncode=0, stdout="", stderr=""):
		self.returncode, self.stdout, self.stderr = returncode, stdout, stderr


class _Nav:
	"""Nothing is on the mesh (every spawn is reported off it); call number `fail_on` raises instead."""

	def __init__(self, fail_on=None):
		self.calls, self.fail_on = 0, fail_on

	def on_mesh(self, point, radius=2.0):
		self.calls += 1
		if self.calls == self.fail_on:
			raise NavError("nav_query went away")
		return None

	def __enter__(self):
		return self

	def __exit__(self, *exc):
		return False


class NavCheckTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.addCleanup(self._tmp.cleanup)
		self.repo = Path(self._tmp.name)
		_template_repo(self.repo)
		(self.repo / "bin" / "Release").mkdir(parents=True)          # the real tools never run: the files only have to exist
		for exe in ("nav_builder.exe", "nav_query.exe", "nav_builder", "nav_query"):
			(self.repo / "bin" / "Release" / exe).write_bytes(b"")
		self.session = _session(self.repo, [_spawn(50.0, 60.0)])
		self.builds = []
		routes = mock.patch.object(dress_commands, "load_routes", return_value=[])
		routes.start()
		self.addCleanup(routes.stop)

	def runner(self, result=None):
		def run(command, **kwargs):
			self.builds.append(command)
			return result or _Result()
		return run

	def applied(self, status="applied") -> dict:
		doc = plan_pass(self.session, "site", "t", seed=4, render=False)
		doc = apply_pass(doc, self.repo, probe=lambda: False)
		doc["status"] = status
		save_doc(doc, self.repo)
		return doc

	def tracked(self, doc) -> Path:
		return manifests_dir(self.repo) / f"{doc['pass_id']}.json"

	def test_success_stores_walkability_and_marks_applied(self):
		doc = self.applied("applied-unchecked")
		nav_check(self.session, [doc], runner=self.runner(), nav_factory=lambda: _Nav())
		self.assertEqual(doc["status"], "applied")
		self.assertEqual(len(self.builds), 1)
		self.assertEqual([v["rule"] for v in doc["checks"]["walkability"]], ["spawn_off_mesh"])
		self.assertNotIn("error", doc["nav"])
		self.assertEqual(load_doc(doc["pass_id"], self.repo)["status"], "applied")

	def test_build_failure_leaves_the_pass_unchecked(self):
		doc = self.applied()
		with self.assertRaises(NavError):
			nav_check(self.session, [doc], runner=self.runner(_Result(2, stderr="out of memory")), nav_factory=lambda: _Nav())
		self.assertEqual(doc["status"], "applied-unchecked")
		self.assertIn("out of memory", doc["nav"]["error"])
		stored = json.loads(self.tracked(doc).read_text(encoding="utf-8"))
		self.assertEqual(stored["status"], "applied-unchecked")
		self.assertIn("error", stored["nav"])

	def test_query_start_failure_leaves_the_pass_unchecked(self):
		doc = self.applied()

		def factory():
			raise NavError("nav_query did not become ready")
		with self.assertRaises(NavError):
			nav_check(self.session, [doc], runner=self.runner(), nav_factory=factory)
		self.assertEqual(doc["status"], "applied-unchecked")
		self.assertIn("did not become ready", doc["nav"]["error"])

	def test_missing_tool_leaves_the_pass_unchecked(self):
		doc = self.applied()
		for exe in (self.repo / "bin" / "Release").iterdir():
			exe.unlink()
		with self.assertRaises(NavError):
			nav_check(self.session, [doc], runner=self.runner(), nav_factory=lambda: _Nav())
		self.assertEqual(doc["status"], "applied-unchecked")
		self.assertIn("nav_builder", doc["nav"]["error"])

	def test_mid_loop_failure_keeps_the_finished_pass(self):
		first = self.applied()
		second = copy.deepcopy(first)
		second["pass_id"] += "-b"
		save_doc(second, self.repo)
		nav = _Nav(fail_on=2)                                      # the second pass's spawn check dies
		with self.assertRaises(NavError):
			nav_check(self.session, [first, second], runner=self.runner(), nav_factory=lambda: nav)
		self.assertEqual(first["status"], "applied")
		self.assertTrue(first["checks"]["walkability"])
		self.assertEqual(second["status"], "applied-unchecked")
		self.assertIn("went away", second["nav"]["error"])
		self.assertEqual(load_doc(first["pass_id"], self.repo)["status"], "applied")
		self.assertEqual(load_doc(second["pass_id"], self.repo)["status"], "applied-unchecked")

	def test_interrupt_during_the_build_leaves_the_pass_unchecked(self):
		doc = self.applied()

		def interrupted(command, **kwargs):
			raise KeyboardInterrupt()
		with self.assertRaises(KeyboardInterrupt):
			nav_check(self.session, [doc], runner=interrupted, nav_factory=lambda: _Nav())
		self.assertEqual(doc["status"], "applied-unchecked")
		self.assertIn("error", doc["nav"])
		self.assertEqual(load_doc(doc["pass_id"], self.repo)["status"], "applied-unchecked")

	def test_interrupt_mid_loop_keeps_the_finished_pass(self):
		first = self.applied()
		second = copy.deepcopy(first)
		second["pass_id"] += "-b"
		save_doc(second, self.repo)

		class Interrupted(_Nav):
			def on_mesh(self, point, radius=2.0):
				self.calls += 1
				if self.calls == 2:
					raise KeyboardInterrupt()
				return None
		nav = Interrupted()
		with self.assertRaises(KeyboardInterrupt):
			nav_check(self.session, [first, second], runner=self.runner(), nav_factory=lambda: nav)
		self.assertEqual(first["status"], "applied")
		self.assertTrue(first["checks"]["walkability"])
		self.assertEqual(second["status"], "applied-unchecked")
		self.assertEqual(load_doc(first["pass_id"], self.repo)["status"], "applied")
		self.assertEqual(load_doc(second["pass_id"], self.repo)["status"], "applied-unchecked")

	def test_a_failing_save_does_not_mask_the_interrupt(self):
		doc = self.applied()

		def interrupted(command, **kwargs):
			raise KeyboardInterrupt()
		with mock.patch.object(dress_commands, "save_doc", side_effect=OSError("disk full")), 				mock.patch.object(sys, "stderr"):
			with self.assertRaises(KeyboardInterrupt):
				nav_check(self.session, [doc], runner=interrupted, nav_factory=lambda: _Nav())
		self.assertEqual(doc["status"], "applied-unchecked")

	def test_planned_and_undone_passes_are_refused(self):
		planned = plan_pass(self.session, "site", "t", seed=4, render=False)
		with self.assertRaises(PassError):
			nav_check(self.session, [planned], runner=self.runner(), nav_factory=lambda: _Nav())
		self.assertEqual(planned["status"], "planned")
		self.assertFalse(self.tracked(planned).exists())           # a draft never becomes a tracked manifest
		undone = self.applied("undone")
		with self.assertRaises(PassError):
			nav_check(self.session, [undone], runner=self.runner(), nav_factory=lambda: _Nav())
		self.assertEqual(undone["status"], "undone")
		self.assertEqual(self.builds, [])                          # refused before any build

	def test_one_refused_pass_stops_the_whole_check(self):
		good = self.applied()
		planned = plan_pass(self.session, "site", "t", seed=5, render=False)
		with self.assertRaises(PassError):
			nav_check(self.session, [good, planned], runner=self.runner(), nav_factory=lambda: _Nav())
		self.assertEqual(self.builds, [])

	def test_partially_undone_keeps_its_status(self):
		doc = self.applied("partially-undone")
		nav_check(self.session, [doc], runner=self.runner(), nav_factory=lambda: _Nav())
		self.assertEqual(doc["status"], "partially-undone")
		self.assertTrue(doc["checks"]["walkability"])
		with self.assertRaises(NavError):
			nav_check(self.session, [doc], runner=self.runner(_Result(1)), nav_factory=lambda: _Nav())
		self.assertEqual(doc["status"], "partially-undone")
		self.assertIn("error", doc["nav"])


class PacketAndRenderTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.addCleanup(self._tmp.cleanup)
		self.repo = Path(self._tmp.name)
		_template_repo(self.repo)
		self.session = _session(self.repo)

	def applied(self) -> dict:
		doc = plan_pass(self.session, "site", "t", seed=4, render=False)
		return apply_pass(doc, self.repo, probe=lambda: False)

	def test_unchecked_packet_says_so_and_embeds_the_map_pictures(self):
		from PIL import Image
		doc = self.applied()
		doc["status"] = "applied-unchecked"
		save_doc(doc, self.repo)
		work = passes_dir(self.repo) / doc["pass_id"]
		for name in ("before.png", "after.png"):
			Image.new("RGB", (4, 4)).save(work / name)
		folder = write_packet(doc, {"preview_1.png": Image.new("RGB", (4, 4))}, self.repo)
		readme = (folder / "README.md").read_text(encoding="utf-8")
		self.assertIn("Walkability unchecked", readme)
		self.assertIn(f"check --nav {doc['pass_id']}", readme)
		for name in ("![before.png](before.png)", "![after.png](after.png)", "![preview_1.png](preview_1.png)"):
			self.assertIn(name, readme)
		self.assertTrue((folder / "before.png").is_file() and (folder / "after.png").is_file())
		stored = manifests_dir(self.repo) / f"{doc['pass_id']}.json"
		self.assertEqual((folder / "manifest.json").read_text(encoding="utf-8"), stored.read_text(encoding="utf-8"))  # same rounding

	def test_gap_reasons_and_batch_index(self):
		doc = self.applied()
		doc["gaps"] = [{"role": "banner", "kind": "asset", "placed": 0, "wanted": 2},
					   {"role": "tent", "kind": "space", "placed": 1, "wanted": 3}]
		readme = (write_packet(doc, {}, self.repo) / "README.md").read_text(encoding="utf-8")
		self.assertIn("`banner`: placed 0 of 2: no asset matches the role", readme)
		self.assertIn("`tent`: placed 1 of 3: no valid spot found", readme)
		other = copy.deepcopy(doc)
		other["pass_id"] += "-b"
		index = (write_batch_index("batch", [doc, other], self.repo) / "README.md").read_text(encoding="utf-8")
		self.assertIn(doc["pass_id"], index)
		self.assertIn(other["pass_id"], index)
		self.assertIn(f"{len(doc['items'])} | 0 | 2 |", index)

	def test_render_failure_is_a_pass_error_with_the_tail_of_the_output(self):
		doc = plan_pass(self.session, "site", "t", seed=4, render=False)
		failure = subprocess.CalledProcessError(3, ["render_map.py"], output="", stderr="x" * 2000 + "no such poi")
		with mock.patch.object(dress_commands.subprocess, "run", side_effect=failure):
			with self.assertRaises(PassError) as caught:
				dress_commands._render_map(self.session, doc, "before.png", [])
		message = str(caught.exception)
		self.assertIn("no such poi", message)
		self.assertLess(len(message), 1000)

	def test_applied_pass_does_not_collide_with_itself(self):
		doc = self.applied()
		self.assertTrue(doc["items"])
		after = _session(self.repo)                                  # a fresh snapshot now contains the applied props
		own = {int(item["unique_id"], 16) for item in doc["items"]}
		in_world = {e.unique_id for e in after.query.snapshot.entities} | {i.unique_id for f in after.foliage.values() for i in f.instances}
		self.assertTrue(own <= in_world)
		self.assertEqual([v for v in dress_commands._lint(after, doc) if v["severity"] == "error"], [])


if __name__ == "__main__":
	unittest.main()
