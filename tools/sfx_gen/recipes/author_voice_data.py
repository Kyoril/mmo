# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Wire the generated human voice lines into the game data.

Touches three datasets, each in both the editor project and the client ClientDB copy:

* ``sounds.data``      -- the SoundEntry catalog (one entry per voice bank)
* ``races.data``       -- the human RaceEntry's male/female VoiceLineSet maps
* ``model_data.data``  -- gossip / pissed / goodbye sound ids per display model

Idempotent: re-running replaces the same ids rather than appending duplicates.

    python tools/sfx_gen/recipes/author_voice_data.py            # validate only
    python tools/sfx_gen/recipes/author_voice_data.py --apply    # write all datasets
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from proto_runtime import find_protoc  # noqa: E402

import voice_lines  # noqa: E402

OUT = ROOT / "generated/voice_lines"
LOCALE = ROOT / "data/client/Locales/Locale_enUS"

VOICE = 4  # SoundEntryCategory.VOICE

PM = voice_lines.PLAYER_MALE
PF = voice_lines.PLAYER_FEMALE
NPC = voice_lines.NPC

# ---------------------------------------------------------------------------
# SoundEntry catalog
#
# Ids 1-33 were already in use; 9 is a hole left by a deleted entry and is
# deliberately NOT reused, so nothing that still holds a stale reference to 9 can
# silently start playing a voice line. New banks start at 34.
#
# (id, name, [files], is_3d)
SOUND_ENTRIES = [
    # -- existing player banks, topped up with the new takes ------------------
    (3, "Human Female - Voice - Out Of Mana", [
        PF + "HumanFemale_OutOfMana.mp3",
        PF + "HumanFemale_OutOfMana_02.wav",
        PF + "HumanFemale_OutOfMana_03.wav"], False),
    (4, "Human Female - Voice - Out Of Range", [
        PF + "HumanFemale_OutOfRange.mp3",
        PF + "HumanFemale_OutOfRange_02.wav",
        PF + "HumanFemale_OutOfRange_03.wav"], False),
    (5, "Human Female - Voice - Cooldown", [
        PF + "HumanFemale_Cooldown.mp3",
        PF + "HumanFemale_Cooldown_02.wav",
        PF + "HumanFemale_Cooldown_03.wav"], False),
    (8, "Human Male - Voice - Out Of Mana", [
        PM + "HumanMale_OutOfMana_01.wav",
        PM + "Humanmale_OutOfMana_02.wav",
        PM + "HumanMale_OutOfMana_03.wav"], False),
    (10, "Human Male - Voice - Out Of Rage", [
        PM + "HumanMale_OutOfRage_01.wav",
        PM + "HumanMale_OutOfRage_02.wav",
        PM + "HumanMale_OutOfRage_03.wav"], False),
    (11, "Human Male - Voice - Out Of Energy", [
        PM + "HumanMale_OutOfEnergy_01.wav",
        PM + "HumanMale_OutOfEnergy_02.wav",
        PM + "HumanMale_OutOfEnergy_03.wav"], False),
    (12, "Human Male - Voice - Cooldown", [
        PM + "HumanMale_Cooldown.wav",
        PM + "HumanMale_Cooldown_02.wav",
        PM + "HumanMale_Cooldown_03.wav"], False),
    (13, "Human Male - Voice - Out Of Range", [
        PM + "HumanMale_OutOfRange_01.wav",
        PM + "HumanMale_OutOfRange_02.wav",
        PM + "HumanMale_OutOfRange_03.wav"], False),
    (14, "Human Male - Voice - Cant Attack", [
        PM + "HumanMale_CantAttack_01.wav",
        PM + "HumanMale_CantAttack_02.wav",
        PM + "HumanMale_CantAttack_03.wav"], False),

    # -- new player banks ----------------------------------------------------
    (34, "Human Female - Voice - Out Of Rage", [
        PF + "HumanFemale_OutOfRage_01.wav",
        PF + "HumanFemale_OutOfRage_02.wav",
        PF + "HumanFemale_OutOfRage_03.wav"], False),
    (35, "Human Female - Voice - Out Of Energy", [
        PF + "HumanFemale_OutOfEnergy_01.wav",
        PF + "HumanFemale_OutOfEnergy_02.wav",
        PF + "HumanFemale_OutOfEnergy_03.wav"], False),
    (36, "Human Female - Voice - Cant Attack", [
        PF + "HumanFemale_CantAttack_01.wav",
        PF + "HumanFemale_CantAttack_02.wav",
        PF + "HumanFemale_CantAttack_03.wav"], False),
    (37, "Human Male - Voice - Target Dead", [
        PM + "HumanMale_TargetDead_01.wav",
        PM + "HumanMale_TargetDead_02.wav",
        PM + "HumanMale_TargetDead_03.wav"], False),
    (38, "Human Female - Voice - Target Dead", [
        PF + "HumanFemale_TargetDead_01.wav",
        PF + "HumanFemale_TargetDead_02.wav",
        PF + "HumanFemale_TargetDead_03.wav"], False),
    (39, "Human Male - Voice - Wrong Facing", [
        PM + "HumanMale_WrongFacing_01.wav",
        PM + "HumanMale_WrongFacing_02.wav",
        PM + "HumanMale_WrongFacing_03.wav"], False),
    (40, "Human Female - Voice - Wrong Facing", [
        PF + "HumanFemale_WrongFacing_01.wav",
        PF + "HumanFemale_WrongFacing_02.wav",
        PF + "HumanFemale_WrongFacing_03.wav"], False),
    (41, "Human Male - Voice - Bad Target", [
        PM + "HumanMale_BadTarget_01.wav",
        PM + "HumanMale_BadTarget_02.wav",
        PM + "HumanMale_BadTarget_03.wav"], False),
    (42, "Human Female - Voice - Bad Target", [
        PF + "HumanFemale_BadTarget_01.wav",
        PF + "HumanFemale_BadTarget_02.wav",
        PF + "HumanFemale_BadTarget_03.wav"], False),
    (43, "Human Male - Voice - Caster Dead", [
        PM + "HumanMale_CasterDead_01.wav",
        PM + "HumanMale_CasterDead_02.wav"], False),
    (44, "Human Female - Voice - Caster Dead", [
        PF + "HumanFemale_CasterDead_01.wav",
        PF + "HumanFemale_CasterDead_02.wav"], False),

    # -- npc banks (3D: they play at the unit's position) --------------------
    (45, "Human Male Npc - Voice - Goodbye", [
        NPC + "HumanMale01/HumanMale_Npc_Goodbye_01.wav",
        NPC + "HumanMale01/HumanMale_Npc_Goodbye_02.wav",
        NPC + "HumanMale01/HumanMale_Npc_Goodbye_03.wav",
        NPC + "HumanMale01/HumanMale_Npc_Goodbye_04.wav"], True),
    (46, "Human Female Npc - Voice - Hello", [
        NPC + "HumanFemale01/HumanFemale_Npc_Hello_01.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Hello_02.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Hello_03.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Hello_04.wav"], True),
    (47, "Human Female Npc - Voice - Pissed", [
        NPC + "HumanFemale01/HumanFemale_Npc_Annoyed_01.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Annoyed_02.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Annoyed_03.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Annoyed_04.wav"], True),
    (48, "Human Female Npc - Voice - Goodbye", [
        NPC + "HumanFemale01/HumanFemale_Npc_Goodbye_01.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Goodbye_02.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Goodbye_03.wav",
        NPC + "HumanFemale01/HumanFemale_Npc_Goodbye_04.wav"], True),
    (49, "Clarissa Whiteshield - Voice - Hello", [
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Hello_01.wav",
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Hello_02.wav",
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Hello_03.wav",
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Hello_04.wav"], True),
    (50, "Clarissa Whiteshield - Voice - Pissed", [
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Annoyed_01.wav",
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Annoyed_02.wav",
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Annoyed_03.wav"], True),
    (51, "Clarissa Whiteshield - Voice - Goodbye", [
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Goodbye_01.wav",
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Goodbye_02.wav",
        NPC + "ClarissaWhiteshield/Clarissa_Npc_Goodbye_03.wav"], True),
    (52, "Elira Hawke - Voice - Hello", [
        NPC + "EliraHawke/Elira_Npc_Hello_01.wav",
        NPC + "EliraHawke/Elira_Npc_Hello_02.wav",
        NPC + "EliraHawke/Elira_Npc_Hello_03.wav",
        NPC + "EliraHawke/Elira_Npc_Hello_04.wav"], True),
    (53, "Elira Hawke - Voice - Pissed", [
        NPC + "EliraHawke/Elira_Npc_Annoyed_01.wav",
        NPC + "EliraHawke/Elira_Npc_Annoyed_02.wav",
        NPC + "EliraHawke/Elira_Npc_Annoyed_03.wav"], True),
    (54, "Elira Hawke - Voice - Goodbye", [
        NPC + "EliraHawke/Elira_Npc_Goodbye_01.wav",
        NPC + "EliraHawke/Elira_Npc_Goodbye_02.wav",
        NPC + "EliraHawke/Elira_Npc_Goodbye_03.wav"], True),
]

# spell_cast_result values (src/shared/game/spell.h)
CAST_BAD_TARGETS = 3
CAST_CASTER_DEAD = 4
CAST_NOT_READY = 10
CAST_NO_POWER = 11
CAST_OUT_OF_RANGE = 14
CAST_UNIT_NOT_INFRONT = 19

# power_type values
POWER_MANA, POWER_RAGE, POWER_ENERGY = 0, 1, 2

# attack_swing_event values (src/shared/game/auto_attack.h)
SWING_OUT_OF_RANGE = 1
SWING_CANT_ATTACK = 2
SWING_WRONG_FACING = 3
SWING_TARGET_DEAD = 4

HUMAN_RACE_ID = 0

MALE_VOICE = {
    "cast_error_sounds": {
        CAST_NOT_READY: 12,
        CAST_NO_POWER: 8,
        CAST_OUT_OF_RANGE: 13,
        CAST_BAD_TARGETS: 41,
        CAST_CASTER_DEAD: 43,
        CAST_UNIT_NOT_INFRONT: 39,
    },
    "no_power_sounds": {POWER_MANA: 8, POWER_RAGE: 10, POWER_ENERGY: 11},
    # NotStanding has no line: the player has no ranged auto attack yet, so the event
    # never reaches the client. Left unmapped rather than voiced with a stand-in.
    "attack_error_sounds": {
        SWING_OUT_OF_RANGE: 13,
        SWING_CANT_ATTACK: 14,
        SWING_WRONG_FACING: 39,
        SWING_TARGET_DEAD: 37,
    },
}

FEMALE_VOICE = {
    "cast_error_sounds": {
        CAST_NOT_READY: 5,
        CAST_NO_POWER: 3,
        CAST_OUT_OF_RANGE: 4,
        CAST_BAD_TARGETS: 42,
        CAST_CASTER_DEAD: 44,
        CAST_UNIT_NOT_INFRONT: 40,
    },
    "no_power_sounds": {POWER_MANA: 3, POWER_RAGE: 34, POWER_ENERGY: 35},
    "attack_error_sounds": {
        SWING_OUT_OF_RANGE: 4,
        SWING_CANT_ATTACK: 36,
        SWING_WRONG_FACING: 40,
        SWING_TARGET_DEAD: 38,
    },
}

# model display id -> (gossip, pissed, goodbye)
MODEL_VOICES = {
    3: (46, 47, 48),          # Human Female Model (generic npc)
    4: (6, 7, 45),            # Human Male Model (generic npc)
    7: (46, 47, 48),          # PLAYER - Human Female Model
    8: (6, 7, 45),            # PLAYER - Human Male Model
    14: (49, 50, 51),         # Clarissa Whiteshield
    16: (52, 53, 54),         # Elira Hawke (also Scout Mirelle Voss -- both are scouts)
}


def load_type(schema_dir, proto_name, message_name):
    with tempfile.TemporaryDirectory(prefix="voice_data_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", proto_name],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for f in file_set.file:
        pool.Add(f)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


def upsert_sounds(dataset):
    by_id = {e.id: e for e in dataset.entry}
    for entry_id, name, files, is_3d in SOUND_ENTRIES:
        target = by_id.get(entry_id)
        if target is None:
            target = dataset.entry.add()
        target.Clear()
        target.id = entry_id
        target.name = name
        for f in files:
            target.files.append(f)
        target.category = VOICE
        target.is_3d = is_3d
        target.looped = False
        target.stream = False
        target.volume = 1.0
        if is_3d:
            # Same attenuation as the existing HumanMale01 npc banks.
            target.min_distance = 1.0
            target.max_distance = 30.0
    if len({e.id for e in dataset.entry}) != len(dataset.entry):
        raise SystemExit("duplicate sound ids")


def fill_voice_set(voice_set, spec):
    voice_set.Clear()
    for key, value in spec["cast_error_sounds"].items():
        voice_set.cast_error_sounds[key] = value
    for key, value in spec["no_power_sounds"].items():
        voice_set.no_power_sounds[key] = value
    for key, value in spec["attack_error_sounds"].items():
        voice_set.attack_error_sounds[key] = value


def upsert_races(dataset):
    for entry in dataset.entry:
        if entry.id != HUMAN_RACE_ID:
            continue
        fill_voice_set(entry.male_voice, MALE_VOICE)
        fill_voice_set(entry.female_voice, FEMALE_VOICE)
        return
    raise SystemExit(f"race {HUMAN_RACE_ID} not found")


def upsert_models(dataset):
    seen = set()
    for entry in dataset.entry:
        voices = MODEL_VOICES.get(entry.id)
        if voices is None:
            continue
        entry.gossip_sound_id, entry.gossip_pissed_sound_id, entry.goodbye_sound_id = voices
        seen.add(entry.id)
    missing = sorted(set(MODEL_VOICES) - seen)
    if missing:
        raise SystemExit(f"model display ids not found: {missing}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    # Every referenced file must exist, in both the SoundEntry table and the clip
    # recipe -- a SoundEntry pointing at a missing file fails silently at runtime.
    referenced = set()
    for _id, name, files, _is3d in SOUND_ENTRIES:
        for f in files:
            referenced.add(f)
            if not (LOCALE / f).is_file():
                raise SystemExit(f"missing voice file for '{name}': {LOCALE / f}")

    generated = {c[0] for c in voice_lines.CLIPS}
    orphans = sorted(generated - referenced)
    if orphans:
        raise SystemExit("generated clips no SoundEntry references: " + ", ".join(orphans))

    targets = [
        (ROOT / "data/editor/data/sounds.data",
         load_type(ROOT / "src/shared/proto_data", "sounds.proto", "mmo.proto.Sounds"),
         upsert_sounds),
        (ROOT / "data/client/ClientDB/sounds.data",
         load_type(ROOT / "src/shared/client_data", "sounds.proto", "mmo.proto_client.Sounds"),
         upsert_sounds),
        (ROOT / "data/editor/data/races.data",
         load_type(ROOT / "src/shared/proto_data", "races.proto", "mmo.proto.Races"),
         upsert_races),
        (ROOT / "data/client/ClientDB/races.data",
         load_type(ROOT / "src/shared/client_data", "races.proto", "mmo.proto_client.Races"),
         upsert_races),
        (ROOT / "data/editor/data/model_data.data",
         load_type(ROOT / "src/shared/proto_data", "model_data.proto", "mmo.proto.ModelDatas"),
         upsert_models),
        (ROOT / "data/client/ClientDB/model_data.data",
         load_type(ROOT / "src/shared/client_data", "model_data.proto", "mmo.proto_client.ModelDatas"),
         upsert_models),
    ]

    # Parse and mutate every dataset before writing any of them, so a failure partway
    # through cannot leave the editor and client copies diverged.
    datasets = []
    for path, message_type, mutate in targets:
        dataset = message_type.FromString(path.read_bytes())
        mutate(dataset)
        if not dataset.IsInitialized():
            raise SystemExit(f"{path.name} is missing required fields after upsert")
        datasets.append(dataset)

    if not args.apply:
        print(f"validated {len(SOUND_ENTRIES)} sound entries, {len(referenced)} voice files "
              f"and {len(MODEL_VOICES)} model bindings across {len(targets)} datasets")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for index, (path, _t, _m) in enumerate(targets):
        shutil.copy2(path, backup / f"{index}_{path.name}")

    for (path, _t, _m), dataset in zip(targets, datasets):
        path.write_bytes(dataset.SerializeToString())

    print(f"wrote {len(targets)} datasets. Backup: {backup}")


if __name__ == "__main__":
    main()
