# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the sound-effect recipe tables against the mistakes that have actually bitten.

Both checks here correspond to a real failure:

* The 450-character cap is enforced by eleven_text_to_sound_v2 itself. A 465-character
  LastStand prompt failed all four of its generations while the other fourteen sounds
  succeeded, so the breakage was silent until the download step came up short.
* The "Layers:" clause is what separates layered game audio from realistic foley. The first
  warrior pass asked for dry isolated single sounds and the whole set came back as thin
  metallic clanks.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sfx_gen"))

from recipes import cleric, warrior


class WarriorRecipeTests(unittest.TestCase):
    def test_table_is_complete(self):
        self.assertEqual(len(warrior.SOUNDS), 15)

    def test_every_prompt_fits_the_model_limit(self):
        for name, spec in warrior.SOUNDS.items():
            self.assertLessEqual(
                len(spec.prompt), warrior.MAX_PROMPT_CHARS,
                f"{name}: prompt is {len(spec.prompt)} chars; the model rejects anything "
                f"over {warrior.MAX_PROMPT_CHARS} and the generation fails silently")

    def test_every_prompt_names_its_layers(self):
        # A prompt without an explicit layer list produces flat single-object foley.
        for name, spec in warrior.SOUNDS.items():
            self.assertIn("Layers:", spec.prompt, f"{name}: prompt has no layer list")

    def test_no_prompt_reintroduces_the_foley_clause(self):
        # These phrases produced the thin metallic clanks of the first pass.
        banned = ("dry close-mic", "no reverb", "single isolated")
        for name, spec in warrior.SOUNDS.items():
            lowered = spec.prompt.lower()
            for phrase in banned:
                self.assertNotIn(phrase, lowered,
                                 f"{name}: '{phrase}' flattens the result to foley")

    def test_durations_and_levels_are_sane(self):
        for name, spec in warrior.SOUNDS.items():
            # The model itself accepts 0.5-30s; these are ability sounds, not ambience.
            self.assertTrue(0.5 <= spec.duration <= 5.0, f"{name}: duration {spec.duration}")
            self.assertTrue(-12.0 <= spec.target_dbfs <= 0.0, f"{name}: {spec.target_dbfs}")


class ClericRecipeTests(unittest.TestCase):
    def test_table_is_complete(self):
        self.assertEqual(len(cleric.SOUNDS), 15)

    def test_every_prompt_fits_the_model_limit(self):
        for name, spec in cleric.SOUNDS.items():
            self.assertLessEqual(len(spec.prompt), cleric.MAX_PROMPT_CHARS,
                                 f"{name}: prompt is {len(spec.prompt)} chars")

    def test_every_prompt_names_its_layers(self):
        for name, spec in cleric.SOUNDS.items():
            self.assertIn("Layers:", spec.prompt, f"{name}: prompt has no layer list")

    def test_no_prompt_reintroduces_the_foley_clause(self):
        banned = ("dry close-mic", "no reverb", "single isolated")
        for name, spec in cleric.SOUNDS.items():
            for phrase in banned:
                self.assertNotIn(phrase, spec.prompt.lower(), f"{name}: '{phrase}'")

    def test_choir_layers_stay_wordless(self):
        # A sung or spoken word would read as dialogue and clash across races and genders.
        for name, spec in cleric.SOUNDS.items():
            if "choir" in spec.prompt.lower():
                self.assertTrue("wordless" in spec.prompt.lower() or "no words" in spec.prompt.lower(),
                                f"{name}: a choir layer must be wordless")

    def test_loops_are_quieter_than_one_shots(self):
        # A 10 s cast loop at impact level would mask combat for its whole duration.
        loudest_loop = max(s.target_dbfs for s in cleric.SOUNDS.values() if s.loop)
        quietest_impact = min(s.target_dbfs for n, s in cleric.SOUNDS.items()
                              if not s.loop and "Tick" not in n)
        self.assertLess(loudest_loop, quietest_impact)

    def test_durations_and_levels_are_sane(self):
        for name, spec in cleric.SOUNDS.items():
            self.assertTrue(0.5 <= spec.duration <= 5.0, f"{name}: duration {spec.duration}")
            self.assertTrue(-12.0 <= spec.target_dbfs <= 0.0, f"{name}: {spec.target_dbfs}")


if __name__ == "__main__":
    unittest.main()
