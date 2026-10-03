# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the cleric spell sound catalog entries (ids 120-134) into sounds.data.

Ids 120-134 deliberately leave a gap after the mage set (82-102, feature/mage-spell-visuals),
which was authored in parallel; the upsert refuses to overwrite an id another class owns.

Writes both the editor dataset and the client ClientDB copy. Idempotent: re-running replaces
the same ids rather than appending duplicates. The files come from the prompt table in
tools/sfx_gen/recipes/cleric.py (generated via the ElevenLabs connector, postprocessed with
tools/sfx_gen/fetch.py).

    python tools/cleric_visuals/author_sounds.py            # validate only
    python tools/cleric_visuals/author_sounds.py --apply    # write both datasets

The three cast-bar sounds are catalog entries with ``looped`` set: the visualization
service only plays a looping channel for a looping kit when the entry itself loops. They
run quieter than the one-shots so a 10 s Resurrection channel does not mask combat.
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

OUT = ROOT / "generated/cleric_visuals"
SOUND_DIR = "Sound/Spells/Cleric/"

# id, name, files, looped, pitch_min, pitch_max, volume
# Ticks fire every 2-3 s for the length of an aura, so they get the widest pitch range and
# the lowest volume -- they are the sounds that must never fatigue.
ENTRIES = [
    (120, "Cleric - Holy Cast Loop", ["HolyCastLoop.wav"], True, 1.0, 1.0, 0.55),
    (121, "Cleric - Holy Fire Cast Loop", ["HolyFireCastLoop.wav"], True, 1.0, 1.0, 0.55),
    (122, "Cleric - Resurrection Channel", ["ResurrectionChannel.wav"], True, 1.0, 1.0, 0.60),
    (123, "Cleric - Holy Release", ["HolyRelease.wav"], False, 0.95, 1.05, 0.80),
    (124, "Cleric - Healing Light", ["HealingLight.wav"], False, 0.97, 1.03, 0.90),
    (125, "Cleric - Smite", ["Smite.wav"], False, 0.95, 1.05, 1.00),
    (126, "Cleric - Holy Fire", ["HolyFire.wav"], False, 0.97, 1.03, 0.95),
    (127, "Cleric - Holy Fire Tick", ["HolyFireTick.wav"], False, 0.90, 1.10, 0.65),
    (128, "Cleric - Divine Vitality", ["DivineVitality.wav"], False, 0.98, 1.02, 0.90),
    (129, "Cleric - Renewing Light", ["RenewingLight.wav"], False, 0.97, 1.03, 0.85),
    (130, "Cleric - Renewing Light Tick", ["RenewingLightTick.wav"], False, 0.90, 1.10, 0.60),
    (131, "Cleric - Healing Aura", ["HealingAura.wav"], False, 0.98, 1.02, 0.90),
    (132, "Cleric - Protective Aura", ["ProtectiveAura.wav"], False, 0.98, 1.02, 0.90),
    (133, "Cleric - Faithward", ["Faithward.wav"], False, 0.96, 1.04, 0.85),
    (134, "Cleric - Resurrection", ["Resurrection.wav"], False, 1.0, 1.0, 1.00),
]


def load_type(schema_dir, proto_name, message_name):
    with tempfile.TemporaryDirectory(prefix="cleric_sounds_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", proto_name],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for f in file_set.file:
        pool.Add(f)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


def populate(entry, spec):
    entry_id, name, files, looped, pitch_min, pitch_max, volume = spec
    entry.Clear()
    entry.id = entry_id
    entry.name = name
    for f in files:
        entry.files.append(SOUND_DIR + f)
    entry.category = 0          # SOUND_EFFECTS
    entry.is_3d = True
    entry.looped = looped
    entry.stream = False
    entry.volume = volume
    entry.pitch_min = pitch_min
    entry.pitch_max = pitch_max
    # Same audible range as the warrior ability entries.
    entry.min_distance = 5.0
    entry.max_distance = 30.0


def upsert(dataset):
    by_id = {e.id: e for e in dataset.entry}
    for spec in ENTRIES:
        target = by_id.get(spec[0])
        if target is not None and not target.name.startswith("Cleric - "):
            raise SystemExit(f"sound id {spec[0]} is already taken by {target.name!r}")
        if target is None:
            target = dataset.entry.add()
        populate(target, spec)
    if len({e.id for e in dataset.entry}) != len(dataset.entry):
        raise SystemExit("duplicate sound ids")
    if not dataset.IsInitialized():
        raise SystemExit("sounds dataset is missing required fields after upsert")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    for spec in ENTRIES:
        for f in spec[2]:
            path = ROOT / "data/client" / SOUND_DIR / f
            if not path.is_file():
                raise SystemExit(f"missing sound file: {path}")

    targets = [
        (ROOT / "data/editor/data/sounds.data",
         load_type(ROOT / "src/shared/proto_data", "sounds.proto", "mmo.proto.Sounds")),
        (ROOT / "data/client/ClientDB/sounds.data",
         load_type(ROOT / "src/shared/client_data", "sounds.proto", "mmo.proto_client.Sounds")),
    ]

    # Parse and validate every dataset before writing any, so a failure on the second can
    # never leave the two data submodules diverged.
    datasets = []
    for path, message_type in targets:
        dataset = message_type.FromString(path.read_bytes())
        upsert(dataset)
        datasets.append(dataset)

    if not args.apply:
        print(f"validated {len(ENTRIES)} cleric sound entries (ids 120-134) in both datasets")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_sounds_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for index, (path, _) in enumerate(targets):
        shutil.copy2(path, backup / f"{index}_{path.name}")

    for (path, _), dataset in zip(targets, datasets):
        path.write_bytes(dataset.SerializeToString())

    print(f"wrote {len(ENTRIES)} cleric sound entries (ids 120-134) to both datasets. "
          f"Backup: {backup}")


if __name__ == "__main__":
    main()
