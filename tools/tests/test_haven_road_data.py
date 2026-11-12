#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Guards the Haven Road quests (78-94), the level 6-10 content west of the river
(docs/haven-road-quests.md).

The quests lean on wiring that lives outside quests.data: gossip conditions, triggers with a
quest condition, inactive spawners a trigger switches on, quest-gated objects and their spawns.
Each of those can be broken by an unrelated edit without any quest row changing, so they are
checked here against the editor data:

* every quest has a giver (or a starting item) and an ender, both standing on map 0;
* the chains are what the design says, and nothing is AutoRewarded;
* every objective has a source: a spawn, a trigger that grants the credit, or a gated object;
* the scripted pieces are wired: who talks, which trigger answers, which spawner it switches on;
* quest items are never grey, and every text has its German translation in the Ihr form.

Skips rather than fails when protoc or the data submodules are unavailable.

	python tools/tests/test_haven_road_data.py
"""

import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402

QUESTS = range(78, 95)
LOCALE_DE = 1

# trigger_helper.h
ACTION_SET_SPAWN_STATE, ACTION_QUEST_KILL_CREDIT, ACTION_SET_FACTION, ACTION_DELIVER_FOLLOWERS = 4, 16, 40, 41
EVENT_ON_INTERACTION, EVENT_ON_SPELL_HIT, EVENT_ON_GOSSIP_ACTION = 10, 13, 17
GOSSIP_ACTION_MENU, GOSSIP_ACTION_TRIGGER = 3, 4
QUEST_STATUS_INCOMPLETE = 3
CONDITION_QUEST_CHECK = 3

# unit entries
BRASK, THUG, WAYLAYER, SOW, HARROW, TALLOW, TALLOW_FOE = 129, 130, 131, 134, 136, 144, 145
TRADERS = {99: 148, 100: 149, 102: 150}     # trader -> the credit entry for their account


def german(loc):
	return next((entry.value for entry in loc if entry.locale == LOCALE_DE), "")


class HavenRoadDataTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		try:
			from worldkit.data import load_game_data
			cls.data = load_game_data()
		except (FileNotFoundError, OSError, SystemExit) as exc:
			raise unittest.SkipTest(f"data or protoc unavailable: {exc}")
		if 78 not in cls.data.quests:
			raise unittest.SkipTest("the Haven Road quests are not in this data checkout")
		# catalogs worldkit does not load
		import importlib
		from worldkit.paths import data_dir

		def catalog(module, container, file_name):
			message = getattr(importlib.import_module(module), container)()
			message.ParseFromString((data_dir() / file_name).read_bytes())
			return {entry.id: entry for entry in message.entry}

		cls.gossip_menus = catalog("gossip_menus_pb2", "GossipMenus", "gossip_menus.data")
		cls.conditions = catalog("conditions_pb2", "Conditions", "conditions.data")
		cls.area_triggers = catalog("area_triggers_pb2", "AreaTriggers", "area_triggers.data")
		cls.faction_templates = catalog("faction_templates_pb2", "FactionTemplates", "faction_templates.data")
		cls.map0 = cls.data.maps[0]
		cls.spawned_units = {spawn.unitentry for spawn in cls.map0.unitspawns}
		cls.spawn_names = {spawn.name: spawn for spawn in cls.map0.unitspawns}
		cls.spawned_objects = {spawn.objectentry for spawn in cls.map0.objectspawns}

	# ---- helpers ----------------------------------------------------------------------------

	def givers(self, quest_id):
		return [uid for uid, unit in self.data.units.items() if quest_id in unit.quests]

	def enders(self, quest_id):
		return [uid for uid, unit in self.data.units.items() if quest_id in unit.end_quests]

	def unit_triggers(self, unit_id, event):
		return [self.data.triggers[tid] for tid in self.data.units[unit_id].triggers
				if any(e.type == event for e in self.data.triggers[tid].newevents)]

	def actions_reachable_from(self, trigger, seen=None):
		"""All actions a trigger runs, following Trigger (0) and RandomTrigger (39) actions."""
		seen = seen if seen is not None else set()
		if trigger.id in seen:
			return []
		seen.add(trigger.id)
		actions = []
		for action in trigger.actions:
			actions.append(action)
			if action.action in (0, 39):
				for nested in action.data:
					actions += self.actions_reachable_from(self.data.triggers[nested], seen)
		return actions

	# ---- quests -----------------------------------------------------------------------------

	def test_every_quest_can_be_taken_and_handed_in_on_map_0(self):
		starting_items = {item.questentry for item in self.data.items.values() if item.questentry}
		for quest_id in QUESTS:
			with self.subTest(quest=quest_id):
				givers = self.givers(quest_id)
				self.assertTrue(givers or quest_id in starting_items, "no giver and no starting item")
				enders = self.enders(quest_id)
				self.assertEqual(len(enders), 1, "exactly one npc ends the quest")
				for unit_id in givers + enders:
					self.assertIn(unit_id, self.spawned_units, f"unit {unit_id} is not spawned on map 0")

	def test_no_quest_is_auto_rewarded(self):
		for quest_id in QUESTS:
			self.assertFalse(self.data.quests[quest_id].flags & 0x20, f"quest {quest_id} is AutoRewarded")

	def test_chains(self):
		expected = {79: 78, 81: 78, 85: 84, 90: 87, 91: 90, 92: 91, 94: 53}
		for quest_id in QUESTS:
			with self.subTest(quest=quest_id):
				self.assertEqual(self.data.quests[quest_id].prevquestid, expected.get(quest_id, 0))

	def test_levels_cover_six_to_ten(self):
		levels = [self.data.quests[quest_id].questlevel for quest_id in QUESTS]
		self.assertEqual(set(levels), {6, 7, 8, 9, 10})
		for quest_id in QUESTS:
			quest = self.data.quests[quest_id]
			self.assertLessEqual(quest.minlevel, quest.questlevel)
			self.assertGreaterEqual(quest.minlevel, 5)

	def test_at_most_four_objectives(self):
		for quest_id in QUESTS:
			self.assertLessEqual(len(self.data.quests[quest_id].requirements), 4)

	def test_item_started_quest(self):
		token = next(item for item in self.data.items.values() if item.questentry == 84)
		self.assertEqual(self.givers(84), [])
		# the token is the quest's requirement, so it is taken at the turn-in
		self.assertEqual([r.itemid for r in self.data.quests[84].requirements], [token.id])
		# and it comes from Harrow, whom quest 83 has the player kill
		harrow_loot = set(self.data.units[HARROW].unitlootentries)
		drops = {definition.item for loot_id in harrow_loot for group in self.data.unit_loot[loot_id].groups
				 for definition in group.definitions}
		self.assertIn(token.id, drops)

	# ---- objectives -------------------------------------------------------------------------

	def test_kill_objectives_have_a_source(self):
		credited = {action.data[0] for trigger in self.data.triggers.values() for action in trigger.actions
					if action.action in (ACTION_QUEST_KILL_CREDIT, ACTION_DELIVER_FOLLOWERS) and len(action.data)}
		for quest_id in QUESTS:
			for requirement in self.data.quests[quest_id].requirements:
				if requirement.creatureid:
					with self.subTest(quest=quest_id, creature=requirement.creatureid):
						self.assertTrue(requirement.creatureid in self.spawned_units or requirement.creatureid in credited)

	def test_object_objectives_are_gated_and_spawned(self):
		for quest_id in QUESTS:
			quest = self.data.quests[quest_id]
			for requirement in quest.requirements:
				if requirement.objectid:
					with self.subTest(quest=quest_id, object=requirement.objectid):
						entry = self.data.objects[requirement.objectid]
						self.assertEqual(entry.requiredquest, quest_id)
						spawns = sum(1 for spawn in self.map0.objectspawns if spawn.objectentry == requirement.objectid)
						self.assertGreaterEqual(spawns, 1)

	def test_lanterns_and_lures_outnumber_what_is_asked(self):
		for quest_id in (80, 85):
			requirement = self.data.quests[quest_id].requirements[0]
			spawns = sum(1 for spawn in self.map0.objectspawns if spawn.objectentry == requirement.objectid)
			self.assertGreaterEqual(spawns, requirement.objectcount)

	def test_collect_objectives_come_from_gated_chests(self):
		for quest_id, item_name in ((87, "Smuggler's Manifest"), (88, "King's Sealed Goods")):
			item = next(i for i in self.data.items.values() if i.name == item_name)
			chests = [obj for obj in self.data.objects.values() if obj.requiredquest == quest_id and obj.objectlootentry]
			self.assertTrue(chests, f"no gated chest for quest {quest_id}")
			for chest in chests:
				drops = {d.item for group in self.data.unit_loot[chest.objectlootentry].groups for d in group.definitions}
				self.assertIn(item.id, drops)
				self.assertIn(chest.id, self.spawned_objects)
		needed = next(r.itemcount for r in self.data.quests[88].requirements if r.itemid)
		crates = sum(1 for spawn in self.map0.objectspawns if self.data.objects[spawn.objectentry].requiredquest == 88)
		self.assertGreaterEqual(crates, needed)

	# ---- scripted pieces --------------------------------------------------------------------

	def test_tollmaster_turns_hostile_only_for_players_on_the_quest(self):
		brask = self.data.units[BRASK]
		menu = self.gossip_menus[brask.gossip_menus[0]]
		self.assertEqual(len(menu.options), 2)
		for option in menu.options:
			self.assertEqual(option.action_type, GOSSIP_ACTION_TRIGGER)
			condition = self.conditions[option.conditionId]
			self.assertEqual((condition.conditionType, condition.param1, condition.param2), (CONDITION_QUEST_CHECK, 79, QUEST_STATUS_INCOMPLETE))

		answers = self.unit_triggers(BRASK, EVENT_ON_GOSSIP_ACTION)
		self.assertEqual(len(answers), 2)
		for trigger in answers:
			actions = self.actions_reachable_from(trigger)
			kinds = [action.action for action in actions]
			self.assertIn(ACTION_SET_FACTION, kinds)
			# the faction changes before the thugs spawn, or they attack their own master
			self.assertLess(kinds.index(ACTION_SET_FACTION), kinds.index(ACTION_SET_SPAWN_STATE))
			faction = next(action for action in actions if action.action == ACTION_SET_FACTION)
			self.assertEqual(list(faction.data), [self.data.units[THUG].factionTemplate, 1])

		# the bluff can end without a fight: a credit for Brask himself
		bluff = next(t for t in answers if any(a.action == 39 for a in t.actions))
		self.assertIn((ACTION_QUEST_KILL_CREDIT, BRASK),
					  {(a.action, a.data[0]) for a in self.actions_reachable_from(bluff) if len(a.data)})

	def test_event_spawners_are_inactive_and_switched_on_by_a_trigger(self):
		switched_on = {action.targetname for trigger in self.data.triggers.values() for action in trigger.actions
					   if action.action == ACTION_SET_SPAWN_STATE and len(action.data) and action.data[0] != 0}
		for unit_id in (THUG, WAYLAYER, HARROW):
			spawns = [spawn for spawn in self.map0.unitspawns if spawn.unitentry == unit_id]
			self.assertTrue(spawns)
			for spawn in spawns:
				with self.subTest(spawn=spawn.name):
					self.assertFalse(spawn.isactive)
					self.assertFalse(spawn.respawn)
					self.assertIn(spawn.name, switched_on)
					# they leave again when the fight ends without them dying
					self.assertIn(17, self.data.units[unit_id].triggers)

	def test_named_trigger_targets_exist(self):
		object_names = {spawn.name for spawn in self.map0.objectspawns}
		for trigger in self.data.triggers.values():
			if trigger.id < 118:
				continue
			for action in trigger.actions:
				if action.target == 5:
					self.assertIn(action.targetname, self.spawn_names, f"trigger {trigger.id}")
				elif action.target == 4:
					self.assertIn(action.targetname, object_names, f"trigger {trigger.id}")

	def test_area_triggers_only_act_for_players_on_the_quest(self):
		areas = {area.name: area for area in self.area_triggers.values()}
		for name, quest_id in (("Haven Road - Dispatch Ambush", 81), ("Thornhand Landing - Overlook", 87)):
			trigger = self.data.triggers[areas[name].on_enter_trigger]
			self.assertTrue(trigger.HasField("condition"), name)
			self.assertIn(f"leftfunctiondata: {quest_id}", str(trigger.condition))

	def test_ambush_also_springs_for_a_quest_that_is_complete_on_accept(self):
		# Quest 81 has no objective, so it is "complete" (1) from the moment it is accepted and
		# never "incomplete" (3). A condition on 3 alone would never spring the ambush.
		self.assertEqual(len(self.data.quests[81].requirements), 0)
		ambush = next(t for t in self.data.triggers.values() if t.name == "Haven Road - Dispatch Ambush")
		statuses = sorted(side.rightlong for side in (ambush.condition.leftcondition, ambush.condition.rightcondition))
		self.assertEqual(statuses, [1, 3])

	def test_sows_are_nobodys_enemy(self):
		# They share their fields with boars and, at night, dusklings, which attack townsfolk.
		sow_faction = self.faction_templates[self.data.units[SOW].factionTemplate].faction
		for template in self.faction_templates.values():
			self.assertNotIn(sow_faction, template.enemies, template.name)
		# and players can use a friendly-target spell on them
		self.assertIn(sow_faction, self.faction_templates[0].friends)

	def test_sows_follow_the_turnip_and_are_handed_in(self):
		turnip_spell = next(spell.spell for item in self.data.items.values() if item.name == "Wainbrook Turnips" for spell in item.spells)
		follow = self.unit_triggers(SOW, EVENT_ON_SPELL_HIT)
		self.assertEqual(len(follow), 1)
		self.assertEqual(list(follow[0].newevents[0].data), [turnip_spell])
		self.assertIn(35, [action.action for action in follow[0].actions])     # SetFollowTarget
		deliver = [action for trigger in self.data.triggers.values() for action in trigger.actions if action.action == ACTION_DELIVER_FOLLOWERS]
		self.assertEqual([action.data[0] for action in deliver], [SOW])
		needed = self.data.quests[82].requirements[0].creaturecount
		self.assertGreaterEqual(sum(1 for spawn in self.map0.unitspawns if spawn.unitentry == SOW), needed)
		self.assertEqual(self.data.quests[82].srcitemid, next(i.id for i in self.data.items.values() if i.name == "Wainbrook Turnips"))

	def test_traders_give_their_account_on_a_sub_page(self):
		for trader, credit in TRADERS.items():
			with self.subTest(trader=self.data.units[trader].name):
				unit = self.data.units[trader]
				main = self.gossip_menus[unit.gossip_menus[0]]
				# the whole menu is conditional, so without the quest the vendor window opens directly
				condition = self.conditions[main.conditionId]
				self.assertEqual((condition.conditionType, condition.param1, condition.param2), (CONDITION_QUEST_CHECK, 90, QUEST_STATUS_INCOMPLETE))
				self.assertIn(1, [option.action_type for option in main.options], "the vendor option is gone")
				page_id = next(option.action_param for option in main.options if option.action_type == GOSSIP_ACTION_MENU)
				page = self.gossip_menus[page_id]
				self.assertEqual([option.action_type for option in page.options], [GOSSIP_ACTION_TRIGGER])
				answer = next(t for t in self.unit_triggers(trader, EVENT_ON_GOSSIP_ACTION) if list(t.newevents[0].data) == [page_id, page.options[0].id])
				self.assertEqual([(a.action, a.data[0]) for a in answer.actions], [(ACTION_QUEST_KILL_CREDIT, credit)])
				self.assertNotIn(credit, self.spawned_units, "a credit entry is never spawned")

	def test_two_tallows(self):
		# the chandler at his stall can be talked to; the one at the basin is the fight
		self.assertNotEqual(self.data.units[TALLOW].factionTemplate, self.data.units[TALLOW_FOE].factionTemplate)
		self.assertEqual(self.data.units[TALLOW].name, self.data.units[TALLOW_FOE].name)
		ledger = next(i for i in self.data.items.values() if i.name == "The Thorn Ledger")
		drops = [(d.item, d.condition) for loot_id in self.data.units[TALLOW_FOE].unitlootentries
				 for group in self.data.unit_loot[loot_id].groups for d in group.definitions if d.item == ledger.id]
		self.assertEqual(len(drops), 1)
		condition = self.conditions[drops[0][1]]
		self.assertEqual((condition.param1, condition.param2), (92, QUEST_STATUS_INCOMPLETE))

	def test_night_dusklings_are_part_of_the_map_routine(self):
		routine = next(t for t in self.data.triggers.values() if t.name == "Farmlands - Duskling Night Routine")
		self.assertIn(routine.id, self.map0.instance_triggers)
		names = {a.targetname for nested in routine.actions for a in self.data.triggers[nested.data[0]].actions}
		self.assertEqual(len(names), 6)
		for name in names:
			self.assertFalse(self.spawn_names[name].isactive, "they must not stand in the fields at server start by day")

	# ---- items and texts --------------------------------------------------------------------

	def test_quest_items_are_never_grey(self):
		for quest_id in QUESTS:
			quest = self.data.quests[quest_id]
			item_ids = [r.itemid for r in quest.requirements if r.itemid]
			item_ids += [r.itemid for r in quest.rewarditems] + [r.itemid for r in quest.rewarditemschoice]
			if quest.srcitemid:
				item_ids.append(quest.srcitemid)
			for item_id in item_ids:
				self.assertGreaterEqual(self.data.items[item_id].quality, 1, f"quest {quest_id} item {item_id}")

	def test_every_text_has_a_german_translation_in_the_ihr_form(self):
		# "Sie" cannot be checked this way: it is also plain "they" at the start of a sentence.
		informal = re.compile(r"\b(du|dich|dir|dein\w*)\b", re.IGNORECASE)
		texts = []
		for quest_id in QUESTS:
			quest = self.data.quests[quest_id]
			texts += [(f"quest {quest_id} name", quest.name, quest.name_loc),
					  (f"quest {quest_id} details", quest.detailstext, quest.detailstext_loc),
					  (f"quest {quest_id} objectives", quest.objectivestext, quest.objectivestext_loc),
					  (f"quest {quest_id} reward", quest.offerrewardtext, quest.offerrewardtext_loc)]
			if quest.requestitemstext:
				texts.append((f"quest {quest_id} request", quest.requestitemstext, quest.requestitemstext_loc))
		for trigger in self.data.triggers.values():
			if trigger.id >= 118:
				texts += [(f"trigger {trigger.id}", action.texts[0], action.texts_loc) for action in trigger.actions if len(action.texts)]
		for menu in self.gossip_menus.values():
			if menu.id >= 23:
				texts.append((f"gossip {menu.id}", menu.text, menu.text_loc))
				texts += [(f"gossip {menu.id} option {option.id}", option.text, option.text_loc) for option in menu.options]
		self.assertGreater(len(texts), 100)
		for label, base, loc in texts:
			with self.subTest(text=label):
				self.assertTrue(base.strip())
				translated = german(loc)
				self.assertTrue(translated.strip(), "no German text")
				self.assertIsNone(informal.search(translated), f"not in the Ihr form: {translated[:80]}")


if __name__ == "__main__":
	unittest.main()
