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

	def test_missing_manifest_warns(self):
		os.remove(os.path.join(self.root, gbc.MANIFEST))
		self.assertEqual(len(self.run_main()), 1)

	def test_real_header_constant_is_readable(self):
		self.assertIsNotNone(gbc._read_rules_version(REPO_ROOT))


if __name__ == "__main__":
	unittest.main(verbosity=2)
