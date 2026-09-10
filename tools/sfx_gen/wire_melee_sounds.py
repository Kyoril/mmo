# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Wire the melee auto attack sounds into the game data.

Idempotent: re-running updates the rows it owns instead of duplicating them, so it can be
re-run after regenerating or renaming sounds. Writes BOTH copies of every table it touches --
``data/editor/data/<t>.data`` (authoring, mmo.proto schema) and
``data/client/ClientDB/<t>.data`` (runtime, mmo.proto_client schema). Those two files are
byte-identical for these tables because the mirrored schemas share field numbers, which is
why this script can skip the editor GUI entirely.

Run:  python tools/sfx_gen/wire_melee_sounds.py [--dry-run]
"""

from __future__ import annotations

import argparse
import importlib
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
EDITOR_DATA = REPO / "data" / "editor" / "data"
CLIENT_DATA = REPO / "data" / "client" / "ClientDB"

sys.path.insert(0, str(REPO / ".claude" / "skills" / "mmo-item-designer" / "scripts"))
from proto_runtime import compile_proto_modules  # noqa: E402

SOUND_DIR = "Sound/Combat/Melee"

# --- Surface types (combat materials). Ids 1-7 are the pre-existing floor surfaces. --------
# Wood (3) is reused as-is: a straw-and-timber training dummy is exactly that material.
MATERIALS = {
    "Flesh": 8,
    "Plate": 9,
    "Mail": 10,
    "Leather": 11,
    "Cloth": 12,
    "Bone": 13,
}
WOOD = 3

# --- Sound entries. Each maps to one or more generated files, played shuffle-bag style. ----
# id: (name, [slot names])
SOUND_ENTRIES = {
    55: ("Melee - Swing - Light Blade", ["SwingLight01", "SwingLight02", "SwingLight03"]),
    56: ("Melee - Swing - Heavy", ["SwingHeavy01", "SwingHeavy02", "SwingHeavy03"]),
    57: ("Melee - Swing - Blunt", ["SwingBlunt01", "SwingBlunt02", "SwingBlunt03"]),
    58: ("Melee - Swing - Unarmed", ["SwingUnarmed01", "SwingUnarmed02"]),
    59: ("Melee - Impact - Blade on Flesh", ["BladeFlesh01", "BladeFlesh02", "BladeFlesh03"]),
    60: ("Melee - Impact - Blade on Metal", ["BladeMetal01", "BladeMetal02", "BladeMetal03"]),
    61: ("Melee - Impact - Axe on Flesh", ["AxeFlesh01", "AxeFlesh02", "AxeFlesh03"]),
    62: ("Melee - Impact - Axe on Metal", ["AxeMetal01", "AxeMetal02"]),
    63: ("Melee - Impact - Blunt on Flesh", ["BluntFlesh01", "BluntFlesh02", "BluntFlesh03"]),
    64: ("Melee - Impact - Blunt on Metal", ["BluntMetal01", "BluntMetal02"]),
    65: ("Melee - Impact - Fist on Flesh", ["FistFlesh01", "FistFlesh02"]),
    66: ("Melee - Crit Layer", ["Crit01", "Crit02"]),
    67: ("Melee - Miss", ["Miss01", "Miss02", "Miss03"]),
    68: ("Melee - Parry", ["Parry01", "Parry02"]),
    69: ("Melee - Block", ["Block01", "Block02"]),
}

SWING_LIGHT, SWING_HEAVY, SWING_BLUNT, SWING_UNARMED = 55, 56, 57, 58
BLADE_FLESH, BLADE_METAL = 59, 60
AXE_FLESH, AXE_METAL = 61, 62
BLUNT_FLESH, BLUNT_METAL = 63, 64
FIST_FLESH, CRIT, MISS, PARRY, BLOCK = 65, 66, 67, 68, 69

# --- Weapon subclasses -> which sound family they belong to -------------------------------
# (subclass id, swing sound, flesh impact, metal impact)
WEAPONS = [
    (1, SWING_LIGHT, BLADE_FLESH, BLADE_METAL),   # One-Handed Sword
    (2, SWING_HEAVY, BLADE_FLESH, BLADE_METAL),   # Two-Handed Sword
    (3, SWING_BLUNT, BLUNT_FLESH, BLUNT_METAL),   # One-Handed Mace
    (4, SWING_BLUNT, BLUNT_FLESH, BLUNT_METAL),   # Two-Handed Mace
    (5, SWING_LIGHT, AXE_FLESH, AXE_METAL),       # One-Handed Axe
    (6, SWING_HEAVY, AXE_FLESH, AXE_METAL),       # Two-Handed Axe
    (7, SWING_LIGHT, BLADE_FLESH, BLADE_METAL),   # Dagger
    (8, SWING_BLUNT, BLUNT_FLESH, BLUNT_METAL),   # Stave
]

SHIELD_SUBCLASS = 9

# Armor subclass -> the material its wearer sounds like when struck.
ARMOR_MATERIALS = {
    10: MATERIALS["Cloth"],
    11: MATERIALS["Leather"],
    12: MATERIALS["Mail"],
    13: MATERIALS["Plate"],
}

# --- Models -> body material. Everything not listed is flesh. ------------------------------
MODEL_MATERIAL_OVERRIDES = {
    6: MATERIALS["Bone"],   # Skeleton Warrior
    19: WOOD,               # Training Dummy: a timber mannequin, not a body
}
# Not a combat unit, gets no combat sounds at all.
MODEL_SKIP = {5}            # GameObject - Silverleaf Sprig


def load_modules():
    compile_proto_modules(REPO)
    return {
        "sounds": importlib.import_module("sounds_pb2"),
        "surface_types": importlib.import_module("surface_types_pb2"),
        "item_subclasses": importlib.import_module("item_subclasses_pb2"),
        "model_data": importlib.import_module("model_data_pb2"),
    }


def read(msg_cls, name):
    msg = msg_cls()
    msg.ParseFromString((EDITOR_DATA / f"{name}.data").read_bytes())
    return msg


def write(msg, name, dry_run):
    blob = msg.SerializeToString()
    if dry_run:
        print(f"    [dry-run] would write {name}.data ({len(blob)} bytes) to both copies")
        return
    (EDITOR_DATA / f"{name}.data").write_bytes(blob)
    (CLIENT_DATA / f"{name}.data").write_bytes(blob)
    print(f"    wrote {name}.data ({len(blob)} bytes) to editor + ClientDB")


def upsert(container, entry_id):
    """Returns the entry with this id, creating it if missing."""
    for entry in container.entry:
        if entry.id == entry_id:
            return entry
    entry = container.entry.add()
    entry.id = entry_id
    return entry


def set_impacts(target, flesh_sound, metal_sound):
    """Replaces the impact rows: a default (any material) row plus the metal rows."""
    target.ClearField("impact_sounds")
    for material, sound in (
        (0, flesh_sound),                       # default row, used when nothing matches
        (MATERIALS["Plate"], metal_sound),
        (MATERIALS["Mail"], metal_sound),
    ):
        row = target.impact_sounds.add()
        row.target_material = material
        row.sound = sound


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    mods = load_modules()

    print("surface types:")
    surfaces = read(mods["surface_types"].SurfaceTypes, "surface_types")
    for name, sid in MATERIALS.items():
        entry = upsert(surfaces, sid)
        entry.name = name
        # Combat materials carry no footstep sound: nobody walks on flesh.
        print(f"    {sid:3} {name}")
    write(surfaces, "surface_types", args.dry_run)

    print("sound entries:")
    sounds = read(mods["sounds"].Sounds, "sounds")
    for sid, (name, slots) in SOUND_ENTRIES.items():
        entry = upsert(sounds, sid)
        entry.name = name
        entry.ClearField("files")
        for slot in slots:
            entry.files.append(f"{SOUND_DIR}/{slot}.wav")
        entry.category = 0          # SOUND_EFFECTS
        entry.is_3d = True
        entry.looped = False
        entry.stream = False
        entry.volume = 1.0
        # Slight pitch scatter so a sound heard hundreds of times per fight does not
        # machine-gun into an obviously identical loop.
        entry.pitch_min = 0.94
        entry.pitch_max = 1.06
        entry.min_distance = 5.0
        entry.max_distance = 40.0
        print(f"    {sid:3} {name:34} ({len(slots)} files)")
    write(sounds, "sounds", args.dry_run)

    print("item subclasses:")
    subclasses = read(mods["item_subclasses"].ItemSubclasses, "item_subclasses")
    for sub_id, swing, flesh, metal in WEAPONS:
        entry = upsert(subclasses, sub_id)
        entry.swing_sound = swing
        entry.crit_layer_sound = CRIT
        entry.miss_sound = MISS
        entry.parry_sound = PARRY
        set_impacts(entry, flesh, metal)
        print(f"    {sub_id:3} {entry.name:22} swing={swing} impacts=flesh:{flesh}/metal:{metal}")

    shield = upsert(subclasses, SHIELD_SUBCLASS)
    shield.block_sound = BLOCK
    print(f"    {SHIELD_SUBCLASS:3} {shield.name:22} block={BLOCK}")

    for sub_id, material in ARMOR_MATERIALS.items():
        entry = upsert(subclasses, sub_id)
        entry.hit_material = material
        print(f"    {sub_id:3} {entry.name:22} hit_material={material}")
    write(subclasses, "item_subclasses", args.dry_run)

    print("models:")
    models = read(mods["model_data"].ModelDatas, "model_data")
    for entry in models.entry:
        if entry.id in MODEL_SKIP:
            continue
        combat = entry.combat_sounds
        combat.hit_material = MODEL_MATERIAL_OVERRIDES.get(entry.id, MATERIALS["Flesh"])
        # Natural weapon, used when the unit has nothing equipped in the swinging hand.
        # Creatures currently share the unarmed set; per-creature bites and claws are a
        # follow-up, and this at least makes every creature swing audible.
        combat.swing_sound = SWING_UNARMED
        combat.crit_layer_sound = CRIT
        combat.miss_sound = MISS
        set_impacts(combat, FIST_FLESH, BLUNT_METAL)
        print(f"    {entry.id:3} {entry.name:32} material={combat.hit_material}")
    write(models, "model_data", args.dry_run)

    print("\ndone.")


if __name__ == "__main__":
    main()
