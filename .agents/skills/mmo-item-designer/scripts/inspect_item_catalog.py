#!/usr/bin/env python3
"""Inspect selected MMO protobuf data without requiring generated Python bindings."""

import argparse
import json
from pathlib import Path


def read_varint(data, pos):
    value = 0
    shift = 0
    while pos < len(data):
        byte = data[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, pos
        shift += 7
        if shift > 70:
            raise ValueError("Invalid protobuf varint")
    raise ValueError("Truncated protobuf varint")


def decode_message(data):
    fields = {}
    pos = 0
    while pos < len(data):
        key, pos = read_varint(data, pos)
        number = key >> 3
        wire = key & 7
        if wire == 0:
            value, pos = read_varint(data, pos)
        elif wire == 1:
            if pos + 8 > len(data):
                raise ValueError("Truncated fixed64 field")
            value = data[pos:pos + 8]
            pos += 8
        elif wire == 2:
            size, pos = read_varint(data, pos)
            if pos + size > len(data):
                raise ValueError("Truncated length-delimited field")
            value = data[pos:pos + size]
            pos += size
        elif wire == 5:
            if pos + 4 > len(data):
                raise ValueError("Truncated fixed32 field")
            value = data[pos:pos + 4]
            pos += 4
        else:
            raise ValueError(f"Unsupported protobuf wire type {wire}")
        fields.setdefault(number, []).append((wire, value))
    return fields


def entries(path):
    root = decode_message(path.read_bytes())
    return [decode_message(value) for wire, value in root.get(1, []) if wire == 2]


def integer(fields, number, default=0):
    values = fields.get(number)
    return values[-1][1] if values and values[-1][0] == 0 else default


def text(fields, number, default=""):
    values = fields.get(number)
    if not values or values[-1][0] != 2:
        return default
    return values[-1][1].decode("utf-8", errors="replace")


def load_classes(data_dir):
    return [
        {"id": integer(entry, 1), "name": text(entry, 2)}
        for entry in entries(data_dir / "item_classes.data")
    ]


def load_subclasses(data_dir):
    return [
        {
            "id": integer(entry, 1),
            "name": text(entry, 2),
            "itemclass": integer(entry, 3),
            "requiredproficiency": integer(entry, 4),
        }
        for entry in entries(data_dir / "item_subclasses.data")
    ]


def load_displays(data_dir):
    return [
        {"id": integer(entry, 1), "name": text(entry, 2), "icon": text(entry, 3)}
        for entry in entries(data_dir / "item_displays.data")
    ]


def load_spells(data_dir):
    return [
        {"id": integer(entry, 1), "name": text(entry, 2)}
        for entry in entries(data_dir / "spells.data")
    ]


def load_items(data_dir):
    result = []
    for entry in entries(data_dir / "items.data"):
        result.append(
            {
                "id": integer(entry, 1),
                "name": text(entry, 2),
                "itemclass": integer(entry, 4),
                "subclass": integer(entry, 5),
                "displayid": integer(entry, 6),
                "quality": integer(entry, 7),
                "inventorytype": integer(entry, 12),
                "itemlevel": integer(entry, 15),
                "requiredlevel": integer(entry, 16),
                "maxstack": integer(entry, 25),
                "delay": integer(entry, 36),
            }
        )
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default="F:\\mmo")
    parser.add_argument(
        "--section",
        choices=["all", "classes", "subclasses", "displays", "spells", "items"],
        default="all",
    )
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    data_dir = Path(args.project_root) / "data" / "editor" / "data"
    loaders = {
        "classes": load_classes,
        "subclasses": load_subclasses,
        "displays": load_displays,
        "spells": load_spells,
        "items": load_items,
    }
    if args.section == "all":
        output = {name: loader(data_dir) for name, loader in loaders.items()}
    else:
        output = {args.section: loaders[args.section](data_dir)}
    print(json.dumps(output, indent=2 if args.pretty else None, ensure_ascii=True))


if __name__ == "__main__":
    main()

