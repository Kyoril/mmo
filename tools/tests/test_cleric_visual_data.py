# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the cleric visualization data invariants.

On top of the warrior rules (no duration_ms time warp, catalog sound ids, existing files,
translucent materials) the cleric kits use the lifetime-sensitive features -- looping hand
glows, aura idle loops, ribbon trails, tints -- and each of those leaks or sticks if it is
put on the wrong event:

* a looping particle file outside Casting/AuraIdle never finishes, and nothing tears it down;
* a ribbon trail outside the cast phase is never retired;
* a tint outside the cast phase without ``duration_ms`` is never removed from an impact target.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/particle_gen"))
sys.path.insert(0, str(ROOT / "tools/particle_gen/recipes"))

SAFE_PARTICLE_MATERIALS = {
    "Particles/Particle_Beam.hmi",
    "Particles/Particle_Glow.hmi",
    "Particles/Particle_Ring.hmi",
    "Particles/Particle_Star.hmi",
    "Particles/Particle_Sigil.hmi",
    "Particles/Particle_Rays.hmi",
    "Particles/Particle_Feather.hmi",
    "Particles/Particle_Flame.hmi",
}

CASTING, CAST_SUCCEEDED, AURA_IDLE = 2, 3, 8

# spell id -> visualization name. Every active cleric ability must be covered.
EXPECTED_LINKS = {
    12: "Cleric - Healing Light", 61: "Cleric - Healing Light",
    13: "Cleric - Smite", 57: "Cleric - Smite",
    20: "Cleric - Divine Vitality",
    55: "Cleric - Healing Aura",
    58: "Cleric - Holy Fire",
    65: "Cleric - Protective Aura",
    99: "Cleric - Faithward", 100: "Cleric - Faithward", 101: "Cleric - Faithward",
    170: "Cleric - Renewing Light",
    179: "Cleric - Resurrection",
}


def _load():
    from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
    sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
    from proto_runtime import find_protoc

    def load(schema, protos, names, files):
        with tempfile.TemporaryDirectory() as tmp:
            desc = Path(tmp) / "d.pb"
            subprocess.run([str(find_protoc(ROOT)), f"-I{schema}",
                            f"--descriptor_set_out={desc}", "--include_imports", *protos],
                           cwd=schema, check=True)
            file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
        pool = descriptor_pool.DescriptorPool()
        for f in file_set.file:
            pool.Add(f)
        return [message_factory.GetMessageClass(pool.FindMessageTypeByName(n)).FromString(
                    path.read_bytes()) for n, path in zip(names, files)]

    editor = load(ROOT / "src/shared/proto_data",
                  ["spell_visualizations.proto", "sounds.proto", "spells.proto"],
                  ["mmo.proto.SpellVisualizations", "mmo.proto.Sounds", "mmo.proto.Spells"],
                  [ROOT / "data/editor/data/spell_visualizations.data",
                   ROOT / "data/editor/data/sounds.data",
                   ROOT / "data/editor/data/spells.data"])
    client = load(ROOT / "src/shared/client_data",
                  ["spells.proto", "spell_visualizations.proto"],
                  ["mmo.proto_client.SpellVisualizations", "mmo.proto_client.Spells"],
                  [ROOT / "data/client/ClientDB/spell_visualizations.data",
                   ROOT / "data/client/ClientDB/spells.data"])
    return editor + client


class ClericVisualDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / "data/editor/data/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/editor submodule is not checked out")
        if not (ROOT / "data/client/ClientDB/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/client submodule is not checked out")
        try:
            (cls.visuals, cls.sounds, cls.spells,
             cls.client_visuals, cls.client_spells) = _load()
        except (ImportError, FileNotFoundError, subprocess.CalledProcessError) as exc:
            raise unittest.SkipTest(f"cannot load proto data: {exc}")

        import cleric_common
        cls.looping = {"Particles/Cleric/" + n for n in cleric_common.LOOPING_EFFECTS}
        cls.entries = [v for v in cls.visuals.entry if v.name.startswith("Cleric - ")]
        cls.kits = [(v.name, event, kit)
                    for v in cls.entries
                    for event, kl in v.kits_by_event.items() for kit in kl.kits]

    def test_every_cleric_spell_points_at_its_visualization(self):
        names = {v.id: v.name for v in self.visuals.entry}
        for dataset_name, spells in (("editor", self.spells), ("client", self.client_spells)):
            by_id = {s.id: s for s in spells.entry}
            for spell_id, expected in EXPECTED_LINKS.items():
                vis_id = by_id[spell_id].visualization_id
                self.assertEqual(names.get(vis_id), expected,
                                 f"{dataset_name} spell {spell_id} points at visualization "
                                 f"{vis_id}, expected {expected!r}")

    def test_editor_and_client_visualizations_match(self):
        from google.protobuf import json_format
        client = {v.id: v for v in self.client_visuals.entry}
        for vis in self.entries:
            self.assertIn(vis.id, client, f"{vis.name} missing from ClientDB")
            self.assertEqual(json_format.MessageToDict(vis),
                             json_format.MessageToDict(client[vis.id]),
                             f"{vis.name}: editor and ClientDB copies differ")

    def test_no_kit_time_warps_its_animation(self):
        for name, _, kit in self.kits:
            self.assertFalse(kit.HasField("duration_ms"), f"{name}: duration_ms time-warps the clip")

    def test_kits_use_the_sound_catalog(self):
        known = {e.id: e for e in self.sounds.entry}
        for name, _, kit in self.kits:
            self.assertFalse(list(kit.sounds), f"{name}: cleric kits use sound_ids")
            for sound_id in kit.sound_ids:
                self.assertIn(sound_id, known, f"{name}: unknown sound id {sound_id}")
                for path in known[sound_id].files:
                    self.assertTrue((ROOT / "data/client" / path).is_file(),
                                    f"{name}: missing sound file {path}")

    def test_looping_sounds_only_in_looping_cast_kits(self):
        # The service plays a looped channel only for a looping kit, and stops it only on
        # the terminating events -- so a looped entry anywhere else would play forever.
        by_id = {e.id: e for e in self.sounds.entry}
        for name, event, kit in self.kits:
            for sound_id in kit.sound_ids:
                if by_id[sound_id].looped:
                    self.assertTrue(kit.loop and event == CASTING,
                                    f"{name}: looped sound {sound_id} outside the cast loop kit")

    def test_looping_lifetimes_are_owned_by_the_service(self):
        for name, event, kit in self.kits:
            if kit.loop:
                self.assertEqual(event, CASTING, f"{name}: only the cast loop kit may loop")
            if kit.HasField("ribbon_trail"):
                self.assertEqual(event, CASTING, f"{name}: ribbons are only retired in the cast phase")
            if kit.HasField("tint") and event != CASTING:
                self.assertGreater(kit.tint.duration_ms, 0,
                                   f"{name}: a tint outside the cast phase is never removed "
                                   f"unless it is a timed pulse")
            if event in (CASTING, CAST_SUCCEEDED):
                self.assertEqual(kit.scope, 0, f"{name}: cast events have no target list")

    def test_particle_files_exist_and_pass_the_validator(self):
        import inspect_hpar
        import hpar
        data_root = str(ROOT / "data/client")
        for name, event, kit in self.kits:
            for particle in kit.particles:
                path = ROOT / "data/client" / particle
                self.assertTrue(path.is_file(), f"{name}: missing particle {particle}")
                loops = any(e.loop for e in hpar.load(str(path)).emitters)
                if loops:
                    self.assertIn(particle, self.looping,
                                  f"{name}: {particle} loops but is not in LOOPING_EFFECTS")
                    self.assertIn(event, (CASTING, AURA_IDLE),
                                  f"{name}: looping {particle} on event {event} is never torn down")
                with contextlib.redirect_stdout(io.StringIO()):
                    problems = inspect_hpar.check(str(path), one_shot=not loops, data_root=data_root)
                self.assertEqual(problems, [], f"{name}: {particle} failed validation: {problems}")

    def test_every_particle_uses_a_translucent_material(self):
        import hpar
        for name, _, kit in self.kits:
            for particle in kit.particles:
                for emitter in hpar.load(str(ROOT / "data/client" / particle)).emitters:
                    self.assertIn(emitter.material_name, SAFE_PARTICLE_MATERIALS,
                                  f"{name}: {emitter.name!r} in {particle} uses "
                                  f"{emitter.material_name!r}")

    def test_new_sprite_materials_are_translucent_without_depth_write(self):
        # The ATTR chunk is what keeps a particle material from rendering opaque.
        for material in ("Particle_Sigil", "Particle_Rays", "Particle_Feather", "Particle_Flame"):
            data = (ROOT / "data/client/Particles" / (material + ".hmi")).read_bytes()
            attr = data.index(b"ATTR")
            payload = data[attr + 8:attr + 14]
            self.assertEqual(payload[3], 3, f"{material}: not typed Translucent")
            self.assertEqual(payload[4], 0, f"{material}: depth write is on")
            texture = data[data.index(b"Particles/T_Particle_"):].decode("ascii")
            self.assertTrue((ROOT / "data/client" / texture).is_file(), f"{material}: missing {texture}")


if __name__ == "__main__":
    unittest.main()
