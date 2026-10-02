# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Review packets for dressing passes: generated/world/review/<pass-id>/ (README, images, manifest)."""

from __future__ import annotations

import json
import shutil
from pathlib import Path

from .dressing import _round
from .paths import REPO, passes_dir, review_dir


def _checks(lines: list[str], title: str, violations: list[dict]) -> None:
    lines.append(f"## {title}")
    lines.append("")
    if not violations:
        lines.append("None.")
    for v in violations:
        lines.append(f"- **{v['severity']}** `{v['rule']}`: {v['message']}")
    lines.append("")


def write_packet(doc: dict, images: dict, repo: Path = REPO) -> Path:
    folder = review_dir(repo) / doc["pass_id"]
    folder.mkdir(parents=True, exist_ok=True)
    for name, image in images.items():
        image.save(folder / name)
    (folder / "manifest.json").write_text(json.dumps(_round(doc), indent=2, ensure_ascii=False) + "\n", encoding="utf-8",
                                          newline="\n")                           # same rounding as the stored manifest
    # The map pictures live in the pass's working folder (dress.py renders them there); embed them first.
    work = passes_dir(repo) / doc["pass_id"]
    maps = []
    for name in ("before.png", "after.png"):
        if (work / name).is_file():
            shutil.copyfile(work / name, folder / name)
            maps.append(name)
    lines = [f"# Dressing pass {doc['pass_id']}", "",
             f"- Place: `{doc['poi']}`; template `{doc['template']}`; seed {doc['seed']}; status **{doc['status']}**",
             f"- Anchor ({doc['anchor'][0]:.1f}, {doc['anchor'][1]:.1f}); entry point ({doc['entry'][0]:.1f}, {doc['entry'][1]:.1f}). North is up (-Z).",
             f"- Undo: `python tools/world/dress.py undo {doc['pass_id']}` (mmo_edit closed). "
             "Keep it: commit data/client yourself; single props can be moved or deleted in mmo_edit.", ""]
    for note in doc.get("notes", []):
        lines.append(f"> {note}")
    if doc.get("notes"):
        lines.append("")
    pictures = maps + sorted(name for name in images if name not in maps)
    if pictures:
        lines += ["## Pictures", ""] + [f"![{name}]({name})" for name in pictures] + [""]
    lines += ["## Items", "", "| # | Role | Asset | Position (x, y, z) | Yaw | Scale | Stored in |", "|---|---|---|---|---|---|---|"]
    for index, item in enumerate(doc["items"]):
        x, y, z = item["position"]
        lines.append(f"| {index} | {item['role']} | {item['asset'].rsplit('/', 1)[-1]} | ({x:.1f}, {y:.1f}, {z:.1f}) | "
                     f"{item['yaw']:.0f} | {item['scale']:.2f} | {item['store']} |")
    lines.append("")
    lines += ["## Gaps", ""]
    if not doc["gaps"]:
        lines.append("None.")
    for gap in doc["gaps"]:
        reason = "no asset matches the role (art request)" if gap["kind"] == "asset" else "no valid spot found"
        lines.append(f"- `{gap['role']}`: placed {gap['placed']} of {gap['wanted']}: {reason}")
    lines.append("")
    _checks(lines, "Placement checks", doc["checks"]["placement"])
    _checks(lines, "Walkability checks", doc["checks"]["walkability"])
    if doc["status"] == "applied-unchecked":
        lines += ["**Walkability unchecked**: run `python tools/world/dress.py check --nav " + doc["pass_id"] + "`.", ""]
    (folder / "README.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return folder


def write_batch_index(name: str, docs: list[dict], repo: Path = REPO) -> Path:
    folder = review_dir(repo) / name
    folder.mkdir(parents=True, exist_ok=True)
    lines = [f"# Dressing batch {name}", "", "| Pass | Place | Template | Status | Items | Errors | Gaps |", "|---|---|---|---|---|---|---|"]
    for doc in docs:
        errors = sum(1 for v in doc["checks"]["placement"] + doc["checks"]["walkability"] if v["severity"] == "error")
        lines.append(f"| [{doc['pass_id']}](../{doc['pass_id']}/README.md) | {doc['poi']} | {doc['template']} | {doc['status']} | "
                     f"{len(doc['items'])} | {errors} | {len(doc['gaps'])} |")
    (folder / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return folder
