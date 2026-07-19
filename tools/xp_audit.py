# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""XP coverage audit for the 1-10 leveling path.

Simulates a single character following every reachable quest (excluding
other-class-only quests and exploration quests), counting quest reward XP plus
the kill XP of the required kills and of the expected kills behind collection
drops. Prints per-quest progression and the coverage ratio against the XP
needed to reach level 10.

This intentionally under-counts real play: incidental kills, discovery XP and
repeat visits are ignored, so real players end up with MORE xp than the
simulation. Keep the coverage ratio at or above ~120%.

Usage:
    python tools/xp_audit.py [--project-root H:/mmo] [--class-id 1]
"""

import argparse
import math
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--class-id", type=int, default=1, help="Simulated character class id (default: warrior)")
    parser.add_argument("--target-level", type=int, default=10)
    args = parser.parse_args()

    root = Path(args.project_root)
    sys.path.insert(0, str(root / ".agents/skills/mmo-quest-creator/scripts"))
    from proto_runtime import load_modules, find_project_root

    mods = load_modules(find_project_root(str(root)))
    data = root / "data/editor/data"

    quests = mods["quests"].Quests()
    quests.ParseFromString((data / "quests.data").read_bytes())
    units = mods["units"].Units()
    units.ParseFromString((data / "units.data").read_bytes())
    classes = mods["classes"].Classes()
    classes.ParseFromString((data / "classes.data").read_bytes())

    unit_by_id = {u.id: u for u in units.entry}

    # Item id -> (avg kill xp of the dropping unit, dropchance) for collection estimates.
    drop_sources = {}
    loot_mod = None
    try:
        import unit_loot_pb2 as loot_mod  # compiled by proto_runtime
    except ImportError:
        pass
    if loot_mod is not None and (data / "unit_loot.data").exists():
        unit_loot = loot_mod.UnitLoot()
        unit_loot.ParseFromString((data / "unit_loot.data").read_bytes())
        loot_by_id = {e.id: e for e in unit_loot.entry}
        for u in units.entry:
            avg_xp = (u.minlevelxp + u.maxlevelxp) / 2.0
            table_ids = list(u.unitlootentries) or ([u.unitlootentry] if u.unitlootentry else [])
            for tid in table_ids:
                entry = loot_by_id.get(tid)
                if not entry:
                    continue
                for group in entry.groups:
                    for d in group.definitions:
                        prev = drop_sources.get(d.item)
                        # Prefer the highest drop chance source (the intended farm target).
                        if prev is None or d.dropchance > prev[1]:
                            drop_sources[d.item] = (avg_xp, d.dropchance)

    class_entry = next(c for c in classes.entry if c.id == args.class_id)
    xp_to_next = list(class_entry.xpToNextLevel)
    max_level = len(class_entry.levelbasevalues)
    xp_needed = sum(xp_to_next[: args.target_level - 1])

    class_bit = 1 << (args.class_id - 1)

    def class_allowed(mask: int) -> bool:
        return mask == 0 or bool(mask & class_bit)

    # Static eligibility: predicates that never change during the simulation.
    eligible = [
        q for q in quests.entry
        if class_allowed(q.requiredclasses)
        # Class-unlock chains are optional side content: reaching the target level
        # must not depend on them, so the audit excludes them entirely.
        and not q.HasField("unlocksclass")
        # Exploration / scripted quests can't be completed by kills or items.
        and all(r.creatureid or r.itemid for r in q.requirements)
        and q.minlevel < args.target_level
    ]

    rewarded = set()
    level = 1
    xp_into_level = 0
    total_quest_xp = 0
    total_kill_xp = 0

    def grant(xp: int):
        nonlocal level, xp_into_level
        if level >= max_level:
            return
        xp_into_level += xp
        while level < max_level and xp_into_level >= xp_to_next[level - 1]:
            xp_into_level -= xp_to_next[level - 1]
            level += 1

    def available(q) -> bool:
        if q.id in rewarded:
            return False
        if q.minlevel and level < q.minlevel:
            return False
        if q.prevquestid and q.prevquestid not in rewarded:
            return False
        return True

    print(f"XP needed for level {args.target_level}: {xp_needed}")
    print(f"{'quest':>5}  {'name':<32} {'reward':>6} {'killxp':>6}  level after")

    while True:
        candidates = [q for q in eligible if available(q)]
        if not candidates:
            break
        # Lowest quest level first, then id, mirrors natural play order.
        q = min(candidates, key=lambda e: (e.questlevel, e.id))

        kill_xp = 0.0
        for r in q.requirements:
            if r.creatureid:
                u = unit_by_id.get(r.creatureid)
                if u:
                    kill_xp += r.creaturecount * (u.minlevelxp + u.maxlevelxp) / 2.0
            elif r.itemid and r.itemid in drop_sources:
                avg_xp, chance = drop_sources[r.itemid]
                if chance > 0:
                    kill_xp += math.ceil(r.itemcount / (chance / 100.0)) * avg_xp

        grant(int(kill_xp))
        grant(q.rewardxp)
        rewarded.add(q.id)
        total_quest_xp += q.rewardxp
        total_kill_xp += int(kill_xp)
        print(f"{q.id:>5}  {q.name:<32} {q.rewardxp:>6} {int(kill_xp):>6}  {level}")

    total = total_quest_xp + total_kill_xp
    coverage = 100.0 * total / xp_needed
    print()
    print(f"quest xp: {total_quest_xp}, kill xp (required + drop-farm kills): {total_kill_xp}")
    print(f"total simulated xp: {total} / {xp_needed} needed -> coverage {coverage:.0f}%")
    print(f"final simulated level: {level} (target {args.target_level})")

    ok = level >= args.target_level and coverage >= 120.0
    print("RESULT:", "OK" if ok else "INSUFFICIENT")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
