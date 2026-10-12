# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the scout visualization data invariants (docs/scout_spell_visuals.md).

Reuses the warrior suite's loader. Scout-specific rules on top of the mage ones:

* No kit loops at all -- the scout has no cast bars, so nothing would ever stop a loop.
* Every ribbon trail carries an ``emit_duration``: blade arcs sit on one-shot CAST_SUCCEEDED
  kits, and an untimed ribbon outside the cast phase is never torn down.
* No sound on aura events, including AURA_TICK (a bleed ticks every 2 s).
* Every scout spell points at its own "Scout - ..." visualization, so a later edit cannot
  silently send it back to a borrowed warrior or legacy entry.
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

CAST, AURA_APPLIED, AURA_TICK, AURA_IDLE = 3, 5, 7, 8

# The scout adds three sprites of its own (tools/particle_gen/make_sprites.py), all instances
# of Particle_Alpha_Tex.hmat with depth-write off, plus the cleric's sigil.
SCOUT_PARTICLE_MATERIALS = SAFE_PARTICLE_MATERIALS | {
    "Particles/Particle_Slash.hmi",
    "Particles/Particle_Smoke.hmi",
    "Particles/Particle_Crosshair.hmi",
    "Particles/Particle_Sigil.hmi",
}

SCOUT_SPELLS = {
    10: "Sprint", 76: "Wounding Strike", 160: "Wounding Strike", 77: "Ruthless Strike",
    161: "Knockout", 163: "Quick Cut", 165: "Wounded", 166: "Evasive Step",
    167: "Throwing Knife", 168: "Twin Slash", 169: "Mark Weakness", 1409: "Fever Pitch",
    1413: "Steady Hand", 1426: "Smoke and Mirrors",
}


def _load_spells():
    sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
    sys.path.insert(0, str(ROOT / "tools/warrior_visuals"))
    from author_visuals import load_type
    spells_type = load_type(ROOT / "src/shared/proto_data", ["spells.proto"], "mmo.proto.Spells")
    return spells_type.FromString((ROOT / "data/editor/data/spells.data").read_bytes())


class ScoutVisualDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / "data/editor/data/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/editor submodule is not checked out")
        try:
            cls.visuals, cls.sounds = _load()
            cls.spells = _load_spells()
        except (ImportError, FileNotFoundError, subprocess.CalledProcessError) as exc:
            raise unittest.SkipTest(f"cannot load proto data: {exc}")

        cls.scout = [v for v in cls.visuals.entry if v.name.startswith("Scout - ")]
        cls.kits = [(v.name, event, kit)
                    for v in cls.scout
                    for event, kl in v.kits_by_event.items() for kit in kl.kits]

        # Particle path -> may it loop? Only aura idles and projectile trails are torn down by
        # the engine; everything else must finish on its own.
        cls.particles = {}
        for name, event, kit in cls.kits:
            for particle in kit.particles:
                may_loop = event == AURA_IDLE
                cls.particles[particle] = cls.particles.get(particle, True) and may_loop
        for vis in cls.scout:
            for proj in vis.projectiles:
                if proj.trail_particle:
                    cls.particles.setdefault(proj.trail_particle, True)

    def test_scout_visualizations_are_present(self):
        self.assertEqual(len(self.scout), 13, [v.name for v in self.scout])

    def test_every_scout_spell_uses_its_own_visualization(self):
        by_id = {v.id: v for v in self.visuals.entry}
        spells = {s.id: s for s in self.spells.entry}
        for spell_id, name in SCOUT_SPELLS.items():
            spell = spells[spell_id]
            self.assertEqual(spell.name, name)
            vis = by_id.get(spell.visualization_id)
            self.assertIsNotNone(vis, f"{name} ({spell_id}) has no visualization")
            self.assertTrue(vis.name.startswith("Scout - "),
                            f"{name} ({spell_id}) points at {vis.name!r}")

    def test_no_kit_time_warps_its_animation(self):
        for name, _, kit in self.kits:
            self.assertFalse(kit.HasField("duration_ms"), f"{name}: duration_ms time-warps the clip")

    def test_kits_use_the_sound_catalog_not_raw_paths(self):
        for name, _, kit in self.kits:
            self.assertFalse(list(kit.sounds), f"{name}: scout kits reference sound_ids")

    def test_no_kit_loops(self):
        for name, event, kit in self.kits:
            self.assertFalse(kit.loop, f"{name}: event {event} kit loops and nothing stops it")

    def test_every_ribbon_retires_itself(self):
        ribbons = [(name, kit) for name, _, kit in self.kits if kit.HasField("ribbon_trail")]
        self.assertGreater(len(ribbons), 0, "the strikes lost their blade arcs")
        for name, kit in ribbons:
            self.assertGreater(kit.ribbon_trail.emit_duration, 0.0,
                               f"{name}: an untimed ribbon outside the cast phase never ends")
            self.assertTrue(kit.ribbon_trail.material_name,
                            f"{name}: the default ribbon material does not exist")

    def test_no_sound_on_aura_events(self):
        for name, event, kit in self.kits:
            if event in (AURA_APPLIED, AURA_TICK, AURA_IDLE):
                self.assertFalse(list(kit.sound_ids),
                                 f"{name}: aura sounds replay whenever a carrier comes into view")

    def test_aura_kits_target_the_aura_holder(self):
        for name, event, kit in self.kits:
            if event in (AURA_APPLIED, AURA_TICK, AURA_IDLE):
                self.assertEqual(kit.scope, 1, f"{name}: aura kits must be TARGET-scoped")

    def test_cast_succeeded_kits_are_caster_scoped(self):
        # CastSucceeded is dispatched without a target list; a TARGET kit there never runs.
        for name, event, kit in self.kits:
            if event == CAST:
                self.assertEqual(kit.scope, 0, f"{name}: CastSucceeded has no target list")

    def test_every_referenced_sound_exists_with_its_files(self):
        by_id = {e.id: e for e in self.sounds.entry}
        for name, _, kit in self.kits:
            for sound_id in kit.sound_ids:
                self.assertIn(sound_id, by_id, f"{name}: unknown sound id {sound_id}")
                self.assertFalse(by_id[sound_id].looped, f"{name}: sound {sound_id} loops")
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
                self.assertIn(emitter.material_name, SCOUT_PARTICLE_MATERIALS,
                              f"{particle}: emitter {emitter.name!r} uses "
                              f"{emitter.material_name!r}")


if __name__ == "__main__":
    unittest.main()
