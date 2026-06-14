#!/usr/bin/env python3
"""Validate an MMO quest JSON draft against live project data."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from quest_catalog_lib import (
    TRIGGER_ACTION_NAMES,
    find_project_root,
    load_catalog_bundle,
    parse_message_dict,
)


def add_error(errors: list[str], message: str) -> None:
    errors.append(message)


def add_warning(warnings: list[str], message: str) -> None:
    warnings.append(message)


def is_int_like(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def validate_id_list(value, label: str, valid_ids: set[int], kind: str, errors: list[str]) -> None:
    if not isinstance(value, list):
        add_error(errors, f"{label} must be an array")
        return
    seen = set()
    for index, entry_id in enumerate(value):
        if not is_int_like(entry_id) or entry_id <= 0:
            add_error(errors, f"{label}[{index}] must be a positive integer")
            continue
        if entry_id not in valid_ids:
            add_error(errors, f"{label}[{index}] references unknown {kind} {entry_id}")
        if entry_id in seen:
            add_error(errors, f"{label}[{index}] duplicates {kind} {entry_id}")
        seen.add(entry_id)


def validate_document(doc: dict, project_root: Path) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    modules, catalogs, indexes = load_catalog_bundle(project_root)

    if not isinstance(doc, dict):
        return ["root must be an object"], warnings

    if doc.get("format") != "mmo-quest":
        add_error(errors, "format must be 'mmo-quest'")
    if doc.get("version") != 1:
        add_error(errors, "version must be 1")

    quest_data = doc.get("quest")
    if not isinstance(quest_data, dict):
        add_error(errors, "quest must be an object")
        return errors, warnings

    try:
        quest_message = parse_message_dict(modules["quests"].QuestEntry, quest_data)
    except Exception as exc:
        add_error(errors, f"quest could not be parsed as QuestEntry: {exc}")
        return errors, warnings

    if not quest_message.HasField("id") or quest_message.id <= 0:
        add_error(errors, "quest.id must be a positive integer")
    if not quest_message.HasField("name") or not quest_message.name.strip():
        add_error(errors, "quest.name must be a non-empty string")

    if quest_message.HasField("minlevel") and quest_message.minlevel < 0:
        add_error(errors, "quest.minlevel must not be negative")
    if quest_message.HasField("maxlevel") and quest_message.maxlevel < 0:
        add_error(errors, "quest.maxlevel must not be negative")
    if quest_message.HasField("minlevel") and quest_message.HasField("maxlevel") and quest_message.maxlevel > 0 and quest_message.minlevel > quest_message.maxlevel:
        add_error(errors, "quest.minlevel must not exceed quest.maxlevel when maxlevel is set")
    if quest_message.HasField("questlevel") and quest_message.questlevel < 0:
        add_error(errors, "quest.questlevel must not be negative")
    if quest_message.HasField("type") and quest_message.type not in {0, 1, 2}:
        add_error(errors, "quest.type must be one of 0, 1, 2")
    if len(quest_message.requirements) > 4:
        add_error(errors, "quest.requirements may contain at most 4 entries because runtime quest counters are packed into 4 slots")

    quest_ids = set(indexes["quests"])
    item_ids = set(indexes["items"])
    unit_ids = set(indexes["units"])
    object_ids = set(indexes["objects"])
    spell_ids = set(indexes["spells"])
    skill_ids = set(indexes["skills"])
    class_ids = set(indexes["classes"])
    race_ids = set(indexes["races"])

    for field_name in ("prevquestid", "nextquestid", "nextchainquestid"):
        if quest_message.HasField(field_name):
            target_id = getattr(quest_message, field_name)
            if target_id == quest_message.id:
                add_error(errors, f"quest.{field_name} must not point to quest.id itself")
            if target_id != 0 and target_id not in quest_ids:
                add_warning(warnings, f"quest.{field_name} {target_id} is not present in live project data; ensure this is intentional when creating multiple new quests together")

    if quest_message.HasField("requiredskill") and quest_message.requiredskill and quest_message.requiredskill not in skill_ids:
        add_error(errors, f"quest.requiredskill {quest_message.requiredskill} is not present in project data")
    if quest_message.HasField("srcitemid") and quest_message.srcitemid and quest_message.srcitemid not in item_ids:
        add_error(errors, f"quest.srcitemid {quest_message.srcitemid} is not present in project data")
    if quest_message.HasField("srcitemcount") and quest_message.srcitemid and quest_message.srcitemcount < 1:
        add_error(errors, "quest.srcitemcount must be at least 1 when quest.srcitemid is set")
    if quest_message.HasField("srcspell") and quest_message.srcspell and quest_message.srcspell not in spell_ids:
        add_error(errors, f"quest.srcspell {quest_message.srcspell} is not present in project data")
    if quest_message.HasField("rewardspell") and quest_message.rewardspell and quest_message.rewardspell not in spell_ids:
        add_error(errors, f"quest.rewardspell {quest_message.rewardspell} is not present in project data")
    if quest_message.HasField("rewardspellcast") and quest_message.rewardspellcast and quest_message.rewardspellcast not in spell_ids:
        add_error(errors, f"quest.rewardspellcast {quest_message.rewardspellcast} is not present in project data")
    if quest_message.requiredraces < 0:
        add_error(errors, "quest.requiredraces must not be negative")
    if quest_message.requiredclasses < 0:
        add_error(errors, "quest.requiredclasses must not be negative")

    for reward_index, reward in enumerate(quest_message.rewarditemschoice):
        if reward.itemid not in item_ids:
            add_error(errors, f"quest.rewarditemschoice[{reward_index}].itemid {reward.itemid} is not present in project data")
        if reward.count < 1:
            add_error(errors, f"quest.rewarditemschoice[{reward_index}].count must be at least 1")

    for reward_index, reward in enumerate(quest_message.rewarditems):
        if reward.itemid not in item_ids:
            add_error(errors, f"quest.rewarditems[{reward_index}].itemid {reward.itemid} is not present in project data")
        if reward.count < 1:
            add_error(errors, f"quest.rewarditems[{reward_index}].count must be at least 1")

    for req_index, requirement in enumerate(quest_message.requirements):
        primary_modes = sum(
            1
            for active in (
                requirement.itemid != 0,
                requirement.creatureid != 0,
                requirement.objectid != 0,
            )
            if active
        )
        if primary_modes > 1:
            add_error(errors, f"quest.requirements[{req_index}] mixes multiple primary objective types; split it into separate requirement rows")
        if requirement.itemid and requirement.itemid not in item_ids:
            add_error(errors, f"quest.requirements[{req_index}].itemid {requirement.itemid} is not present in project data")
        if requirement.sourceid and requirement.sourceid not in item_ids:
            add_error(errors, f"quest.requirements[{req_index}].sourceid {requirement.sourceid} is not present in project data")
        if requirement.itemid and requirement.itemcount < 1:
            add_error(errors, f"quest.requirements[{req_index}].itemcount must be at least 1")
        if requirement.sourceid and requirement.sourcecount < 1:
            add_error(errors, f"quest.requirements[{req_index}].sourcecount must be at least 1")
        if requirement.creatureid and requirement.creatureid not in unit_ids:
            add_error(errors, f"quest.requirements[{req_index}].creatureid {requirement.creatureid} is not present in project data")
        if requirement.creatureid and requirement.creaturecount < 1:
            add_error(errors, f"quest.requirements[{req_index}].creaturecount must be at least 1")
        if requirement.objectid and requirement.objectid not in object_ids:
            add_error(errors, f"quest.requirements[{req_index}].objectid {requirement.objectid} is not present in project data")
        if requirement.objectid and requirement.objectcount < 1:
            add_error(errors, f"quest.requirements[{req_index}].objectcount must be at least 1")
        if requirement.spellcast and requirement.spellcast not in spell_ids:
            add_error(errors, f"quest.requirements[{req_index}].spellcast {requirement.spellcast} is not present in project data")
        if requirement.objectid and not requirement.spellcast:
            add_warning(warnings, f"quest.requirements[{req_index}] uses objectid without spellcast; pure object-use progress is not natively incremented by the current runtime")
        if not any((requirement.itemid, requirement.sourceid, requirement.creatureid, requirement.objectid, requirement.text.strip())):
            add_warning(warnings, f"quest.requirements[{req_index}] has no visible objective payload")

    providers = doc.get("providers", {})
    enders = doc.get("enders", {})
    gated_object_ids = doc.get("gated_object_ids", [])

    if providers and not isinstance(providers, dict):
        add_error(errors, "providers must be an object when present")
        providers = {}
    if enders and not isinstance(enders, dict):
        add_error(errors, "enders must be an object when present")
        enders = {}

    validate_id_list(providers.get("unit_ids", []), "providers.unit_ids", unit_ids, "unit", errors)
    validate_id_list(providers.get("object_ids", []), "providers.object_ids", object_ids, "object", errors)
    validate_id_list(enders.get("unit_ids", []), "enders.unit_ids", unit_ids, "unit", errors)
    validate_id_list(enders.get("object_ids", []), "enders.object_ids", object_ids, "object", errors)
    validate_id_list(gated_object_ids, "gated_object_ids", object_ids, "object", errors)

    if not providers.get("unit_ids", []) and not providers.get("object_ids", []):
        add_warning(warnings, "quest has no providers in the document; after apply it may become unreachable unless wired elsewhere")
    if not enders.get("unit_ids", []) and not enders.get("object_ids", []):
        add_warning(warnings, "quest has no enders in the document; after apply it may become impossible to turn in unless it is auto-rewarded or rewarded elsewhere")

    attached_triggers_data = doc.get("attached_triggers", [])
    attached_area_triggers_data = doc.get("attached_area_triggers", [])
    attached_trigger_ids: set[int] = set()
    completion_trigger_found = False

    if attached_triggers_data:
        if not isinstance(attached_triggers_data, list):
            add_error(errors, "attached_triggers must be an array")
        else:
            for index, trigger_data in enumerate(attached_triggers_data):
                if not isinstance(trigger_data, dict):
                    add_error(errors, f"attached_triggers[{index}] must be an object")
                    continue
                try:
                    trigger_message = parse_message_dict(modules["triggers"].TriggerEntry, trigger_data)
                except Exception as exc:
                    add_error(errors, f"attached_triggers[{index}] could not be parsed as TriggerEntry: {exc}")
                    continue
                if trigger_message.id <= 0:
                    add_error(errors, f"attached_triggers[{index}].id must be a positive integer")
                    continue
                if trigger_message.id in attached_trigger_ids:
                    add_error(errors, f"attached_triggers[{index}].id {trigger_message.id} is duplicated inside the document")
                attached_trigger_ids.add(trigger_message.id)
                for action_index, action in enumerate(trigger_message.actions):
                    action_type = action.action if action.HasField("action") else 0
                    if action_type == 17 and action.data and action.data[0] == quest_message.id:
                        completion_trigger_found = True
                    if action_type == 16 and action.data and action.data[0] not in unit_ids:
                        add_error(errors, f"attached_triggers[{index}].actions[{action_index}] grants quest kill credit for unknown unit {action.data[0]}")
                    if action_type == 6 and action.data and action.data[0] not in spell_ids:
                        add_error(errors, f"attached_triggers[{index}].actions[{action_index}] casts unknown spell {action.data[0]}")
                for event_index, event in enumerate(trigger_message.newevents):
                    if event.type == 18 and event.data and event.data[0] == quest_message.id:
                        completion_trigger_found = True

    if quest_message.starttriggers:
        add_warning(warnings, "quest.starttriggers is populated, but the current runtime does not execute QuestEntry.starttriggers directly")

    all_known_trigger_ids = set(indexes["triggers"]) | attached_trigger_ids
    for label in ("starttriggers", "failtriggers", "rewardtriggers"):
        for trigger_id in getattr(quest_message, label):
            if trigger_id not in all_known_trigger_ids:
                add_error(errors, f"quest.{label} references unknown trigger {trigger_id}")

    if attached_area_triggers_data:
        if not isinstance(attached_area_triggers_data, list):
            add_error(errors, "attached_area_triggers must be an array")
        else:
            seen_area_ids = set()
            for index, area_data in enumerate(attached_area_triggers_data):
                if not isinstance(area_data, dict):
                    add_error(errors, f"attached_area_triggers[{index}] must be an object")
                    continue
                try:
                    area_message = parse_message_dict(modules["area_triggers"].AreaTriggerEntry, area_data)
                except Exception as exc:
                    add_error(errors, f"attached_area_triggers[{index}] could not be parsed as AreaTriggerEntry: {exc}")
                    continue
                if area_message.id <= 0:
                    add_error(errors, f"attached_area_triggers[{index}].id must be a positive integer")
                    continue
                if area_message.id in seen_area_ids:
                    add_error(errors, f"attached_area_triggers[{index}].id {area_message.id} is duplicated inside the document")
                seen_area_ids.add(area_message.id)
                if area_message.map not in indexes["maps"]:
                    add_error(errors, f"attached_area_triggers[{index}].map {area_message.map} is not present in project data")
                has_radius = area_message.HasField("radius")
                has_box = area_message.HasField("box_x") and area_message.HasField("box_y") and area_message.HasField("box_z")
                if not has_radius and not has_box:
                    add_error(errors, f"attached_area_triggers[{index}] must define either radius or box dimensions")
                if area_message.on_enter_trigger and area_message.on_enter_trigger not in all_known_trigger_ids:
                    add_error(errors, f"attached_area_triggers[{index}].on_enter_trigger {area_message.on_enter_trigger} references an unknown trigger")
                if area_message.on_enter_trigger in attached_trigger_ids:
                    linked_trigger = None
                    for trigger_data in attached_triggers_data:
                        if isinstance(trigger_data, dict) and trigger_data.get("id") == area_message.on_enter_trigger:
                            linked_trigger = trigger_data
                            break
                    if linked_trigger:
                        for action in linked_trigger.get("actions", []):
                            if action.get("action") == 17 and isinstance(action.get("data"), list) and action["data"] and action["data"][0] == quest_message.id:
                                completion_trigger_found = True

    if quest_message.flags & 0x0100 and not completion_trigger_found:
        add_warning(warnings, "quest uses the Exploration flag but no attached completion trigger path to this quest was detected; verify area-trigger or scripted completion manually")

    if quest_message.rewardspellcast:
        add_warning(warnings, "quest.rewardspellcast is runtime-sensitive; the current NPC reward flow may cast it twice")

    return errors, warnings


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

    errors, warnings = validate_document(doc, project_root)
    for message in warnings:
        print(f"WARNING: {message}")
    if errors:
        for message in errors:
            print(f"ERROR: {message}")
        return 1

    print(f"OK: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
