#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placement lint for every prop (.wobj) and tree (.hfol) of a map, compared to the baseline.

    python tools/world/prop_lint.py --map 0
    python tools/world/prop_lint.py --map 0 --update-baseline   # accept today's findings as known

Exit code 1 when there are new errors (not in the "props" section of tools/world/lint_baseline.json).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from worldkit.assets import build_catalog
from worldkit.baseline import BASELINE_PATH, load_baseline, save_baseline, split
from worldkit.cli_query import open_map
from worldkit.foliage import load_world_foliage
from worldkit.prop_lint import PropContext, lint_world_props, props_from_entities, props_from_foliage
from worldkit.snapshot import NoTerrainError
from worldkit.spawns import object_spawn_records, unit_spawn_records
from worldkit.tags import load_tag_rules


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--baseline", type=Path, default=BASELINE_PATH)
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--show-known", action="store_true")
    args = parser.parse_args(argv)
    try:
        _, map_entry, query = open_map(args.map)
    except NoTerrainError as exc:
        print(f"skipped map {args.map}: {exc}")
        return 0
    if map_entry.instancetype != 0:
        print(f"skipped map {args.map}: instance map")
        return 0
    foliage = load_world_foliage(map_entry.directory)
    existing = props_from_entities(query.snapshot.entities) + props_from_foliage(foliage)
    roads = [road["points"] for road in query.atlas.roads] if query.atlas else []
    context = PropContext(query, build_catalog(), load_tag_rules(), existing,
                          unit_spawn_records(map_entry) + object_spawn_records(map_entry), None, roads)
    violations = lint_world_props(context)
    if args.update_baseline:
        count = save_baseline("props", args.map, violations, args.baseline)
        print(f"baselined {count} prop findings for map {args.map} in {args.baseline}")
        return 0
    new, known = split(violations, load_baseline("props", args.baseline, args.map))
    new_errors = [v for v in new if v.severity == "error"]
    new_warnings = [v for v in new if v.severity == "warning"]
    print(f"map {args.map} ({map_entry.name}): checked {len(existing)} props, {len(new_errors)} new errors, "
          f"{len(new_warnings)} new warnings, {len(known)} known")
    for v in new_errors:
        print(f"  ERROR   {v.rule}: {v.message}")
    for v in new_warnings:
        print(f"  WARNING {v.rule}: {v.message}")
    if args.show_known:
        for v in known:
            print(f"  known   {v.rule}: {v.message}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({"known": len(known), "new_errors": [v.to_dict() for v in new_errors],
                                         "new_warnings": [v.to_dict() for v in new_warnings]}, indent=2), encoding="utf-8")
    return 1 if new_errors else 0


if __name__ == "__main__":
    sys.exit(main())
