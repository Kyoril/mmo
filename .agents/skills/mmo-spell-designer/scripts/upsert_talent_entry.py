#!/usr/bin/env python3
"""Create or update a talent entry in talents.data."""

from __future__ import annotations

import argparse
from pathlib import Path

from proto_runtime import find_project_root, load_modules


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument("--talent-id", type=int, required=True)
    parser.add_argument("--tab-id", type=int, required=True)
    parser.add_argument("--row", type=int, required=True)
    parser.add_argument("--column", type=int, required=True)
    parser.add_argument("--rank-spell-id", type=int, action="append", required=True)
    parser.add_argument("--position-x", type=int)
    parser.add_argument("--position-y", type=int)
    parser.add_argument("--required-points", type=int)
    parser.add_argument("--node-scale", type=float)
    parser.add_argument(
        "--prerequisite",
        action="append",
        default=[],
        metavar="TALENT_ID:RANK",
        help="Required talent and one-based rank; may be specified more than once",
    )
    parser.add_argument(
        "--clear-prerequisites",
        action="store_true",
        help="Remove all prerequisites from the talent",
    )
    args = parser.parse_args()

    if args.clear_prerequisites and args.prerequisite:
        raise SystemExit("--clear-prerequisites cannot be combined with --prerequisite")

    project_root = find_project_root(args.project_root)
    modules = load_modules(project_root)
    data_dir = Path(project_root) / "data" / "editor" / "data"

    talents = modules["talents"].Talents()
    talents_path = data_dir / "talents.data"
    talents.ParseFromString(talents_path.read_bytes())

    talent_tabs = modules["talent_tabs"].TalentTabs()
    talent_tabs.ParseFromString((data_dir / "talent_tabs.data").read_bytes())

    if not any(entry.id == args.tab_id for entry in talent_tabs.entry):
        raise SystemExit(f"Unknown talent tab id {args.tab_id}")

    spells = modules["spells"].Spells()
    spells.ParseFromString((data_dir / "spells.data").read_bytes())
    known_spells = {entry.id for entry in spells.entry}
    unknown_spells = [spell_id for spell_id in args.rank_spell_id if spell_id not in known_spells]
    if unknown_spells:
        raise SystemExit(f"Unknown rank spell ids: {', '.join(str(spell_id) for spell_id in unknown_spells)}")

    prerequisites = []
    for value in args.prerequisite:
        try:
            talent_id_text, rank_text = value.split(":", 1)
            prerequisite_talent_id = int(talent_id_text)
            prerequisite_rank = int(rank_text)
        except ValueError as exc:
            raise SystemExit(f"Invalid prerequisite '{value}'; expected TALENT_ID:RANK") from exc
        prerequisites.append((prerequisite_talent_id, prerequisite_rank))

    talents_by_id = {entry.id: entry for entry in talents.entry}
    for prerequisite_talent_id, prerequisite_rank in prerequisites:
        prerequisite = talents_by_id.get(prerequisite_talent_id)
        if prerequisite is None:
            raise SystemExit(f"Unknown prerequisite talent id {prerequisite_talent_id}")
        if prerequisite_talent_id == args.talent_id:
            raise SystemExit("A talent cannot require itself")
        if prerequisite.tab != args.tab_id:
            raise SystemExit(f"Prerequisite talent {prerequisite_talent_id} belongs to another tab")
        if prerequisite_rank <= 0 or prerequisite_rank > len(prerequisite.ranks):
            raise SystemExit(
                f"Prerequisite rank {prerequisite_rank} is invalid for talent {prerequisite_talent_id}"
            )

    talent = None
    for entry in talents.entry:
        if entry.id == args.talent_id:
            talent = entry
            break

    if talent is None:
        talent = talents.entry.add()
        talent.id = args.talent_id

    talent.tab = args.tab_id
    talent.row = args.row
    talent.column = args.column
    del talent.ranks[:]
    talent.ranks.extend(args.rank_spell_id)

    if (args.position_x is None) != (args.position_y is None):
        raise SystemExit("--position-x and --position-y must be provided together")
    if args.position_x is not None:
        talent.position_x = args.position_x
        talent.position_y = args.position_y
    if args.required_points is not None:
        if args.required_points < 0:
            raise SystemExit("--required-points must not be negative")
        talent.required_points = args.required_points
    if args.node_scale is not None:
        if args.node_scale <= 0:
            raise SystemExit("--node-scale must be greater than zero")
        talent.node_scale = args.node_scale

    if args.clear_prerequisites:
        del talent.prerequisites[:]
    elif prerequisites:
        del talent.prerequisites[:]
        for prerequisite_talent_id, prerequisite_rank in prerequisites:
            prerequisite = talent.prerequisites.add()
            prerequisite.talent_id = prerequisite_talent_id
            prerequisite.rank = prerequisite_rank

    talents_path.write_bytes(talents.SerializeToString())
    print(f"OK: updated talent {args.talent_id} in {talents_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
