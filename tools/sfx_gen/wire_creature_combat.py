# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Wire the creature and NPC combat sounds (aggro, attack, pain, death) into the game data.

Prompts and casting live in ``recipes/creature_combat.py``; this script only turns the picked
takes into SoundEntries and ``ModelDataEntry.combat_sounds``. Idempotent: it owns a fixed id
range and fixed fields, so re-running after re-picking takes changes nothing else.

Writes BOTH copies of every client table it touches -- ``data/editor/data/<t>.data`` and
``data/client/ClientDB/<t>.data`` are byte-identical for these tables because the mirrored
schemas share field numbers. ``units.data`` is server-only and lives in the editor copy alone.

Run:  python tools/sfx_gen/wire_creature_combat.py [--dry-run]
"""

from __future__ import annotations

import argparse
import importlib
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1].parent
EDITOR_DATA = REPO / "data" / "editor" / "data"
CLIENT_DATA = REPO / "data" / "client" / "ClientDB"
CLIENT_ROOT = REPO / "data" / "client"
LOCALE_ROOT = CLIENT_ROOT / "Locales" / "Locale_enUS"

sys.path.insert(0, str(REPO / ".claude" / "skills" / "mmo-item-designer" / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent / "recipes"))
from proto_runtime import compile_proto_modules  # noqa: E402

import creature_combat  # noqa: E402

SOUND_EFFECTS = 0   # SoundEntryCategory.SOUND_EFFECTS
VOICE = 4           # SoundEntryCategory.VOICE

# --- Animal banks --------------------------------------------------------------------------
# One set of files per species, but one set of SoundEntries per *size*: pitch is a property of
# the entry, so a piglet, a grown boar and the Grimtusk rare share recordings and still sound
# like three different animals. (bank name, creature, pitch min, pitch max, first entry id)
# Entry ids are first_id + EVENTS index (Aggro, Attack, Pain, Death).
ANIMAL_BANKS = [
    ("Young Boar", "Boar", 1.10, 1.18, 173),
    ("Boar", "Boar", 0.95, 1.05, 177),
    ("Grimtusk", "Boar", 0.78, 0.86, 181),
    ("Rat", "Rat", 0.95, 1.05, 185),
    ("Wolf", "Wolf", 0.94, 1.06, 189),
]

# --- Human NPC banks: (bank name, recipe path prefix, file prefix, first entry id) ----------
VOICE_BANKS = [
    ("NPC Human Male", creature_combat.MALE, "NpcHumanMale", 193),
    ("NPC Human Female", creature_combat.FEMALE, "NpcHumanFemale", 197),
]
# The recipe's "Attack" event is spoken as an effort grunt.
VOICE_EVENT_FILES = {"Aggro": "Aggro", "Attack": "Effort", "Pain": "Pain", "Death": "Death"}

# --- Models -> bank ------------------------------------------------------------------------
MODEL_BANKS = {
    1: "Young Boar",           # Young Forest Boar
    10: "Boar",                # Forest Boar
    9: "Grimtusk",             # Grimtusk, the Ironback
    11: "Rat",                 # Cellar Rat, Bog Rat
    20: "Wolf", 21: "Wolf", 22: "Wolf", 23: "Wolf",
    # Generic human NPCs: bandits, cutthroats, poachers, guards, townsfolk. Models 14 and 16
    # (Clarissa, Elira) have their own cast voices and stay silent rather than borrow one.
    2: "NPC Human Male", 4: "NPC Human Male", 24: "NPC Human Male", 26: "NPC Human Male",
    28: "NPC Human Male", 30: "NPC Human Male", 32: "NPC Human Male", 33: "NPC Human Male",
    3: "NPC Human Female", 25: "NPC Human Female", 27: "NPC Human Female",
    29: "NPC Human Female", 31: "NPC Human Female", 34: "NPC Human Female",
    35: "NPC Human Female",
}

# The Cellar Rat unit used the Young Forest Boar model (1). Model 11 "Cellar Rat" is
# byte-identical apart from its name and is what the rat was always meant to use; pointing
# the unit at it gives the rat rat sounds without touching how it looks.
UNIT_MODEL_FIXES = {28: 11}

# Play chances in percent. Animals vocalise more readily than people: a boar grunts on
# nearly every charge, whereas a human grunting on every swing sounds like a parody.
ANIMAL_CHANCES = dict(attack=30, attack_crit=75, hit=40, hit_crit=90)
HUMAN_CHANCES = dict(attack=18, attack_crit=65, hit=30, hit_crit=80)  # = player barks
VOICE_MIN_INTERVAL_MS = 2500


def load_modules():
    compile_proto_modules(REPO)
    return {
        "sounds": importlib.import_module("sounds_pb2"),
        "model_data": importlib.import_module("model_data_pb2"),
        "units": importlib.import_module("units_pb2"),
    }


def read(msg_cls, name):
    msg = msg_cls()
    msg.ParseFromString((EDITOR_DATA / f"{name}.data").read_bytes())
    return msg


def write(msg, name, dry_run, client=True):
    blob = msg.SerializeToString()
    targets = [EDITOR_DATA] + ([CLIENT_DATA] if client else [])
    if dry_run:
        print(f"    [dry-run] would write {name}.data ({len(blob)} bytes)")
        return
    for target in targets:
        (target / f"{name}.data").write_bytes(blob)
    print(f"    wrote {name}.data ({len(blob)} bytes) to {'editor + ClientDB' if client else 'editor'}")


def upsert(container, entry_id):
    for entry in container.entry:
        if entry.id == entry_id:
            return entry
    entry = container.entry.add()
    entry.id = entry_id
    return entry


def existing(paths, root):
    """Shuffle-bag files that exist on disk, in order. A bank may have lost a take to the
    audition; a missing file must not be referenced."""
    found = [p for p in paths if (root / p).is_file()]
    if not found:
        raise SystemExit(f"no files found for bank: {paths[0]} ...")
    return found


def set_entry(sounds, sid, name, files, category, pitch_min, pitch_max, max_distance):
    entry = upsert(sounds, sid)
    entry.name = name
    entry.ClearField("files")
    entry.files.extend(files)
    entry.category = category
    entry.is_3d = True
    entry.looped = False
    entry.stream = False
    entry.volume = 1.0
    entry.pitch_min = pitch_min
    entry.pitch_max = pitch_max
    entry.min_distance = 5.0
    entry.max_distance = max_distance
    print(f"    {sid:3} {name:36} ({len(files)} files)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    mods = load_modules()

    bank_ids = {}   # bank name -> {event: sound id}
    bank_chances = {}

    print("sound entries:")
    sounds = read(mods["sounds"].Sounds, "sounds")
    for name, creature, pmin, pmax, first in ANIMAL_BANKS:
        ids = {}
        for i, event in enumerate(creature_combat.EVENTS):
            files = existing([creature_combat.creature_path(creature, event, n) for n in (1, 2, 3, 4)],
                             CLIENT_ROOT)
            # Aggro is the "you pulled something" cue and has to reach the edge of the fight.
            max_distance = 50.0 if event == "Aggro" else 40.0
            set_entry(sounds, first + i, f"{name} - Combat - {event}", files, SOUND_EFFECTS,
                      pmin, pmax, max_distance)
            ids[event] = first + i
        bank_ids[name] = ids
        bank_chances[name] = ANIMAL_CHANCES

    for name, prefix, file_prefix, first in VOICE_BANKS:
        ids = {}
        for i, event in enumerate(creature_combat.EVENTS):
            stem = VOICE_EVENT_FILES[event]
            files = existing([f"{prefix}{file_prefix}_{stem}_{n:02d}.wav" for n in (1, 2, 3, 4)],
                             LOCALE_ROOT)
            max_distance = 50.0 if event == "Aggro" else 40.0
            # No pitch scatter on a human voice: it reads as processing, not variation.
            set_entry(sounds, first + i, f"{name} - Combat - {stem}", files, VOICE,
                      1.0, 1.0, max_distance)
            ids[event] = first + i
        bank_ids[name] = ids
        bank_chances[name] = HUMAN_CHANCES
    write(sounds, "sounds", args.dry_run)

    print("models:")
    models = read(mods["model_data"].ModelDatas, "model_data")
    for entry in models.entry:
        bank = MODEL_BANKS.get(entry.id)
        if not bank:
            continue
        ids = bank_ids[bank]
        chances = bank_chances[bank]
        combat = entry.combat_sounds
        combat.attack_voice_sound = ids["Attack"]
        combat.attack_voice_chance = chances["attack"]
        # No dedicated crit takes: a crit plays the same bank, just far more reliably.
        combat.attack_crit_voice_sound = ids["Attack"]
        combat.attack_crit_voice_chance = chances["attack_crit"]
        combat.hit_voice_sound = ids["Pain"]
        combat.hit_voice_chance = chances["hit"]
        combat.crit_hit_voice_sound = ids["Pain"]
        combat.crit_hit_voice_chance = chances["hit_crit"]
        combat.voice_min_interval_ms = VOICE_MIN_INTERVAL_MS
        combat.aggro_sound = ids["Aggro"]
        combat.death_sound = ids["Death"]
        print(f"    {entry.id:3} {entry.name:32} -> {bank}")
    write(models, "model_data", args.dry_run)

    print("units:")
    units = read(mods["units"].Units, "units")
    for entry in units.entry:
        if entry.id in UNIT_MODEL_FIXES:
            if entry.maleModel != UNIT_MODEL_FIXES[entry.id]:
                print(f"    {entry.id:3} {entry.name:32} model {entry.maleModel} -> {UNIT_MODEL_FIXES[entry.id]}")
            entry.maleModel = UNIT_MODEL_FIXES[entry.id]
    write(units, "units", args.dry_run, client=False)

    print("\ndone.")


if __name__ == "__main__":
    main()
