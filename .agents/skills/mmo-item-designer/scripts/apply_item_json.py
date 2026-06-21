#!/usr/bin/env python3
"""Apply validated MMO item JSON back into project data."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from item_catalog_lib import (
    data_dir,
    find_project_root,
    load_catalog_bundle,
    next_free_id,
    parse_message_dict,
    replace_or_append_by_id,
)
from validate_item_json import validate_document


def backup_file(path: Path) -> None:
    backup_path = path.with_suffix(path.suffix + ".bak")
    shutil.copy2(path, backup_path)


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
    errors = validate_document(doc, project_root)
    if errors:
        for message in errors:
            print(f"ERROR: {message}")
        return 1

    modules, catalogs, indexes = load_catalog_bundle(project_root)

    item_data = dict(doc["item"])
    display_data = dict(doc.get("item_display", {})) if isinstance(doc.get("item_display"), dict) else None

    item_id = int(item_data.get("id") or 0)
    if item_id == 0:
        item_id = next_free_id(indexes["items"])
    item_data["id"] = item_id

    assigned_display_id = 0
    if display_data is not None:
        assigned_display_id = int(display_data.get("id") or item_data.get("displayid") or 0)
        if assigned_display_id == 0:
            assigned_display_id = next_free_id(indexes["item_displays"])
        display_data["id"] = assigned_display_id
        if not display_data.get("name"):
            display_data["name"] = item_data["name"]
        item_data["displayid"] = assigned_display_id

    item_message = parse_message_dict(modules["items"].ItemEntry, item_data)
    item_action = replace_or_append_by_id(catalogs["items"].entry, item_message)

    display_action = None
    if display_data is not None:
        display_message = parse_message_dict(modules["item_display"].ItemDisplayEntry, display_data)
        display_action = replace_or_append_by_id(catalogs["item_displays"].entry, display_message)

    root = data_dir(project_root)
    touched = [root / "items.data"]
    if display_data is not None:
        touched.append(root / "item_displays.data")

    if args.dry_run:
        print(f"DRY-RUN: item {item_id}: {item_action}")
        if display_data is not None:
            print(f"DRY-RUN: item display {assigned_display_id}: {display_action}")
        for file_path in touched:
            print(f"DRY-RUN: would write {file_path}")
        return 0

    if args.backup:
        for file_path in touched:
            backup_file(file_path)

    (root / "items.data").write_bytes(catalogs["items"].SerializeToString())
    if display_data is not None:
        (root / "item_displays.data").write_bytes(catalogs["item_displays"].SerializeToString())

    print(f"OK: item {item_id}: {item_action}")
    if display_data is not None:
        print(f"OK: item display {assigned_display_id}: {display_action}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
