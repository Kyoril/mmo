#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for .claude/skills/mmo-npc-designer/scripts/apply_encounter_json.py.

This applier is the only way trigger rows and map encounter slots reach the binary
project data. Its failure modes are quiet: appending a duplicate id instead of updating
one, or dropping an instance trigger, both produce a file that still loads and a boss
that simply never fires. Those cases are pinned down here.

	python tools/tests/test_apply_encounter_json.py
"""

import importlib.util
import os
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPTS_DIR = os.path.join(REPO_ROOT, ".claude", "skills", "mmo-npc-designer", "scripts")
MODULE_PATH = os.path.join(SCRIPTS_DIR, "apply_encounter_json.py")

sys.path.insert(0, SCRIPTS_DIR)
_spec = importlib.util.spec_from_file_location("apply_encounter_json", MODULE_PATH)
aej = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(aej)

from proto_runtime import load_modules  # noqa: E402

# load_modules (via proto_runtime.find_proto_dir/find_protoc) joins project_root with "/",
# which requires a Path -- REPO_ROOT is a plain str from os.path, so it must be wrapped here.
MODULES = load_modules(Path(REPO_ROOT))


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


if __name__ == "__main__":
	unittest.main(verbosity=2)
