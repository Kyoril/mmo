# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the mage visualization data invariants (docs/mage_spell_visuals.md).

Reuses the warrior suite's loader and translucent-material list, and adds the rules that are
specific to a caster class whose effects mix one-shots with engine-terminated loops:

* A particle referenced from a CASTING kit, an AURA_IDLE kit or a projectile trail is torn
  down by the engine and may loop. Anything else must be a true one-shot, or it never reports
  finished and leaks its scene node.
* No sound on aura events: AURA_APPLIED replays whenever a carrier comes into view.
* Only CASTING kits may loop (CastLoop + channel sound end with the cast).
"""

import contextlib
import io
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_warrior_visual_data import SAFE_PARTICLE_MATERIALS, _load  # noqa: E402

CASTING, AURA_APPLIED, AURA_IDLE, CHANNELING = 2, 5, 8, 9

# Chilled is the deliberate exception to "no sound on aura events": a 5 s slow that is the
# attacker's only feedback, and its AURA_APPLIED is the only event it has.
AURA_SOUND_EXCEPTIONS = {"Mage - Chilled"}


class MageVisualDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / "data/editor/data/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/editor submodule is not checked out")
        try:
            cls.visuals, cls.sounds = _load()
        except (ImportError, FileNotFoundError, subprocess.CalledProcessError) as exc:
            raise unittest.SkipTest(f"cannot load proto data: {exc}")

        cls.mage = [v for v in cls.visuals.entry if v.name.startswith("Mage - ")]
        cls.kits = [(v.name, event, kit)
                    for v in cls.mage
                    for event, kl in v.kits_by_event.items() for kit in kl.kits]

        # Particle path -> may it loop?
        cls.particles = {}
        for name, event, kit in cls.kits:
            for particle in kit.particles:
                may_loop = event in (CASTING, AURA_IDLE, CHANNELING)
                cls.particles[particle] = cls.particles.get(particle, True) and may_loop
        for vis in cls.mage:
            for proj in vis.projectiles:
                if proj.trail_particle:
                    cls.particles.setdefault(proj.trail_particle, True)

    def test_mage_visualizations_are_present(self):
        self.assertEqual(len(self.mage), 15, [v.name for v in self.mage])

    def test_no_kit_time_warps_its_animation(self):
        for name, _, kit in self.kits:
            self.assertFalse(kit.HasField("duration_ms"), f"{name}: duration_ms time-warps the clip")

    def test_kits_use_the_sound_catalog_not_raw_paths(self):
        for name, _, kit in self.kits:
            self.assertFalse(list(kit.sounds), f"{name}: mage kits reference sound_ids")

    def test_only_casting_kits_loop(self):
        # CASTING ends with the cast, CHANNELING with the channel; nothing else stops a loop.
        for name, event, kit in self.kits:
            self.assertFalse(kit.loop and event not in (CASTING, CHANNELING),
                             f"{name}: event {event} kit loops and nothing would stop it")

    def test_no_sound_on_aura_events(self):
        for name, event, kit in self.kits:
            if event in (AURA_APPLIED, AURA_IDLE) and name not in AURA_SOUND_EXCEPTIONS:
                self.assertFalse(list(kit.sound_ids),
                                 f"{name}: aura sounds replay whenever a carrier comes into view")

    def test_aura_kits_target_the_aura_holder(self):
        # A CASTER-scoped aura kit is keyed on the caster, and nothing ever removes it: cast
        # end skips aura-bound effects and aura removal only cleans the holder.
        for name, event, kit in self.kits:
            if event in (AURA_APPLIED, AURA_IDLE):
                self.assertEqual(kit.scope, 1, f"{name}: aura kits must be TARGET-scoped")

    def test_channeled_fire_barrage_holds_a_channel_kit(self):
        barrage = next(v for v in self.mage if v.name == "Mage - Fire Barrage")
        kits = list(barrage.kits_by_event[CHANNELING].kits)
        self.assertTrue(any(k.loop and k.animation_name == "Channel" for k in kits))

    def test_channeled_fire_barrage_has_no_casting_kit(self):
        # SpellStart, ChannelStart and SpellGo arrive in one tick for a channeled spell, so a
        # CASTING kit is destroyed the frame it spawns (and spawned twice before that).
        barrage = next(v for v in self.mage if v.name == "Mage - Fire Barrage")
        self.assertNotIn(CASTING, barrage.kits_by_event)

    def test_every_referenced_sound_exists_with_its_files(self):
        by_id = {e.id: e for e in self.sounds.entry}
        for name, _, kit in self.kits:
            for sound_id in kit.sound_ids:
                self.assertIn(sound_id, by_id, f"{name}: unknown sound id {sound_id}")
                for path in by_id[sound_id].files:
                    self.assertTrue((ROOT / "data/client" / path).is_file(),
                                    f"{name}: missing sound file {path}")

    def test_looping_kit_sounds_are_looped_entries(self):
        # A one-shot entry on a looping CASTING kit would play once and leave the cast silent.
        by_id = {e.id: e for e in self.sounds.entry}
        for name, event, kit in self.kits:
            for sound_id in kit.sound_ids:
                self.assertEqual(by_id[sound_id].looped, bool(kit.loop),
                                 f"{name}: sound {sound_id} looped flag does not match the kit")

    def test_particles_pass_the_hpar_validator(self):
        sys.path.insert(0, str(ROOT / "tools/particle_gen"))
        import inspect_hpar

        data_root = str(ROOT / "data/client")
        for particle, may_loop in self.particles.items():
            path = ROOT / "data/client" / particle
            self.assertTrue(path.is_file(), f"missing particle {particle}")
            with contextlib.redirect_stdout(io.StringIO()):
                problems = inspect_hpar.check(str(path), one_shot=not may_loop,
                                              data_root=data_root)
            self.assertEqual(problems, [], f"{particle} failed hpar validation: {problems}")

    def test_every_particle_uses_a_translucent_material(self):
        sys.path.insert(0, str(ROOT / "tools/particle_gen"))
        import hpar

        for particle in self.particles:
            system = hpar.load(str(ROOT / "data/client" / particle))
            for emitter in system.emitters:
                self.assertIn(emitter.material_name, SAFE_PARTICLE_MATERIALS,
                              f"{particle}: emitter {emitter.name!r} uses "
                              f"{emitter.material_name!r}")


if __name__ == "__main__":
    unittest.main()
