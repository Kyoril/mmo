# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the warrior visualization data invariants.

These are the mistakes the first warrior pass shipped: particle files that did not exist,
kits that time-warped their animation via duration_ms, and effects pointing at materials
the engine renders opaque. Skips rather than fails when protoc or the data submodules are
unavailable, so a partial checkout does not break the gate.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Instances the engine actually renders translucent. Particles/Additive.hmat above all is
# typed Unlit, which the engine renders opaque -- that single mistake turned every particle
# in the original warrior pass into a hard occluding rectangle.
SAFE_PARTICLE_MATERIALS = {
    "Particles/Particle_Beam.hmi",
    "Particles/Particle_Glow.hmi",
    "Particles/Particle_Ring.hmi",
    "Particles/Particle_Star.hmi",
}


def _load():
    from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
    sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
    from proto_runtime import find_protoc

    schema = ROOT / "src/shared/proto_data"
    with tempfile.TemporaryDirectory() as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema}",
                        f"--descriptor_set_out={desc}", "--include_imports",
                        "spell_visualizations.proto", "sounds.proto"],
                       cwd=schema, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())

    pool = descriptor_pool.DescriptorPool()
    for f in file_set.file:
        pool.Add(f)

    visuals_type = message_factory.GetMessageClass(
        pool.FindMessageTypeByName("mmo.proto.SpellVisualizations"))
    sounds_type = message_factory.GetMessageClass(
        pool.FindMessageTypeByName("mmo.proto.Sounds"))

    visuals = visuals_type.FromString(
        (ROOT / "data/editor/data/spell_visualizations.data").read_bytes())
    sounds = sounds_type.FromString(
        (ROOT / "data/editor/data/sounds.data").read_bytes())
    return visuals, sounds


class WarriorVisualDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / "data/editor/data/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/editor submodule is not checked out")
        try:
            cls.visuals, cls.sounds = _load()
        except Exception as exc:  # protoc missing, protobuf missing
            raise unittest.SkipTest(f"cannot load proto data: {exc}")

        cls.kits = [(v.name, kit)
                    for v in cls.visuals.entry if v.name.startswith("Warrior - ")
                    for kl in v.kits_by_event.values() for kit in kl.kits]

    def test_warrior_visualizations_are_present(self):
        names = {v.name for v in self.visuals.entry if v.name.startswith("Warrior - ")}
        self.assertEqual(len(names), 14, f"expected 14 warrior visualizations, got {names}")

    def test_no_kit_time_warps_its_animation(self):
        # duration_ms becomes playRate = clipLength / duration. Guessed values ran the
        # original warrior animations 1.4x to 1.8x too fast.
        for name, kit in self.kits:
            self.assertFalse(kit.HasField("duration_ms"),
                             f"{name}: duration_ms time-warps the clip")

    def test_kits_use_the_sound_catalog_not_raw_paths(self):
        for name, kit in self.kits:
            self.assertFalse(list(kit.sounds),
                             f"{name}: warrior kits reference sound_ids, not file paths")

    def test_no_kit_sets_both_sound_fields(self):
        for name, kit in self.kits:
            self.assertFalse(kit.sounds and kit.sound_ids,
                             f"{name}: setting both fields double-triggers audio")

    def test_every_referenced_sound_id_exists(self):
        known = {e.id for e in self.sounds.entry}
        for name, kit in self.kits:
            for sound_id in kit.sound_ids:
                self.assertIn(sound_id, known, f"{name}: unknown sound id {sound_id}")

    def test_every_referenced_sound_file_exists(self):
        by_id = {e.id: e for e in self.sounds.entry}
        for name, kit in self.kits:
            for sound_id in kit.sound_ids:
                for path in by_id[sound_id].files:
                    self.assertTrue((ROOT / "data/client" / path).is_file(),
                                    f"{name}: missing sound file {path}")

    def test_every_referenced_particle_exists(self):
        for name, kit in self.kits:
            for particle in kit.particles:
                self.assertTrue((ROOT / "data/client" / particle).is_file(),
                                f"{name}: missing particle {particle}")

    def test_no_kit_loops(self):
        for name, kit in self.kits:
            self.assertFalse(kit.loop, f"{name}: a looping kit never self-terminates")

    def test_every_particle_uses_a_translucent_material(self):
        # Particles/Additive.hmat is typed Unlit, so the engine renders it opaque and every
        # particle becomes a hard occluding rectangle -- the mistake that broke all ten
        # original warrior effects. Every emitter in every particle a warrior kit references
        # must use one of the four known-translucent material instances.
        sys.path.insert(0, str(ROOT / "tools/particle_gen"))
        import hpar

        for name, kit in self.kits:
            for particle in kit.particles:
                path = ROOT / "data/client" / particle
                system = hpar.load(str(path))
                for emitter in system.emitters:
                    self.assertIn(
                        emitter.material_name, SAFE_PARTICLE_MATERIALS,
                        f"{name}: emitter {emitter.name!r} in {particle} uses "
                        f"{emitter.material_name!r}, which is not a known-translucent "
                        f"particle material")


if __name__ == "__main__":
    unittest.main()
