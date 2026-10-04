# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the constellation talent trees authored by tools/talent_trees/author_talent_trees.py.

* The live data is exactly what the authoring script produces (a re-run changes nothing), so
  a hand edit in mmo_edit that the script would revert shows up here instead of being lost.
* ClientDB mirrors the editor blobs for talents, talent tabs and spells.
* Every class tab is a hub with three labeled paths of nine talents, prerequisites stay
  inside the tab, no real talent sits behind a placeholder, and a capstone costs six points.
* Placeholders carry a spell without effects; real talents carry working spells.
* The path labels and the "Coming soon" string exist in all four locales.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/talent_trees"))
sys.path.insert(0, str(ROOT / "tools/spell_text"))


class TalentTreesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import author_talent_trees as att
            cls.att = att
            cls.spells = att.load_editor("spells")
            cls.talents = att.load_editor("talents")
            cls.tabs = att.load_editor("talent_tabs")
        except (FileNotFoundError, OSError, SystemExit, ImportError) as exc:
            raise unittest.SkipTest(f"data or protoc unavailable: {exc}")
        cls.spells_by_id = {spell.id: spell for spell in cls.spells.entry}
        cls.talents_by_id = {talent.id: talent for talent in cls.talents.entry}
        cls.class_tabs = {cls_def["tab_id"] for cls_def in att.CLASSES}

    def test_live_data_matches_the_authoring_script(self):
        spells = self.att.load_editor("spells")
        talents = self.att.load_editor("talents")
        tabs = self.att.load_editor("talent_tabs")
        self.att.build(spells, talents, tabs)
        for name, rebuilt in (("spells", spells), ("talents", talents), ("talent_tabs", tabs)):
            self.assertEqual(rebuilt.SerializeToString(), self.att.editor_path(name).read_bytes(),
                             f"{name}.data differs from author_talent_trees.py; re-run it with --apply "
                             f"or move the hand edit into the script")

    def test_client_db_mirrors_the_editor(self):
        for name in ("spells", "talents", "talent_tabs"):
            self.assertEqual(self.att.client_path(name).read_bytes(), self.att.editor_path(name).read_bytes(),
                             f"ClientDB/{name}.data drifted from the editor")

    def test_every_class_tab_is_a_hub_with_three_labeled_paths(self):
        for tab in self.tabs.entry:
            if tab.id not in self.class_tabs:
                continue
            self.assertEqual((tab.hub_x, tab.hub_y), self.att.HUB, tab.name)
            self.assertEqual(len(tab.labels), 3, tab.name)
            self.assertTrue((ROOT / "data/client" / tab.icon).is_file(), f"{tab.name}: missing emblem {tab.icon}")
            talents = [t for t in self.talents.entry if t.tab == tab.id]
            self.assertEqual(len(talents), 27, tab.name)
            roots = [t for t in talents if not t.prerequisites]
            self.assertEqual(len(roots), 9, f"{tab.name}: three roots per path")

    def test_prerequisites_stay_in_the_tab_and_never_hide_real_talents(self):
        for talent in self.talents.entry:
            for prerequisite in talent.prerequisites:
                parent = self.talents_by_id.get(prerequisite.talent_id)
                self.assertIsNotNone(parent, f"talent {talent.id}")
                self.assertEqual(parent.tab, talent.tab, f"talent {talent.id}")
                self.assertLessEqual(prerequisite.rank, len(parent.ranks), f"talent {talent.id}")
                if parent.placeholder:
                    self.assertTrue(talent.placeholder, f"real talent {talent.id} behind placeholder {parent.id}")

    def test_a_capstone_costs_six_of_nine_points(self):
        for talent in self.talents.entry:
            if talent.tab not in self.class_tabs or talent.row != 3:
                continue
            chain_cost, node = 1, talent
            while node.prerequisites:
                prerequisite = node.prerequisites[0]
                chain_cost += prerequisite.rank
                node = self.talents_by_id[prerequisite.talent_id]
            self.assertLessEqual(chain_cost, talent.required_points + 1)
            self.assertEqual(talent.required_points + 1, 6, f"capstone {talent.id}")

    def test_placeholders_do_nothing_and_real_talents_work(self):
        for talent in self.talents.entry:
            if talent.tab not in self.class_tabs:
                continue
            for spell_id in talent.ranks:
                spell = self.spells_by_id[spell_id]
                if talent.placeholder:
                    self.assertEqual(len(spell.effects), 0, f"placeholder {spell.name} must not have effects")
                else:
                    self.assertGreater(len(spell.effects), 0, f"talent spell {spell.name} has no effects")

    def test_single_rank_talents_have_rank_zero(self):
        for talent in self.talents.entry:
            if talent.tab not in self.class_tabs:
                continue
            ranks = [self.spells_by_id[spell_id] for spell_id in talent.ranks]
            if len(ranks) == 1:
                self.assertEqual((ranks[0].rank, ranks[0].baseid), (0, 0), ranks[0].name)
            else:
                self.assertEqual([spell.rank for spell in ranks], list(range(1, len(ranks) + 1)))

    def test_texts_exist_in_all_locales(self):
        keys = {label.text for tab in self.tabs.entry for label in tab.labels} | {"TALENT_COMING_SOON"}
        for folder in ("Locale_deDE", "Locale_enUS", "Locale_frFR", "Locale_ruRU"):
            text = (ROOT / "data/client/Locales" / folder / "Localization.txt").read_text(encoding="utf-8")
            for key in keys:
                self.assertIn(f'"{key}"', text, f"{folder} lacks {key}")
        for talent in self.talents.entry:
            if talent.tab not in self.class_tabs:
                continue
            for spell_id in talent.ranks:
                spell = self.spells_by_id[spell_id]
                self.assertEqual(sorted(entry.locale for entry in spell.name_loc), [1, 2, 3, 4], spell.name)


if __name__ == "__main__":
    unittest.main()
