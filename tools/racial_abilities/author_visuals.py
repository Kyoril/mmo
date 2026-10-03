# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the Call of the Watch sound entry (135) and visualization (78), and link spell 250.

    py tools/racial_abilities/author_visuals.py            # validate only
    py tools/racial_abilities/author_visuals.py --apply    # write editor + ClientDB

ClientDB receives byte copies of the editor blobs (what mmo_edit's ExportToClient does).

Design notes that are easy to undo by accident:

* The spell is instant: only CAST_SUCCEEDED (3) fires on the caster, so it carries the
  animation, the activation effect, the light and the sound.
* AURA_APPLIED (5) plays on every buffed party member and replays whenever a unit with the
  aura comes into view -- so it carries particles only, never a sound.
* EmoteRoar exists on both human rigs; no kit sets duration_ms (it time-warps the clip).
"""

import argparse
import shutil
import sys
from datetime import datetime
from pathlib import Path

from google.protobuf import json_format

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_human_racials as ahr  # noqa: E402

ROOT = ahr.ROOT
OUT = ahr.OUT
CAST_SUCCEEDED, AURA_APPLIED = "3", "5"

SOUND_FILE = "Sound/Spells/Human/CallOfTheWatch.wav"
ACTIVATE = "Particles/Human/CallOfTheWatchActivate.hpar"
APPLY = "Particles/Human/CallOfTheWatchApply.hpar"
WATCH_GOLD = (1.00, 0.78, 0.30)

VIS_NAME = "Human - Call of the Watch"
SOUND_NAME = "Human - Call of the Watch"


def light(colour, intensity, rng, fade_in, fade_out):
    return {"r": colour[0], "g": colour[1], "b": colour[2], "intensity": intensity,
            "range": rng, "fade_in_time": fade_in, "fade_out_time": fade_out}


def visualization():
    return {
        "id": ahr.VIS_ID,
        "name": VIS_NAME,
        "kits_by_event": {
            CAST_SUCCEEDED: {"kits": [
                {"scope": "CASTER", "loop": False, "animation_name": "EmoteRoar"},
                {"scope": "CASTER", "loop": False, "particles": [ACTIVATE],
                 "sound_ids": [ahr.SOUND_ID]},
                {"scope": "CASTER", "loop": False, "attach_bone": "spine_03",
                 "light": light(WATCH_GOLD, 2.6, 9.0, 0.05, 1.0),
                 "tint": {"r": WATCH_GOLD[0], "g": WATCH_GOLD[1], "b": WATCH_GOLD[2],
                          "a": 0.45, "duration_ms": 700}},
            ]},
            AURA_APPLIED: {"kits": [
                {"scope": "TARGET", "loop": False, "particles": [APPLY]},
            ]},
        },
    }


def populate_sound(entry):
    entry.Clear()
    entry.id = ahr.SOUND_ID
    entry.name = SOUND_NAME
    entry.files.append(SOUND_FILE)
    entry.category = 0          # SOUND_EFFECTS
    entry.is_3d = True
    entry.looped = False
    entry.stream = False
    entry.volume = 0.95
    entry.pitch_min = 0.98
    entry.pitch_max = 1.02
    # A rallying call is heard further than a single strike.
    entry.min_distance = 8.0
    entry.max_distance = 40.0


def load_editor_sounds():
    message = ahr.load_type(ROOT / "src/shared/proto_data", ["sounds.proto"], "mmo.proto.Sounds")
    return message.FromString(ahr.editor_path("sounds").read_bytes())


def load_editor_visuals():
    message = ahr.load_type(ROOT / "src/shared/proto_data", ["spell_visualizations.proto"],
                            "mmo.proto.SpellVisualizations")
    return message.FromString(ahr.editor_path("spell_visualizations").read_bytes())


def upsert_sound(sounds):
    by_id = {e.id: e for e in sounds.entry}
    target = by_id.get(ahr.SOUND_ID)
    if target is not None:
        ahr._require(target.name == SOUND_NAME,
                     f"sound id {ahr.SOUND_ID} is already taken by {target.name!r}")
    else:
        target = sounds.entry.add()
    populate_sound(target)


def upsert_visual(visuals):
    by_id = {v.id: v for v in visuals.entry}
    target = by_id.get(ahr.VIS_ID)
    if target is not None:
        ahr._require(target.name == VIS_NAME,
                     f"visualization {ahr.VIS_ID} is already taken by {target.name!r}")
    else:
        target = visuals.entry.add()
    target.Clear()
    json_format.ParseDict(visualization(), target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    for path in (SOUND_FILE, ACTIVATE, APPLY):
        ahr._require((ROOT / "data/client" / path).is_file(), f"missing {path}")

    sounds = load_editor_sounds()
    visuals = load_editor_visuals()
    spells = ahr.load_editor("spells")

    upsert_sound(sounds)
    upsert_visual(visuals)
    spell = next((s for s in spells.entry if s.id == ahr.CALL_OF_THE_WATCH), None)
    ahr._require(spell is not None, "spell 250 missing -- run author_human_racials.py first")
    spell.visualization_id = ahr.VIS_ID

    for name, dataset in (("sounds", sounds), ("spell_visualizations", visuals),
                          ("spells", spells)):
        ids = [e.id for e in dataset.entry]
        ahr._require(len(ids) == len(set(ids)), f"duplicate ids in {name}")
        ahr._require(dataset.IsInitialized(), f"{name} dataset is missing required fields")

    if not args.apply:
        print(f"validated sound {ahr.SOUND_ID}, visualization {ahr.VIS_ID}, spell link")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_vis_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    datasets = (("sounds", sounds), ("spell_visualizations", visuals), ("spells", spells))
    for name, _ in datasets:
        shutil.copy2(ahr.editor_path(name), backup / f"editor_{name}.data")
        shutil.copy2(ahr.client_path(name), backup / f"client_{name}.data")
    for name, dataset in datasets:
        blob = dataset.SerializeToString()
        ahr.editor_path(name).write_bytes(blob)
        ahr.client_path(name).write_bytes(blob)

    print(f"wrote sound {ahr.SOUND_ID}, visualization {ahr.VIS_ID} and the spell 250 link "
          f"to editor + ClientDB. Backup: {backup}")


if __name__ == "__main__":
    main()
