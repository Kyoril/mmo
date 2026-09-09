# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the Open-spell visualization kits (ids 13, 14, 41) and their spell links.

Writes both the editor dataset and the client ClientDB copy, for spell_visualizations.data
AND spells.data. Idempotent: re-running replaces the same ids rather than appending.

    python tools/open_spell_visuals/author_open_visuals.py            # validate only
    python tools/open_spell_visuals/author_open_visuals.py --apply    # write both trees

Design notes that are easy to undo by accident:

* No kit sets duration_ms. ApplyAnimationToActor computes
  playRate = clipLength / duration_ms, so any value time-warps the clip.
* 520 ms is where UseStart ends (0.567 s male, 0.500 s female). One number has to serve
  both rigs, so the loop kit and the door's UseEnd kit both wait 520 ms.
* Spell 235 "Open Door" is instant. WorldState::OnSpellStart gates START_CAST and CASTING
  on castTime > 0, so an instant spell only ever sees CAST_SUCCEEDED -- hence a separate
  visualization (41) that carries the whole sequence on that one event.
* Visualization 13 rides the Use clips as a placeholder until pickaxe animations exist.
  It keeps its id so the swap stays data-only.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, json_format, message_factory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

OUT = ROOT / "generated/open_spell_visuals"

# Event ids: 2 = CASTING, 3 = CAST_SUCCEEDED.
CASTING, CAST_SUCCEEDED = "2", "3"

# Where UseStart ends. 0.567 s on HumanMale, 0.500 s on HumanFemale_V2.
USE_START_MS = 520

HUMAN_SKELETONS = [
    ROOT / "data/client/Models/Character/Human/Male/HumanMale.skel",
    ROOT / "data/client/Models/Character/Human/Female/HumanFemale_V2.skel",
]


def _require(condition, message):
    """Like assert, but survives ``python -O`` -- these guards gate writing game data, so
    they must not disappear when assertions are stripped."""
    if not condition:
        raise SystemExit(message)


def one_shot(clip, delay=0):
    kit = {"scope": "CASTER", "loop": False, "animation_name": clip}
    if delay:
        kit["delay_ms"] = delay
    return kit


def looped(clip, delay=0):
    kit = {"scope": "CASTER", "loop": True, "animation_name": clip}
    if delay:
        kit["delay_ms"] = delay
    return kit


def definitions():
    """(visualization id, name, spell ids that link it, kits_by_event)."""
    channelled = {
        CASTING: {"kits": [one_shot("UseStart"), looped("UseLoop", USE_START_MS)]},
        CAST_SUCCEEDED: {"kits": [one_shot("UseEnd")]},
    }
    return [
        (13, "Mining", [157], channelled),
        (14, "Open", [39, 112], channelled),
        # Instant: only CAST_SUCCEEDED ever fires, so it carries the whole sequence.
        (41, "Open Door", [235], {
            CAST_SUCCEEDED: {"kits": [one_shot("UseStart"),
                                      one_shot("UseEnd", USE_START_MS)]}}),
    ]


def clip_names(skeleton_path):
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


def load_type(schema_dir, protos, message_name):
    with tempfile.TemporaryDirectory(prefix="open_vis_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


def upsert_visuals(dataset, drafts):
    by_id = {vis.id: vis for vis in dataset.entry}
    for draft in drafts:
        target = by_id.get(draft["id"])
        if target is None:
            target = dataset.entry.add()
        else:
            _require(target.name in (draft["name"], "Open", "Mining"),
                     f"visualization {draft['id']} is named {target.name!r}, refusing to "
                     f"overwrite it with {draft['name']!r}")
            target.Clear()
        json_format.ParseDict(draft, target)


def link_spells(dataset, mapping):
    by_id = {spell.id: spell for spell in dataset.entry}
    for spell_id, vis_id in mapping.items():
        spell = by_id.get(spell_id)
        _require(spell is not None, f"spell {spell_id} is missing from the dataset")
        spell.visualization_id = vis_id


def validate(visuals, spells, mapping, available_clips):
    ids = [vis.id for vis in visuals.entry]
    _require(len(ids) == len(set(ids)), "duplicate visualization ids")

    authored = {draft_id for draft_id, _, _, _ in definitions()}
    for vis in visuals.entry:
        if vis.id not in authored:
            continue
        for kit_list in vis.kits_by_event.values():
            for kit in kit_list.kits:
                _require(kit.animation_name,
                         f"visualization {vis.id}: kit has no animation_name")
                _require(kit.animation_name in available_clips,
                         f"visualization {vis.id}: clip {kit.animation_name!r} is not on "
                         f"both human rigs")
                _require(not kit.HasField("duration_ms"),
                         f"visualization {vis.id}: duration_ms time-warps the clip")
                _require(not kit.sounds and not kit.sound_ids,
                         f"visualization {vis.id}: Open kits carry no audio")

    by_id = {spell.id: spell for spell in spells.entry}
    for spell_id, vis_id in mapping.items():
        _require(by_id[spell_id].visualization_id == vis_id,
                 f"spell {spell_id} points at {by_id[spell_id].visualization_id}, "
                 f"expected {vis_id}")

    _require(visuals.IsInitialized(), "visualizations dataset is missing required fields")
    _require(spells.IsInitialized(), "spells dataset is missing required fields")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    available = None
    for skeleton in HUMAN_SKELETONS:
        _require(skeleton.is_file(), f"missing skeleton {skeleton}")
        names = clip_names(skeleton)
        available = names if available is None else (available & names)

    drafts, mapping = [], {}
    for vis_id, name, spell_ids, events in definitions():
        drafts.append({"id": vis_id, "name": name, "kits_by_event": events})
        for spell_id in spell_ids:
            mapping[spell_id] = vis_id

    editor_visuals_type = load_type(ROOT / "src/shared/proto_data",
                                    ["spell_visualizations.proto"],
                                    "mmo.proto.SpellVisualizations")
    editor_spells_type = load_type(ROOT / "src/shared/proto_data", ["spells.proto"],
                                   "mmo.proto.Spells")
    client_visuals_type = load_type(ROOT / "src/shared/client_data",
                                    ["spell_visualizations.proto"],
                                    "mmo.proto_client.SpellVisualizations")
    client_spells_type = load_type(ROOT / "src/shared/client_data", ["spells.proto"],
                                   "mmo.proto_client.Spells")

    paths = [
        ROOT / "data/editor/data/spell_visualizations.data",
        ROOT / "data/editor/data/spells.data",
        ROOT / "data/client/ClientDB/spell_visualizations.data",
        ROOT / "data/client/ClientDB/spells.data",
    ]
    types = [editor_visuals_type, editor_spells_type, client_visuals_type,
             client_spells_type]
    datasets = [message.FromString(path.read_bytes())
                for message, path in zip(types, paths)]

    for index in (0, 2):
        upsert_visuals(datasets[index], drafts)
    for index in (1, 3):
        link_spells(datasets[index], mapping)

    validate(datasets[0], datasets[1], mapping, available)
    validate(datasets[2], datasets[3], mapping, available)

    (OUT / "visualizations.json").write_text(json.dumps(drafts, indent=2) + "\n")

    if not args.apply:
        print(f"validated {len(drafts)} visualizations across {len(mapping)} spells")
        return

    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for index, path in enumerate(paths):
        shutil.copy2(path, backup / f"{index}_{path.name}")

    for dataset, path in zip(datasets, paths):
        path.write_bytes(dataset.SerializeToString())

    print(f"wrote {len(drafts)} visualizations and {len(mapping)} spell links "
          f"to both trees. Backup: {backup}")


if __name__ == "__main__":
    main()
