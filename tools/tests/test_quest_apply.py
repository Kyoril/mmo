#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the quest skill's apply step (provider/ender link reconciliation).

	python tools/tests/test_quest_apply.py
"""

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / ".agents" / "skills" / "mmo-quest-creator" / "scripts"))
from apply_quest_json import reconcile_quest_links  # noqa: E402


class ReconcileQuestLinksTests(unittest.TestCase):
	def test_relinking_keeps_the_npcs_quest_order(self):
		# Re-applying a quest that is already linked must not move it to the end of the NPC's
		# list: the list order is the order the quest dialog offers the quests in.
		npc = SimpleNamespace(id=64, quests=[49, 57, 60])
		reconcile_quest_links([npc], 57, "quests", {64})
		self.assertEqual(npc.quests, [49, 57, 60])

	def test_links_and_unlinks(self):
		giver = SimpleNamespace(id=1, quests=[5])
		other = SimpleNamespace(id=2, quests=[7, 9])
		reconcile_quest_links([giver, other], 7, "quests", {1})
		self.assertEqual(giver.quests, [5, 7])
		self.assertEqual(other.quests, [9])


if __name__ == "__main__":
	unittest.main()
