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
import re
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sfx_gen"))

from recipes import cleric, hollow_choir, human, mage, warrior


class _RecipeChecks:
    """Checks every recipe table must pass. Subclasses set ``recipe`` and ``count``."""

    recipe = None
    count = 0

    def test_table_is_complete(self):
        self.assertEqual(len(self.recipe.SOUNDS), self.count)

    def test_every_prompt_fits_the_model_limit(self):
        for name, spec in self.recipe.SOUNDS.items():
            self.assertLessEqual(
                len(spec.prompt), self.recipe.MAX_PROMPT_CHARS,
                f"{name}: prompt is {len(spec.prompt)} chars; the model rejects anything "
                f"over {self.recipe.MAX_PROMPT_CHARS} and the generation fails silently")

    def test_every_prompt_names_its_layers(self):
        # A prompt without an explicit layer list produces flat single-object foley.
        for name, spec in self.recipe.SOUNDS.items():
            self.assertIn("Layers:", spec.prompt, f"{name}: prompt has no layer list")

    def test_no_prompt_reintroduces_the_foley_clause(self):
        # These phrases produced the thin metallic clanks of the first pass.
        banned = ("dry close-mic", "no reverb", "single isolated")
        for name, spec in self.recipe.SOUNDS.items():
            lowered = spec.prompt.lower()
            for phrase in banned:
                self.assertNotIn(phrase, lowered,
                                 f"{name}: '{phrase}' flattens the result to foley")

    def test_durations_and_levels_are_sane(self):
        for name, spec in self.recipe.SOUNDS.items():
            # The model itself accepts 0.5-30s; these are ability sounds, not ambience.
            self.assertTrue(0.5 <= spec.duration <= 5.0, f"{name}: duration {spec.duration}")
            self.assertTrue(-12.0 <= spec.target_dbfs <= 0.0, f"{name}: {spec.target_dbfs}")


class WarriorRecipeTests(_RecipeChecks, unittest.TestCase):
    recipe = warrior
    count = 15


class MageRecipeTests(_RecipeChecks, unittest.TestCase):
    recipe = mage
    count = 23

    def test_only_channels_loop(self):
        # A looping one-shot would never stop; a one-shot channel would cut out mid-cast.
        for name, spec in mage.SOUNDS.items():
            self.assertEqual(spec.loop, name.endswith("Channel"), name)


class ClericRecipeTests(_RecipeChecks, unittest.TestCase):
    recipe = cleric
    count = 15

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



class HumanRecipeTests(_RecipeChecks, unittest.TestCase):
    recipe = human
    count = 4

    def test_every_prompt_names_its_layers(self):
        # Human sounds are *layered by mixing*: each prompt is one element on purpose, because
        # a multi-layer prompt collapsed into a single dull drum hit. Instead every layer
        # prompt must exclude the other elements, or they bleed into each other.
        for name, spec in human.SOUNDS.items():
            lowered = spec.prompt.lower()
            for exclusion in ("no voice", "no music"):
                self.assertIn(exclusion, lowered, f"{name}: must exclude the other elements")

    def test_mixes_reference_generated_layers(self):
        for mix_name, mix in human.MIXES.items():
            self.assertGreaterEqual(len(mix["layers"]), 2, f"{mix_name}: a mix needs layers")
            for entry in mix["layers"]:
                self.assertIn(entry.sound, human.SOUNDS, f"{mix_name}: unknown layer")
                self.assertTrue(1 <= entry.take <= 4, f"{mix_name}: take {entry.take}")
                self.assertGreaterEqual(entry.offset, 0.0)
                self.assertLessEqual(entry.gain_db, 0.0, f"{mix_name}: boost before mixing")


class HollowChoirRecipeTests(_RecipeChecks, unittest.TestCase):
    recipe = hollow_choir
    count = 29

    def test_every_prompt_names_its_layers(self):
        # Layered by mixing (see human.py): each prompt is one element and must exclude music,
        # or a generic score bed bleeds into every layer.
        for name, spec in hollow_choir.SOUNDS.items():
            self.assertIn("no music", spec.prompt.lower(), f"{name}: must exclude music")

    def test_voice_layers_stay_wordless(self):
        # A sung or spoken word would read as dialogue. ("no choir" is an exclusion, not a voice.)
        for name, spec in hollow_choir.SOUNDS.items():
            lowered = spec.prompt.lower()
            if re.search(r"(?<!no )\b(choir|wail|chant|cry|voices)\b", lowered):
                self.assertTrue("wordless" in lowered or "no words" in lowered, name)

    def test_only_loop_finals_loop(self):
        for final, (sound, take, _) in hollow_choir.SINGLES.items():
            self.assertIn(sound, hollow_choir.SOUNDS)
            self.assertTrue(1 <= take <= 4)
            self.assertEqual(hollow_choir.SOUNDS[sound].loop, final.endswith("Loop"), final)

    def test_mixes_reference_generated_layers(self):
        for mix_name, mix in hollow_choir.MIXES.items():
            self.assertGreaterEqual(len(mix["layers"]), 2, f"{mix_name}: a mix needs layers")
            for entry in mix["layers"]:
                self.assertIn(entry.sound, hollow_choir.SOUNDS, f"{mix_name}: unknown layer")
                self.assertFalse(hollow_choir.SOUNDS[entry.sound].loop, mix_name)
                self.assertTrue(1 <= entry.take <= 4, f"{mix_name}: take {entry.take}")
                self.assertGreaterEqual(entry.offset, 0.0)
                self.assertLessEqual(entry.gain_db, 0.0, f"{mix_name}: boost before mixing")

    def test_every_final_is_built_exactly_once(self):
        built = list(hollow_choir.MIXES) + list(hollow_choir.SINGLES)
        self.assertEqual(sorted(built), sorted(hollow_choir.FINAL_ORDER))


if __name__ == "__main__":
    unittest.main()
