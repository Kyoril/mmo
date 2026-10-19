# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the acolyte visualization data invariants (docs/acolyte_spell_visuals.md).

Reuses the warrior suite's loader. Acolyte rules on top of the shared ones:

* Only CASTING kits may loop (the cast-bar channels); cast end tears them down.
* No sound on aura events, including AURA_TICK (Corrupting Touch ticks every 2 s).
* Every acolyte spell points at its own "Acolyte - ..." visualization, so a later edit cannot
  silently send it back to the shared creature "Shadow Bolt" entry, the default
  visualization or the mage's Arcane Pulse.
* The talent-built spells carry the same ids in tools/talent_trees/author_talent_trees.py,
  or a re-run of that script would undo the links.
"""

import contextlib
import io
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_scout_visual_data import SCOUT_PARTICLE_MATERIALS, _load_spells  # noqa: E402
from test_warrior_visual_data import _load  # noqa: E402

CASTING, CAST, AURA_APPLIED, AURA_TICK, AURA_IDLE = 2, 3, 5, 7, 8

# The acolyte adds a skull, a ghost wisp and an occult ground sigil (make_sprites.py).
ACOLYTE_PARTICLE_MATERIALS = SCOUT_PARTICLE_MATERIALS | {
    "Particles/Particle_Skull.hmi",
    "Particles/Particle_Wisp.hmi",
    "Particles/Particle_VoidSigil.hmi",
}

ACOLYTE_SPELLS = {
    23: "Shadowbolt", 74: "Shadowbolt", 24: "Corrupting Touch", 73: "Fear",
    38: "Veil of Shadows", 1309: "Umbral Bloom", 1328: "Embrace of Dread",
    1306: "Gloaming Trance", 1314: "Leeching Rot",
    119: "Void Resurgence", 120: "Void Resurgence", 121: "Void Resurgence",
}
TALENT_SPELLS = (1306, 1309, 1314, 1328)


class AcolyteVisualDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / "data/editor/data/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/editor submodule is not checked out")
        try:
            cls.visuals, cls.sounds = _load()
            cls.spells = _load_spells()
        except (ImportError, FileNotFoundError, subprocess.CalledProcessError) as exc:
            raise unittest.SkipTest(f"cannot load proto data: {exc}")

        cls.acolyte = [v for v in cls.visuals.entry if v.name.startswith("Acolyte - ")]
        cls.kits = [(v.name, event, kit)
                    for v in cls.acolyte
                    for event, kl in v.kits_by_event.items() for kit in kl.kits]

        # Particle path -> may it loop? Cast-bar channels, aura idles and projectile trails
        # are torn down by the engine; everything else must finish on its own.
        cls.particles = {}
        for _, event, kit in cls.kits:
            for particle in kit.particles:
                may_loop = event in (CASTING, AURA_IDLE)
                cls.particles[particle] = cls.particles.get(particle, True) and may_loop
        for vis in cls.acolyte:
            for proj in vis.projectiles:
                if proj.trail_particle:
                    cls.particles.setdefault(proj.trail_particle, True)

    def test_acolyte_visualizations_are_present(self):
        self.assertEqual(len(self.acolyte), 9, [v.name for v in self.acolyte])

    def test_every_acolyte_spell_uses_its_own_visualization(self):
        by_id = {v.id: v for v in self.visuals.entry}
        spells = {s.id: s for s in self.spells.entry}
        for spell_id, name in ACOLYTE_SPELLS.items():
            spell = spells[spell_id]
            self.assertEqual(spell.name, name)
            vis = by_id.get(spell.visualization_id)
            self.assertIsNotNone(vis, f"{name} ({spell_id}) has no visualization")
            self.assertTrue(vis.name.startswith("Acolyte - "),
                            f"{name} ({spell_id}) points at {vis.name!r}")

    def test_talent_script_carries_the_same_ids(self):
        source = (ROOT / "tools/talent_trees/author_talent_trees.py").read_text(encoding="utf-8")
        spells = {s.id: s for s in self.spells.entry}
        for spell_id in TALENT_SPELLS:
            marker = f'"visualization_id": {spells[spell_id].visualization_id},  # Acolyte - '
            self.assertIn(marker, source, f"spell {spell_id} would be relinked by the talent script")

    def test_shadowbolt_keeps_a_projectile(self):
        vis = next(v for v in self.acolyte if v.name == "Acolyte - Shadowbolt")
        self.assertEqual(len(vis.projectiles), 1)
        self.assertTrue(vis.projectiles[0].trail_particle)

    def test_no_kit_time_warps_its_animation(self):
        for name, _, kit in self.kits:
            self.assertFalse(kit.HasField("duration_ms"), f"{name}: duration_ms time-warps the clip")

    def test_kits_use_the_sound_catalog_not_raw_paths(self):
        for name, _, kit in self.kits:
            self.assertFalse(list(kit.sounds), f"{name}: acolyte kits reference sound_ids")

    def test_only_cast_bar_kits_loop(self):
        for name, event, kit in self.kits:
            if kit.loop:
                self.assertEqual(event, CASTING, f"{name}: event {event} kit loops and nothing stops it")

    def test_no_sound_on_aura_events(self):
        for name, event, kit in self.kits:
            if event in (AURA_APPLIED, AURA_TICK, AURA_IDLE):
                self.assertFalse(list(kit.sound_ids),
                                 f"{name}: aura sounds replay whenever a carrier comes into view")

    def test_aura_kits_target_the_aura_holder(self):
        for name, event, kit in self.kits:
            if event in (AURA_APPLIED, AURA_TICK, AURA_IDLE):
                self.assertEqual(kit.scope, 1, f"{name}: aura kits must be TARGET-scoped")

    def test_cast_kits_are_caster_scoped(self):
        for name, event, kit in self.kits:
            if event in (CASTING, CAST):
                self.assertEqual(kit.scope, 0, f"{name}: cast events have no target list")

    def test_every_referenced_sound_exists_with_its_files(self):
        by_id = {e.id: e for e in self.sounds.entry}
        for name, event, kit in self.kits:
            for sound_id in kit.sound_ids:
                self.assertIn(sound_id, by_id, f"{name}: unknown sound id {sound_id}")
                # Only a looping cast-bar kit may carry a looping sound.
                if by_id[sound_id].looped:
                    self.assertTrue(kit.loop and event == CASTING, f"{name}: sound {sound_id} loops")
                for path in by_id[sound_id].files:
                    self.assertTrue((ROOT / "data/client" / path).is_file(),
                                    f"{name}: missing sound file {path}")

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
                self.assertIn(emitter.material_name, ACOLYTE_PARTICLE_MATERIALS,
                              f"{particle}: emitter {emitter.name!r} uses "
                              f"{emitter.material_name!r}")


if __name__ == "__main__":
    unittest.main()
