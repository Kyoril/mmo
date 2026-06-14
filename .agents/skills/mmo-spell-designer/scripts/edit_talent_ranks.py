#!/usr/bin/env python3
"""Add, remove, or replace rank spell IDs on an existing talent entry."""

from __future__ import annotations

import argparse
from pathlib import Path

from proto_runtime import find_project_root, load_modules


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--talent-id", type=int, required=True)
    parser.add_argument("--set-rank-spell-id", type=int, action="append")
    parser.add_argument("--add-rank-spell-id", type=int, action="append")
    parser.add_argument("--remove-rank-spell-id", type=int, action="append")
    args = parser.parse_args()

    requested_modes = sum(
        1 for value in [args.set_rank_spell_id, args.add_rank_spell_id, args.remove_rank_spell_id] if value
    )
    if requested_modes != 1:
        raise SystemExit("Choose exactly one of --set-rank-spell-id, --add-rank-spell-id, or --remove-rank-spell-id")

    project_root = find_project_root(args.project_root)
    modules = load_modules(project_root)
    data_dir = Path(project_root) / "data" / "editor" / "data"

    talents = modules["talents"].Talents()
    talents_path = data_dir / "talents.data"
    talents.ParseFromString(talents_path.read_bytes())

    spells = modules["spells"].Spells()
    spells.ParseFromString((data_dir / "spells.data").read_bytes())
    known_spells = {entry.id for entry in spells.entry}

    talent = None
    for entry in talents.entry:
        if entry.id == args.talent_id:
            talent = entry
            break

    if talent is None:
        raise SystemExit(f"Unknown talent id {args.talent_id}")

    def ensure_known(spell_ids: list[int]) -> None:
        for spell_id in spell_ids:
            if spell_id not in known_spells:
                raise SystemExit(f"Unknown spell id {spell_id}")

    if args.set_rank_spell_id:
        ensure_known(args.set_rank_spell_id)
        del talent.ranks[:]
        talent.ranks.extend(args.set_rank_spell_id)
    elif args.add_rank_spell_id:
        ensure_known(args.add_rank_spell_id)
        existing = set(talent.ranks)
        for spell_id in args.add_rank_spell_id:
            if spell_id not in existing:
                talent.ranks.append(spell_id)
                existing.add(spell_id)
    else:
        removable = set(args.remove_rank_spell_id)
        missing = [spell_id for spell_id in removable if spell_id not in talent.ranks]
        if missing:
            raise SystemExit(f"Talent {args.talent_id} does not contain spell ids: {', '.join(str(spell_id) for spell_id in missing)}")
        kept = [spell_id for spell_id in talent.ranks if spell_id not in removable]
        del talent.ranks[:]
        talent.ranks.extend(kept)

    talents_path.write_bytes(talents.SerializeToString())
    print(f"OK: updated talent {args.talent_id} in {talents_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
