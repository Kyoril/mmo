# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards quest 66 "Report to the Gate", the breadcrumb from Guard Emrik to Guard-Corporal
Rowan Hale at Oakenshire's west gate.

* The authoring script finds nothing left to change: the editor data is what it writes.
* It is only offered after BOTH the final boar quest (prevquestid 6) and the cellar rat quest
  (nextquestid back-link 10 -> 66), from level 4, to Humans of any class.
* Minimum "go to x" reward: 40-45 XP and the same amount of class XP, nothing else.
* Only Emrik offers it, only Rowan Hale ends it, and every text carries all four locales.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/oakenshire_quests"))


class ReportToTheGateDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import author_report_to_the_gate as art
            cls.art = art
            cls.quests = art.load_editor("quests")
            cls.units = art.load_editor("units")
            cls.planned_quests, cls.planned_units = art.plan()
        except (FileNotFoundError, OSError, SystemExit) as exc:
            raise unittest.SkipTest(f"data or protoc unavailable: {exc}")
        cls.quest = art.by_id(cls.quests, art.QUEST_ID)

    def test_data_matches_the_design(self):
        self.assertEqual(self.planned_quests.SerializeToString(), self.quests.SerializeToString())
        self.assertEqual(self.planned_units.SerializeToString(), self.units.SerializeToString())

    def test_requires_both_the_boar_and_the_rat_quests(self):
        self.assertIsNotNone(self.quest)
        self.assertEqual(self.quest.prevquestid, self.art.BOAR_FINAL_QUEST)
        # Back-links are OR-ed: a second quest naming 66 as nextquestid would drop the rat requirement.
        back_links = [entry.id for entry in self.quests.entry if entry.nextquestid == self.art.QUEST_ID]
        self.assertEqual(back_links, [self.art.RAT_QUEST])

    def test_level_and_availability(self):
        self.assertEqual(self.quest.minlevel, 4)
        self.assertEqual(self.quest.questlevel, 4)
        self.assertEqual(self.quest.requiredraces, self.art.HUMAN_RACEMASK)
        self.assertEqual(self.quest.requiredclasses, 0)
        self.assertFalse(self.quest.HasField("unlocksclass"))
        self.assertEqual(len(self.quest.requirements), 0)

    def test_minimum_reward(self):
        self.assertTrue(40 <= self.quest.rewardxp <= 45)
        self.assertEqual(self.quest.rewardclassxp, self.quest.rewardxp)
        self.assertEqual(self.quest.rewardmoney, 0)
        self.assertEqual(len(self.quest.rewarditems), 0)
        self.assertEqual(len(self.quest.rewarditemschoice), 0)

    def test_offered_by_emrik_and_ended_by_rowan_hale_only(self):
        givers = [unit.id for unit in self.units.entry if self.art.QUEST_ID in unit.quests]
        enders = [unit.id for unit in self.units.entry if self.art.QUEST_ID in unit.end_quests]
        self.assertEqual(givers, [self.art.GUARD_EMRIK])
        self.assertEqual(enders, [self.art.ROWAN_HALE])

    def test_every_text_has_all_four_locales(self):
        for field in ("name_loc", "detailstext_loc", "objectivestext_loc", "offerrewardtext_loc"):
            locales = sorted(entry.locale for entry in getattr(self.quest, field))
            self.assertEqual(locales, [1, 2, 3, 4], field)
            english = next(entry.value for entry in getattr(self.quest, field) if entry.locale == 2)
            self.assertEqual(english, getattr(self.quest, field[:-4]), field)


if __name__ == "__main__":
    unittest.main()
