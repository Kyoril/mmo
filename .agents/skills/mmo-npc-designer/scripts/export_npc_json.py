#!/usr/bin/env python3
"""Export one MMO NPC or creature and its linked rows to JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from npc_catalog_lib import (
    NO_LOOT_ENTRY_ID,
    find_project_root,
    load_catalog_bundle,
    message_to_dict,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--unit-id", type=int, required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--exclude-spawns", action="store_true")
    args = parser.parse_args()

    project_root = find_project_root(args.project_root)
    _, catalogs, indexes = load_catalog_bundle(project_root)
    unit = indexes["units"].get(args.unit_id)
    if unit is None:
        raise SystemExit(f"Unit {args.unit_id} not found")

    loot_id = unit.unitlootentry if unit.HasField("unitlootentry") else 0
    vendor_id = unit.vendorentry if unit.HasField("vendorentry") else 0
    trainer_id = unit.trainerentry if unit.HasField("trainerentry") else 0

    doc = {
        "format": "mmo-npc",
        "version": 1,
        "unit": message_to_dict(unit),
    }

    if loot_id not in (0, NO_LOOT_ENTRY_ID) and loot_id in indexes["unit_loot"]:
        doc["loot_entry"] = message_to_dict(indexes["unit_loot"][loot_id])
    if vendor_id in indexes["vendors"]:
        doc["vendor_entry"] = message_to_dict(indexes["vendors"][vendor_id])
    if trainer_id in indexes["trainers"]:
        doc["trainer_entry"] = message_to_dict(indexes["trainers"][trainer_id])
    if unit.gossip_menus:
        doc["gossip_menus"] = [
            message_to_dict(indexes["gossip_menus"][gossip_id])
            for gossip_id in unit.gossip_menus
            if gossip_id in indexes["gossip_menus"]
        ]
    if not args.exclude_spawns:
        spawns = []
        for map_entry in catalogs["maps"].entry:
            for spawn_index, spawn in enumerate(map_entry.unitspawns):
                if spawn.unitentry == unit.id:
                    replace_mode = "by_name" if spawn.HasField("name") and spawn.name else "replace_by_map_and_index"
                    spawns.append(
                        {
                            "map_id": map_entry.id,
                            "replace_mode": replace_mode,
                            "spawn": message_to_dict(spawn),
                            **({"match_index": spawn_index} if replace_mode == "replace_by_map_and_index" else {}),
                        }
                    )
        if spawns:
            doc["spawns"] = spawns

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(doc, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(f"OK: exported unit {unit.id} to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
