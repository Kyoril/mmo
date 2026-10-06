# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import json
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.news import MANIFEST_LIMIT, MAX_PATCHES, add_patch_note, describe_change


class News(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.path = Path(self.tmp.name) / "launcher.json"
		self.path.write_text(json.dumps({"version": 1, "news": [], "patches": [{"title": "Old", "summary": "s", "body": "b"}]}), encoding="utf-8")

	def tearDown(self):
		self.tmp.cleanup()

	def _load(self):
		return json.loads(self.path.read_text(encoding="utf-8"))

	def test_describe_change_strips_conventional_prefix(self):
		self.assertEqual(describe_change("fix(loot): roll on the right table"), "Roll on the right table")
		self.assertEqual(describe_change("feat!: breaking"), "Breaking")
		self.assertEqual(describe_change("Plain subject"), "Plain subject")

	def test_prepends_entry(self):
		self.assertTrue(add_patch_note(self.path, "2026-10-07", ["fix(loot): roll on the right table", "feat(chat): emotes"]))
		patches = self._load()["patches"]
		self.assertEqual(patches[0]["title"], "Update 2026-10-07")
		self.assertEqual(patches[0]["summary"], "2 fixes and improvements")
		self.assertIn("- Roll on the right table", patches[0]["body"])
		self.assertEqual(patches[1]["title"], "Old")
		self.assertEqual(self._load()["version"], 1)

	def test_no_changes(self):
		add_patch_note(self.path, "2026-10-07", [])
		self.assertEqual(self._load()["patches"][0]["summary"], "Maintenance update")

	def test_missing_file(self):
		self.assertFalse(add_patch_note(Path(self.tmp.name) / "none.json", "2026-10-07", ["x"]))

	def test_manifest_stays_within_launcher_limits(self):
		big = ["fix: " + "x" * 300] * 100
		for day in range(1, 41):
			add_patch_note(self.path, "2026-11-{:02d}".format(day), big)
		data = self._load()
		self.assertLessEqual(len(data["patches"]), MAX_PATCHES)
		self.assertLessEqual(len(self.path.read_bytes()), MANIFEST_LIMIT)
		self.assertEqual(data["patches"][0]["title"], "Update 2026-11-40")
		for entry in data["patches"]:
			self.assertLessEqual(len(entry["summary"].encode("utf-8")), 400)

	def test_malformed_json_returns_false(self):
		self.path.write_text("{invalid json}", encoding="utf-8")
		original = self.path.read_bytes()
		self.assertFalse(add_patch_note(self.path, "2026-10-07", ["fix: x"]))
		self.assertEqual(self.path.read_bytes(), original)

	def test_bom_prefixed_valid_file_is_updated(self):
		data = {"version": 1, "news": [], "patches": []}
		text = json.dumps(data)
		self.path.write_bytes(b'\xef\xbb\xbf' + text.encode("utf-8"))
		self.assertTrue(add_patch_note(self.path, "2026-10-07", ["fix: x"]))
		updated = self._load()
		self.assertEqual(len(updated["patches"]), 1)
		self.assertEqual(updated["patches"][0]["title"], "Update 2026-10-07")

	def test_patches_wrong_type_returns_false(self):
		self.path.write_text(json.dumps({"version": 1, "news": [], "patches": "x"}), encoding="utf-8")
		original = self.path.read_bytes()
		self.assertFalse(add_patch_note(self.path, "2026-10-07", ["fix: x"]))
		self.assertEqual(self.path.read_bytes(), original)


if __name__ == "__main__":
	unittest.main()
