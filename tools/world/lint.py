#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placement lint for a map's spawns (or for the spawns of an NPC draft).

    python tools/world/lint.py --map 0                      # whole map, compared to the baseline
    python tools/world/lint.py --map 0 --draft generated/npcs/bog_rat.json
    python tools/world/lint.py --map 0 --update-baseline    # accept today's violations as known

Exit code 1 when there are new errors (not in tools/world/lint_baseline.json). New warnings are
printed but do not fail.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from worldkit.baseline import BASELINE_PATH, load_baseline, save_baseline, split
from worldkit.cli_query import open_map
from worldkit.lint import entry_names, lint_map, lint_records, naming_violations
from worldkit.snapshot import NoTerrainError
from worldkit.spawns import records_from_npc_draft


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--draft", type=Path, help="mmo-npc-designer draft JSON to lint instead of the live map")
    parser.add_argument("--baseline", type=Path, default=BASELINE_PATH)
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--json", type=Path, help="write the full result as JSON")
    parser.add_argument("--show-known", action="store_true")
    parser.add_argument("--no-cache", action="store_true")
    args = parser.parse_args(argv)

    if args.draft and args.update_baseline:
        parser.error("--update-baseline cannot be combined with --draft")

    try:
        data, map_entry, query = open_map(args.map, use_cache=not args.no_cache)
    except NoTerrainError as exc:
        print(f"skipped map {args.map}: {exc}")
        return 0

    if map_entry.instancetype != 0:
        print(f"skipped map {args.map}: instance map - spawns stand on world-model floors, which the terrain lint cannot see")
        return 0

    if args.draft:
        doc = json.loads(args.draft.read_text(encoding="utf-8"))
        records = [r for r in records_from_npc_draft(doc) if r.map_id == args.map]
        levels = data.unit_levels()
        unit = doc.get("unit") or {}
        if "id" in unit and "minlevel" in unit:
            lo = int(unit["minlevel"])
            levels[int(unit["id"])] = (lo, max(lo, int(unit.get("maxlevel", lo))))
        names = entry_names(data)
        if "id" in unit and unit.get("name"):
            names[("unit", int(unit["id"]))] = unit["name"]
        violations = lint_records(records, query, levels, names, data.service_units()) + naming_violations(records)
    else:
        records, violations = lint_map(data, map_entry, query)

    if args.update_baseline:
        count = save_baseline("placement", args.map, violations, args.baseline)
        print(f"baselined {count} placement violations for map {args.map} in {args.baseline}")
        return 0

    new, known = split(violations, load_baseline("placement", args.baseline))
    new_errors = [v for v in new if v.severity == "error"]
    new_warnings = [v for v in new if v.severity == "warning"]
    print(f"map {args.map} ({map_entry.name}): checked {len(records)} spawns, "
          f"{len(new_errors)} new errors, {len(new_warnings)} new warnings, {len(known)} known")
    for v in new_errors:
        print(f"  ERROR   {v.rule}: {v.message}")
    for v in new_warnings:
        print(f"  WARNING {v.rule}: {v.message}")
    if args.show_known:
        for v in known:
            print(f"  known   {v.rule}: {v.message}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({
            "map": args.map, "checked": len(records),
            "new_errors": [v.to_dict() for v in new_errors],
            "new_warnings": [v.to_dict() for v in new_warnings],
            "known": len(known),
        }, indent=2, ensure_ascii=False), encoding="utf-8")
    return 1 if new_errors else 0


if __name__ == "__main__":
    sys.exit(main())
