#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for dressing pass documents, apply and undo.

	python tools/tests/test_world_dressing_passes.py
"""

import hashlib
import os
import random
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.dressing import PassError, apply_pass, load_doc, new_draft, new_pass_id, save_doc, undo_pass, world_fingerprint  # noqa: E402
from worldkit.formats.wobj import entity_file, parse_wobj  # noqa: E402
from worldkit.paths import foliage_dir, manifests_dir  # noqa: E402

PAGE = (32 << 8) | 32
NOT_RUNNING = lambda: False  # noqa: E731
RUNNING = lambda: True  # noqa: E731


def items():
	return [
		{"role": "a", "asset": "Models/Test/Cube.hmsh", "store": "wobj", "position": [50.0, 0.0, 50.0], "yaw": 30.0, "tilt": [0.0, 0.0], "scale": 1.2, "collides": True},
		{"role": "b", "asset": "Models/Test/Shed.hwmo", "store": "wobj", "position": [70.0, 0.0, 50.0], "yaw": 0.0, "tilt": [0.0, 0.0], "scale": 1.0, "collides": True},
		{"role": "c", "asset": "Models/Trees/A.hmsh", "store": "hfol", "position": [60.0, 0.0, 60.0], "yaw": 90.0, "tilt": [2.0, -1.0], "scale": 0.9, "collides": True},
		{"role": "c", "asset": "Models/Trees/New.hmsh", "store": "hfol", "position": [62.0, 0.0, 60.0], "yaw": 10.0, "tilt": [0.0, 0.0], "scale": 1.1, "collides": False},
	]


class PassTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.repo = Path(self._tmp.name)
		fx.make_world(self.repo, "D", {(32, 32): {}})
		folder = foliage_dir("D", self.repo)
		folder.mkdir(parents=True)
		self.hfol = folder / f"{PAGE}.hfol"
		self.original = fx.hfol_bytes(["Models/Trees/A.hmsh"], [(5, 0, (1.0, 0.0, 1.0), (1.0, 0.0, 0.0, 0.0), (1.0, 1.0, 1.0), True)])
		self.hfol.write_bytes(self.original)
		self.doc = new_draft(new_pass_id("site", self.repo, today="20261001"), 0, "D", "site", "t", 1, [50.0, 50.0], [],
							 [55.0, 50.0], items(), [], [], world_fingerprint("D", self.repo))
		save_doc(self.doc, self.repo)

	def tearDown(self):
		self._tmp.cleanup()

	def apply(self):
		return apply_pass(self.doc, self.repo, probe=NOT_RUNNING, rng=random.Random(1))

	def test_pass_ids_count_up(self):
		self.assertEqual(self.doc["pass_id"], "20261001-site-1")
		self.assertEqual(new_pass_id("site", self.repo, today="20261001"), "20261001-site-2")

	def test_apply_writes_entities_and_foliage(self):
		doc = self.apply()
		self.assertEqual(doc["status"], "applied")
		cube = parse_wobj(self.repo / "data" / "client" / doc["items"][0]["file"])
		self.assertEqual((cube.kind, cube.asset, cube.category), ("mesh", "Models/Test/Cube.hmsh", "dressing/t"))
		self.assertEqual(parse_wobj(self.repo / "data" / "client" / doc["items"][1]["file"]).kind, "wmo")
		self.assertTrue((manifests_dir(self.repo) / f"{doc['pass_id']}.json").is_file())
		self.assertEqual(load_doc(doc["pass_id"], self.repo)["status"], "applied")
		self.assertNotEqual(self.hfol.read_bytes(), self.original)

	def test_undo_restores_everything(self):
		doc = self.apply()
		report = undo_pass(doc, self.repo, probe=NOT_RUNNING)
		self.assertEqual((len(report.removed), report.changed, report.missing), (4, [], []))
		self.assertEqual(self.hfol.read_bytes(), self.original)
		self.assertFalse((self.repo / "data" / "client" / doc["items"][0]["file"]).exists())
		self.assertEqual(doc["status"], "undone")

	def test_undo_keeps_what_the_user_changed(self):
		doc = self.apply()
		changed = self.repo / "data" / "client" / doc["items"][0]["file"]
		changed.write_bytes(changed.read_bytes()[:-1] + b"\x01")
		(self.repo / "data" / "client" / doc["items"][1]["file"]).unlink()
		report = undo_pass(doc, self.repo, probe=NOT_RUNNING)
		self.assertEqual((len(report.changed), len(report.missing), len(report.removed)), (1, 1, 2))
		self.assertTrue(changed.exists())
		self.assertEqual(doc["status"], "partially-undone")

	def test_guards(self):
		with self.assertRaises(PassError):
			apply_pass(self.doc, self.repo, probe=RUNNING)
		bad = dict(self.doc, checks={"placement": [{"severity": "error", "rule": "prop_floating"}], "walkability": []})
		with self.assertRaises(PassError):
			apply_pass(bad, self.repo, probe=NOT_RUNNING)
		stale = dict(self.doc, world_fingerprint="0" * 40)
		with self.assertRaises(PassError):
			apply_pass(stale, self.repo, probe=NOT_RUNNING)

	def test_failed_apply_rolls_back(self):
		self.doc["items"].append({"role": "x", "asset": "Models/Test/Bad.txt", "store": "wobj", "position": [1.0, 0.0, 1.0],
								  "yaw": 0.0, "tilt": [0.0, 0.0], "scale": 1.0, "collides": False})
		with self.assertRaises(PassError):
			self.apply()
		self.assertEqual(self.hfol.read_bytes(), self.original)
		self.assertEqual(list((self.repo / "data" / "client" / "Worlds" / "D" / "D" / "Entities").glob("*/*.wobj")), [])
		self.assertEqual(self.doc["status"], "planned")

	def test_failed_apply_with_malformed_item_still_rolls_back(self):
		del self.doc["items"][3]["collides"]  # KeyError after earlier items were already written
		with self.assertRaises(PassError):
			self.apply()
		self.assertEqual(self.hfol.read_bytes(), self.original)
		self.assertEqual(list((self.repo / "data" / "client" / "Worlds" / "D" / "D" / "Entities").glob("*/*.wobj")), [])
		self.assertNotIn("file", self.doc["items"][0])

	def test_undo_removes_foliage_file_the_pass_created(self):
		self.hfol.unlink()
		self.doc["world_fingerprint"] = world_fingerprint("D", self.repo)
		doc = self.apply()
		self.assertEqual(len(doc["created_files"]), 1)
		self.assertTrue(self.hfol.is_file())
		undo_pass(doc, self.repo, probe=NOT_RUNNING)
		self.assertFalse(self.hfol.exists())

	def test_undo_forced_removes_changed_prop(self):
		doc = self.apply()
		changed = self.repo / "data" / "client" / doc["items"][0]["file"]
		changed.write_bytes(changed.read_bytes()[:-1] + b"")
		report = undo_pass(doc, self.repo, force=True, probe=NOT_RUNNING)
		self.assertEqual((len(report.removed), report.changed), (4, []))
		self.assertFalse(changed.exists())
		self.assertEqual(doc["status"], "undone")

	def failing_apply(self):
		self.doc["items"].append({"role": "x", "asset": "Models/Test/Bad.txt", "store": "wobj", "position": [1.0, 0.0, 1.0],
								  "yaw": 0.0, "tilt": [0.0, 0.0], "scale": 1.0, "collides": False})
		with self.assertRaises(PassError) as ctx:
			self.apply()
		return ctx.exception

	def entity_dir(self):
		return entity_file("D", 1, 50.0, 50.0, self.repo).parent

	def test_rollback_removes_directories_apply_created(self):
		self.failing_apply()
		self.assertFalse(self.entity_dir().exists())

	def test_rollback_keeps_preexisting_empty_directory(self):
		self.entity_dir().mkdir(parents=True)
		self.failing_apply()
		self.assertTrue(self.entity_dir().is_dir())
		self.assertEqual(list(self.entity_dir().iterdir()), [])

	def test_failure_before_foliage_write_leaves_hfol_mtime_alone(self):
		os.utime(self.hfol, ns=(1_000_000_000_000_000_000, 1_000_000_000_000_000_000))
		self.doc["world_fingerprint"] = world_fingerprint("D", self.repo)
		del self.doc["items"][3]["collides"]  # fails while building the foliage page, before any .hfol write
		with self.assertRaises(PassError):
			self.apply()
		self.assertEqual(self.hfol.stat().st_mtime_ns, 1_000_000_000_000_000_000)
		self.assertEqual(self.hfol.read_bytes(), self.original)

	def test_partial_wobj_write_is_removed_by_rollback(self):
		real = Path.write_bytes

		def half_write(path, data):
			if path.suffix == ".wobj":
				real(path, data[:4])
				raise OSError("disk full")
			return real(path, data)

		with mock.patch.object(Path, "write_bytes", half_write):
			with self.assertRaises(PassError):
				self.apply()
		self.assertEqual(list((self.repo / "data" / "client" / "Worlds" / "D" / "D" / "Entities").glob("*/*.wobj")), [])

	def test_rollback_step_failure_does_not_skip_the_rest(self):
		real = Path.unlink
		stuck = []

		def flaky_unlink(path, missing_ok=False):
			if path.suffix == ".wobj" and not stuck:
				stuck.append(path)
				raise OSError("locked")
			return real(path, missing_ok=missing_ok)

		self.doc["items"].append({"role": "x", "asset": "Models/Test/Bad.txt", "store": "wobj", "position": [1.0, 0.0, 1.0],
								  "yaw": 0.0, "tilt": [0.0, 0.0], "scale": 1.0, "collides": False})
		with mock.patch.object(Path, "unlink", flaky_unlink):
			with self.assertRaises(PassError) as ctx:
				self.apply()
		self.assertIn("rollback was incomplete", str(ctx.exception))
		self.assertIn("locked", str(ctx.exception))
		left = list((self.repo / "data" / "client" / "Worlds" / "D" / "D" / "Entities").glob("*/*.wobj"))
		self.assertEqual(left, stuck)  # only the one that could not be removed survives

	def test_status_guards(self):
		doc = self.apply()
		with self.assertRaises(PassError):
			apply_pass(doc, self.repo, probe=NOT_RUNNING)  # already applied
		planned = dict(doc, status="planned")
		with self.assertRaises(PassError):
			undo_pass(planned, self.repo, probe=NOT_RUNNING)  # nothing to undo

	def test_undo_refuses_while_editor_runs(self):
		doc = self.apply()
		with self.assertRaises(PassError):
			undo_pass(doc, self.repo, probe=RUNNING)
		self.assertTrue((self.repo / "data" / "client" / doc["items"][0]["file"]).exists())
		self.assertEqual(doc["status"], "applied")

	def test_undo_refuses_paths_outside_the_client_folder(self):
		doc = self.apply()
		outside = self.repo / "outside.txt"
		outside.write_bytes(b"precious")
		doc["items"][0]["file"] = "../../outside.txt"
		doc["items"][0]["written_hash"] = hashlib.sha1(b"precious").hexdigest()
		report = undo_pass(doc, self.repo, force=True, probe=NOT_RUNNING)
		self.assertEqual(outside.read_bytes(), b"precious")
		self.assertTrue(any("../../outside.txt" in m and "outside" in m for m in report.missing))
		self.assertNotIn("../../outside.txt", report.removed)

	def test_undo_with_malformed_foliage_deletes_nothing(self):
		doc = self.apply()
		self.hfol.write_bytes(b"not a foliage file")
		with self.assertRaises(PassError):
			undo_pass(doc, self.repo, probe=NOT_RUNNING)
		for item in doc["items"][:2]:
			self.assertTrue((self.repo / "data" / "client" / item["file"]).exists())
		self.assertEqual(doc["status"], "applied")

	def test_undo_leaves_foliage_alone_when_nothing_to_remove(self):
		doc = self.apply()
		self.hfol.write_bytes(self.original)  # the pass's trees are gone already
		os.utime(self.hfol, ns=(1_000_000_000_000_000_000, 1_000_000_000_000_000_000))
		report = undo_pass(doc, self.repo, probe=NOT_RUNNING)
		self.assertEqual(len(report.missing), 2)
		self.assertEqual(self.hfol.stat().st_mtime_ns, 1_000_000_000_000_000_000)


if __name__ == "__main__":
	unittest.main()
