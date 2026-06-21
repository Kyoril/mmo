#!/usr/bin/env python3
"""Export an MMO item and its linked display entry to JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from item_catalog_lib import find_project_root, load_catalog_bundle, message_to_dict


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--item-id", type=int, required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    project_root = find_project_root(args.project_root)
    _, _, indexes = load_catalog_bundle(project_root)

    item = indexes["items"].get(args.item_id)
    if item is None:
        print(f"ERROR: item {args.item_id} was not found")
        return 1

    doc = {
        "format": "mmo-item",
        "version": 1,
        "item": message_to_dict(item),
    }

    display_id = item.displayid if item.HasField("displayid") else 0
    if display_id and display_id in indexes["item_displays"]:
        doc["item_display"] = message_to_dict(indexes["item_displays"][display_id])

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(doc, indent=2, ensure_ascii=True), encoding="utf-8")
    print(f"OK: wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
