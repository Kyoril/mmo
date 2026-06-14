#!/usr/bin/env python3
"""Validate MMO NPC editor JSON and project references."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from npc_catalog_lib import (
    NO_LOOT_ENTRY_ID,
    REPLACE_MODES,
    find_project_root,
    list_builtin_combat_scripts,
    load_catalog_bundle,
    parse_message_dict,
)


def is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def add_error(errors: list[str], message: str) -> None:
    errors.append(message)


def validate_document(doc: dict, project_root: Path) -> list[str]:
    errors: list[str] = []
    modules, catalogs, indexes = load_catalog_bundle(project_root)
    builtin_scripts = set(list_builtin_combat_scripts(project_root))

    if not isinstance(doc, dict):
        return ["root must be an object"]

    if doc.get("format") != "mmo-npc":
        add_error(errors, "format must be 'mmo-npc'")
    if doc.get("version") != 1:
        add_error(errors, "version must be 1")

    unit_data = doc.get("unit")
    if not isinstance(unit_data, dict):
        add_error(errors, "unit must be an object")
        return errors

    try:
        unit_message = parse_message_dict(modules["units"].UnitEntry, unit_data)
    except Exception as exc:  # pragma: no cover - protobuf message detail
        add_error(errors, f"unit could not be parsed as UnitEntry: {exc}")
        return errors

    loot_data = doc.get("loot_entry")
    vendor_data = doc.get("vendor_entry")
    trainer_data = doc.get("trainer_entry")
    gossip_data = doc.get("gossip_menus", [])
    spawn_data = doc.get("spawns", [])

    attached_loot_id = None
    if loot_data is not None:
        if not isinstance(loot_data, dict):
            add_error(errors, "loot_entry must be an object when present")
        else:
            try:
                loot_message = parse_message_dict(modules["loot_entry"].LootEntry, loot_data)
                attached_loot_id = loot_message.id
            except Exception as exc:
                add_error(errors, f"loot_entry could not be parsed as LootEntry: {exc}")

    attached_vendor_id = None
    if vendor_data is not None:
        if not isinstance(vendor_data, dict):
            add_error(errors, "vendor_entry must be an object when present")
        else:
            try:
                vendor_message = parse_message_dict(modules["vendors"].VendorEntry, vendor_data)
                attached_vendor_id = vendor_message.id
            except Exception as exc:
                add_error(errors, f"vendor_entry could not be parsed as VendorEntry: {exc}")

    attached_trainer_id = None
    if trainer_data is not None:
        if not isinstance(trainer_data, dict):
            add_error(errors, "trainer_entry must be an object when present")
        else:
            try:
                trainer_message = parse_message_dict(modules["trainers"].TrainerEntry, trainer_data)
                attached_trainer_id = trainer_message.id
            except Exception as exc:
                add_error(errors, f"trainer_entry could not be parsed as TrainerEntry: {exc}")

    attached_gossip_ids: set[int] = set()
    if gossip_data:
        if not isinstance(gossip_data, list):
            add_error(errors, "gossip_menus must be an array")
        else:
            for index, menu_data in enumerate(gossip_data):
                if not isinstance(menu_data, dict):
                    add_error(errors, f"gossip_menus[{index}] must be an object")
                    continue
                try:
                    menu_message = parse_message_dict(modules["gossip_menus"].GossipMenuEntry, menu_data)
                    attached_gossip_ids.add(menu_message.id)
                except Exception as exc:
                    add_error(errors, f"gossip_menus[{index}] could not be parsed as GossipMenuEntry: {exc}")

    if not unit_message.HasField("id") or unit_message.id <= 0:
        add_error(errors, "unit.id must be a positive integer")
    if not unit_message.HasField("name") or not unit_message.name.strip():
        add_error(errors, "unit.name must be a non-empty string")
    if unit_message.minlevel > unit_message.maxlevel:
        add_error(errors, "unit.minlevel must not exceed unit.maxlevel")
    if unit_message.factionTemplate not in indexes["faction_templates"]:
        add_error(errors, f"unit.factionTemplate {unit_message.factionTemplate} is not present in project data")
    if unit_message.maleModel not in indexes["model_data"]:
        add_error(errors, f"unit.maleModel {unit_message.maleModel} is not present in project data")
    if unit_message.femaleModel not in indexes["model_data"]:
        add_error(errors, f"unit.femaleModel {unit_message.femaleModel} is not present in project data")
    if unit_message.useStatBasedSystem and unit_message.unitClassId not in indexes["unit_classes"]:
        add_error(errors, f"unit.unitClassId {unit_message.unitClassId} is not present in project data")
    if unit_message.HasField("script_name") and unit_message.script_name and unit_message.script_name not in builtin_scripts:
        add_error(errors, f"unit.script_name '{unit_message.script_name}' is not registered in the current runtime")
    if unit_message.HasField("auto_attack_spell") and unit_message.auto_attack_spell not in indexes["spells"]:
        add_error(errors, f"unit.auto_attack_spell {unit_message.auto_attack_spell} is not present in project data")

    loot_link_id = unit_message.unitlootentry if unit_message.HasField("unitlootentry") else 0
    if loot_link_id not in (0, NO_LOOT_ENTRY_ID):
        if loot_link_id not in indexes["unit_loot"] and loot_link_id != attached_loot_id:
            add_error(errors, f"unit.unitlootentry {loot_link_id} is not present in project data and no matching loot_entry was provided")
    if attached_loot_id is not None and loot_link_id != attached_loot_id:
        add_error(errors, f"loot_entry.id {attached_loot_id} must match unit.unitlootentry {loot_link_id}")

    vendor_link_id = unit_message.vendorentry if unit_message.HasField("vendorentry") else 0
    if vendor_link_id != 0 and vendor_link_id not in indexes["vendors"] and vendor_link_id != attached_vendor_id:
        add_error(errors, f"unit.vendorentry {vendor_link_id} is not present in project data and no matching vendor_entry was provided")
    if attached_vendor_id is not None and vendor_link_id != attached_vendor_id:
        add_error(errors, f"vendor_entry.id {attached_vendor_id} must match unit.vendorentry {vendor_link_id}")

    trainer_link_id = unit_message.trainerentry if unit_message.HasField("trainerentry") else 0
    if trainer_link_id != 0 and trainer_link_id not in indexes["trainers"] and trainer_link_id != attached_trainer_id:
        add_error(errors, f"unit.trainerentry {trainer_link_id} is not present in project data and no matching trainer_entry was provided")
    if attached_trainer_id is not None and trainer_link_id != attached_trainer_id:
        add_error(errors, f"trainer_entry.id {attached_trainer_id} must match unit.trainerentry {trainer_link_id}")

    for quest_id in unit_message.quests:
        if quest_id not in indexes["quests"]:
            add_error(errors, f"unit.quests contains unknown quest {quest_id}")
    for quest_id in unit_message.end_quests:
        if quest_id not in indexes["quests"]:
            add_error(errors, f"unit.end_quests contains unknown quest {quest_id}")
    for trigger_id in unit_message.triggers:
        if trigger_id not in indexes["triggers"]:
            add_error(errors, f"unit.triggers contains unknown trigger {trigger_id}")
    for gossip_id in unit_message.gossip_menus:
        if gossip_id not in indexes["gossip_menus"] and gossip_id not in attached_gossip_ids:
            add_error(errors, f"unit.gossip_menus contains unknown gossip menu {gossip_id}")

    for spell_index, creature_spell in enumerate(unit_message.creaturespells):
        if creature_spell.spellid not in indexes["spells"]:
            add_error(errors, f"unit.creaturespells[{spell_index}].spellid {creature_spell.spellid} is not present in project data")
        if creature_spell.HasField("probability") and not 0 <= creature_spell.probability <= 100:
            add_error(errors, f"unit.creaturespells[{spell_index}].probability must be in range 0..100")
        if creature_spell.HasField("minrange") and creature_spell.minrange < 0:
            add_error(errors, f"unit.creaturespells[{spell_index}].minrange must be non-negative")
        if creature_spell.HasField("maxrange") and creature_spell.maxrange < 0:
            add_error(errors, f"unit.creaturespells[{spell_index}].maxrange must be non-negative")
        if creature_spell.HasField("minrange") and creature_spell.HasField("maxrange") and creature_spell.minrange > creature_spell.maxrange:
            add_error(errors, f"unit.creaturespells[{spell_index}] must satisfy minrange <= maxrange")

    if isinstance(loot_data, dict):
        for group_index, group in enumerate(loot_data.get("groups", [])):
            if not isinstance(group, dict):
                add_error(errors, f"loot_entry.groups[{group_index}] must be an object")
                continue
            definitions = group.get("definitions", [])
            if not isinstance(definitions, list):
                add_error(errors, f"loot_entry.groups[{group_index}].definitions must be an array")
                continue
            for definition_index, definition in enumerate(definitions):
                if not isinstance(definition, dict):
                    add_error(errors, f"loot_entry.groups[{group_index}].definitions[{definition_index}] must be an object")
                    continue
                item_id = definition.get("item", 0)
                if item_id not in indexes["items"]:
                    add_error(errors, f"loot_entry.groups[{group_index}].definitions[{definition_index}].item {item_id} is not present in project data")
                mincount = definition.get("mincount", 1)
                maxcount = definition.get("maxcount", 1)
                chance = definition.get("dropchance", 0)
                condition_id = definition.get("condition", 0)
                if not is_number(mincount) or mincount < 1:
                    add_error(errors, f"loot_entry.groups[{group_index}].definitions[{definition_index}].mincount must be at least 1")
                if not is_number(maxcount) or maxcount < mincount:
                    add_error(errors, f"loot_entry.groups[{group_index}].definitions[{definition_index}].maxcount must be >= mincount")
                if not is_number(chance) or chance < 0 or chance > 100:
                    add_error(errors, f"loot_entry.groups[{group_index}].definitions[{definition_index}].dropchance must be in range 0..100")
                if condition_id and condition_id not in indexes["conditions"]:
                    add_error(errors, f"loot_entry.groups[{group_index}].definitions[{definition_index}].condition {condition_id} is not present in project data")

    if isinstance(vendor_data, dict):
        for item_index, item in enumerate(vendor_data.get("items", [])):
            if not isinstance(item, dict):
                add_error(errors, f"vendor_entry.items[{item_index}] must be an object")
                continue
            item_id = item.get("item", 0)
            if item_id not in indexes["items"]:
                add_error(errors, f"vendor_entry.items[{item_index}].item {item_id} is not present in project data")

    if isinstance(trainer_data, dict):
        for spell_index, spell in enumerate(trainer_data.get("spells", [])):
            if not isinstance(spell, dict):
                add_error(errors, f"trainer_entry.spells[{spell_index}] must be an object")
                continue
            spell_id = spell.get("spell", 0)
            reqskill = spell.get("reqskill", 0)
            if spell_id not in indexes["spells"]:
                add_error(errors, f"trainer_entry.spells[{spell_index}].spell {spell_id} is not present in project data")
            if reqskill and reqskill not in indexes["skills"]:
                add_error(errors, f"trainer_entry.spells[{spell_index}].reqskill {reqskill} is not present in project data")

    trigger_gossip_options = 0
    if isinstance(gossip_data, list):
        live_or_attached_gossip_ids = set(indexes["gossip_menus"].keys()) | attached_gossip_ids
        for menu_index, menu in enumerate(gossip_data):
            if not isinstance(menu, dict):
                continue
            options = menu.get("options", [])
            if not isinstance(options, list):
                add_error(errors, f"gossip_menus[{menu_index}].options must be an array")
                continue
            if menu.get("conditionId", 0) and menu["conditionId"] not in indexes["conditions"]:
                add_error(errors, f"gossip_menus[{menu_index}].conditionId {menu['conditionId']} is not present in project data")
            for option_index, option in enumerate(options):
                if not isinstance(option, dict):
                    add_error(errors, f"gossip_menus[{menu_index}].options[{option_index}] must be an object")
                    continue
                action_type = option.get("action_type", 0)
                condition_id = option.get("conditionId", 0)
                if not is_number(action_type) or not 0 <= action_type <= 4:
                    add_error(errors, f"gossip_menus[{menu_index}].options[{option_index}].action_type must be in range 0..4")
                if condition_id and condition_id not in indexes["conditions"]:
                    add_error(errors, f"gossip_menus[{menu_index}].options[{option_index}].conditionId {condition_id} is not present in project data")
                if action_type == 3:
                    action_param = option.get("action_param", 0)
                    if action_param not in live_or_attached_gossip_ids:
                        add_error(errors, f"gossip_menus[{menu_index}].options[{option_index}].action_param {action_param} does not reference a live or attached gossip menu")
                if action_type == 4:
                    trigger_gossip_options += 1

    if trigger_gossip_options > 0 and not unit_message.triggers:
        add_error(errors, "gossip trigger actions require unit.triggers to contain matching trigger rows")

    if spawn_data:
        if not isinstance(spawn_data, list):
            add_error(errors, "spawns must be an array")
        else:
            for spawn_index, spawn_wrapper in enumerate(spawn_data):
                if not isinstance(spawn_wrapper, dict):
                    add_error(errors, f"spawns[{spawn_index}] must be an object")
                    continue
                map_id = spawn_wrapper.get("map_id")
                if map_id not in indexes["maps"]:
                    add_error(errors, f"spawns[{spawn_index}].map_id {map_id} is not present in project data")
                replace_mode = spawn_wrapper.get("replace_mode", "append")
                if replace_mode not in REPLACE_MODES:
                    add_error(errors, f"spawns[{spawn_index}].replace_mode must be one of {sorted(REPLACE_MODES)}")
                if replace_mode == "replace_by_map_and_index":
                    match_index = spawn_wrapper.get("match_index")
                    if not isinstance(match_index, int) or match_index < 0:
                        add_error(errors, f"spawns[{spawn_index}].match_index must be a non-negative integer when replace_mode is replace_by_map_and_index")
                    elif map_id in indexes["maps"] and match_index >= len(indexes["maps"][map_id].unitspawns):
                        add_error(errors, f"spawns[{spawn_index}].match_index {match_index} is out of range for map {map_id}")
                spawn_payload = spawn_wrapper.get("spawn")
                if not isinstance(spawn_payload, dict):
                    add_error(errors, f"spawns[{spawn_index}].spawn must be an object")
                    continue
                try:
                    spawn_message = parse_message_dict(modules["maps"].UnitSpawnEntry, spawn_payload)
                except Exception as exc:
                    add_error(errors, f"spawns[{spawn_index}].spawn could not be parsed as UnitSpawnEntry: {exc}")
                    continue
                if spawn_message.HasField("unitentry") and spawn_message.unitentry != unit_message.id:
                    add_error(errors, f"spawns[{spawn_index}].spawn.unitentry {spawn_message.unitentry} must match unit.id {unit_message.id}")
                if spawn_message.HasField("movement") and not 0 <= spawn_message.movement <= 2:
                    add_error(errors, f"spawns[{spawn_index}].spawn.movement must be in range 0..2")
                if spawn_message.HasField("maxcount") and spawn_message.maxcount < 1:
                    add_error(errors, f"spawns[{spawn_index}].spawn.maxcount must be at least 1")
                has_locations = "locations" in spawn_payload and isinstance(spawn_payload["locations"], list) and len(spawn_payload["locations"]) > 0
                has_legacy_position = all(key in spawn_payload for key in ("positionx", "positiony", "positionz"))
                if not has_locations and not has_legacy_position:
                    add_error(errors, f"spawns[{spawn_index}].spawn must provide either locations[] or legacy positionx/positiony/positionz")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--project-root", default=None)
    args = parser.parse_args()

    path = Path(args.path)
    project_root = find_project_root(args.project_root)
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}")
        return 1

    errors = validate_document(doc, project_root)
    if errors:
        for message in errors:
            print(f"ERROR: {message}")
        return 1

    print(f"OK: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
