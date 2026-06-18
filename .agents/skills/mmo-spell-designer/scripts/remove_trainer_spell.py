#!/usr/bin/env python3
"""Remove a spell entry from a trainer definition."""

from __future__ import annotations

import argparse
from pathlib import Path

from proto_runtime import find_project_root, load_modules


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--trainer-id", type=int, required=True)
    parser.add_argument("--spell-id", type=int, required=True)
    args = parser.parse_args()

    project_root = find_project_root(args.project_root)
    modules = load_modules(project_root)
    data_dir = Path(project_root) / "data" / "editor" / "data"

    trainers = modules["trainers"].Trainers()
    trainers_path = data_dir / "trainers.data"
    trainers.ParseFromString(trainers_path.read_bytes())

    trainer = None
    for entry in trainers.entry:
        if entry.id == args.trainer_id:
            trainer = entry
            break

    if trainer is None:
        raise SystemExit(f"Unknown trainer id {args.trainer_id}")

    match_index = None
    for index, entry in enumerate(trainer.spells):
        if entry.spell == args.spell_id:
            match_index = index
            break

    if match_index is None:
        raise SystemExit(f"Trainer {args.trainer_id} does not teach spell {args.spell_id}")

    del trainer.spells[match_index]
    trainers_path.write_bytes(trainers.SerializeToString())
    print(f"OK: removed spell {args.spell_id} from trainer {args.trainer_id} in {trainers_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
