#!/usr/bin/env python3
"""Apply an MMO spell JSON draft into data/editor/data/spells.data."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

from google.protobuf import json_format

from proto_runtime import find_project_root, load_modules


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--backup", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    project_root = find_project_root(args.project_root)
    validation_script = project_root / ".agents/skills/mmo-spell-designer/scripts/validate_spell_json.py"
    validation = subprocess.run(
        [sys.executable, str(validation_script), str(args.path), "--project-root", str(project_root)],
        check=False,
    )
    if validation.returncode != 0:
        return validation.returncode

    modules = load_modules(project_root)
    spells_message = modules["spells"].Spells()
    spells_path = project_root / "data/editor/data/spells.data"
    spells_message.ParseFromString(spells_path.read_bytes())

    document = json.loads(Path(args.path).read_text(encoding="utf-8"))
    spell_data = document["spell"]
    incoming = modules["spells"].SpellEntry()
    json_format.ParseDict(spell_data, incoming, ignore_unknown_fields=False)

    replaced = False
    for index, existing in enumerate(spells_message.entry):
        if existing.id == incoming.id:
            spells_message.entry[index].CopyFrom(incoming)
            replaced = True
            break
    if not replaced:
        spells_message.entry.add().CopyFrom(incoming)

    if args.dry_run:
        action = "would update" if replaced else "would create"
        print(f"OK: {action} spell {incoming.id} in {spells_path}")
        return 0

    if args.backup:
        backup_path = spells_path.with_suffix(".data.bak")
        shutil.copy2(spells_path, backup_path)

    spells_path.write_bytes(spells_message.SerializeToString())
    action = "updated" if replaced else "created"
    print(f"OK: {action} spell {incoming.id} in {spells_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
