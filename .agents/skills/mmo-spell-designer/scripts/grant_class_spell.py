#!/usr/bin/env python3
"""Grant a spell to a class at a given level in classes.data."""

from __future__ import annotations

import argparse

from proto_runtime import find_project_root, load_modules


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--class-id", type=int)
    parser.add_argument("--class-name")
    parser.add_argument("--spell-id", type=int, required=True)
    parser.add_argument("--level", type=int, required=True)
    args = parser.parse_args()

    if args.class_id is None and not args.class_name:
        raise SystemExit("Provide either --class-id or --class-name")

    project_root = find_project_root(args.project_root)
    modules = load_modules(project_root)

    classes_path = project_root / "data/editor/data/classes.data"
    spells_path = project_root / "data/editor/data/spells.data"

    classes = modules["classes"].Classes()
    classes.ParseFromString(classes_path.read_bytes())

    spells = modules["spells"].Spells()
    spells.ParseFromString(spells_path.read_bytes())
    if not any(entry.id == args.spell_id for entry in spells.entry):
        raise SystemExit(f"Spell {args.spell_id} not found in spells.data")

    target_class = None
    for entry in classes.entry:
        if args.class_id is not None and entry.id == args.class_id:
            target_class = entry
            break
        if args.class_name and entry.name.lower() == args.class_name.lower():
            target_class = entry
            break

    if target_class is None:
        raise SystemExit("Class not found")

    for grant in target_class.spells:
        if grant.spell == args.spell_id:
            grant.level = args.level
            classes_path.write_bytes(classes.SerializeToString())
            print(f"OK: updated class spell grant for {target_class.name} -> spell {args.spell_id} at level {args.level}")
            return 0

    ordered = [(grant.level, grant.spell) for grant in target_class.spells]
    ordered.append((args.level, args.spell_id))
    ordered.sort()

    del target_class.spells[:]
    for level, spell_id in ordered:
        grant = target_class.spells.add()
        grant.level = level
        grant.spell = spell_id

    classes_path.write_bytes(classes.SerializeToString())
    print(f"OK: granted spell {args.spell_id} to {target_class.name} at level {args.level}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
