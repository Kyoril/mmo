#!/usr/bin/env python3
"""Validate MMO item editor JSON and project references."""

import argparse
import json
import sys
from pathlib import Path

from inspect_item_catalog import load_classes, load_displays, load_spells, load_subclasses


def error(errors, message):
    errors.append(message)


def validate_array(item, name, errors):
    value = item.get(name, [])
    if not isinstance(value, list):
        error(errors, f"item.{name} must be an array")
        return []
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--project-root", default="F:\\mmo")
    args = parser.parse_args()

    path = Path(args.path)
    errors = []
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}")
        return 1

    if not isinstance(doc, dict):
        error(errors, "root must be an object")
        item = {}
    else:
        if doc.get("format") != "mmo-item":
            error(errors, "format must be 'mmo-item'")
        if doc.get("version") != 1:
            error(errors, "version must be 1")
        item = doc.get("item")
        if not isinstance(item, dict):
            error(errors, "item must be an object")
            item = {}

    if not isinstance(item.get("name"), str) or not item.get("name", "").strip():
        error(errors, "item.name must be a non-empty string")

    numeric_fields = {
        "itemclass", "subclass", "displayid", "quality", "flags", "buycount",
        "buyprice", "sellprice", "inventorytype", "allowedclasses", "allowedraces",
        "itemlevel", "requiredlevel", "maxcount", "maxstack", "containerslots",
        "armor", "delay", "bonding", "durability", "block", "rangedrange",
    }
    for name in numeric_fields:
        if name in item and not isinstance(item[name], (int, float)):
            error(errors, f"item.{name} must be numeric")

    maxstack = item.get("maxstack", 1)
    if isinstance(maxstack, (int, float)) and maxstack < 1:
        error(errors, "item.maxstack must be at least 1")
    quality = item.get("quality", 0)
    if isinstance(quality, (int, float)) and not 0 <= quality <= 5:
        error(errors, "item.quality must be in range 0..5")
    inventorytype = item.get("inventorytype", 0)
    if isinstance(inventorytype, (int, float)) and not 0 <= inventorytype <= 28:
        error(errors, "item.inventorytype must be in range 0..28")
    bonding = item.get("bonding", 0)
    if isinstance(bonding, (int, float)) and not 0 <= bonding <= 3:
        error(errors, "item.bonding must be in range 0..3")
    if item.get("requiredlevel", 0) > item.get("itemlevel", item.get("requiredlevel", 0)):
        error(errors, "item.requiredlevel must not exceed item.itemlevel")
    if item.get("buyprice", 0) and item.get("sellprice", 0) > item.get("buyprice", 0):
        error(errors, "item.sellprice must not exceed item.buyprice")

    damage = item.get("damage")
    if damage is not None:
        if not isinstance(damage, dict):
            error(errors, "item.damage must be an object")
        else:
            minimum = damage.get("mindmg")
            maximum = damage.get("maxdmg")
            if not isinstance(minimum, (int, float)) or not isinstance(maximum, (int, float)):
                error(errors, "item.damage requires numeric mindmg and maxdmg")
            elif minimum < 0 or maximum < minimum:
                error(errors, "item.damage must satisfy 0 <= mindmg <= maxdmg")
            if item.get("delay", 0) <= 0:
                error(errors, "items with damage require a positive delay")

    stats = validate_array(item, "stats", errors)
    for index, stat in enumerate(stats):
        if not isinstance(stat, dict) or not isinstance(stat.get("type"), (int, float)) or not isinstance(stat.get("value"), (int, float)):
            error(errors, f"item.stats[{index}] requires numeric type and value")
        elif not 0 <= stat["type"] <= 31:
            error(errors, f"item.stats[{index}].type must be in range 0..31")

    spells = validate_array(item, "spells", errors)
    validate_array(item, "sockets", errors)
    validate_array(item, "name_loc", errors)
    validate_array(item, "description_loc", errors)

    data_dir = Path(args.project_root) / "data" / "editor" / "data"
    try:
        class_ids = {entry["id"] for entry in load_classes(data_dir)}
        subclass_keys = {(entry["itemclass"], entry["id"]) for entry in load_subclasses(data_dir)}
        display_ids = {entry["id"] for entry in load_displays(data_dir)}
        spell_ids = {entry["id"] for entry in load_spells(data_dir)}
        itemclass = item.get("itemclass", 0)
        subclass = item.get("subclass", 0)
        if itemclass not in class_ids:
            error(errors, f"item.itemclass {itemclass} is not present in project data")
        if subclass != 0 and (itemclass, subclass) not in subclass_keys:
            error(errors, f"item subclass pair ({itemclass}, {subclass}) is not present in project data")
        if item.get("displayid", 0) not in display_ids and item.get("displayid", 0) != 0:
            error(errors, f"item.displayid {item.get('displayid')} is not present in project data")
        for index, spell in enumerate(spells):
            if not isinstance(spell, dict) or not isinstance(spell.get("spell"), (int, float)):
                error(errors, f"item.spells[{index}] requires a numeric spell ID")
            elif spell["spell"] not in spell_ids:
                error(errors, f"item.spells[{index}].spell {spell['spell']} is not present in project data")
    except (OSError, ValueError) as exc:
        error(errors, f"could not inspect project catalogs: {exc}")

    if errors:
        for message in errors:
            print(f"ERROR: {message}")
        return 1
    print(f"OK: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
