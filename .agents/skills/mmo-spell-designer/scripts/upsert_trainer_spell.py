#!/usr/bin/env python3
"""Create or update a spell entry inside a trainer definition."""

from __future__ import annotations

import argparse
from pathlib import Path

from proto_runtime import find_project_root, load_modules


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--trainer-id", type=int, required=True)
    parser.add_argument("--spell-id", type=int, required=True)
    parser.add_argument("--cost", type=int, required=True)
    parser.add_argument("--required-skill-id", type=int, default=0)
    parser.add_argument("--required-skill-value", type=int, default=0)
    parser.add_argument("--required-level", type=int, default=1)
    args = parser.parse_args()

    project_root = find_project_root(args.project_root)
    modules = load_modules(project_root)
    data_dir = Path(project_root) / "data" / "editor" / "data"

    trainers = modules["trainers"].Trainers()
    trainers_path = data_dir / "trainers.data"
    trainers.ParseFromString(trainers_path.read_bytes())

    spells = modules["spells"].Spells()
    spells.ParseFromString((data_dir / "spells.data").read_bytes())

    skills = modules["skills"].Skills()
    skills.ParseFromString((data_dir / "skills.data").read_bytes())

    trainer = None
    for entry in trainers.entry:
        if entry.id == args.trainer_id:
            trainer = entry
            break

    if trainer is None:
        raise SystemExit(f"Unknown trainer id {args.trainer_id}")

    if not any(entry.id == args.spell_id for entry in spells.entry):
        raise SystemExit(f"Unknown spell id {args.spell_id}")

    if args.required_skill_id and not any(entry.id == args.required_skill_id for entry in skills.entry):
        raise SystemExit(f"Unknown skill id {args.required_skill_id}")

    trainer_spell = None
    for entry in trainer.spells:
        if entry.spell == args.spell_id:
            trainer_spell = entry
            break

    if trainer_spell is None:
        trainer_spell = trainer.spells.add()
        trainer_spell.spell = args.spell_id

    trainer_spell.spellcost = args.cost

    if args.required_skill_id > 0:
        trainer_spell.reqskill = args.required_skill_id
        trainer_spell.reqskillval = args.required_skill_value
    else:
        trainer_spell.ClearField("reqskill")
        trainer_spell.ClearField("reqskillval")

    if args.required_level > 1:
        trainer_spell.reqlevel = args.required_level
    else:
        trainer_spell.ClearField("reqlevel")

    trainers_path.write_bytes(trainers.SerializeToString())
    print(f"OK: updated trainer {args.trainer_id} spell {args.spell_id} in {trainers_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
