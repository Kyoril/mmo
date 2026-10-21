#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the mixed-spawn (weighted unitentries) checks of the mmo-npc-designer skill.

The world server skips weight 0 entries and falls back to unitentry for unknown ids, so a typo
in a camp's spawn list silently changes what spawns. The validator turns both into errors.

	python tools/tests/test_npc_mixed_spawn.py
"""

import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / ".agents" / "skills" / "mmo-npc-designer" / "scripts"
sys.path.insert(0, str(SCRIPTS))

from validate_npc_json import validate_mixed_spawn  # noqa: E402

BANDIT = 120
MAGE = 121
INDEXES = {"units": {MAGE: object()}}


def errors_for(spawn: dict) -> list[str]:
	errors: list[str] = []
	validate_mixed_spawn(errors, "spawns[0].spawn", spawn, BANDIT, INDEXES)
	return errors


class MixedSpawnValidationTests(unittest.TestCase):
	def test_plain_spawn_has_nothing_to_check(self):
		self.assertEqual(errors_for({"unitentry": BANDIT}), [])

	def test_valid_mixed_spawn(self):
		spawn = {"unitentry": BANDIT, "unitentries": [{"unitentry": BANDIT, "weight": 3}, {"unitentry": MAGE}]}
		self.assertEqual(errors_for(spawn), [])

	def test_unknown_entry(self):
		spawn = {"unitentries": [{"unitentry": BANDIT}, {"unitentry": 999999, "weight": 1}]}
		self.assertEqual(len(errors_for(spawn)), 1)
		self.assertIn("999999 is not present", errors_for(spawn)[0])

	def test_zero_and_negative_weights(self):
		spawn = {"unitentries": [{"unitentry": BANDIT, "weight": 0}, {"unitentry": MAGE, "weight": -2}]}
		errors = errors_for(spawn)
		self.assertEqual(len(errors), 2)
		self.assertTrue(all("weight must be an integer > 0" in e for e in errors))

	def test_non_integer_weight(self):
		self.assertEqual(len(errors_for({"unitentries": [{"unitentry": BANDIT, "weight": 1.5}]})), 1)

	def test_first_entry_must_be_the_document_unit(self):
		errors = errors_for({"unitentries": [{"unitentry": MAGE}, {"unitentry": BANDIT}]})
		self.assertEqual(len(errors), 1)
		self.assertIn("unitentries[0].unitentry must be the document's unit", errors[0])

	def test_list_shape(self):
		self.assertEqual(len(errors_for({"unitentries": {"unitentry": BANDIT}})), 1)
		self.assertEqual(len(errors_for({"unitentries": [BANDIT]})), 1)   # entries must be objects


if __name__ == "__main__":
	unittest.main()
