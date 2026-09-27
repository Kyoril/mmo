#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Content health report for a map: quest reachability, turn-in rule, spawn/level distribution.

    python tools/world/report.py --map 0 --markdown generated/world/report_map_0.md
    python tools/world/report.py --map 0 --update-baseline

Exit code 1 when there are new errors (not in the reachability section of the baseline).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from worldkit.baseline import BASELINE_PATH, load_baseline, save_baseline, split
from worldkit.cli_query import open_map
from worldkit.report import DEFAULT_MAX_DISTANCE, check_reachability, summarize, to_markdown
from worldkit.snapshot import NoTerrainError


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--max-distance", type=float, default=DEFAULT_MAX_DISTANCE)
    parser.add_argument("--baseline", type=Path, default=BASELINE_PATH)
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args(argv)

    try:
        data, map_entry, query = open_map(args.map)
    except NoTerrainError as exc:
        print(f"skipped map {args.map}: {exc}")
        return 0

    findings = check_reachability(data, map_entry, args.max_distance)
    if args.update_baseline:
        count = save_baseline("reachability", args.map, findings, args.baseline)
        print(f"baselined {count} reachability findings for map {args.map} in {args.baseline}")
        return 0

    new, known = split(findings, load_baseline("reachability", args.baseline))
    summary = summarize(data, map_entry, query)
    new_errors = [f for f in new if f.severity == "error"]
    print(f"map {args.map} ({map_entry.name}): {len(new_errors)} new errors, {len(new) - len(new_errors)} new warnings, {len(known)} known")
    for finding in new:
        print(f"  {finding.severity.upper():7} {finding.rule}: {finding.message}")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(to_markdown(map_entry, new, known, summary), encoding="utf-8")
        print(f"wrote {args.markdown}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({"map": args.map, "new_errors": [f.to_dict() for f in new_errors],
                                         "new_warnings": [f.to_dict() for f in new if f.severity == "warning"],
                                         "known": len(known), "summary": summary}, indent=2, ensure_ascii=False),
                             encoding="utf-8")
    return 1 if new_errors else 0


if __name__ == "__main__":
    sys.exit(main())
