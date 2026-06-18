#!/usr/bin/env python3
"""Apply validated MMO NPC JSON back into project data."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from npc_catalog_lib import (
    NO_LOOT_ENTRY_ID,
    find_project_root,
    load_catalog_bundle,
    parse_message_dict,
)
from validate_npc_json import validate_document


def replace_or_append_by_id(container, message) -> str:
    for index, entry in enumerate(container):
        if entry.id == message.id:
            container[index].CopyFrom(message)
            return "updated"
    container.add().CopyFrom(message)
    return "created"


def backup_file(path: Path) -> None:
    backup_path = path.with_suffix(path.suffix + ".bak")
    shutil.copy2(path, backup_path)


def apply_spawn(map_entry, spawn_message, replace_mode: str) -> str:
    if replace_mode == "by_name" and spawn_message.HasField("name") and spawn_message.name:
        for index, existing in enumerate(map_entry.unitspawns):
            if existing.HasField("name") and existing.name == spawn_message.name:
                map_entry.unitspawns[index].CopyFrom(spawn_message)
                return "updated"
        map_entry.unitspawns.add().CopyFrom(spawn_message)
        return "created"

    if replace_mode == "replace_first_for_unit":
        for index, existing in enumerate(map_entry.unitspawns):
            if existing.unitentry == spawn_message.unitentry:
                map_entry.unitspawns[index].CopyFrom(spawn_message)
                return "updated"
        map_entry.unitspawns.add().CopyFrom(spawn_message)
        return "created"

    if replace_mode == "replace_by_map_and_index":
        raise ValueError("replace_by_map_and_index requires match_index and must be handled by the caller")

    map_entry.unitspawns.add().CopyFrom(spawn_message)
    return "created"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--backup", action="store_true")
    parser.add_argument("--apply-spawns", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    path = Path(args.path)
    project_root = find_project_root(args.project_root)
    doc = json.loads(path.read_text(encoding="utf-8"))
    errors = validate_document(doc, project_root)
    if errors:
        for message in errors:
            print(f"ERROR: {message}")
        return 1

    modules, catalogs, indexes = load_catalog_bundle(project_root)
    data_root = project_root / "data" / "editor" / "data"
    touched_files: list[Path] = []
    actions: list[str] = []

    unit_message = parse_message_dict(modules["units"].UnitEntry, doc["unit"])
    actions.append(f"unit {unit_message.id}: {replace_or_append_by_id(catalogs['units'].entry, unit_message)}")
    touched_files.append(data_root / "units.data")

    loot_data = doc.get("loot_entry")
    loot_link_id = unit_message.unitlootentry if unit_message.HasField("unitlootentry") else 0
    if isinstance(loot_data, dict) and loot_link_id not in (0, NO_LOOT_ENTRY_ID):
        loot_message = parse_message_dict(modules["loot_entry"].LootEntry, loot_data)
        actions.append(f"loot {loot_message.id}: {replace_or_append_by_id(catalogs['unit_loot'].entry, loot_message)}")
        touched_files.append(data_root / "unit_loot.data")

    vendor_data = doc.get("vendor_entry")
    if isinstance(vendor_data, dict):
        vendor_message = parse_message_dict(modules["vendors"].VendorEntry, vendor_data)
        actions.append(f"vendor {vendor_message.id}: {replace_or_append_by_id(catalogs['vendors'].entry, vendor_message)}")
        touched_files.append(data_root / "vendors.data")

    trainer_data = doc.get("trainer_entry")
    if isinstance(trainer_data, dict):
        trainer_message = parse_message_dict(modules["trainers"].TrainerEntry, trainer_data)
        actions.append(f"trainer {trainer_message.id}: {replace_or_append_by_id(catalogs['trainers'].entry, trainer_message)}")
        touched_files.append(data_root / "trainers.data")

    gossip_data = doc.get("gossip_menus", [])
    if isinstance(gossip_data, list) and gossip_data:
        for menu_data in gossip_data:
            menu_message = parse_message_dict(modules["gossip_menus"].GossipMenuEntry, menu_data)
            actions.append(f"gossip {menu_message.id}: {replace_or_append_by_id(catalogs['gossip_menus'].entry, menu_message)}")
        touched_files.append(data_root / "gossip_menus.data")

    spawn_data = doc.get("spawns", [])
    if args.apply_spawns and isinstance(spawn_data, list) and spawn_data:
        for spawn_wrapper in spawn_data:
            map_entry = indexes["maps"][spawn_wrapper["map_id"]]
            spawn_message = parse_message_dict(modules["maps"].UnitSpawnEntry, spawn_wrapper["spawn"])
            if not spawn_message.HasField("unitentry"):
                spawn_message.unitentry = unit_message.id
            replace_mode = spawn_wrapper.get("replace_mode", "append")
            if replace_mode == "replace_by_map_and_index":
                match_index = spawn_wrapper["match_index"]
                map_entry.unitspawns[match_index].CopyFrom(spawn_message)
                result = "updated"
            else:
                result = apply_spawn(map_entry, spawn_message, replace_mode)
            actions.append(f"spawn on map {map_entry.id}: {result}")
        touched_files.append(data_root / "maps.data")

    unique_touched = []
    seen = set()
    for file_path in touched_files:
        if file_path not in seen:
            seen.add(file_path)
            unique_touched.append(file_path)

    if args.dry_run:
        for action in actions:
            print(f"DRY-RUN: {action}")
        for file_path in unique_touched:
            print(f"DRY-RUN: would write {file_path}")
        return 0

    if args.backup:
        for file_path in unique_touched:
            backup_file(file_path)

    (data_root / "units.data").write_bytes(catalogs["units"].SerializeToString())
    if data_root / "unit_loot.data" in unique_touched:
        (data_root / "unit_loot.data").write_bytes(catalogs["unit_loot"].SerializeToString())
    if data_root / "vendors.data" in unique_touched:
        (data_root / "vendors.data").write_bytes(catalogs["vendors"].SerializeToString())
    if data_root / "trainers.data" in unique_touched:
        (data_root / "trainers.data").write_bytes(catalogs["trainers"].SerializeToString())
    if data_root / "gossip_menus.data" in unique_touched:
        (data_root / "gossip_menus.data").write_bytes(catalogs["gossip_menus"].SerializeToString())
    if data_root / "maps.data" in unique_touched:
        (data_root / "maps.data").write_bytes(catalogs["maps"].SerializeToString())

    for action in actions:
        print(f"OK: {action}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
