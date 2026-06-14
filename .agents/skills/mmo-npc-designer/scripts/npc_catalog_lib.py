#!/usr/bin/env python3
"""Shared catalog helpers for MMO NPC tooling."""

from __future__ import annotations

import re
from pathlib import Path

from google.protobuf.json_format import MessageToDict, ParseDict

from proto_runtime import find_project_root, load_modules


NO_LOOT_ENTRY_ID = 0xFFFFFFFF
REPLACE_MODES = {"append", "by_name", "replace_first_for_unit", "replace_by_map_and_index"}


CATALOG_FACTORIES = {
    "units": lambda mods: mods["units"].Units(),
    "factions": lambda mods: mods["factions"].Factions(),
    "faction_templates": lambda mods: mods["faction_templates"].FactionTemplates(),
    "unit_loot": lambda mods: mods["unit_loot"].UnitLoot(),
    "vendors": lambda mods: mods["vendors"].Vendors(),
    "trainers": lambda mods: mods["trainers"].Trainers(),
    "gossip_menus": lambda mods: mods["gossip_menus"].GossipMenus(),
    "maps": lambda mods: mods["maps"].Maps(),
    "unit_classes": lambda mods: mods["unit_classes"].UnitClasses(),
    "model_data": lambda mods: mods["model_data"].ModelDatas(),
    "classes": lambda mods: mods["classes"].Classes(),
    "races": lambda mods: mods["races"].Races(),
    "quests": lambda mods: mods["quests"].Quests(),
    "conditions": lambda mods: mods["conditions"].Conditions(),
    "triggers": lambda mods: mods["triggers"].Triggers(),
    "spells": lambda mods: mods["spells"].Spells(),
    "ranges": lambda mods: mods["spells"].Ranges(),
    "items": lambda mods: mods["items"].Items(),
    "skills": lambda mods: mods["skills"].Skills(),
}


CATALOG_FILES = {
    "units": "units.data",
    "factions": "factions.data",
    "faction_templates": "faction_templates.data",
    "unit_loot": "unit_loot.data",
    "vendors": "vendors.data",
    "trainers": "trainers.data",
    "gossip_menus": "gossip_menus.data",
    "maps": "maps.data",
    "unit_classes": "unit_classes.data",
    "model_data": "model_data.data",
    "classes": "classes.data",
    "races": "races.data",
    "quests": "quests.data",
    "conditions": "conditions.data",
    "triggers": "triggers.data",
    "spells": "spells.data",
    "ranges": "ranges.data",
    "items": "items.data",
    "skills": "skills.data",
}


def data_dir(project_root: Path) -> Path:
    return project_root / "data" / "editor" / "data"


def load_catalog_bundle(project_root: Path) -> tuple[dict[str, object], dict[str, object], dict[str, dict[int, object]]]:
    modules = load_modules(project_root)
    catalogs: dict[str, object] = {}
    root = data_dir(project_root)
    for key, factory in CATALOG_FACTORIES.items():
        message = factory(modules)
        message.ParseFromString((root / CATALOG_FILES[key]).read_bytes())
        catalogs[key] = message
    indexes = {
        "units": {entry.id: entry for entry in catalogs["units"].entry},
        "factions": {entry.id: entry for entry in catalogs["factions"].entry},
        "faction_templates": {entry.id: entry for entry in catalogs["faction_templates"].entry},
        "unit_loot": {entry.id: entry for entry in catalogs["unit_loot"].entry},
        "vendors": {entry.id: entry for entry in catalogs["vendors"].entry},
        "trainers": {entry.id: entry for entry in catalogs["trainers"].entry},
        "gossip_menus": {entry.id: entry for entry in catalogs["gossip_menus"].entry},
        "maps": {entry.id: entry for entry in catalogs["maps"].entry},
        "unit_classes": {entry.id: entry for entry in catalogs["unit_classes"].entry},
        "model_data": {entry.id: entry for entry in catalogs["model_data"].entry},
        "classes": {entry.id: entry for entry in catalogs["classes"].entry},
        "races": {entry.id: entry for entry in catalogs["races"].entry},
        "quests": {entry.id: entry for entry in catalogs["quests"].entry},
        "conditions": {entry.id: entry for entry in catalogs["conditions"].entry},
        "triggers": {entry.id: entry for entry in catalogs["triggers"].entry},
        "spells": {entry.id: entry for entry in catalogs["spells"].entry},
        "ranges": {entry.id: entry for entry in catalogs["ranges"].entry},
        "items": {entry.id: entry for entry in catalogs["items"].entry},
        "skills": {entry.id: entry for entry in catalogs["skills"].entry},
    }
    return modules, catalogs, indexes


def message_to_dict(message) -> dict:
    return MessageToDict(message, preserving_proto_field_name=True)


def parse_message_dict(message_cls, data: dict):
    message = message_cls()
    ParseDict(data, message, ignore_unknown_fields=False)
    return message


def safe_name(index: dict[int, object], entry_id: int, fallback_prefix: str = "Unknown") -> str | None:
    if entry_id == 0:
        return None
    entry = index.get(entry_id)
    if not entry:
        return f"{fallback_prefix}({entry_id})"
    return getattr(entry, "name", f"{fallback_prefix}({entry_id})")


def find_unit_by_name(catalogs: dict[str, object], name: str):
    folded = name.casefold()
    for entry in catalogs["units"].entry:
        if entry.name.casefold() == folded:
            return entry
    return None


def list_builtin_combat_scripts(project_root: Path) -> list[str]:
    registry_path = project_root / "src" / "shared" / "game_server" / "ai" / "creature_combat_script_registry.cpp"
    if not registry_path.is_file():
        return []
    content = registry_path.read_text(encoding="utf-8")
    scripts = re.findall(r'RegisterScript<[^>]+>\("([^"]+)"\)', content)
    return sorted(set(scripts))


def iter_spawns_for_unit(catalogs: dict[str, object], unit_id: int):
    for map_entry in catalogs["maps"].entry:
        for spawn in map_entry.unitspawns:
            if spawn.unitentry == unit_id:
                yield map_entry, spawn


def summarize_loot_entry(entry, indexes: dict[str, dict[int, object]]) -> dict:
    groups = []
    for group_index, group in enumerate(entry.groups):
        drops = []
        for definition in group.definitions:
            drops.append(
                {
                    "item": definition.item,
                    "item_name": safe_name(indexes["items"], definition.item),
                    "dropchance": definition.dropchance,
                    "mincount": definition.mincount if definition.HasField("mincount") else 1,
                    "maxcount": definition.maxcount if definition.HasField("maxcount") else 1,
                    "condition": definition.condition if definition.HasField("condition") else 0,
                    "condition_name": safe_name(indexes["conditions"], definition.condition if definition.HasField("condition") else 0),
                }
            )
        groups.append({"group_index": group_index, "drops": drops})
    return {
        "id": entry.id,
        "name": entry.name,
        "minmoney": entry.minmoney if entry.HasField("minmoney") else 0,
        "maxmoney": entry.maxmoney if entry.HasField("maxmoney") else 0,
        "groups": groups,
    }


def summarize_vendor_entry(entry, indexes: dict[str, dict[int, object]]) -> dict:
    items = []
    for vendor_item in entry.items:
        items.append(
            {
                "item": vendor_item.item,
                "item_name": safe_name(indexes["items"], vendor_item.item),
                "maxcount": vendor_item.maxcount if vendor_item.HasField("maxcount") else 0,
                "interval": vendor_item.interval if vendor_item.HasField("interval") else 0,
                "extendedcost": vendor_item.extendedcost if vendor_item.HasField("extendedcost") else 0,
            }
        )
    return {"id": entry.id, "name": entry.name, "items": items}


def summarize_trainer_entry(entry, indexes: dict[str, dict[int, object]]) -> dict:
    spells = []
    for trainer_spell in entry.spells:
        reqskill = trainer_spell.reqskill if trainer_spell.HasField("reqskill") else 0
        spells.append(
            {
                "spell": trainer_spell.spell,
                "spell_name": safe_name(indexes["spells"], trainer_spell.spell),
                "spellcost": trainer_spell.spellcost,
                "reqskill": reqskill,
                "reqskill_name": safe_name(indexes["skills"], reqskill),
                "reqskillval": trainer_spell.reqskillval if trainer_spell.HasField("reqskillval") else 0,
                "reqlevel": trainer_spell.reqlevel if trainer_spell.HasField("reqlevel") else 1,
            }
        )
    return {
        "id": entry.id,
        "name": entry.name,
        "type": entry.type if entry.HasField("type") else 0,
        "classid": entry.classid if entry.HasField("classid") else 0,
        "class_name": safe_name(indexes["classes"], entry.classid if entry.HasField("classid") else 0),
        "title": entry.title if entry.HasField("title") else "",
        "spells": spells,
    }


def summarize_gossip_menu(entry, indexes: dict[str, dict[int, object]]) -> dict:
    options = []
    for option in entry.options:
        condition_id = option.conditionId if option.HasField("conditionId") else 0
        options.append(
            {
                "id": option.id,
                "text": option.text,
                "action_type": option.action_type,
                "action_param": option.action_param if option.HasField("action_param") else 0,
                "conditionId": condition_id,
                "condition_name": safe_name(indexes["conditions"], condition_id),
            }
        )
    condition_id = entry.conditionId if entry.HasField("conditionId") else 0
    return {
        "id": entry.id,
        "name": entry.name,
        "text": entry.text if entry.HasField("text") else "",
        "show_quests": entry.show_quests if entry.HasField("show_quests") else False,
        "conditionId": condition_id,
        "condition_name": safe_name(indexes["conditions"], condition_id),
        "options": options,
    }


def summarize_spawn(map_entry, spawn, map_spawn_index: int | None = None) -> dict:
    replace_mode = "by_name" if spawn.HasField("name") and spawn.name else "replace_by_map_and_index"
    summary = {
        "map_id": map_entry.id,
        "map_name": map_entry.name,
        "replace_mode": replace_mode,
        "spawn": message_to_dict(spawn),
    }
    if replace_mode == "replace_by_map_and_index" and map_spawn_index is not None:
        summary["match_index"] = map_spawn_index
    return summary


def summarize_condition(entry) -> dict:
    return {
        "id": entry.id,
        "name": entry.name,
        "conditionType": entry.conditionType,
        "logicOperator": entry.logicOperator if entry.HasField("logicOperator") else 0,
        "param1": entry.param1 if entry.HasField("param1") else 0,
        "param2": entry.param2 if entry.HasField("param2") else 0,
        "param3": entry.param3 if entry.HasField("param3") else 0,
        "subConditionIds": list(entry.subConditionIds),
    }


def summarize_trigger(entry) -> dict:
    actions = []
    for action in entry.actions:
        actions.append(
            {
                "action": action.action if action.HasField("action") else 0,
                "target": action.target if action.HasField("target") else 0,
                "targetname": action.targetname if action.HasField("targetname") else "",
                "texts": list(action.texts),
                "data": list(action.data),
            }
        )
    events = [{"type": event.type, "data": list(event.data)} for event in entry.newevents]
    return {
        "id": entry.id,
        "name": entry.name,
        "probability": entry.probability if entry.HasField("probability") else 100,
        "events": events,
        "actions": actions,
    }


def summarize_unit_entry(unit, catalogs: dict[str, object], indexes: dict[str, dict[int, object]], project_root: Path) -> dict:
    loot_id = unit.unitlootentry if unit.HasField("unitlootentry") else 0
    vendor_id = unit.vendorentry if unit.HasField("vendorentry") else 0
    trainer_id = unit.trainerentry if unit.HasField("trainerentry") else 0
    model_ids = [unit.maleModel, unit.femaleModel]
    spawns = []
    for map_entry in catalogs["maps"].entry:
        for spawn_index, spawn in enumerate(map_entry.unitspawns):
            if spawn.unitentry == unit.id:
                spawns.append(summarize_spawn(map_entry, spawn, spawn_index))
    creature_spells = []
    for spell in unit.creaturespells:
        creature_spells.append(
            {
                "spellid": spell.spellid,
                "spell_name": safe_name(indexes["spells"], spell.spellid),
                "priority": spell.priority if spell.HasField("priority") else 100,
                "minrange": spell.minrange if spell.HasField("minrange") else 0.0,
                "maxrange": spell.maxrange if spell.HasField("maxrange") else 0.0,
                "probability": spell.probability if spell.HasField("probability") else 100,
            }
        )
    return {
        "unit": message_to_dict(unit),
        "resolved": {
            "faction_template_name": safe_name(indexes["faction_templates"], unit.factionTemplate),
            "faction_name": safe_name(indexes["factions"], indexes["faction_templates"][unit.factionTemplate].faction)
            if unit.factionTemplate in indexes["faction_templates"]
            else None,
            "male_model_name": safe_name(indexes["model_data"], model_ids[0]),
            "female_model_name": safe_name(indexes["model_data"], model_ids[1]),
            "unit_class_name": safe_name(indexes["unit_classes"], unit.unitClassId if unit.HasField("unitClassId") else 0),
            "loot_entry_name": None if loot_id in (0, NO_LOOT_ENTRY_ID) else safe_name(indexes["unit_loot"], loot_id),
            "vendor_entry_name": safe_name(indexes["vendors"], vendor_id),
            "trainer_entry_name": safe_name(indexes["trainers"], trainer_id),
            "quest_names": [safe_name(indexes["quests"], quest_id) for quest_id in unit.quests],
            "end_quest_names": [safe_name(indexes["quests"], quest_id) for quest_id in unit.end_quests],
            "gossip_menu_names": [safe_name(indexes["gossip_menus"], gossip_id) for gossip_id in unit.gossip_menus],
            "trigger_names": [safe_name(indexes["triggers"], trigger_id) for trigger_id in unit.triggers],
            "creature_spells": creature_spells,
            "available_combat_scripts": list_builtin_combat_scripts(project_root),
            "spawns": spawns,
        },
        "linked_entries": {
            "loot_entry": summarize_loot_entry(indexes["unit_loot"][loot_id], indexes)
            if loot_id not in (0, NO_LOOT_ENTRY_ID) and loot_id in indexes["unit_loot"]
            else None,
            "vendor_entry": summarize_vendor_entry(indexes["vendors"][vendor_id], indexes) if vendor_id in indexes["vendors"] else None,
            "trainer_entry": summarize_trainer_entry(indexes["trainers"][trainer_id], indexes) if trainer_id in indexes["trainers"] else None,
            "gossip_menus": [
                summarize_gossip_menu(indexes["gossip_menus"][gossip_id], indexes)
                for gossip_id in unit.gossip_menus
                if gossip_id in indexes["gossip_menus"]
            ],
        },
    }
