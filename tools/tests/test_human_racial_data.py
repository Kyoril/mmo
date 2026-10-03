# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the human racial spell data.

* Racials are Human-only (racemask 0x80000000 = race id 0 under the ``1 << (id - 1)``
  encoding) and class-independent (classmask 0), so class switches keep them.
* Passives are hidden auras; the active is an Ability so new characters get it on the bar.
* Every row carries all four locales, like the rest of the spell data.
* Race 0 lists exactly the three racials, and ClientDB mirrors the editor blobs.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/racial_abilities"))


class HumanRacialDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import author_human_racials as ahr
            cls.ahr = ahr
            cls.spells = ahr.load_editor("spells")
            cls.races = ahr.load_editor("races")
        except (FileNotFoundError, OSError, SystemExit) as exc:
            raise unittest.SkipTest(f"data or protoc unavailable: {exc}")

    def spell(self, spell_id):
        for entry in self.spells.entry:
            if entry.id == spell_id:
                return entry
        self.fail(f"spell {spell_id} missing")

    def test_racials_are_human_only_and_class_independent(self):
        for spell_id in self.ahr.SPELL_IDS:
            spell = self.spell(spell_id)
            self.assertEqual(spell.racemask, self.ahr.HUMAN_RACEMASK, spell.name)
            self.assertEqual(spell.classmask, 0, spell.name)

    def test_passives_are_hidden_passive_auras(self):
        for spell_id in (248, 249):
            spell = self.spell(spell_id)
            self.assertTrue(spell.attributes[0] & 0x40, f"{spell.name} must be Passive")
            self.assertFalse(spell.attributes[0] & 0x10, f"{spell.name} must not be an Ability")
            self.assertTrue(spell.attributes[1] & 0x2, f"{spell.name} must be a HiddenAura")

    def test_versatility_raises_all_five_stats_by_two_percent(self):
        effects = self.spell(248).effects
        self.assertEqual(sorted(e.miscvaluea for e in effects), [0, 1, 2, 3, 4])
        for effect in effects:
            self.assertEqual((effect.type, effect.aura, effect.basepoints), (6, 31, 2))

    def test_call_of_the_watch_is_a_timed_party_buff(self):
        spell = self.spell(self.ahr.CALL_OF_THE_WATCH)
        self.assertTrue(spell.attributes[0] & 0x10, "must be an Ability")
        self.assertEqual(spell.duration, 10000)
        self.assertEqual(spell.cooldown, 120000)
        self.assertEqual(spell.rangetype, 5)
        self.assertEqual(sorted((e.type, e.aura, e.basepoints) for e in spell.effects),
                         [(49, 23, 5), (49, 24, -5)])

    def test_every_racial_has_all_four_locales(self):
        for spell_id in self.ahr.SPELL_IDS:
            spell = self.spell(spell_id)
            for field in ("name_loc", "description_loc", "auratext_loc"):
                locales = sorted(entry.locale for entry in getattr(spell, field))
                self.assertEqual(locales, [1, 2, 3, 4], f"{spell.name}.{field}")

    def test_human_race_lists_the_racials(self):
        human = next(race for race in self.races.entry if race.id == 0)
        self.assertEqual(list(human.racialSpells), list(self.ahr.SPELL_IDS))

    def test_client_db_mirrors_the_editor_blobs(self):
        for name in ("spells", "races"):
            editor = (ROOT / f"data/editor/data/{name}.data").read_bytes()
            client = (ROOT / f"data/client/ClientDB/{name}.data").read_bytes()
            self.assertEqual(editor, client, f"ClientDB/{name}.data drifted from the editor")


if __name__ == "__main__":
    unittest.main()
