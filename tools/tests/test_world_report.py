#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.report on hand-built protobuf game data.

	python tools/tests/test_world_report.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402

from worldkit.data import GameData, load_proto_modules  # noqa: E402
from worldkit.report import check_reachability, quest_links  # noqa: E402


def build(target_pos=(100.0, 0.0), target_active=True, flags=0, with_ender=True, requirement="creature"):
	m = load_proto_modules()
	giver = m["units"].UnitEntry(id=1, name="Farmer Haldor", minlevel=5, maxlevel=5)
	giver.quests.append(10)
	if with_ender:
		giver.end_quests.append(10)
	boar = m["units"].UnitEntry(id=2, name="Young Forest Boar", minlevel=2, maxlevel=3, unitlootentry=50)
	loot = m["unit_loot"].UnitLoot().entry.add(id=50, name="Boar")
	loot.groups.add().definitions.add(item=900, dropchance=50.0)
	quest = m["quests"].QuestEntry(id=10, name="The Boar Problem", flags=flags)
	if requirement == "creature":
		quest.requirements.add(creatureid=2, creaturecount=10)
	else:
		quest.requirements.add(itemid=900, itemcount=5)
	map_entry = m["maps"].MapEntry(id=0, name="Dev", directory="Development")
	map_entry.unitspawns.add(unitentry=1, positionx=0, positiony=0, positionz=0)
	map_entry.unitspawns.add(unitentry=2, positionx=target_pos[0], positiony=0, positionz=target_pos[1], isactive=target_active)
	item = m["items"].ItemEntry(id=900, name="Boar Tusk")
	return GameData(units={1: giver, 2: boar}, maps={0: map_entry}, quests={10: quest}, zones={}, objects={},
					items={900: item}, unit_loot={50: loot}, object_loot={}, modules=m), map_entry


class ReachabilityTests(unittest.TestCase):
	def rules(self, **kwargs):
		data, map_entry = build(**kwargs)
		return {(f.rule, f.severity) for f in check_reachability(data, map_entry)}

	def test_reachable(self):
		self.assertEqual(self.rules(), set())

	def test_inactive_target(self):
		self.assertEqual(self.rules(target_active=False), {("no_active_source", "error")})

	def test_far_target(self):
		self.assertEqual(self.rules(target_pos=(1000.0, 0.0)), {("source_far", "warning")})

	def test_item_from_loot(self):
		self.assertEqual(self.rules(requirement="item"), set())

	def test_turn_in_rules(self):
		self.assertIn(("auto_rewarded", "error"), self.rules(flags=0x20))
		self.assertIn(("no_turn_in", "error"), self.rules(with_ender=False))

	def test_trigger_activated_and_summoned_sources(self):
		data, map_entry = build(target_active=False)
		map_entry.unitspawns[1].name = "Marsh Ambusher 01"
		trigger = data.modules["triggers"].TriggerEntry(id=1, name="Spawn ambush")
		trigger.actions.add(action=4, target=5, targetname="Marsh Ambusher 01", data=[2])   # any non-zero activates
		data.triggers = {1: trigger}
		self.assertEqual({f.rule for f in check_reachability(data, map_entry)}, set())
		data, map_entry = build(target_active=False)
		summon = data.modules["triggers"].TriggerEntry(id=2, name="Boss adds")
		summon.actions.add(action=25, data=[2])
		data.triggers = {2: summon}
		self.assertEqual({f.rule for f in check_reachability(data, map_entry)}, set())

	def test_loot_uses_list_or_legacy_like_the_server(self):
		data, map_entry = build(requirement="item")
		data.units[2].unitlootentries.append(51)   # the list wins; legacy 50 (with the item) is ignored
		self.assertEqual({f.rule for f in check_reachability(data, map_entry)}, {"no_active_source"})

	def test_kill_credit_proxy(self):
		data, map_entry = build()
		proxy = data.modules["units"].UnitEntry(id=3, name="Boar Proxy", minlevel=2, maxlevel=2, killcredit=2)
		data.units[3] = proxy
		map_entry.unitspawns[1].unitentry = 3   # only the proxy is spawned; it credits unit 2
		self.assertEqual({f.rule for f in check_reachability(data, map_entry)}, set())

	def test_quest_links(self):
		data, map_entry = build()
		self.assertEqual(quest_links(data, map_entry), [(10, (0.0, 0.0), (100.0, 0.0))])


if __name__ == "__main__":
	unittest.main()
