# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
One-shot migration of the game data to data-driven attributes
(see docs/superpowers/specs/2026-10-29-data-driven-attributes-design.md).

    py tools/attributes/migrate_attributes.py            # show what would change
    py tools/attributes/migrate_attributes.py --apply    # write editor data + ClientDB

Idempotent: data that is already migrated is left alone.

The pre-migration fields are reserved in the current schema, so they survive parsing as unknown
fields and are decoded from there:

* ClassBaseValues / UnitClassBaseValues 3-7 (stamina, strength, agility, intellect, spirit)
  -> repeated attributes (index = attribute id)
* ClassEntry 13 / 15 (spiritPerManaRegen / spiritPerHealthRegen) -> mana/healthRegenSources
* UnitClassEntry 10 (spiritPerManaRegen, default 5.0) -> manaRegenSources
  (12, spiritPerHealthRegen, was never used for creatures and is dropped)
* creature stat source ids from StatConstants order (Str, Agi, Sta, Int, Spi) to attribute ids
* item stat types 2-6 (Agility, Strength, Intellect, Spirit, Stamina) -> 64 + attribute id
* every class and unit class gets the formerly hard-coded "20 Agility = 1% melee crit / dodge"
"""

import argparse
import importlib
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / "data/editor/data"
CLIENT_DB = ROOT / "data/client/ClientDB"
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

STAMINA, STRENGTH, AGILITY, INTELLECT, SPIRIT = range(5)

# Locale indices of base/localization.h
DE_DE, FR_FR = 1, 3

ATTRIBUTES = [
    # id, name, description, level up text, {locale: (name, description, level up text)}
    (STAMINA, "Stamina", "", "You have gained %d stamina.", {
        DE_DE: ("Ausdauer", "", "Eure Ausdauer wurde um %d Punkt(e) erhöht."),
        FR_FR: ("Énergie", "", "Vous avez gagné %d points d'endurance."),
    }),
    (STRENGTH, "Strength", "", "You have gained %d strength.", {
        DE_DE: ("Stärke", "", "Eure Stärke wurde um %d Punkt(e) erhöht."),
        FR_FR: ("Force", "", "Vous avez gagné %d points de force."),
    }),
    (AGILITY, "Agility", "", "You have gained %d agility.", {
        DE_DE: ("Beweglichkeit", "", "Eure Beweglichkeit wurde um %d Punkt(e) erhöht."),
        FR_FR: ("Agilité", "", "Vous avez gagné %d points d'agilité."),
    }),
    (INTELLECT, "Intellect", "", "You have gained %d intellect.", {
        DE_DE: ("Intelligenz", "", "Eure Intelligenz wurde um %d Punkt(e) erhöht."),
        FR_FR: ("Intelligence", "", "Vous avez gagné %d points d'intelligence."),
    }),
    (SPIRIT, "Spirit",
     "Increases health regeneration while out of combat. Increases mana regeneration while not casting.",
     "You have gained %d spirit.", {
        DE_DE: ("Willenskraft",
                "Erhöht die Gesundheitsregeneration außerhalb des Kampfes. Erhöht die Manaregeneration, solange Ihr nicht zaubert.",
                "Eure Willenskraft wurde um %d Punkt(e) erhöht."),
        FR_FR: ("Volonté", "", "Vous avez gagné %d points d'esprit."),
    }),
]

# Legacy base value field numbers -> attribute id
LEGACY_BASE_FIELDS = {3: STAMINA, 4: STRENGTH, 5: AGILITY, 6: INTELLECT, 7: SPIRIT}

# proto::StatConstants::StatType (Str, Agi, Sta, Int, Spi) -> attribute id
STAT_TYPE_TO_ATTRIBUTE = {0: STRENGTH, 1: AGILITY, 2: STAMINA, 3: INTELLECT, 4: SPIRIT}

# Legacy item_stat values -> attribute id
ITEM_STAT_ATTRIBUTE_START = 64
LEGACY_ITEM_STATS = {2: AGILITY, 3: STRENGTH, 4: INTELLECT, 5: SPIRIT, 6: STAMINA}

# The formerly hard-coded crit/dodge rule: 20 agility = 1 %
AGILITY_PER_PERCENT = 20.0

_MODULES = {}


def find_any_protoc():
    try:
        return find_protoc(ROOT)
    except FileNotFoundError:
        common = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute",
                                 "--git-common-dir"], capture_output=True, text=True, check=True)
        return find_protoc(Path(common.stdout.strip()).parent)


def load_modules():
    if not _MODULES:
        out = Path(tempfile.mkdtemp(prefix="attributes_proto_"))
        proto_dir = ROOT / "src/shared/proto_data"
        subprocess.run([str(find_any_protoc()), f"-I{proto_dir}", f"--python_out={out}",
                        *sorted(p.name for p in proto_dir.glob("*.proto"))], check=True, cwd=proto_dir)
        sys.path.insert(0, str(out))
        for name in ("attributes", "classes", "unit_classes", "items"):
            _MODULES[name] = importlib.import_module(f"{name}_pb2")
    return _MODULES


def unknown_fields(message):
    """Returns {field number: [raw values]} of the unknown fields of a message."""
    from google.protobuf.unknown_fields import UnknownFieldSet
    result = {}
    for field in UnknownFieldSet(message):
        result.setdefault(field.field_number, []).append(field.data)
    return result


def as_float(raw):
    """Decodes a fixed32 unknown field value as float."""
    return struct.unpack("<f", struct.pack("<I", raw))[0]


def read(module, container, file_name):
    message = getattr(module, container)()
    message.ParseFromString((EDITOR / file_name).read_bytes())
    return message


def build_attributes(m):
    attributes = m["attributes"].Attributes()
    for attribute_id, name, description, level_up_text, localized in ATTRIBUTES:
        entry = attributes.entry.add(id=attribute_id, name=name, levelUpText=level_up_text)
        if description:
            entry.description = description
        for locale, (loc_name, loc_description, loc_level_up) in sorted(localized.items()):
            entry.name_loc.add(locale=locale, value=loc_name)
            if loc_description:
                entry.description_loc.add(locale=locale, value=loc_description)
            entry.levelUpText_loc.add(locale=locale, value=loc_level_up)
    return attributes


def migrate_base_values(base_values, label, log):
    """Moves legacy fields 3-7 into the repeated attributes field. Returns True if changed."""
    legacy = unknown_fields(base_values)
    if not any(number in legacy for number in LEGACY_BASE_FIELDS):
        return False
    if len(base_values.attributes) > 0:
        raise SystemExit(f"{label}: has both legacy attribute fields and new attributes - refusing to guess")

    values = [0] * len(LEGACY_BASE_FIELDS)
    for number, attribute in LEGACY_BASE_FIELDS.items():
        values[attribute] = legacy.get(number, [0])[-1]
    base_values.attributes.extend(values)
    log.append(f"{label}: attributes = {values}")
    return True


def migrate_classes(m, log):
    classes = read(m["classes"], "Classes", "classes.data")
    changed = False
    for entry in classes.entry:
        legacy = unknown_fields(entry)
        migrated_values = False
        for level, base_values in enumerate(entry.levelbasevalues, start=1):
            migrated_values |= migrate_base_values(base_values, f"class {entry.id} {entry.name} level {level}", [])
        if not migrated_values:
            continue

        changed = True
        log.append(f"class {entry.id} {entry.name}: {len(entry.levelbasevalues)} levels of base attributes")
        for number, field, label in ((15, entry.healthRegenSources, "health"), (13, entry.manaRegenSources, "mana")):
            if number in legacy:
                points_per = as_float(legacy[number][-1])
                if points_per != 0.0:
                    field.add(attribute=SPIRIT, pointsPer=points_per)
                    log.append(f"class {entry.id} {entry.name}: {label} regen = Spirit / {points_per}")
        entry.meleeCritSources.add(attribute=AGILITY, pointsPer=AGILITY_PER_PERCENT)
        entry.dodgeSources.add(attribute=AGILITY, pointsPer=AGILITY_PER_PERCENT)
        entry.DiscardUnknownFields()
    return classes if changed else None


def migrate_unit_classes(m, log):
    unit_classes = read(m["unit_classes"], "UnitClasses", "unit_classes.data")
    changed = False
    for entry in unit_classes.entry:
        legacy = unknown_fields(entry)
        migrated_values = False
        for level, base_values in enumerate(entry.levelbasevalues, start=1):
            migrated_values |= migrate_base_values(base_values, f"unit class {entry.id} {entry.name} level {level}", [])
        if not migrated_values:
            continue

        changed = True
        log.append(f"unit class {entry.id} {entry.name}: {len(entry.levelbasevalues)} levels of base attributes")
        for sources in (entry.attackPowerStatSources, entry.armorStatSources, entry.healthStatSources, entry.manaStatSources):
            for source in sources:
                source.statId = STAT_TYPE_TO_ATTRIBUTE[source.statId]

        # spiritPerManaRegen had a proto default of 5.0 when unset
        points_per = as_float(legacy[10][-1]) if 10 in legacy else 5.0
        if points_per != 0.0:
            entry.manaRegenSources.add(attribute=SPIRIT, pointsPer=points_per)
            log.append(f"unit class {entry.id} {entry.name}: mana regen = Spirit / {points_per}")
        entry.meleeCritSources.add(attribute=AGILITY, pointsPer=AGILITY_PER_PERCENT)
        entry.dodgeSources.add(attribute=AGILITY, pointsPer=AGILITY_PER_PERCENT)
        entry.DiscardUnknownFields()
    return unit_classes if changed else None


def migrate_items(m, log):
    items = read(m["items"], "Items", "items.data")
    changed = 0
    for entry in items.entry:
        for stat in entry.stats:
            if stat.type in LEGACY_ITEM_STATS:
                stat.type = ITEM_STAT_ATTRIBUTE_START + LEGACY_ITEM_STATS[stat.type]
                changed += 1
    if changed:
        log.append(f"items: {changed} attribute stats remapped to {ITEM_STAT_ATTRIBUTE_START} + attribute id")
    return items if changed else None


def ensure_client_manifest_entry():
    manifest = CLIENT_DB / "project.txt"
    text = manifest.read_text(encoding="utf-8")
    if "attributes = " in text:
        return
    line = 'attributes = (file = "attributes.data")\n'
    anchor = 'classes = (file = "classes.data")\n'
    text = text.replace(anchor, anchor + line) if anchor in text else text + line
    manifest.write_text(text, encoding="utf-8")


def ensure_editor_manifest_entry():
    manifest = EDITOR / "project.txt"
    text = manifest.read_text(encoding="utf-8")
    if "attributes = " in text:
        return
    line = 'attributes = (file = "attributes.data")\n'
    anchor = 'classes = (file = "classes.data")\n'
    if anchor not in text:
        raise SystemExit("data/editor/data/project.txt has no classes entry to anchor the attributes entry")
    manifest.write_text(text.replace(anchor, anchor + line), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--apply", action="store_true", help="write the migrated data")
    args = parser.parse_args()

    m = load_modules()
    log = []
    outputs = {}

    if not (EDITOR / "attributes.data").exists():
        outputs["attributes.data"] = build_attributes(m)
        log.append("attributes: " + ", ".join(f"{a[0]} {a[1]}" for a in ATTRIBUTES))
    for file_name, migrate in (("classes.data", migrate_classes), ("unit_classes.data", migrate_unit_classes),
                               ("items.data", migrate_items)):
        result = migrate(m, log)
        if result is not None:
            outputs[file_name] = result

    print("\n".join(log) if log else "Nothing to migrate.")
    if not args.apply or not outputs:
        return

    for file_name, message in outputs.items():
        (EDITOR / file_name).write_bytes(message.SerializeToString())
        print(f"wrote {EDITOR / file_name}")
    ensure_editor_manifest_entry()

    # ClientDB receives byte copies of the shared managers, exactly like mmo_edit's ExportToClient
    for file_name in ("attributes.data", "classes.data"):
        shutil.copyfile(EDITOR / file_name, CLIENT_DB / file_name)
        print(f"copied {file_name} to {CLIENT_DB}")
    ensure_client_manifest_entry()


if __name__ == "__main__":
    main()
