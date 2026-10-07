# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import json
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.news import MANIFEST_LIMIT, MAX_PATCHES, add_patch_note, describe_change, render_notes


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

	def test_versioned_player_notes(self):
		notes = {"headline": "Mages rejoice.", "sections": [
			{"title": "Classes: Mage", "bullets": ["Frostbolt has a new impact effect."]},
			{"title": "Bug Fixes", "bullets": ["Fixed an issue where doors blocked sight."]}]}
		self.assertTrue(add_patch_note(self.path, "2026-10-07", ["fix: x"], "0.3.0.3441", notes))
		entry = self._load()["patches"][0]
		self.assertEqual(entry["title"], "Patch 0.3.0.3441")
		self.assertEqual(entry["date"], "2026-10-07")
		self.assertEqual(entry["summary"], "Mages rejoice.")
		self.assertEqual(entry["body"], "## Classes: Mage\n- Frostbolt has a new impact effect.\n\n## Bug Fixes\n- Fixed an issue where doors blocked sight.")

	def test_empty_or_malformed_notes_fall_back_to_changes(self):
		for notes in ({"headline": "h", "sections": []}, {"sections": [{"title": "General", "bullets": []}]}, "garbage"):
			add_patch_note(self.path, "2026-10-07", ["fix(loot): roll on the right table"], "0.3.0.1", notes)
			entry = self._load()["patches"][0]
			self.assertEqual(entry["title"], "Patch 0.3.0.1")
			self.assertEqual(entry["body"], "## Changes\n- Roll on the right table")

	def test_render_notes_skips_empty_sections(self):
		self.assertEqual(render_notes({"sections": [{"title": "General", "bullets": []}, {"title": "World", "bullets": ["a"]}]}), "## World\n- a")

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
