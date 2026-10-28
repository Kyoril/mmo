#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for tools/glue_bake_check.py (stdlib unittest):

    python tools/tests/test_glue_bake_check.py
"""

import importlib.util
import io
import json
import os
import shutil
import sys
import tempfile
import time
import unittest
from contextlib import redirect_stdout

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_spec = importlib.util.spec_from_file_location("glue_bake_check", os.path.join(REPO_ROOT, "tools", "glue_bake_check.py"))
gbc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gbc)


class GlueBakeCheckTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="glue_bake_check_")
		self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
		self.write("data/client/Interface/GlueUI/GlueScene.json", b'{"world":"w"}')
		self.write("data/client/Kits/a.hkit", b"aaa")
		self.write("data/client/Kits/b.hkit", b"bbb")
		self.write("src/shared/game_common/glue_bake.h", b"constexpr uint32 GlueBakeRulesVersion = 2;\n")
		self.write_manifest(rules=2)

	def write(self, rel, data):
		path = os.path.join(self.root, rel)
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, "wb") as handle:
			handle.write(data)

	def read_manifest(self):
		with open(os.path.join(self.root, gbc.MANIFEST), "r", encoding="utf-8") as handle:
			return json.load(handle)

	def write_manifest(self, rules=2):
		manifest = {
			"version": 1,
			"rulesVersion": rules,
			"sceneHash": gbc.fnv1a64(b'{"world":"w"}'),
			"inputs": [
				{"path": "Kits/a.hkit", "hash": gbc.fnv1a64(b"aaa")},
				{"path": "Kits/b.hkit", "hash": gbc.fnv1a64(b"bbb")},
			],
		}
		self.write(gbc.MANIFEST, json.dumps(manifest).encode("utf-8"))

	def run_main(self):
		out = io.StringIO()
		with redirect_stdout(out):
			code = gbc.main(["--root", self.root])
		self.assertEqual(code, 0)
		return out.getvalue().splitlines()

	def test_hash_of_abc(self):
		self.assertEqual(gbc.fnv1a64(b"abc"), "e71fa2190541574b")

	def test_fresh_tree_is_silent(self):
		self.assertEqual(self.run_main(), [])

	def test_modified_input_warns_naming_it(self):
		self.write("data/client/Kits/b.hkit", b"changed")
		lines = self.run_main()
		self.assertEqual(len(lines), 1)
		self.assertTrue(lines[0].startswith("WARNING: glue bake stale: 1 input(s) changed (Kits/b.hkit)"))
		self.assertIn("mmo_edit --bake-glue-scene data/client/Interface/GlueUI/GlueScene.json", lines[0])

	def test_missing_input_warns(self):
		os.remove(os.path.join(self.root, "data/client/Kits/a.hkit"))
		lines = self.run_main()
		self.assertEqual(len(lines), 1)
		self.assertIn("Kits/a.hkit", lines[0])

	def test_only_first_five_paths_are_listed(self):
		manifest = self.read_manifest()
		manifest["inputs"] = [{"path": "gone/%d" % i, "hash": "x"} for i in range(7)]
		self.write(gbc.MANIFEST, json.dumps(manifest).encode("utf-8"))
		lines = self.run_main()
		self.assertIn("7 input(s) changed", lines[0])
		self.assertIn("gone/4", lines[0])
		self.assertNotIn("gone/5", lines[0])

	def test_changed_scene_warns(self):
		self.write("data/client/Interface/GlueUI/GlueScene.json", b'{"world":"other"}')
		lines = self.run_main()
		self.assertEqual(len(lines), 1)
		self.assertIn("GlueScene.json", lines[0])

	def test_rules_version_mismatch_warns(self):
		self.write_manifest(rules=1)
		lines = self.run_main()
		self.assertEqual(len(lines), 1)
		self.assertIn("bake rules version 1 != 2", lines[0])

	def test_missing_rules_version_warns(self):
		manifest = self.read_manifest()
		del manifest["rulesVersion"]
		self.write(gbc.MANIFEST, json.dumps(manifest).encode("utf-8"))
		self.assertEqual(len(self.run_main()), 1)

	def test_top_level_list_is_malformed(self):
		self.write(gbc.MANIFEST, b"[1, 2]")
		lines = self.run_main()
		self.assertEqual(len(lines), 1)
		self.assertIn("manifest malformed", lines[0])

	def test_inputs_not_a_list_is_malformed(self):
		manifest = self.read_manifest()
		manifest["inputs"] = {"a": 1}
		self.write(gbc.MANIFEST, json.dumps(manifest).encode("utf-8"))
		lines = self.run_main()
		self.assertEqual(len(lines), 1)
		self.assertIn("manifest malformed", lines[0])

	def test_non_dict_entry_is_skipped_and_warned(self):
		manifest = self.read_manifest()
		manifest["inputs"].append("oops")
		manifest["inputs"].append({"path": 3, "hash": None})
		self.write(gbc.MANIFEST, json.dumps(manifest).encode("utf-8"))
		lines = self.run_main()
		self.assertEqual(len(lines), 1)
		self.assertIn("manifest malformed", lines[0])

	def add_outputs(self):
		manifest = self.read_manifest()
		manifest["meshes"] = [{"mesh": "Interface/GlueUI/GlueBake/Chunk_0.hmsh"}, {"mesh": "Models/tree.hmsh"}]
		manifest["terrainTiles"] = [
			{"mesh": "Interface/GlueUI/GlueBake/NearTile_0.hmsh", "material": "Models/Terrain/a.hmi",
			 "splatting": "Interface/GlueUI/GlueBake/Splat_0.htex"},
			{"mesh": "Interface/GlueUI/GlueBake/FarTile_0.hmsh", "material": "Interface/GlueUI/GlueBake/FarAlbedo_0.hmi",
			 "splatting": ""},
		]
		manifest["water"] = [{"mesh": "Interface/GlueUI/GlueBake/Water_0.hmsh"}]
		self.write(gbc.MANIFEST, json.dumps(manifest).encode("utf-8"))
		outputs = ["Chunk_0.hmsh", "NearTile_0.hmsh", "Splat_0.htex", "FarTile_0.hmsh", "FarAlbedo_0.hmi",
				   "FarAlbedo_0.htex", "Water_0.hmsh"]
		for name in outputs:
			self.write("data/client/Interface/GlueUI/GlueBake/" + name, b"x")
		return outputs

	def test_present_outputs_are_silent(self):
		self.add_outputs()
		self.assertEqual(self.run_main(), [])

	def test_each_missing_output_warns(self):
		for name in self.add_outputs():
			path = os.path.join(self.root, "data/client/Interface/GlueUI/GlueBake", name)
			os.remove(path)
			lines = self.run_main()
			self.assertEqual(len(lines), 1, name)
			self.assertTrue(lines[0].startswith("WARNING: glue bake incomplete: 1 output(s) missing"), lines[0])
			self.assertIn(name, lines[0])
			self.write("data/client/Interface/GlueUI/GlueBake/" + name, b"x")

	def test_outputs_outside_the_bake_are_not_checked(self):
		self.add_outputs()
		self.assertNotIn("Models/tree.hmsh", gbc._owned_outputs(self.read_manifest()))
		self.assertNotIn("Models/Terrain/a.hmi", gbc._owned_outputs(self.read_manifest()))

	def test_missing_manifest_warns(self):
		os.remove(os.path.join(self.root, gbc.MANIFEST))
		self.assertEqual(len(self.run_main()), 1)

	def test_real_header_constant_is_readable(self):
		self.assertIsNotNone(gbc._read_rules_version(REPO_ROOT))


class HashCacheTests(unittest.TestCase):
	OLD = 1_600_000_000_000_000_000

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="glue_bake_cache_")
		self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
		self.cache_path = os.path.join(self.root, "cache.json")
		self.file = os.path.join(self.root, "a.bin")
		self.write(b"abc")
		self.calls = 0
		original = gbc.fnv1a64

		def counting(data):
			self.calls += 1
			return original(data)

		gbc.fnv1a64 = counting
		self.addCleanup(setattr, gbc, "fnv1a64", original)

	def write(self, data, mtime_ns=None):
		with open(self.file, "wb") as handle:
			handle.write(data)
		stamp = self.OLD if mtime_ns is None else mtime_ns
		os.utime(self.file, ns=(stamp, stamp))

	def hash_once(self):
		cache = gbc.HashCache(self.cache_path)
		value = cache.hash_file(self.file)
		cache.save()
		return value

	def test_cache_hit_skips_hashing(self):
		self.assertEqual(self.hash_once(), "e71fa2190541574b")
		self.assertEqual(self.calls, 1)
		self.assertEqual(self.hash_once(), "e71fa2190541574b")
		self.assertEqual(self.calls, 1)

	def test_changed_mtime_rehashes(self):
		self.hash_once()
		os.utime(self.file, ns=(self.OLD + 1000, self.OLD + 1000))
		self.hash_once()
		self.assertEqual(self.calls, 2)

	def test_changed_size_rehashes(self):
		self.hash_once()
		self.write(b"abcd")
		self.assertEqual(self.hash_once(), gbc.fnv1a64(b"abcd"))
		self.assertEqual(self.calls, 3)

	def test_recently_modified_file_is_not_cached(self):
		now = time.time_ns()
		self.write(b"abc", mtime_ns=now)
		self.hash_once()
		self.hash_once()
		self.assertEqual(self.calls, 2)

	def test_corrupt_cache_is_rebuilt(self):
		for garbage in (b"{not json", b"[1, 2]", b'{"files": {"x": 5}}', b'{"files": []}', b"\xff\xfe"):
			with open(self.cache_path, "wb") as handle:
				handle.write(garbage)
			self.assertEqual(self.hash_once(), "e71fa2190541574b")
		self.assertEqual(self.hash_once(), "e71fa2190541574b")
		self.assertEqual(self.calls, 5)

	def test_wrong_typed_entry_rehashes(self):
		key = os.path.normcase(os.path.abspath(self.file))
		with open(self.cache_path, "w", encoding="utf-8") as handle:
			json.dump({"files": {key: {"size": 3, "mtime_ns": self.OLD, "hash": 7}}}, handle)
		self.assertEqual(self.hash_once(), "e71fa2190541574b")
		self.assertEqual(self.calls, 1)

	def test_missing_file_is_none(self):
		os.remove(self.file)
		self.assertIsNone(self.hash_once())

	def test_check_uses_the_default_cache_in_the_root(self):
		tree = GlueBakeCheckTests("test_fresh_tree_is_silent")
		tree.setUp()
		self.addCleanup(tree.doCleanups)
		for directory, _, names in os.walk(os.path.join(tree.root, "data")):
			for name in names:
				os.utime(os.path.join(directory, name), ns=(self.OLD, self.OLD))
		self.assertEqual(gbc.check(tree.root), [])
		first = self.calls
		self.assertTrue(os.path.isfile(os.path.join(tree.root, gbc.CACHE)))
		self.assertEqual(gbc.check(tree.root), [])
		self.assertEqual(self.calls, first)


if __name__ == "__main__":
	unittest.main(verbosity=2)
