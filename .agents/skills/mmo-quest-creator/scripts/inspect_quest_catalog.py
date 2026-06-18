#!/usr/bin/env python3
"""Inspect live MMO quest data and related dependencies."""

from __future__ import annotations

import argparse
import json

from quest_catalog_lib import (
    collect_links_for_quest,
    dump_json,
    find_project_root,
    find_quest,
    find_quest_by_name,
    load_catalog_bundle,
    summarize_area_trigger,
    summarize_quest,
    summarize_trigger,
)


def summarize_stats(catalogs: dict[str, object]) -> dict:
    quests = catalogs["quests"].entry
    stats = {
        "quest_count": len(quests),
        "kill_requirements": 0,
        "item_requirements": 0,
        "object_requirements": 0,
        "spellcast_requirements": 0,
        "exploration_flag_quests": 0,
        "provider_units": 0,
        "ender_units": 0,
        "provider_objects": 0,
        "ender_objects": 0,
        "gated_objects": 0,
    }
    for quest in quests:
        if quest.flags & 0x0100:
            stats["exploration_flag_quests"] += 1
        for requirement in quest.requirements:
            if requirement.creatureid:
                stats["kill_requirements"] += 1
            if requirement.itemid or requirement.sourceid:
                stats["item_requirements"] += 1
            if requirement.objectid:
                stats["object_requirements"] += 1
            if requirement.spellcast:
                stats["spellcast_requirements"] += 1
        links = collect_links_for_quest(quest.id, catalogs)
        stats["provider_units"] += len(links["provider_units"])
        stats["ender_units"] += len(links["ender_units"])
        stats["provider_objects"] += len(links["provider_objects"])
        stats["ender_objects"] += len(links["ender_objects"])
        stats["gated_objects"] += len(links["gated_objects"])
    return stats


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--quest-id", type=int)
    parser.add_argument("--name")
    parser.add_argument("--section", choices=["stats", "quests", "triggers", "area-triggers"], default="quests")
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    project_root = find_project_root(args.project_root)
    _, catalogs, indexes = load_catalog_bundle(project_root)

    if args.section == "stats":
        output = summarize_stats(catalogs)
    elif args.section == "triggers":
        output = [summarize_trigger(trigger) for trigger in catalogs["triggers"].entry]
    elif args.section == "area-triggers":
        output = [summarize_area_trigger(trigger, indexes) for trigger in catalogs["area_triggers"].entry]
    else:
        if args.quest_id is not None:
            quest = find_quest(indexes, args.quest_id)
            if quest is None:
                raise SystemExit(f"Quest {args.quest_id} not found")
            output = summarize_quest(quest, catalogs, indexes)
        elif args.name:
            quest = find_quest_by_name(catalogs, args.name)
            if quest is None:
                raise SystemExit(f"Quest '{args.name}' not found")
            output = summarize_quest(quest, catalogs, indexes)
        else:
            output = [
                {
                    "id": quest.id,
                    "name": quest.name,
                    "internalname": quest.internalname if quest.HasField("internalname") else "",
                    "questlevel": quest.questlevel if quest.HasField("questlevel") else 0,
                    "minlevel": quest.minlevel if quest.HasField("minlevel") else 0,
                    "type": quest.type if quest.HasField("type") else 0,
                    "flags": quest.flags if quest.HasField("flags") else 0,
                }
                for quest in catalogs["quests"].entry
            ]

    if args.pretty:
        print(dump_json(output))
    else:
        print(json.dumps(output, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
