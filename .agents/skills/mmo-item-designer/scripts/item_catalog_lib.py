#!/usr/bin/env python3
"""Shared catalog helpers for MMO item tooling."""

from __future__ import annotations

from pathlib import Path

from google.protobuf.json_format import MessageToDict, ParseDict

from proto_runtime import find_project_root, load_modules


CATALOG_FACTORIES = {
    "items": lambda mods: mods["items"].Items(),
    "item_displays": lambda mods: mods["item_display"].ItemDisplayData(),
    "item_classes": lambda mods: mods["item_classes"].ItemClasses(),
    "item_subclasses": lambda mods: mods["item_subclasses"].ItemSubclasses(),
    "spells": lambda mods: mods["spells"].Spells(),
    "quests": lambda mods: mods["quests"].Quests(),
}


CATALOG_FILES = {
    "items": "items.data",
    "item_displays": "item_displays.data",
    "item_classes": "item_classes.data",
    "item_subclasses": "item_subclasses.data",
    "spells": "spells.data",
    "quests": "quests.data",
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


def replace_or_append_by_id(container, message) -> str:
    for index, entry in enumerate(container):
        if entry.id == message.id:
            container[index].CopyFrom(message)
            return "updated"
    container.add().CopyFrom(message)
    return "created"


def next_free_id(index: dict[int, object]) -> int:
    return (max(index.keys()) + 1) if index else 1
