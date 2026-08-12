#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for .agents/skills/mmo-npc-designer/scripts/apply_encounter_json.py.

Note the path: .agents/ is the git-tracked home for these scripts. A .claude/skills/
mirror also exists on some machines but is gitignored (.gitignore:67), so pointing this
test at it would make the whole suite die on import in a fresh clone or a git worktree.

This applier is the only way trigger rows and map encounter slots reach the binary
project data. Its failure modes are quiet: appending a duplicate id instead of updating
one, or dropping an instance trigger, both produce a file that still loads and a boss
that simply never fires. Those cases are pinned down here.

The MainCli tests below exercise the CLI end to end (argument parsing, the format guard,
--dry-run, --backup) against a throwaway <root> built under a temp directory -- never
against data/editor, which is a live git submodule holding real game content.

	python tools/tests/test_apply_encounter_json.py
"""

import importlib.util
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPTS_DIR = os.path.join(REPO_ROOT, ".agents", "skills", "mmo-npc-designer", "scripts")
MODULE_PATH = os.path.join(SCRIPTS_DIR, "apply_encounter_json.py")

sys.path.insert(0, SCRIPTS_DIR)
_spec = importlib.util.spec_from_file_location("apply_encounter_json", MODULE_PATH)
aej = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(aej)

from npc_catalog_lib import CATALOG_FILES  # noqa: E402
from proto_runtime import find_protoc, load_modules  # noqa: E402

# load_modules (via proto_runtime.find_proto_dir/find_protoc) joins project_root with "/",
# which requires a Path -- REPO_ROOT is a plain str from os.path, so it must be wrapped here.
MODULES = load_modules(Path(REPO_ROOT))


def _force_remove_readonly(func, target, _exc_info):
	os.chmod(target, stat.S_IWRITE)
	func(target)


def _rmtree_force(path: Path) -> None:
	shutil.rmtree(path, onerror=_force_remove_readonly)


def _build_template_root() -> Path:
	"""A throwaway <root> with the layout apply_encounter_json.py expects, built entirely
	under a fresh tempfile.mkdtemp() -- data/editor (the real submodule) is never touched.

	Contains: a copy of src/shared/proto_data (for the schema compile), a copy of the
	real protoc.exe (so load_modules() never reaches into the real build/ tree), and one
	empty .data file per catalog so load_catalog_bundle() can load all of them -- maps.data
	is seeded with a single MapEntry(id=1) so the map-id lookups in main() resolve.
	"""
	root = Path(tempfile.mkdtemp(prefix="mmo_cli_template_"))

	proto_src = Path(REPO_ROOT) / "src" / "shared" / "proto_data"
	proto_dst = root / "src" / "shared" / "proto_data"
	proto_dst.parent.mkdir(parents=True, exist_ok=True)
	shutil.copytree(proto_src, proto_dst)

	real_protoc = find_protoc(Path(REPO_ROOT))
	protoc_dst_dir = root / "build" / "_deps" / "protobuf-build" / real_protoc.parent.name
	protoc_dst_dir.mkdir(parents=True, exist_ok=True)
	shutil.copy2(real_protoc, protoc_dst_dir / "protoc.exe")

	data_dir = root / "data" / "editor" / "data"
	data_dir.mkdir(parents=True, exist_ok=True)
	for filename in CATALOG_FILES.values():
		# An empty message serializes to zero bytes in protobuf, and ParseFromString(b"")
		# never validates required fields on the (absent) elements -- a valid, empty catalog.
		(data_dir / filename).write_bytes(b"")

	maps = MODULES["maps"].Maps()
	maps.entry.add(id=1, name="Test Dungeon", directory="Test")
	(data_dir / "maps.data").write_bytes(maps.SerializeToString())

	return root


class ReplaceOrAppendById(unittest.TestCase):
	def test_appends_a_new_id(self):
		triggers = MODULES["triggers"].Triggers()
		entry = MODULES["triggers"].TriggerEntry(id=29, name="Sevrin Wax - On Aggro")
		self.assertEqual(aej.replace_or_append_by_id(triggers.entry, entry), "created")
		self.assertEqual(len(triggers.entry), 1)

	def test_updates_an_existing_id_in_place(self):
		# A second apply of the same draft must not leave two rows with id 29 behind:
		# the loader keys triggers by id, so a duplicate silently shadows the real one.
		triggers = MODULES["triggers"].Triggers()
		triggers.entry.add(id=29, name="old name")
		entry = MODULES["triggers"].TriggerEntry(id=29, name="new name")
		self.assertEqual(aej.replace_or_append_by_id(triggers.entry, entry), "updated")
		self.assertEqual(len(triggers.entry), 1)
		self.assertEqual(triggers.entry[0].name, "new name")


class ApplyEncounter(unittest.TestCase):
	def test_adds_an_encounter_slot(self):
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		encounter = MODULES["maps"].EncounterEntry(id=1, name="Sevrin Wax, the Coffinwright")
		self.assertEqual(aej.apply_encounter(map_entry, encounter), "created")
		self.assertEqual(len(map_entry.encounters), 1)

	def test_updates_an_existing_slot_by_id(self):
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		map_entry.encounters.add(id=1, name="placeholder")
		encounter = MODULES["maps"].EncounterEntry(id=1, name="Sevrin Wax, the Coffinwright")
		self.assertEqual(aej.apply_encounter(map_entry, encounter), "updated")
		self.assertEqual(len(map_entry.encounters), 1)
		self.assertEqual(map_entry.encounters[0].name, "Sevrin Wax, the Coffinwright")


class ApplyInstanceTriggers(unittest.TestCase):
	def test_adds_missing_trigger_ids(self):
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		self.assertEqual(aej.apply_instance_triggers(map_entry, [37]), "added 37")
		self.assertEqual(list(map_entry.instance_triggers), [37])

	def test_is_idempotent(self):
		# Re-applying the draft must not grow the list; a duplicated instance trigger
		# would run the wipe handler twice per wipe.
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		map_entry.instance_triggers.append(37)
		self.assertEqual(aej.apply_instance_triggers(map_entry, [37]), "unchanged")
		self.assertEqual(list(map_entry.instance_triggers), [37])

	def test_preserves_unrelated_ids(self):
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		map_entry.instance_triggers.append(99)
		aej.apply_instance_triggers(map_entry, [37])
		self.assertEqual(sorted(map_entry.instance_triggers), [37, 99])


class MainCli(unittest.TestCase):
	"""Exercises main() as a subprocess so exit codes and file side effects are pinned
	down exactly as a caller of the CLI would observe them -- against a throwaway
	--project-root, never against data/editor.
	"""

	@classmethod
	def setUpClass(cls):
		cls.template_root = _build_template_root()

	@classmethod
	def tearDownClass(cls):
		_rmtree_force(cls.template_root)

	def setUp(self):
		work_parent = Path(tempfile.mkdtemp(prefix="mmo_cli_work_"))
		self.addCleanup(_rmtree_force, work_parent)
		self.work_root = work_parent / "root"
		shutil.copytree(self.template_root, self.work_root)

		self.data_dir = self.work_root / "data" / "editor" / "data"
		self.triggers_path = self.data_dir / "triggers.data"
		self.maps_path = self.data_dir / "maps.data"

	def _write_doc(self, doc: dict) -> Path:
		doc_path = self.work_root / "doc.json"
		doc_path.write_text(json.dumps(doc), encoding="utf-8")
		return doc_path

	def _run(self, doc_path: Path, *extra_args: str) -> subprocess.CompletedProcess:
		command = [sys.executable, MODULE_PATH, str(doc_path), "--project-root", str(self.work_root), *extra_args]
		return subprocess.run(command, capture_output=True, text=True)

	def _bak_files(self) -> list[Path]:
		return sorted(self.data_dir.glob("*.bak"))

	def test_dry_run_writes_nothing(self):
		# --backup is passed alongside --dry-run on purpose: dry-run must win even when
		# a backup was also requested, not just when it is the only flag given.
		before_triggers = self.triggers_path.read_bytes()
		before_maps = self.maps_path.read_bytes()
		doc_path = self._write_doc(
			{
				"format": "mmo-encounter",
				"triggers": [{"id": 501, "name": "Dry Run Trigger"}],
				"map_encounters": [{"map_id": 1, "encounter": {"id": 501, "name": "Dry Run Encounter"}}],
				"map_instance_triggers": [{"map_id": 1, "trigger_ids": [501]}],
			}
		)

		result = self._run(doc_path, "--dry-run", "--backup")

		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertIn("DRY-RUN", result.stdout)
		self.assertEqual(self.triggers_path.read_bytes(), before_triggers)
		self.assertEqual(self.maps_path.read_bytes(), before_maps)
		self.assertEqual(self._bak_files(), [])

	def test_backup_captures_pre_apply_bytes_not_post_apply(self):
		# Seed triggers.data with a pre-existing row distinguishable from "empty", so a
		# backup taken after the write (the bug this test exists to catch) is easy to
		# tell apart from a backup taken before it: the former would contain 501 too.
		pre_triggers = MODULES["triggers"].Triggers()
		pre_triggers.entry.add(id=1, name="Pre-existing Trigger")
		self.triggers_path.write_bytes(pre_triggers.SerializeToString())

		before_triggers = self.triggers_path.read_bytes()
		before_maps = self.maps_path.read_bytes()

		doc_path = self._write_doc(
			{
				"format": "mmo-encounter",
				"triggers": [{"id": 501, "name": "New Trigger"}],
				"map_encounters": [{"map_id": 1, "encounter": {"id": 501, "name": "New Encounter"}}],
			}
		)

		result = self._run(doc_path, "--backup")
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

		triggers_bak = self.triggers_path.with_suffix(self.triggers_path.suffix + ".bak")
		maps_bak = self.maps_path.with_suffix(self.maps_path.suffix + ".bak")
		self.assertTrue(triggers_bak.is_file(), "expected triggers.data.bak to be created")
		self.assertTrue(maps_bak.is_file(), "expected maps.data.bak to be created")

		# The backup must hold the PRE-apply bytes exactly -- not a copy taken after the
		# write, which would be indistinguishable from the live file and worthless for
		# recovering from a bad apply.
		self.assertEqual(triggers_bak.read_bytes(), before_triggers)
		self.assertEqual(maps_bak.read_bytes(), before_maps)

		# And the live files must have actually moved past the pre-apply state, so this
		# test cannot pass merely because apply did nothing at all.
		self.assertNotEqual(self.triggers_path.read_bytes(), before_triggers)
		self.assertNotEqual(self.maps_path.read_bytes(), before_maps)

		post_triggers = MODULES["triggers"].Triggers()
		post_triggers.ParseFromString(self.triggers_path.read_bytes())
		self.assertEqual(sorted(entry.id for entry in post_triggers.entry), [1, 501])

		backed_up_triggers = MODULES["triggers"].Triggers()
		backed_up_triggers.ParseFromString(triggers_bak.read_bytes())
		self.assertEqual([entry.id for entry in backed_up_triggers.entry], [1])

	def test_rejects_non_encounter_format(self):
		before_triggers = self.triggers_path.read_bytes()
		before_maps = self.maps_path.read_bytes()
		doc_path = self._write_doc(
			{
				"format": "mmo-npc",
				"triggers": [{"id": 501, "name": "Should Not Apply"}],
			}
		)

		result = self._run(doc_path, "--backup")

		self.assertNotEqual(result.returncode, 0)
		self.assertIn("mmo-encounter", result.stdout + result.stderr)
		self.assertEqual(self.triggers_path.read_bytes(), before_triggers)
		self.assertEqual(self.maps_path.read_bytes(), before_maps)
		self.assertEqual(self._bak_files(), [])

	def test_rejects_document_with_nothing_to_apply(self):
		before_triggers = self.triggers_path.read_bytes()
		before_maps = self.maps_path.read_bytes()
		doc_path = self._write_doc({"format": "mmo-encounter"})

		result = self._run(doc_path, "--backup")

		self.assertNotEqual(result.returncode, 0)
		self.assertEqual(self.triggers_path.read_bytes(), before_triggers)
		self.assertEqual(self.maps_path.read_bytes(), before_maps)
		self.assertEqual(self._bak_files(), [])


if __name__ == "__main__":
	unittest.main(verbosity=2)
