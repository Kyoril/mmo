# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the first pet content (tools/pets/author_pet_content.py).

* The Gravecaller raises skeletons with SummonPet: 3 at a time, 5 at most, ReplaceOldest, at the
  caster's level, living as long as she does.
* The acolyte binds a shadow (GrantPersistentPet) from the acolyte trainer, which teaches the
  summon and dismiss spells; the shade's PetEntry follows the class level, picks a random
  name, may be renamed and knows two abilities plus one by level and one from the trainer.
* The taming kit exists but no class learns it, and one wolf is tamable.
* Every name carries all four locales, the pets manager is registered in project.txt, and
  ClientDB mirrors the shared editor blobs.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/pets"))


class PetContentDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import author_pet_content as apc
            cls.apc = apc
            cls.data = {name: apc.load_editor(name) for name in apc.EDITOR_TYPES}
        except (FileNotFoundError, OSError, SystemExit) as exc:
            raise unittest.SkipTest(f"data or protoc unavailable: {exc}")

    def row(self, dataset, row_id):
        for entry in self.data[dataset].entry:
            if entry.id == row_id:
                return entry
        self.fail(f"{dataset} row {row_id} missing")

    def effect(self, spell_id, index=0):
        return self.row("spells", spell_id).effects[index]

    def test_raise_skeletons_is_a_capped_swarm_at_the_casters_level(self):
        apc = self.apc
        spell = self.row("spells", apc.RAISE_SKELETONS)
        effect = spell.effects[0]
        self.assertEqual(effect.type, 43)
        self.assertEqual(effect.summonunit, apc.RISEN_SKELETON)
        self.assertEqual(effect.basepoints, 3)
        self.assertEqual(effect.pet_max_count, 5)
        self.assertEqual(effect.pet_limit_policy, 0, "ReplaceOldest")
        self.assertEqual(effect.pet_level_source, 1, "CharacterLevel = the NPC caster's level")
        self.assertEqual(effect.targeta, 0, "targets the caster, so it is not a harmful spell")
        self.assertEqual(spell.duration, 0, "skeletons live as long as the caster")
        self.assertEqual(spell.classmask, 0)

    def test_gravecaller_casts_raise_skeletons_and_is_spawned(self):
        apc = self.apc
        unit = self.row("units", apc.GRAVECALLER)
        self.assertIn(apc.RAISE_SKELETONS, [cs.spellid for cs in unit.creaturespells])
        world = self.row("maps", apc.MAP_ID)
        spawns = [s for s in world.unitspawns if s.name == apc.SPAWN_NAME]
        self.assertEqual(len(spawns), 1)
        self.assertEqual(spawns[0].unitentry, apc.GRAVECALLER)

    def test_shade_pet_entry(self):
        apc = self.apc
        pet = self.row("pets", apc.SHADE_PET)
        self.assertEqual(pet.unit_entry, apc.UMBRAL_SHADE)
        self.assertEqual((pet.level_mode, pet.level_source), (1, 2), "MasterLevel, ClassLevel")
        self.assertEqual(pet.name_mode, 1, "RandomFromList")
        self.assertGreaterEqual(len(pet.random_names), 8)
        self.assertTrue(pet.allow_player_naming)
        self.assertEqual(list(pet.default_spells), [apc.GLOOM_DART, apc.SAPPING_MURK])
        self.assertEqual([(r.spell, r.level) for r in pet.level_spells], [(apc.UMBRAL_LASH, 6)])

    def test_pet_abilities_are_castable_by_a_creature(self):
        apc = self.apc
        for spell_id in (apc.GLOOM_DART, apc.SAPPING_MURK, apc.UMBRAL_LASH, apc.MURKBIND):
            spell = self.row("spells", spell_id)
            self.assertEqual(spell.classmask, 0, spell.name)
            self.assertFalse(spell.reagents, spell.name)
            self.assertEqual(spell.cost, 0, spell.name)
            self.assertFalse(spell.attributes[0] & 0x40, f"{spell.name} must not be passive")

    def test_acolyte_spells(self):
        apc = self.apc
        bind = self.row("spells", apc.BIND_SHADOW)
        self.assertEqual(bind.classmask, 4, "acolyte (class 3)")
        self.assertEqual([(e.type, e.miscvaluea, e.triggerspell) for e in bind.effects],
                         [(59, apc.SHADE_PET, 0), (31, 0, apc.SUMMON_SHADOW),
                          (31, 0, apc.DISMISS_SHADOW)])
        summon = self.effect(apc.SUMMON_SHADOW)
        self.assertEqual((summon.type, summon.miscvaluea), (58, apc.SHADE_PET))
        self.assertEqual(self.effect(apc.DISMISS_SHADOW).type, 60)
        lore = self.effect(apc.SHADOW_LORE_MURKBIND)
        self.assertEqual((lore.type, lore.triggerspell), (44, apc.MURKBIND))

    def test_acolyte_trainer_sells_bind_shadow_and_the_pet_lesson(self):
        apc = self.apc
        trainer = self.row("trainers", apc.ACOLYTE_TRAINER)
        self.assertEqual(trainer.classid, 3)
        sold = [row.spell for row in trainer.spells]
        self.assertEqual(sold.count(apc.BIND_SHADOW), 1)
        self.assertEqual(sold.count(apc.SHADOW_LORE_MURKBIND), 1)
        for original in (38, 24, 73, 74):
            self.assertIn(original, sold, "the trainer keeps its original spells")

    def test_taming_kit_is_test_only(self):
        apc = self.apc
        self.assertEqual(self.effect(apc.TAME_BEAST).type, 42)
        self.assertEqual(self.row("spells", apc.TAME_BEAST).attributes[0] & 0x1, 0,
                         "a channel would tame on the first frame")
        call = self.effect(apc.CALL_PET)
        self.assertEqual((call.type, call.miscvaluea), (58, 0))
        revive = self.effect(apc.REVIVE_PET)
        self.assertEqual((revive.type, revive.basepoints), (61, 50))
        for spell_id in (apc.TAME_BEAST, apc.CALL_PET, apc.REVIVE_PET):
            self.assertEqual(self.row("spells", spell_id).classmask, 0)
            for trainer in self.data["trainers"].entry:
                self.assertNotIn(spell_id, [row.spell for row in trainer.spells])

    def test_one_wolf_is_tamable_into_the_wolf_pet(self):
        apc = self.apc
        self.assertEqual(self.row("units", apc.TAMABLE_UNIT).tame_pet_entry, apc.WOLF_PET)
        wolf = self.row("pets", apc.WOLF_PET)
        self.assertEqual((wolf.name, wolf.name_mode), ("Wolf", 0))
        self.assertEqual((wolf.level_mode, wolf.level_source), (0, 1), "OwnLevel, CharacterLevel")
        self.assertTrue(wolf.allow_player_naming)

    def test_every_name_has_all_four_locales(self):
        apc = self.apc
        for spell_id in apc.SPELL_IDS:
            spell = self.row("spells", spell_id)
            for field in ("name_loc", "description_loc"):
                self.assertEqual(sorted(e.locale for e in getattr(spell, field)), [1, 2, 3, 4],
                                 f"{spell.name}.{field}")
        for unit_id in apc.UNIT_IDS:
            unit = self.row("units", unit_id)
            self.assertEqual(sorted(e.locale for e in unit.name_loc), [1, 2, 3, 4], unit.name)
        for pet_id in apc.PET_IDS:
            pet = self.row("pets", pet_id)
            self.assertEqual(sorted(e.locale for e in pet.name_loc), [1, 2, 3, 4], pet.name)

    def test_pets_manager_is_registered(self):
        self.assertIn(self.apc.PETS_PROJECT_LINE, self.apc.PROJECT_TXT.read_bytes())

    def test_client_db_mirrors_the_editor_blobs(self):
        for name in self.apc.CLIENT_SHARED:
            self.assertEqual(self.apc.editor_path(name).read_bytes(),
                             self.apc.client_path(name).read_bytes(),
                             f"ClientDB/{name}.data drifted from the editor")


if __name__ == "__main__":
    unittest.main()
