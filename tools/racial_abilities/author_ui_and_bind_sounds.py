# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the UI fanfare sound entries (level up, quest accepted, quest completed), the Bind
spell sound entry and the Bind spell visualization, and link spell 17 to it.

    py tools/racial_abilities/author_ui_and_bind_sounds.py            # validate only
    py tools/racial_abilities/author_ui_and_bind_sounds.py --apply    # write editor + ClientDB

The UI sounds are 2D, category INTERFACE, and are played from Lua (``PlaySoundEntry``) in
``GameUI/UiSounds.lua`` on PLAYER_LEVEL_UP / QUEST_ACCEPTED / QUEST_REWARDED. Keep the ids in
sync with that file. Level up is deliberately NOT a spell-visual sound: the level-up visual is
broadcast to everyone nearby, and a 2D fanfare must only play for the player who levelled.
"""

import argparse
import shutil
import sys
from datetime import datetime
from pathlib import Path

from google.protobuf import json_format

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_human_racials as ahr  # noqa: E402
import author_racial_visuals as arv  # noqa: E402

ROOT = ahr.ROOT
BIND_SPELL = 17
BIND_VIS_ID = 98
CAST_SUCCEEDED = "3"
BIND_PARTICLES = "Particles/Bind/BindCast.hpar"
HEARTH_ORANGE = (1.00, 0.70, 0.30)

# id, name, file, category (3 = INTERFACE), is_3d, volume, min/max distance
SOUNDS = [
    (169, "UI - Level Up", "Sound/Interface/LevelUp.wav", 3, False, 1.0),
    (170, "UI - Quest Accepted", "Sound/Interface/QuestAccepted.wav", 3, False, 0.8),
    (171, "UI - Quest Completed", "Sound/Interface/QuestCompleted.wav", 3, False, 0.8),
    (172, "Spell - Bind", "Sound/Spells/Bind/Bind.wav", 0, True, 0.95),
]


def populate(entry, spec):
    sid, name, path, category, is_3d, volume = spec
    entry.Clear()
    entry.id = sid
    entry.name = name
    entry.files.append(path)
    entry.category = category
    entry.is_3d = is_3d
    entry.looped = False
    entry.stream = False
    entry.volume = volume
    entry.pitch_min = 1.0
    entry.pitch_max = 1.0
    if is_3d:
        entry.pitch_min, entry.pitch_max = 0.98, 1.02
        entry.min_distance = 8.0
        entry.max_distance = 40.0


def bind_visualization():
    return {
        "id": BIND_VIS_ID,
        "name": "Bind",
        "kits_by_event": {
            CAST_SUCCEEDED: {"kits": [
                {"scope": "CASTER", "loop": False, "animation_name": "CastRelease"},
                {"scope": "CASTER", "loop": False, "particles": [BIND_PARTICLES],
                 "sound_ids": [172]},
                {"scope": "CASTER", "loop": False, "attach_bone": "spine_03",
                 "light": arv.light(HEARTH_ORANGE, 2.4, 8.0, 0.1, 1.4),
                 "tint": {"r": HEARTH_ORANGE[0], "g": HEARTH_ORANGE[1], "b": HEARTH_ORANGE[2],
                          "a": 0.4, "duration_ms": 900}},
            ]},
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    for spec in SOUNDS:
        ahr._require((ROOT / "data/client" / spec[2]).is_file(), f"missing {spec[2]}")
    ahr._require((ROOT / "data/client" / BIND_PARTICLES).is_file(), f"missing {BIND_PARTICLES}")

    sounds = arv.load_editor_sounds()
    visuals = arv.load_editor_visuals()
    spells = ahr.load_editor("spells")

    by_id = {e.id: e for e in sounds.entry}
    for spec in SOUNDS:
        target = by_id.get(spec[0])
        if target is not None:
            ahr._require(target.name == spec[1],
                         f"sound id {spec[0]} is already taken by {target.name!r}")
        else:
            target = sounds.entry.add()
        populate(target, spec)

    target = next((v for v in visuals.entry if v.id == BIND_VIS_ID), None)
    if target is not None:
        ahr._require(target.name == "Bind", f"visualization {BIND_VIS_ID} taken by {target.name!r}")
    else:
        target = visuals.entry.add()
    target.Clear()
    json_format.ParseDict(bind_visualization(), target)

    spell = next((s for s in spells.entry if s.id == BIND_SPELL), None)
    ahr._require(spell is not None and spell.name == "Bind", "spell 17 'Bind' missing")
    spell.visualization_id = BIND_VIS_ID

    datasets = (("sounds", sounds), ("spell_visualizations", visuals), ("spells", spells))
    for name, dataset in datasets:
        ids = [e.id for e in dataset.entry]
        ahr._require(len(ids) == len(set(ids)), f"duplicate ids in {name}")
        ahr._require(dataset.IsInitialized(), f"{name} dataset is missing required fields")

    if not args.apply:
        print("validated sounds 169-172, bind visualization and spell 17 link")
        return

    ahr.OUT.mkdir(parents=True, exist_ok=True)
    backup = ahr.OUT / ("backup_ui_bind_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for name, _ in datasets:
        shutil.copy2(ahr.editor_path(name), backup / f"editor_{name}.data")
        shutil.copy2(ahr.client_path(name), backup / f"client_{name}.data")
    for name, dataset in datasets:
        blob = dataset.SerializeToString()
        ahr.editor_path(name).write_bytes(blob)
        ahr.client_path(name).write_bytes(blob)
    print(f"wrote sounds 169-172, visualization {BIND_VIS_ID}, spell 17 link. Backup: {backup}")


if __name__ == "__main__":
    main()
