#!/usr/bin/env python3
"""Export one MMO quest and its quest-specific wiring into JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from quest_catalog_lib import (
    collect_links_for_quest,
    collect_related_triggers,
    find_project_root,
    load_catalog_bundle,
    message_to_dict,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--quest-id", type=int, required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--exclude-related-triggers", action="store_true")
    parser.add_argument("--exclude-area-triggers", action="store_true")
    args = parser.parse_args()

    project_root = find_project_root(args.project_root)
    modules, catalogs, indexes = load_catalog_bundle(project_root)
    quest = indexes["quests"].get(args.quest_id)
    if quest is None:
        raise SystemExit(f"Quest {args.quest_id} not found")

    links = collect_links_for_quest(quest.id, catalogs)
    doc = {
        "format": "mmo-quest",
        "version": 1,
        "quest": message_to_dict(quest),
        "providers": {
            "unit_ids": links["provider_units"],
            "object_ids": links["provider_objects"],
        },
        "enders": {
            "unit_ids": links["ender_units"],
            "object_ids": links["ender_objects"],
        },
        "gated_object_ids": links["gated_objects"],
    }

    related_trigger_ids, related_area_trigger_ids = collect_related_triggers(quest, catalogs)
    if not args.exclude_related_triggers:
        doc["attached_triggers"] = [
            message_to_dict(indexes["triggers"][trigger_id])
            for trigger_id in related_trigger_ids
            if trigger_id in indexes["triggers"]
        ]
    if not args.exclude_area_triggers:
        doc["attached_area_triggers"] = [
            message_to_dict(indexes["area_triggers"][trigger_id])
            for trigger_id in related_area_trigger_ids
            if trigger_id in indexes["area_triggers"]
        ]

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(doc, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(f"OK: exported quest {quest.id} to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
