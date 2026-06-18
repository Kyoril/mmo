#!/usr/bin/env python3
"""Inspect MMO NPC and creature catalogs."""

from __future__ import annotations

import argparse
import json

from npc_catalog_lib import (
    find_project_root,
    find_unit_by_name,
    list_builtin_combat_scripts,
    load_catalog_bundle,
    summarize_condition,
    summarize_gossip_menu,
    summarize_loot_entry,
    summarize_spawn,
    summarize_trainer_entry,
    summarize_trigger,
    summarize_unit_entry,
    summarize_vendor_entry,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument(
        "--section",
        choices=[
            "all",
            "units",
            "factions",
            "faction_templates",
            "loot",
            "vendors",
            "trainers",
            "gossip",
            "spawns",
            "unit_classes",
            "models",
            "conditions",
            "triggers",
            "scripts",
        ],
        default="all",
    )
    parser.add_argument("--unit-id", type=int)
    parser.add_argument("--unit-name")
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    project_root = find_project_root(args.project_root)
    _, catalogs, indexes = load_catalog_bundle(project_root)

    unit = None
    if args.unit_id is not None:
        unit = indexes["units"].get(args.unit_id)
        if unit is None:
            raise SystemExit(f"Unit {args.unit_id} not found")
    elif args.unit_name:
        unit = find_unit_by_name(catalogs, args.unit_name)
        if unit is None:
            raise SystemExit(f"Unit named '{args.unit_name}' not found")

    if unit is not None:
        output = summarize_unit_entry(unit, catalogs, indexes, project_root)
    elif args.section == "units":
        output = {
            "units": [
                summarize_unit_entry(entry, catalogs, indexes, project_root)
                for entry in catalogs["units"].entry[: args.limit]
            ]
        }
    elif args.section == "factions":
        output = {
            "factions": [
                {"id": entry.id, "name": entry.name}
                for entry in catalogs["factions"].entry[: args.limit]
            ]
        }
    elif args.section == "faction_templates":
        output = {
            "faction_templates": [
                {
                    "id": entry.id,
                    "name": entry.name,
                    "faction": entry.faction,
                    "friends": list(entry.friends),
                    "enemies": list(entry.enemies),
                }
                for entry in catalogs["faction_templates"].entry[: args.limit]
            ]
        }
    elif args.section == "loot":
        output = {
            "unit_loot": [
                summarize_loot_entry(entry, indexes)
                for entry in catalogs["unit_loot"].entry[: args.limit]
            ]
        }
    elif args.section == "vendors":
        output = {
            "vendors": [
                summarize_vendor_entry(entry, indexes)
                for entry in catalogs["vendors"].entry[: args.limit]
            ]
        }
    elif args.section == "trainers":
        output = {
            "trainers": [
                summarize_trainer_entry(entry, indexes)
                for entry in catalogs["trainers"].entry[: args.limit]
            ]
        }
    elif args.section == "gossip":
        output = {
            "gossip_menus": [
                summarize_gossip_menu(entry, indexes)
                for entry in catalogs["gossip_menus"].entry[: args.limit]
            ]
        }
    elif args.section == "spawns":
        spawns = []
        for map_entry in catalogs["maps"].entry:
            for spawn_index, spawn in enumerate(map_entry.unitspawns):
                spawns.append(summarize_spawn(map_entry, spawn, spawn_index))
        output = {"spawns": spawns[: args.limit]}
    elif args.section == "unit_classes":
        output = {
            "unit_classes": [
                {
                    "id": entry.id,
                    "name": entry.name,
                    "internalName": entry.internalName,
                    "powertype": entry.powertype,
                    "level_count": len(entry.levelbasevalues),
                }
                for entry in catalogs["unit_classes"].entry[: args.limit]
            ]
        }
    elif args.section == "models":
        output = {
            "models": [
                {"id": entry.id, "name": entry.name, "filename": entry.filename}
                for entry in catalogs["model_data"].entry[: args.limit]
            ]
        }
    elif args.section == "conditions":
        output = {
            "conditions": [
                summarize_condition(entry)
                for entry in catalogs["conditions"].entry[: args.limit]
            ]
        }
    elif args.section == "triggers":
        output = {
            "triggers": [
                summarize_trigger(entry)
                for entry in catalogs["triggers"].entry[: args.limit]
            ]
        }
    elif args.section == "scripts":
        output = {"combat_scripts": list_builtin_combat_scripts(project_root)}
    else:
        output = {
            "units": [
                {
                    "id": entry.id,
                    "name": entry.name,
                    "factionTemplate": entry.factionTemplate,
                    "vendorentry": entry.vendorentry if entry.HasField("vendorentry") else 0,
                    "trainerentry": entry.trainerentry if entry.HasField("trainerentry") else 0,
                    "unitlootentry": entry.unitlootentry if entry.HasField("unitlootentry") else 0,
                }
                for entry in catalogs["units"].entry[: args.limit]
            ],
            "faction_templates": [
                {"id": entry.id, "name": entry.name}
                for entry in catalogs["faction_templates"].entry[: args.limit]
            ],
            "vendors": [{"id": entry.id, "name": entry.name} for entry in catalogs["vendors"].entry[: args.limit]],
            "trainers": [{"id": entry.id, "name": entry.name} for entry in catalogs["trainers"].entry[: args.limit]],
            "gossip_menus": [{"id": entry.id, "name": entry.name} for entry in catalogs["gossip_menus"].entry[: args.limit]],
            "combat_scripts": list_builtin_combat_scripts(project_root),
        }

    print(json.dumps(output, indent=2 if args.pretty else None, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
