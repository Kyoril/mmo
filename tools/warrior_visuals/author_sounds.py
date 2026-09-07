# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the warrior ability sound catalog entries (ids 21-33) into sounds.data.

Writes both the editor dataset and the client ClientDB copy. Idempotent: re-running
replaces the same ids rather than appending duplicates.

    python tools/warrior_visuals/author_sounds.py            # validate only
    python tools/warrior_visuals/author_sounds.py --apply    # write both datasets
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

OUT = ROOT / "generated/warrior_visuals"
SOUND_DIR = "Sound/Spells/Warrior/"

# id, name, files, pitch_min, pitch_max, volume
# Strike fires on every warrior swing and on eight creature types, so it carries three
# files and the widest pitch range -- it is the one sound that must never fatigue.
ENTRIES = [
    (21, "Warrior - Strike", ["Strike01.wav", "Strike02.wav", "Strike03.wav"], 0.92, 1.08, 0.85),
    (22, "Warrior - Rend", ["Rend.wav"], 0.96, 1.04, 0.90),
    (23, "Warrior - Execute", ["Execute.wav"], 0.97, 1.03, 1.00),
    (24, "Warrior - Skullbash", ["Skullbash.wav"], 0.95, 1.05, 0.90),
    (25, "Warrior - Shield Slam", ["ShieldSlam.wav"], 0.96, 1.04, 0.95),
    (26, "Warrior - Cleave", ["Cleave.wav"], 0.96, 1.04, 0.90),
    (27, "Warrior - Charge", ["Charge.wav"], 0.98, 1.02, 0.90),
    (28, "Warrior - Shockwave", ["Shockwave.wav"], 0.98, 1.02, 1.00),
    (29, "Warrior - Battlecry", ["Battlecry.wav"], 0.98, 1.02, 0.85),
    (30, "Warrior - Bloodrush", ["Bloodrush.wav"], 0.98, 1.02, 0.80),
    (31, "Warrior - Provoke", ["Provoke.wav"], 0.97, 1.03, 0.85),
    (32, "Warrior - Demoralizing Shout", ["DemoralizingShout.wav"], 0.98, 1.02, 0.85),
    (33, "Warrior - Last Stand", ["LastStand.wav"], 0.98, 1.02, 0.85),
]


def load_type(schema_dir, proto_name, message_name):
    with tempfile.TemporaryDirectory(prefix="warrior_sounds_") as tmp:
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
    entry_id, name, files, pitch_min, pitch_max, volume = spec
    entry.Clear()
    entry.id = entry_id
    entry.name = name
    for f in files:
        entry.files.append(SOUND_DIR + f)
    entry.category = 0          # SOUND_EFFECTS
    entry.is_3d = True
    entry.looped = False
    entry.stream = False
    entry.volume = volume
    entry.pitch_min = pitch_min
    entry.pitch_max = pitch_max
    # Matches the distances the visualization service previously hardcoded at its
    # PlaySound3D call site, so audible range does not change.
    entry.min_distance = 5.0
    entry.max_distance = 30.0


def upsert(dataset):
    by_id = {e.id: e for e in dataset.entry}
    for spec in ENTRIES:
        target = by_id.get(spec[0])
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

    # Parse and validate every dataset before writing any of them, so a failure on the
    # second dataset can never leave the first one written and the two data submodules
    # diverged with no backup and no rollback.
    datasets = []
    for path, message_type in targets:
        dataset = message_type.FromString(path.read_bytes())
        upsert(dataset)
        datasets.append(dataset)

    if not args.apply:
        print(f"validated {len(ENTRIES)} warrior sound entries (ids 21-33) in both datasets")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_sounds_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for index, (path, _) in enumerate(targets):
        shutil.copy2(path, backup / f"{index}_{path.name}")

    for (path, _), dataset in zip(targets, datasets):
        path.write_bytes(dataset.SerializeToString())

    print(f"wrote {len(ENTRIES)} warrior sound entries (ids 21-33) to both datasets. "
          f"Backup: {backup}")


if __name__ == "__main__":
    main()
