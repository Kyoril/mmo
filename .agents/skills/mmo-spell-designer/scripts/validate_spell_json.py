#!/usr/bin/env python3
"""Validate an MMO spell JSON draft against live project data."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from spell_catalog_lib import SPELL_EFFECT_NAMES, AURA_TYPE_NAMES, build_indexes, load_catalogs


def add_error(errors: list[str], message: str) -> None:
    errors.append(message)


def is_number_like(value) -> bool:
    if isinstance(value, (int, float)):
        return True
    if isinstance(value, str):
        text = value.strip()
        if text.startswith("-"):
            text = text[1:]
        return text.isdigit()
    return False


def to_int(value) -> int:
    return int(value)


def require_numeric(obj: dict, key: str, errors: list[str], prefix: str) -> None:
    if key in obj and not is_number_like(obj[key]):
        add_error(errors, f"{prefix}.{key} must be numeric")


def validate_mask(mask_value, label: str, valid_ids: set[int], errors: list[str]) -> None:
    if not is_number_like(mask_value):
        add_error(errors, f"{label} must be numeric")
        return
    if to_int(mask_value) < 0:
        add_error(errors, f"{label} must not be negative")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--project-root", default=None)
    args = parser.parse_args()

    path = Path(args.path)
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}")
        return 1

    errors: list[str] = []

    if not isinstance(doc, dict):
        add_error(errors, "root must be an object")
        doc = {}

    if doc.get("format") != "mmo-spell":
        add_error(errors, "format must be 'mmo-spell'")
    if doc.get("version") != 1:
        add_error(errors, "version must be 1")

    spell = doc.get("spell")
    if not isinstance(spell, dict):
        add_error(errors, "spell must be an object")
        spell = {}

    if not is_number_like(spell.get("id")) or to_int(spell.get("id", 0)) <= 0:
        add_error(errors, "spell.id must be a positive integer")
    if not isinstance(spell.get("name"), str) or not spell.get("name", "").strip():
        add_error(errors, "spell.name must be a non-empty string")

    for key in [
        "cooldown", "casttime", "powertype", "cost", "costpct", "maxlevel", "baselevel", "spelllevel",
        "spellSchool", "dmgclass", "itemclass", "itemsubclassmask", "facing", "duration", "maxduration",
        "interruptflags", "aurainterruptflags", "rangetype", "targetmap", "maxtargets", "talentcost",
        "procflags", "procchance", "proccharges", "mechanic", "category", "categorycooldown", "dispel",
        "family", "familyflags", "focusobject", "procschool", "procfamily", "procfamilyflags", "procexflags",
        "procpermin", "proccooldown", "proccustomflags", "baseid", "channelinterruptflags", "stackamount",
        "skill", "trivialskilllow", "trivialskillhigh", "racemask", "classmask", "rank", "prevspell",
        "nextspell", "positive", "threat_multiplier", "visualization_id", "cooldownflags",
        "stacking_rule", "stacking_category_id", "stack_reset_policy",
    ]:
        require_numeric(spell, key, errors, "spell")

    attributes = spell.get("attributes", [])
    if not isinstance(attributes, list):
        add_error(errors, "spell.attributes must be an array")
        attributes = []
    for index, value in enumerate(attributes):
        if not is_number_like(value):
            add_error(errors, f"spell.attributes[{index}] must be numeric")

    effects = spell.get("effects", [])
    if not isinstance(effects, list) or not effects:
        add_error(errors, "spell.effects must be a non-empty array")
        effects = []

    reagents = spell.get("reagents", [])
    if not isinstance(reagents, list):
        add_error(errors, "spell.reagents must be an array")
        reagents = []

    catalogs = load_catalogs(args.project_root)
    indexes = build_indexes(catalogs)
    spell_ids = set(indexes["spells"])
    item_ids = set(indexes["items"])
    class_ids = set(indexes["classes"])
    race_ids = set(indexes["races"])
    category_ids = set(indexes["categories"])
    visualization_ids = set(indexes["visualizations"])
    proficiency_ids = set(indexes["proficiencies"])
    skill_ids = set(indexes["skills"])
    stacking_ids = set(indexes["stacking_categories"])
    range_ids = set(indexes["ranges"])

    if "classmask" in spell:
        validate_mask(spell["classmask"], "spell.classmask", class_ids, errors)
    if "racemask" in spell:
        validate_mask(spell["racemask"], "spell.racemask", race_ids, errors)

    if to_int(spell.get("rangetype", 0)) not in range_ids and to_int(spell.get("rangetype", 0)) != 0:
        add_error(errors, f"spell.rangetype {spell.get('rangetype')} is not present in project data")
    if to_int(spell.get("category", 0)) not in category_ids and to_int(spell.get("category", 0)) != 0:
        add_error(errors, f"spell.category {spell.get('category')} is not present in project data")
    if to_int(spell.get("visualization_id", 0)) not in visualization_ids and to_int(spell.get("visualization_id", 0)) != 0:
        add_error(errors, f"spell.visualization_id {spell.get('visualization_id')} is not present in project data")
    if to_int(spell.get("stacking_category_id", 0)) not in stacking_ids and to_int(spell.get("stacking_category_id", 0)) != 0:
        add_error(errors, f"spell.stacking_category_id {spell.get('stacking_category_id')} is not present in project data")
    if to_int(spell.get("skill", 0)) not in skill_ids and to_int(spell.get("skill", 0)) != 0:
        add_error(errors, f"spell.skill {spell.get('skill')} is not present in project data")

    for ref_name in ["baseid", "prevspell", "nextspell"]:
        ref_value = to_int(spell.get(ref_name, 0))
        if ref_value != 0 and ref_value not in spell_ids and ref_value != to_int(spell.get("id", 0)):
            add_error(errors, f"spell.{ref_name} {ref_value} is not present in project data")

    for index, reagent in enumerate(reagents):
        if not isinstance(reagent, dict):
            add_error(errors, f"spell.reagents[{index}] must be an object")
            continue
        if not is_number_like(reagent.get("item")):
            add_error(errors, f"spell.reagents[{index}].item must be numeric")
            continue
        item_id = to_int(reagent["item"])
        if item_id not in item_ids:
            add_error(errors, f"spell.reagents[{index}].item {item_id} is not present in project data")

    seen_indexes = set()
    for index, effect in enumerate(effects):
        prefix = f"spell.effects[{index}]"
        if not isinstance(effect, dict):
            add_error(errors, f"{prefix} must be an object")
            continue
        if not is_number_like(effect.get("index")):
            add_error(errors, f"{prefix}.index must be numeric")
            continue
        effect_index = to_int(effect["index"])
        if effect_index in seen_indexes:
            add_error(errors, f"{prefix}.index {effect_index} is duplicated")
        seen_indexes.add(effect_index)

        if not is_number_like(effect.get("type")):
            add_error(errors, f"{prefix}.type must be numeric")
            continue
        effect_type = to_int(effect["type"])
        if effect_type not in SPELL_EFFECT_NAMES:
            add_error(errors, f"{prefix}.type {effect_type} is not a known supported effect enum")

        if effect_type in {6, 49}:
            aura_value = effect.get("aura")
            if not is_number_like(aura_value):
                add_error(errors, f"{prefix}.aura is required for aura-applying effects")
            elif to_int(aura_value) not in AURA_TYPE_NAMES:
                add_error(errors, f"{prefix}.aura {to_int(aura_value)} is not a known aura enum")

        if effect_type == 21:
            item_value = effect.get("itemtype")
            if not is_number_like(item_value) or to_int(item_value) not in item_ids:
                add_error(errors, f"{prefix}.itemtype must reference an existing item for CreateItem")

        if effect_type in {31, 53}:
            trigger_value = effect.get("triggerspell")
            if not is_number_like(trigger_value) or to_int(trigger_value) not in spell_ids:
                add_error(errors, f"{prefix}.triggerspell must reference an existing spell")

        if effect_type == 52:
            proficiency_value = effect.get("miscvaluea")
            if not is_number_like(proficiency_value) or to_int(proficiency_value) not in proficiency_ids:
                add_error(errors, f"{prefix}.miscvaluea must reference an existing proficiency for Proficiency")

        aura_value = to_int(effect.get("aura", 0)) if is_number_like(effect.get("aura", 0)) else 0
        if aura_value in {9, 12}:
            trigger_value = effect.get("triggerspell")
            if not is_number_like(trigger_value) or to_int(trigger_value) not in spell_ids:
                add_error(errors, f"{prefix}.triggerspell must reference an existing spell for trigger-style auras")

        # Data-driven conditional gating fields.
        for cond_key in ("conditiontype", "conditionvalue", "conditiontarget"):
            require_numeric(effect, cond_key, errors, prefix)
        condition_type = to_int(effect.get("conditiontype", 0)) if is_number_like(effect.get("conditiontype", 0)) else 0
        if condition_type not in {0, 1, 2}:
            add_error(errors, f"{prefix}.conditiontype {condition_type} is not a known condition (0=None, 1=HasAuraFromCaster, 2=MissingAuraFromCaster)")
        if condition_type in {1, 2}:
            condition_value = effect.get("conditionvalue")
            if not is_number_like(condition_value) or to_int(condition_value) not in spell_ids:
                add_error(errors, f"{prefix}.conditionvalue must reference an existing spell (the aura's source spell) when conditiontype is an aura check")
            condition_target = to_int(effect.get("conditiontarget", 0)) if is_number_like(effect.get("conditiontarget", 0)) else 0
            if condition_target not in {0, 1}:
                add_error(errors, f"{prefix}.conditiontarget {condition_target} is not valid (0=PrimaryTarget, 1=Caster)")

    if errors:
        for message in errors:
            print(f"ERROR: {message}")
        return 1

    print(f"OK: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
