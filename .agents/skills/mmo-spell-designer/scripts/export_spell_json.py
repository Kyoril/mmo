#!/usr/bin/env python3
"""Export a spell entry into an editable JSON wrapper."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from spell_catalog_lib import find_spell, load_catalogs, message_to_dict


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--spell-id", type=int, required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    catalogs = load_catalogs(args.project_root)
    spell = find_spell(catalogs, args.spell_id)
    if not spell:
        raise SystemExit(f"Spell {args.spell_id} not found")

    document = {
        "format": "mmo-spell",
        "version": 1,
        "spell": message_to_dict(spell),
    }

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(f"OK: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
