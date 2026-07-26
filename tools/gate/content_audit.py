# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Weekly content integrity audit.

Round-trips every quest, item, NPC and spell through its authoring skill's
export + validate scripts (catching broken references and invalid data), checks
the quest chain graph for cycles and dangling links (questchain domain), then
runs the XP coverage audit (tools/xp_audit.py, threshold 120%). Writes a JSON
report to tools/gate/reports/ and exits non-zero if anything failed.

Runs each check as a subprocess for isolation; a full run takes a while and is
meant for the weekly scheduled task. Use --limit for a quick smoke test.

Usage:
    python tools/gate/content_audit.py                 # everything
    python tools/gate/content_audit.py --domain quest  # one domain
    python tools/gate/content_audit.py --limit 3       # smoke test
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SKILLS = REPO / ".claude" / "skills"
DATA = REPO / "data" / "editor" / "data"

# domain -> (skill dir, export script, validate script, id flag, module key, message class, data file)
DOMAINS = {
    "quest": ("mmo-quest-creator", "export_quest_json.py", "validate_quest_json.py", "--quest-id", "quests", "Quests", "quests.data"),
    "item": ("mmo-item-designer", "export_item_json.py", "validate_item_json.py", "--item-id", "items", "Items", "items.data"),
    "npc": ("mmo-npc-designer", "export_npc_json.py", "validate_npc_json.py", "--unit-id", "units", "Units", "units.data"),
    "spell": ("mmo-spell-designer", "export_spell_json.py", "validate_spell_json.py", "--spell-id", "spells", "Spells", "spells.data"),
}

_mods = None


def proto_modules():
    global _mods
    if _mods is None:
        sys.path.insert(0, str(SKILLS / "mmo-quest-creator" / "scripts"))
        from proto_runtime import load_modules
        _mods = load_modules(REPO)
    return _mods


def entity_ids(module_key: str, message_name: str, data_file: str) -> list[int]:
    msg = getattr(proto_modules()[module_key], message_name)()
    msg.ParseFromString((DATA / data_file).read_bytes())
    return sorted(entry.id for entry in msg.entry)


def run_script(script: Path, args: list[str]) -> tuple[int, str]:
    proc = subprocess.run(
        [sys.executable, str(script), "--project-root", str(REPO), *args],
        capture_output=True, text=True, cwd=str(script.parent))
    return proc.returncode, (proc.stdout + proc.stderr).strip()


def audit_domain(name: str, limit: int | None, tmp_dir: Path) -> dict:
    skill, export_script, validate_script, id_flag, module_key, message_name, data_file = DOMAINS[name]
    scripts = SKILLS / skill / "scripts"
    ids = entity_ids(module_key, message_name, data_file)
    if limit:
        ids = ids[:limit]

    failures = []
    for entity_id in ids:
        draft = tmp_dir / f"{name}_{entity_id}.json"
        code, output = run_script(scripts / export_script, [id_flag, str(entity_id), "--output", str(draft)])
        if code != 0:
            failures.append({"id": entity_id, "stage": "export", "output": output[-2000:]})
            continue
        code, output = run_script(scripts / validate_script, [str(draft)])
        if code != 0:
            failures.append({"id": entity_id, "stage": "validate", "output": output[-2000:]})

    print(f"[{name}] checked {len(ids)}, failures {len(failures)}")
    return {"checked": len(ids), "failures": failures}


def audit_xp() -> dict:
    code, output = run_script(REPO / "tools" / "xp_audit.py", [])
    print(f"[xp] {'passed' if code == 0 else 'FAILED'}")
    return {"passed": code == 0, "output_tail": output[-2000:]}


QUEST_FLAG_AUTO_REWARDED = 0x0020


def check_quest_chain_graph(quests: dict) -> list[dict]:
    """Pure graph check over {quest id -> QuestEntry}; returns a failure list.

    Catches data states the runtime cannot recover from:
    - prevquestid cycles: GetQuestStatus recurses through prevquestid, so a cycle
      would overflow the server stack the first time any member is evaluated.
    - nextquestid cycles: every member waits for another member to be rewarded
      first, permanently locking the whole group.
    - dangling prevquestid/nextquestid/nextchainquestid references.
    - AutoRewarded quests offering choice rewards (runtime silently falls back to
      manual turn-in, which is almost never the design intent).
    """
    failures = []

    def check_ref(quest_id: int, field: str, target: int) -> None:
        if target > 0 and target not in quests:
            failures.append({"id": quest_id, "stage": field,
                             "output": f"{field} references unknown quest {target}"})

    for entry in quests.values():
        check_ref(entry.id, "prevquestid", entry.prevquestid)
        check_ref(entry.id, "nextquestid", entry.nextquestid)
        check_ref(entry.id, "nextchainquestid", entry.nextchainquestid)

        if (entry.flags & QUEST_FLAG_AUTO_REWARDED) and len(entry.rewarditemschoice) > 0:
            failures.append({"id": entry.id, "stage": "autorewarded",
                             "output": "AutoRewarded quest offers choice rewards - "
                                       "runtime falls back to manual turn-in"})

    def find_cycles(edge_field: str) -> None:
        # Each quest has at most one outgoing edge per field, so following the chain
        # from every start node and watching for revisits finds all cycles.
        reported = set()
        for start in quests:
            path, position = [], {}
            node = start
            while node in quests and node not in position:
                position[node] = len(path)
                path.append(node)
                node = getattr(quests[node], edge_field)
            if node in position:
                cycle_nodes = path[position[node]:]
                key = tuple(sorted(cycle_nodes))
                if key not in reported:
                    reported.add(key)
                    chain = " -> ".join(str(q) for q in cycle_nodes + [node])
                    failures.append({"id": cycle_nodes[0], "stage": f"{edge_field}-cycle",
                                     "output": f"{edge_field} cycle locks these quests forever: {chain}"})

    find_cycles("prevquestid")
    find_cycles("nextquestid")

    return failures


def audit_quest_chains() -> dict:
    """Whole-catalog quest chain graph checks (single pass, not per-entity)."""
    msg = proto_modules()["quests"].Quests()
    msg.ParseFromString((DATA / "quests.data").read_bytes())
    quests = {entry.id: entry for entry in msg.entry}

    failures = check_quest_chain_graph(quests)

    print(f"[questchain] checked {len(quests)}, failures {len(failures)}")
    return {"checked": len(quests), "failures": failures}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--domain", choices=[*DOMAINS, "questchain", "xp"], action="append",
                        help="restrict to specific domain(s); default: all four + questchain + xp")
    parser.add_argument("--limit", type=int, help="max entities per domain (smoke test)")
    args = parser.parse_args()

    selected = args.domain or [*DOMAINS, "questchain", "xp"]
    report = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "limit": args.limit,
        "domains": {},
        "passed": True,
    }

    with tempfile.TemporaryDirectory(prefix="mmo_content_audit_") as tmp:
        tmp_dir = Path(tmp)
        for name in selected:
            if name == "xp":
                result = audit_xp()
            elif name == "questchain":
                result = audit_quest_chains()
            else:
                result = audit_domain(name, args.limit, tmp_dir)
            report["domains"][name] = result
            failed = (not result["passed"]) if name == "xp" else bool(result["failures"])
            if failed:
                report["passed"] = False

    report_dir = REPO / "tools" / "gate" / "reports"
    report_dir.mkdir(parents=True, exist_ok=True)
    path = report_dir / f"content-audit-{datetime.now().strftime('%Y-%m-%d')}.json"
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Report: {path}")
    print("Result:", "GREEN" if report["passed"] else "RED")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
