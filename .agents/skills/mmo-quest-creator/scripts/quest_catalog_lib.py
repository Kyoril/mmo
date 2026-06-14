#!/usr/bin/env python3
"""Shared helpers for inspecting and mutating MMO quest data."""

from __future__ import annotations

import json
from pathlib import Path

from google.protobuf.json_format import MessageToDict, ParseDict

from proto_runtime import find_project_root, load_modules


QUEST_FLAG_NAMES = {
    0x0001: "StayAlive",
    0x0002: "PartyAccept",
    0x0004: "Sharable",
    0x0008: "Raid",
    0x0010: "HiddenRewards",
    0x0020: "AutoRewarded",
    0x0040: "Daily",
    0x0080: "Weekly",
    0x0100: "Exploration",
}

QUEST_TYPE_NAMES = {
    0: "TurnIn",
    1: "Task",
    2: "Quest",
}

TRIGGER_EVENT_NAMES = {
    0: "OnSpawn",
    1: "OnDespawn",
    2: "OnAggro",
    3: "OnKilled",
    4: "OnKill",
    5: "OnDamaged",
    6: "OnHealed",
    7: "OnAttackSwing",
    8: "OnReset",
    9: "OnReachedHome",
    10: "OnInteraction",
    11: "OnHealthDroppedBelow",
    12: "OnReachedTriggeredTarget",
    13: "OnSpellHit",
    14: "OnSpellAuraRemoved",
    15: "OnEmote",
    16: "OnSpellCast",
    17: "OnGossipAction",
    18: "OnQuestAccept",
}

TRIGGER_ACTION_NAMES = {
    0: "Trigger",
    1: "Say",
    2: "Yell",
    3: "SetWorldObjectState",
    4: "SetSpawnState",
    5: "SetRespawnState",
    6: "CastSpell",
    7: "Delay",
    8: "MoveTo",
    9: "SetCombatMovement",
    10: "StopAutoAttack",
    11: "CancelCast",
    12: "SetStandState",
    13: "SetVirtualEquipmentSlot",
    14: "SetPhase",
    15: "SetSpellCooldown",
    16: "QuestKillCredit",
    17: "QuestEventOrExploration",
    18: "SetVariable",
    19: "Dismount",
    20: "SetMount",
    21: "Despawn",
    22: "Teleport",
    23: "Emote",
}

TRIGGER_TARGET_NAMES = {
    0: "None",
    1: "OwningObject",
    2: "OwningUnitVictim",
    3: "RandomUnit",
    4: "NamedWorldObject",
    5: "NamedCreature",
    6: "TriggeringUnit",
}


CATALOG_FACTORIES = {
    "quests": lambda mods: mods["quests"].Quests(),
    "units": lambda mods: mods["units"].Units(),
    "objects": lambda mods: mods["objects"].Objects(),
    "items": lambda mods: mods["items"].Items(),
    "spells": lambda mods: mods["spells"].Spells(),
    "classes": lambda mods: mods["classes"].Classes(),
    "races": lambda mods: mods["races"].Races(),
    "skills": lambda mods: mods["skills"].Skills(),
    "triggers": lambda mods: mods["triggers"].Triggers(),
    "area_triggers": lambda mods: mods["area_triggers"].AreaTriggers(),
    "maps": lambda mods: mods["maps"].Maps(),
}


CATALOG_FILES = {
    "quests": "quests.data",
    "units": "units.data",
    "objects": "objects.data",
    "items": "items.data",
    "spells": "spells.data",
    "classes": "classes.data",
    "races": "races.data",
    "skills": "skills.data",
    "triggers": "triggers.data",
    "area_triggers": "area_triggers.data",
    "maps": "maps.data",
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
        key: {entry.id: entry for entry in catalogs[key].entry}
        for key in catalogs
    }
    return modules, catalogs, indexes


def message_to_dict(message) -> dict:
    return MessageToDict(
        message,
        preserving_proto_field_name=True,
        use_integers_for_enums=True,
    )


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


def decode_mask(mask: int, catalogs_key: str, catalogs: dict[str, object]) -> list[dict]:
    result = []
    if mask <= 0:
        return result
    for entry in catalogs[catalogs_key].entry:
        if entry.id <= 0:
            continue
        bit = 1 << (entry.id - 1)
        if mask & bit:
            result.append({"id": entry.id, "name": entry.name, "bit": bit})
    return result


def decode_quest_flags(flags: int) -> list[str]:
    return [name for bit, name in QUEST_FLAG_NAMES.items() if flags & bit]


def find_quest(indexes: dict[str, dict[int, object]], quest_id: int):
    return indexes["quests"].get(quest_id)


def find_quest_by_name(catalogs: dict[str, object], name: str):
    lowered = name.casefold()
    for quest in catalogs["quests"].entry:
        if quest.name.casefold() == lowered:
            return quest
    return None


def summarize_requirement(requirement, indexes: dict[str, dict[int, object]]) -> dict:
    data = message_to_dict(requirement)
    if requirement.itemid:
        data["item_name"] = safe_name(indexes["items"], requirement.itemid)
    if requirement.sourceid:
        data["source_item_name"] = safe_name(indexes["items"], requirement.sourceid)
    if requirement.creatureid:
        data["creature_name"] = safe_name(indexes["units"], requirement.creatureid)
    if requirement.objectid:
        data["object_name"] = safe_name(indexes["objects"], requirement.objectid)
    if requirement.spellcast:
        data["spell_name"] = safe_name(indexes["spells"], requirement.spellcast)
    return data


def summarize_trigger(trigger) -> dict:
    actions = []
    for action in trigger.actions:
        actions.append(
            {
                "action": action.action if action.HasField("action") else 0,
                "action_name": TRIGGER_ACTION_NAMES.get(action.action if action.HasField("action") else 0, "Unknown"),
                "target": action.target if action.HasField("target") else 0,
                "target_name": TRIGGER_TARGET_NAMES.get(action.target if action.HasField("target") else 0, "Unknown"),
                "targetname": action.targetname if action.HasField("targetname") else "",
                "data": list(action.data),
                "texts": list(action.texts),
            }
        )
    events = []
    for event in trigger.newevents:
        events.append(
            {
                "type": event.type,
                "type_name": TRIGGER_EVENT_NAMES.get(event.type, "Unknown"),
                "data": list(event.data),
            }
        )
    return {
        "id": trigger.id,
        "name": trigger.name,
        "flags": trigger.flags if trigger.HasField("flags") else 0,
        "probability": trigger.probability if trigger.HasField("probability") else 100,
        "events": events,
        "actions": actions,
    }


def summarize_area_trigger(area_trigger, indexes: dict[str, dict[int, object]]) -> dict:
    data = message_to_dict(area_trigger)
    if area_trigger.on_enter_trigger:
        data["on_enter_trigger_name"] = safe_name(indexes["triggers"], area_trigger.on_enter_trigger)
    data["map_name"] = safe_name(indexes["maps"], area_trigger.map, "Map")
    return data


def collect_links_for_quest(quest_id: int, catalogs: dict[str, object]) -> dict[str, list[int]]:
    provider_units = [entry.id for entry in catalogs["units"].entry if quest_id in entry.quests]
    ender_units = [entry.id for entry in catalogs["units"].entry if quest_id in entry.end_quests]
    provider_objects = [entry.id for entry in catalogs["objects"].entry if quest_id in entry.quests]
    ender_objects = [entry.id for entry in catalogs["objects"].entry if quest_id in entry.end_quests]
    gated_objects = [
        entry.id
        for entry in catalogs["objects"].entry
        if entry.HasField("requiredquest") and entry.requiredquest == quest_id
    ]
    return {
        "provider_units": provider_units,
        "ender_units": ender_units,
        "provider_objects": provider_objects,
        "ender_objects": ender_objects,
        "gated_objects": gated_objects,
    }


def collect_related_triggers(quest, catalogs: dict[str, object]) -> tuple[list[int], list[int]]:
    direct_trigger_ids = []
    area_trigger_ids = []
    seen_direct = set()
    seen_area = set()
    trigger_index = {entry.id: entry for entry in catalogs["triggers"].entry}

    for trigger_id in list(quest.starttriggers) + list(quest.failtriggers) + list(quest.rewardtriggers):
        if trigger_id not in seen_direct:
            direct_trigger_ids.append(trigger_id)
            seen_direct.add(trigger_id)

    for unit in catalogs["units"].entry:
        if quest.id not in unit.quests and quest.id not in unit.end_quests:
            continue
        for trigger_id in unit.triggers:
            trigger = trigger_index.get(trigger_id)
            if not trigger:
                continue
            for event in trigger.newevents:
                if event.type == 18 and list(event.data[:1]) == [quest.id]:
                    if trigger_id not in seen_direct:
                        direct_trigger_ids.append(trigger_id)
                        seen_direct.add(trigger_id)

    for trigger in catalogs["triggers"].entry:
        trigger_matches = False
        for action in trigger.actions:
            action_type = action.action if action.HasField("action") else 0
            if action_type == 17 and action.data and action.data[0] == quest.id:
                trigger_matches = True
                break
        if not trigger_matches:
            continue
        if trigger.id not in seen_direct:
            direct_trigger_ids.append(trigger.id)
            seen_direct.add(trigger.id)
        for area_trigger in catalogs["area_triggers"].entry:
            if area_trigger.on_enter_trigger == trigger.id and area_trigger.id not in seen_area:
                area_trigger_ids.append(area_trigger.id)
                seen_area.add(area_trigger.id)

    return direct_trigger_ids, area_trigger_ids


def summarize_quest(quest, catalogs: dict[str, object], indexes: dict[str, dict[int, object]]) -> dict:
    links = collect_links_for_quest(quest.id, catalogs)
    related_trigger_ids, related_area_trigger_ids = collect_related_triggers(quest, catalogs)
    data = message_to_dict(quest)
    data["type_name"] = QUEST_TYPE_NAMES.get(quest.type if quest.HasField("type") else 0, "Unknown")
    data["flag_names"] = decode_quest_flags(quest.flags if quest.HasField("flags") else 0)
    data["requirements"] = [summarize_requirement(req, indexes) for req in quest.requirements]
    data["rewarditemschoice"] = [
        {
            **message_to_dict(reward),
            "item_name": safe_name(indexes["items"], reward.itemid),
        }
        for reward in quest.rewarditemschoice
    ]
    data["rewarditems"] = [
        {
            **message_to_dict(reward),
            "item_name": safe_name(indexes["items"], reward.itemid),
        }
        for reward in quest.rewarditems
    ]
    data["required_races"] = decode_mask(quest.requiredraces, "races", catalogs) if quest.requiredraces else []
    data["required_classes"] = decode_mask(quest.requiredclasses, "classes", catalogs) if quest.requiredclasses else []
    data["required_skill_name"] = safe_name(indexes["skills"], quest.requiredskill) if quest.requiredskill else None
    data["source_item_name"] = safe_name(indexes["items"], quest.srcitemid) if quest.srcitemid else None
    data["source_spell_name"] = safe_name(indexes["spells"], quest.srcspell) if quest.srcspell else None
    data["reward_spell_name"] = safe_name(indexes["spells"], quest.rewardspell) if quest.rewardspell else None
    data["reward_spell_cast_name"] = safe_name(indexes["spells"], quest.rewardspellcast) if quest.rewardspellcast else None
    data["prev_quest_name"] = safe_name(indexes["quests"], quest.prevquestid, "Quest") if quest.HasField("prevquestid") else None
    data["next_quest_name"] = safe_name(indexes["quests"], quest.nextquestid, "Quest") if quest.HasField("nextquestid") else None
    data["next_chain_quest_name"] = safe_name(indexes["quests"], quest.nextchainquestid, "Quest") if quest.HasField("nextchainquestid") else None
    data["providers"] = {
        "units": [{"id": unit_id, "name": safe_name(indexes["units"], unit_id)} for unit_id in links["provider_units"]],
        "objects": [{"id": object_id, "name": safe_name(indexes["objects"], object_id)} for object_id in links["provider_objects"]],
    }
    data["enders"] = {
        "units": [{"id": unit_id, "name": safe_name(indexes["units"], unit_id)} for unit_id in links["ender_units"]],
        "objects": [{"id": object_id, "name": safe_name(indexes["objects"], object_id)} for object_id in links["ender_objects"]],
    }
    data["gated_objects"] = [{"id": object_id, "name": safe_name(indexes["objects"], object_id)} for object_id in links["gated_objects"]]
    data["related_triggers"] = [
        summarize_trigger(indexes["triggers"][trigger_id])
        for trigger_id in related_trigger_ids
        if trigger_id in indexes["triggers"]
    ]
    data["related_area_triggers"] = [
        summarize_area_trigger(indexes["area_triggers"][area_trigger_id], indexes)
        for area_trigger_id in related_area_trigger_ids
        if area_trigger_id in indexes["area_triggers"]
    ]
    return data


def dump_json(data) -> str:
    return json.dumps(data, indent=2, ensure_ascii=True)
