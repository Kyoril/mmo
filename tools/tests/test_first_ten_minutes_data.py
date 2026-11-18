# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the "first ten minutes" data (2026-11-18):

* Forest Boars (quest 5) stand far enough apart to be pulled one at a time and respawn slowly enough
  that a fight is not joined by a fresh boar.
* Quest 8's Injured Guards lie north-east of Haven, where the quest sends the player.
* The opening quests carry an objective area for the minimap and world map, and every area sits on its
  objective.
* The standalone starter quests point to the next quest giver, and the first quest givers greet the
  player with their own lines.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-npc-designer/scripts"))

EDITOR = ROOT / "data/editor/data"

FOREST_BOAR = 19
INJURED_GUARD = 22
HAVEN_CENTRE = (-700.0, 980.0)
# Aggro of a level 4 boar against a level 3 player is 21 m; a body pull at the edge of one boar's range
# must not reach the next.
FOREST_BOAR_SPACING = 24.0
FOREST_BOAR_RESPAWN_MS = 60000
OPENING_QUESTS_WITH_AREAS = (1, 2, 5, 6, 9, 10, 24, 26)
GENERIC_GREETING = "Greetings, $N!"
DE = 1


class FirstTenMinutesDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            from proto_runtime import load_modules
            modules = load_modules(ROOT)
        except (FileNotFoundError, OSError, SystemExit) as exc:
            raise unittest.SkipTest(f"protoc unavailable: {exc}")
        if not (EDITOR / "maps.data").is_file():
            raise unittest.SkipTest("data/editor is not checked out")

        def load(name, cls_name, key):
            message = getattr(modules[key], cls_name)()
            message.ParseFromString((EDITOR / f"{name}.data").read_bytes())
            return message

        cls.units = {e.id: e for e in load("units", "Units", "units").entry}
        cls.quests = {e.id: e for e in load("quests", "Quests", "quests").entry}
        cls.gossip = {e.id: e for e in load("gossip_menus", "GossipMenus", "gossip_menus").entry}
        cls.map0 = next(m for m in load("maps", "Maps", "maps").entry if m.id == 0)

    def spawns_of(self, unit_id):
        return [s for s in self.map0.unitspawns
                if s.unitentry == unit_id or unit_id in {e.unitentry for e in s.unitentries}]

    def test_forest_boars_pull_one_at_a_time(self):
        boars = self.spawns_of(FOREST_BOAR)
        self.assertGreaterEqual(len(boars), 10, "quest 5 needs 10 kills")
        for i, a in enumerate(boars):
            self.assertGreaterEqual(a.respawndelay, FOREST_BOAR_RESPAWN_MS)
            for b in boars[i + 1:]:
                distance = math.hypot(a.positionx - b.positionx, a.positionz - b.positionz)
                self.assertGreaterEqual(distance, FOREST_BOAR_SPACING,
                                        f"boars at ({a.positionx:.0f}, {a.positionz:.0f}) and "
                                        f"({b.positionx:.0f}, {b.positionz:.0f}) are {distance:.1f} m apart")

    def test_injured_guards_lie_north_east_of_haven(self):
        guards = self.spawns_of(INJURED_GUARD)
        self.assertTrue(guards)
        for guard in guards:
            dx = guard.positionx - HAVEN_CENTRE[0]
            dz = guard.positionz - HAVEN_CENTRE[1]
            self.assertLess(math.hypot(dx, dz), 200.0, "quest 8 sends the player to Haven")
            # North is -Z, east is +X (docs/world/bible.md).
            self.assertGreater(dx, 0.0, "east of Haven")
            self.assertLess(dz, 0.0, "north of Haven")

    def test_opening_quests_have_an_objective_area_on_their_objective(self):
        for quest_id in OPENING_QUESTS_WITH_AREAS:
            quest = self.quests[quest_id]
            self.assertTrue(quest.pointx or quest.pointy, f"quest {quest_id} {quest.name} has no objective area")
            self.assertEqual(quest.pointmapid, 0)
            radius = quest.pointopt or 30
            self.assertLessEqual(radius, 90, f"quest {quest_id}: an area this large points nowhere")
            creatures = [r.creatureid for r in quest.requirements if r.creatureid]
            for creature in creatures:
                nearest = min(math.hypot(s.positionx - quest.pointx, s.positionz - quest.pointy)
                              for s in self.spawns_of(creature))
                self.assertLessEqual(nearest, radius, f"quest {quest_id}: no {creature} inside its area")

    def test_standalone_starter_quests_point_onwards(self):
        onwards = {2: ("Harbin", "Harbin"), 10: ("Lina", "Lina"), 9: ("Rowan Hale", "Rowan Hale")}
        for quest_id, (english, german) in onwards.items():
            quest = self.quests[quest_id]
            self.assertIn(english, quest.offerrewardtext, f"quest {quest_id}")
            german_text = next(l.value for l in quest.offerrewardtext_loc if l.locale == DE)
            self.assertIn(german, german_text, f"quest {quest_id} (deDE)")
            self.assertEqual(len({l.locale for l in quest.offerrewardtext_loc}), 4, f"quest {quest_id} locales")

    def test_first_quest_givers_have_their_own_greeting(self):
        for unit_id in (15, 17):
            unit = self.units[unit_id]
            self.assertNotEqual(unit.greeting_text, GENERIC_GREETING, unit.name)
            self.assertEqual(len(unit.greeting_text_loc), 4, unit.name)
        mirenna = self.units[16]
        self.assertEqual(list(mirenna.gossip_menus)[0], 5)
        self.assertNotEqual(self.gossip[5].text, GENERIC_GREETING, "Healer Mirenna's gossip menu")


if __name__ == "__main__":
    unittest.main()
