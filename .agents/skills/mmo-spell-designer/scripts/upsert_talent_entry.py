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
    args = parser.parse_args()

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

    talents_path.write_bytes(talents.SerializeToString())
    print(f"OK: updated talent {args.talent_id} in {talents_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
