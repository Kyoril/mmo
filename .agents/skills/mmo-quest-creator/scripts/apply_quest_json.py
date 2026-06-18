#!/usr/bin/env python3
"""Apply validated MMO quest JSON back into project data."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from quest_catalog_lib import (
    find_project_root,
    load_catalog_bundle,
    parse_message_dict,
)
from validate_quest_json import validate_document


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


def reconcile_quest_links(entries, quest_id: int, field_name: str, target_ids: set[int]) -> None:
    for entry in entries:
        values = list(getattr(entry, field_name))
        filtered = [value for value in values if value != quest_id]
        target_contains = entry.id in target_ids
        if target_contains and quest_id not in filtered:
            filtered.append(quest_id)
        repeated = getattr(entry, field_name)
        del repeated[:]
        repeated.extend(filtered)


def reconcile_required_objects(entries, quest_id: int, target_ids: set[int]) -> None:
    for entry in entries:
        currently_gated = entry.HasField("requiredquest") and entry.requiredquest == quest_id
        should_gate = entry.id in target_ids
        if should_gate:
            entry.requiredquest = quest_id
        elif currently_gated:
            entry.requiredquest = 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--backup", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    path = Path(args.path)
    project_root = find_project_root(args.project_root)
    doc = json.loads(path.read_text(encoding="utf-8"))
    errors, warnings = validate_document(doc, project_root)
    for message in warnings:
        print(f"WARNING: {message}")
    if errors:
        for message in errors:
            print(f"ERROR: {message}")
        return 1

    modules, catalogs, indexes = load_catalog_bundle(project_root)
    data_root = project_root / "data" / "editor" / "data"
    touched_files: list[Path] = []
    actions: list[str] = []

    quest_message = parse_message_dict(modules["quests"].QuestEntry, doc["quest"])
    providers = doc.get("providers", {})
    enders = doc.get("enders", {})
    gated_object_ids = set(doc.get("gated_object_ids", []))

    actions.append(f"quest {quest_message.id}: {replace_or_append_by_id(catalogs['quests'].entry, quest_message)}")
    touched_files.append(data_root / "quests.data")

    attached_triggers = doc.get("attached_triggers", [])
    if isinstance(attached_triggers, list) and attached_triggers:
        for trigger_data in attached_triggers:
            trigger_message = parse_message_dict(modules["triggers"].TriggerEntry, trigger_data)
            actions.append(f"trigger {trigger_message.id}: {replace_or_append_by_id(catalogs['triggers'].entry, trigger_message)}")
        touched_files.append(data_root / "triggers.data")

    attached_area_triggers = doc.get("attached_area_triggers", [])
    if isinstance(attached_area_triggers, list) and attached_area_triggers:
        for area_data in attached_area_triggers:
            area_message = parse_message_dict(modules["area_triggers"].AreaTriggerEntry, area_data)
            actions.append(f"area trigger {area_message.id}: {replace_or_append_by_id(catalogs['area_triggers'].entry, area_message)}")
        touched_files.append(data_root / "area_triggers.data")

    provider_unit_ids = set(providers.get("unit_ids", []))
    provider_object_ids = set(providers.get("object_ids", []))
    ender_unit_ids = set(enders.get("unit_ids", []))
    ender_object_ids = set(enders.get("object_ids", []))

    reconcile_quest_links(catalogs["units"].entry, quest_message.id, "quests", provider_unit_ids)
    reconcile_quest_links(catalogs["units"].entry, quest_message.id, "end_quests", ender_unit_ids)
    reconcile_quest_links(catalogs["objects"].entry, quest_message.id, "quests", provider_object_ids)
    reconcile_quest_links(catalogs["objects"].entry, quest_message.id, "end_quests", ender_object_ids)
    reconcile_required_objects(catalogs["objects"].entry, quest_message.id, gated_object_ids)
    actions.append(f"provider units: {sorted(provider_unit_ids)}")
    actions.append(f"ender units: {sorted(ender_unit_ids)}")
    actions.append(f"provider objects: {sorted(provider_object_ids)}")
    actions.append(f"ender objects: {sorted(ender_object_ids)}")
    actions.append(f"gated objects: {sorted(gated_object_ids)}")
    touched_files.append(data_root / "units.data")
    touched_files.append(data_root / "objects.data")

    unique_touched: list[Path] = []
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

    (data_root / "quests.data").write_bytes(catalogs["quests"].SerializeToString())
    (data_root / "units.data").write_bytes(catalogs["units"].SerializeToString())
    (data_root / "objects.data").write_bytes(catalogs["objects"].SerializeToString())
    if data_root / "triggers.data" in unique_touched:
        (data_root / "triggers.data").write_bytes(catalogs["triggers"].SerializeToString())
    if data_root / "area_triggers.data" in unique_touched:
        (data_root / "area_triggers.data").write_bytes(catalogs["area_triggers"].SerializeToString())

    for action in actions:
        print(f"OK: {action}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
