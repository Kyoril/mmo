# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the Open-spell visualization data invariants.

The Open spells previously pointed at visualizations naming clips ("Open", "Mining") that
exist on no rig, and three of the four did not link a visualization at all. These tests
fail if that state returns. Skips rather than fails when protoc or the data submodules are
unavailable, so a partial checkout does not break the gate.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# spell id -> visualization id. Spells carrying an OpenLock (type 30) effect.
EXPECTED_LINKS = {39: 14, 112: 14, 157: 13, 235: 41}

# Rigs every player character currently uses. A clip named by an Open kit must exist on
# both, or half the player base sees nothing.
HUMAN_SKELETONS = [
    "data/client/Models/Character/Human/Male/HumanMale.skel",
    "data/client/Models/Character/Human/Female/HumanFemale_V2.skel",
]

OPEN_VISUALIZATION_IDS = {13, 14, 41}


def _clip_names(skeleton_path):
    """Animation clip names from a .skel: each MINA chunk holds a 4-byte size, then a
    uint8-prefixed name, then a float duration."""
    data = skeleton_path.read_bytes()
    names = set()
    index = 0
    while True:
        index = data.find(b"MINA", index)
        if index < 0:
            return names
        cursor = index + 8
        if cursor < len(data):
            length = data[cursor]
            if 0 < length < 40:
                raw = data[cursor + 1:cursor + 1 + length]
                if all(32 <= byte < 127 for byte in raw):
                    names.add(raw.decode())
        index += 4


def _load(schema_dir, protos, message_name, data_path):
    from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
    sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
    from proto_runtime import find_protoc

    with tempfile.TemporaryDirectory() as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())

    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)

    message = message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))
    return message.FromString(data_path.read_bytes())


class OpenSpellVisualDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / "data/editor/data/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/editor submodule is not checked out")
        try:
            editor_schema = ROOT / "src/shared/proto_data"
            client_schema = ROOT / "src/shared/client_data"
            cls.visuals = _load(editor_schema, ["spell_visualizations.proto"],
                                "mmo.proto.SpellVisualizations",
                                ROOT / "data/editor/data/spell_visualizations.data")
            cls.spells = _load(editor_schema, ["spells.proto"], "mmo.proto.Spells",
                               ROOT / "data/editor/data/spells.data")
            cls.client_visuals = _load(client_schema, ["spell_visualizations.proto"],
                                       "mmo.proto_client.SpellVisualizations",
                                       ROOT / "data/client/ClientDB/spell_visualizations.data")
            cls.client_spells = _load(client_schema, ["spells.proto"], "mmo.proto_client.Spells",
                                      ROOT / "data/client/ClientDB/spells.data")
        except (ImportError, FileNotFoundError, subprocess.CalledProcessError) as exc:
            # Missing protoc, missing protobuf, or an uninitialised submodule -- environment
            # problems the gate should skip past. A corrupt .data file must NOT land here:
            # this suite exists to catch exactly that, so it has to fail, not skip.
            raise unittest.SkipTest(f"cannot load proto data: {exc}")

        cls.kits = [(vis.id, kit)
                    for vis in cls.visuals.entry if vis.id in OPEN_VISUALIZATION_IDS
                    for kit_list in vis.kits_by_event.values() for kit in kit_list.kits]

    def test_every_open_spell_links_its_visualization(self):
        by_id = {spell.id: spell for spell in self.spells.entry}
        for spell_id, vis_id in EXPECTED_LINKS.items():
            spell = by_id.get(spell_id)
            self.assertIsNotNone(spell, f"spell {spell_id} is missing from spells.data")
            self.assertTrue(spell.HasField("visualization_id"),
                            f"spell {spell_id} ({spell.name}) has no visualization_id")
            self.assertEqual(spell.visualization_id, vis_id,
                             f"spell {spell_id} ({spell.name}) points at "
                             f"{spell.visualization_id}, expected {vis_id}")

    def test_every_open_visualization_exists(self):
        ids = {vis.id for vis in self.visuals.entry}
        for vis_id in OPEN_VISUALIZATION_IDS:
            self.assertIn(vis_id, ids, f"visualization {vis_id} is missing")

    def test_every_kit_names_a_clip_present_on_both_human_rigs(self):
        available = None
        for relative in HUMAN_SKELETONS:
            path = ROOT / relative
            if not path.is_file():
                self.skipTest(f"missing skeleton {relative}")
            names = _clip_names(path)
            available = names if available is None else (available & names)

        self.assertTrue(self.kits, "no Open-spell kits found")
        for vis_id, kit in self.kits:
            self.assertTrue(kit.animation_name,
                            f"visualization {vis_id}: kit has no animation_name")
            self.assertIn(kit.animation_name, available,
                          f"visualization {vis_id}: clip {kit.animation_name!r} is not on "
                          f"both human rigs")

    def test_no_kit_time_warps_its_animation(self):
        # duration_ms becomes playRate = clipLength / duration and speeds the clip up.
        for vis_id, kit in self.kits:
            self.assertFalse(kit.HasField("duration_ms"),
                             f"visualization {vis_id}: duration_ms time-warps the clip")

    def test_editor_and_client_datasets_agree(self):
        # kits_by_event is a protobuf map field, so its serialization order is unspecified;
        # deterministic=True sorts map keys before serializing so this comparison doesn't
        # depend on insertion order happening to match between the editor and ClientDB copies.
        editor = {vis.id: vis.SerializeToString(deterministic=True)
                  for vis in self.visuals.entry if vis.id in OPEN_VISUALIZATION_IDS}
        client = {vis.id: vis.SerializeToString(deterministic=True)
                  for vis in self.client_visuals.entry if vis.id in OPEN_VISUALIZATION_IDS}
        self.assertEqual(editor.keys(), client.keys(),
                         "editor and ClientDB hold different Open visualization ids")
        for vis_id in editor:
            self.assertEqual(editor[vis_id], client[vis_id],
                             f"visualization {vis_id} differs between editor and ClientDB")

        editor_links = {spell.id: spell.visualization_id
                        for spell in self.spells.entry if spell.id in EXPECTED_LINKS}
        client_links = {spell.id: spell.visualization_id
                        for spell in self.client_spells.entry if spell.id in EXPECTED_LINKS}
        self.assertEqual(editor_links, client_links,
                         "spell visualization_id links differ between editor and ClientDB")


if __name__ == "__main__":
    unittest.main()
