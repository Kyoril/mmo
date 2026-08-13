#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Apply encounter JSON (trigger rows, map encounter slots, instance triggers) into project data.

The NPC applier owns units, loot and spawns; the quest applier can carry trigger rows but
only alongside a quest. Neither can write a boss encounter's triggers or a map's encounter
slots on their own, which is what this script is for.

Cross-references are intentionally not validated: an encounter's triggers routinely name
creature entries that the NPC applier has not created yet, and forcing the opposite order
would make a unit's trigger list unresolvable instead. Apply triggers first, units second.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from npc_catalog_lib import (
	find_project_root,
	load_catalog_bundle,
	parse_message_dict,
)


def replace_or_append_by_id(container, message) -> str:
	for index, entry in enumerate(container):
		if entry.id == message.id:
			container[index].CopyFrom(message)
			return "updated"
	container.add().CopyFrom(message)
	return "created"


def apply_encounter(map_entry, encounter_message) -> str:
	return replace_or_append_by_id(map_entry.encounters, encounter_message)


def apply_instance_triggers(map_entry, trigger_ids) -> str:
	added = []
	for trigger_id in trigger_ids:
		if trigger_id not in list(map_entry.instance_triggers):
			map_entry.instance_triggers.append(trigger_id)
			added.append(trigger_id)
	if not added:
		return "unchanged"
	return "added " + ", ".join(str(value) for value in added)


def backup_file(path: Path) -> None:
	shutil.copy2(path, path.with_suffix(path.suffix + ".bak"))


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("path")
	parser.add_argument("--project-root", default=None)
	parser.add_argument("--backup", action="store_true")
	parser.add_argument("--dry-run", action="store_true")
	args = parser.parse_args()

	project_root = find_project_root(args.project_root)
	doc = json.loads(Path(args.path).read_text(encoding="utf-8"))

	if doc.get("format") != "mmo-encounter":
		print("ERROR: expected \"format\": \"mmo-encounter\"")
		return 1

	modules, catalogs, indexes = load_catalog_bundle(project_root)
	data_root = project_root / "data" / "editor" / "data"
	touched: list[Path] = []
	actions: list[str] = []

	for trigger_data in doc.get("triggers", []):
		message = parse_message_dict(modules["triggers"].TriggerEntry, trigger_data)
		actions.append(f"trigger {message.id}: {replace_or_append_by_id(catalogs['triggers'].entry, message)}")
		if data_root / "triggers.data" not in touched:
			touched.append(data_root / "triggers.data")

	for wrapper in doc.get("map_encounters", []):
		map_entry = indexes["maps"][wrapper["map_id"]]
		message = parse_message_dict(modules["maps"].EncounterEntry, wrapper["encounter"])
		actions.append(f"encounter {message.id} on map {map_entry.id}: {apply_encounter(map_entry, message)}")
		if data_root / "maps.data" not in touched:
			touched.append(data_root / "maps.data")

	for wrapper in doc.get("map_instance_triggers", []):
		map_entry = indexes["maps"][wrapper["map_id"]]
		result = apply_instance_triggers(map_entry, wrapper["trigger_ids"])
		actions.append(f"instance triggers on map {map_entry.id}: {result}")
		if data_root / "maps.data" not in touched:
			touched.append(data_root / "maps.data")

	if not touched:
		print("Nothing to do: document contained no triggers, encounters or instance triggers")
		return 1

	if args.dry_run:
		for action in actions:
			print(f"DRY-RUN: {action}")
		for path in touched:
			print(f"DRY-RUN: would write {path}")
		return 0

	if args.backup:
		for path in touched:
			backup_file(path)

	if data_root / "triggers.data" in touched:
		(data_root / "triggers.data").write_bytes(catalogs["triggers"].SerializeToString())
	if data_root / "maps.data" in touched:
		(data_root / "maps.data").write_bytes(catalogs["maps"].SerializeToString())

	for action in actions:
		print(f"OK: {action}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
