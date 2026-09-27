# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Known-violation baseline (tools/world/lint_baseline.json).

Sections ("placement", "reachability") and maps are updated independently, so re-baselining one
map after fixing it never forgets another map's known issues. Fixing a violation changes or removes
its key, so it simply drops out on the next --update-baseline.
"""

from __future__ import annotations

import json
from pathlib import Path

from .paths import REPO

BASELINE_PATH = REPO / "tools" / "world" / "lint_baseline.json"


def _read(path: Path) -> dict:
    if not Path(path).is_file():
        return {"version": 1}
    return json.loads(Path(path).read_text(encoding="utf-8"))


def load_baseline(section: str, path: Path = BASELINE_PATH) -> dict[str, str]:
    return {entry["key"]: entry["message"] for entry in _read(path).get(section, [])}


def save_baseline(section: str, map_id: int, items, path: Path = BASELINE_PATH) -> int:
    doc = _read(path)
    kept = [entry for entry in doc.get(section, []) if entry.get("map") != map_id]
    fresh = [{"map": map_id, "key": item.key, "message": item.message} for item in items]
    doc[section] = sorted(kept + fresh, key=lambda entry: (entry["map"], entry["key"]))
    doc["version"] = 1
    ordered = {"version": 1, **{k: v for k, v in sorted(doc.items()) if k != "version"}}
    Path(path).write_text(json.dumps(ordered, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    return len(fresh)


def split(items, baseline: dict[str, str]) -> tuple[list, list]:
    new = [item for item in items if item.key not in baseline]
    known = [item for item in items if item.key in baseline]
    return new, known
