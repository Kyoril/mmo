# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the placeholders in spell descriptions and aura texts.

* Every placeholder uses a token the client formatter knows, every "$<id>" reference names
  an existing spell, and every effect index exists on the spell it reads. The formatter
  writes broken placeholders back verbatim, so a typo would otherwise ship as "$15s0".
* Fire Barrage shows the damage of the projectile spell it triggers, in all four locales.
* ClientDB mirrors the editor spells blob.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/spell_text"))


class SpellTextPlaceholderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import spell_text_data as std
            cls.std = std
            cls.spells = std.load_spells()
        except (FileNotFoundError, OSError, ImportError) as exc:
            raise unittest.SkipTest(f"data or protoc unavailable: {exc}")
        cls.by_id = {spell.id: spell for spell in cls.spells.entry}

    def test_parser_flags_broken_placeholders(self):
        barrage = self.by_id[150]
        problems = self.std.placeholder_problems
        self.assertEqual(problems("$152s0 and $t0 over $D, $$5", barrage, self.by_id), [])
        self.assertEqual(len(problems("$999999s0", barrage, self.by_id)), 1)
        self.assertEqual(len(problems("$q0", barrage, self.by_id)), 1)
        self.assertEqual(len(problems("$s7", barrage, self.by_id)), 1)

    def test_all_spell_texts_use_valid_placeholders(self):
        failures = []
        for spell in self.spells.entry:
            for label, text in self.std.spell_texts(spell):
                for problem in self.std.placeholder_problems(text, spell, self.by_id):
                    failures.append(f"{spell.id} {spell.name} {label}: {problem}")
        self.assertEqual(failures, [], "\n".join(failures))

    def test_fire_barrage_shows_the_projectile_damage(self):
        barrage = self.by_id[150]
        self.assertEqual(barrage.effects[0].triggerspell, 152)
        texts = {entry.locale: entry.value for entry in barrage.description_loc}
        self.assertEqual(sorted(texts), [1, 2, 3, 4])
        for locale, text in texts.items():
            for placeholder in ("$152s0", "$t0", "$o0", "$D"):
                self.assertIn(placeholder, text, f"locale {locale}")
        self.assertEqual(barrage.description, texts[2])

    def test_client_db_mirrors_the_editor_spells(self):
        self.assertEqual(self.std.client_path().read_bytes(), self.std.editor_path().read_bytes())


if __name__ == "__main__":
    unittest.main()
