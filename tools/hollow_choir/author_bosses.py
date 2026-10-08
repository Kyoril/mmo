# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the reworked Hollow Choir (map 1) boss roster.

    py -3 tools/hollow_choir/author_bosses.py            # validate and report only
    py -3 tools/hollow_choir/author_bosses.py --apply    # write the datasets

What it does, idempotently:

1. Retires the first boss roster: Sevrin Wax (81), Ossuar (84) and Choirmistress Vell (85)
   leave map 1, and every trigger written for them (29-50, including the instance wipe trigger
   37 and the add triggers on 82/83) is deleted and unlinked. The unit rows themselves stay:
   quests 58-61 still name them as kill objectives until those quests are re-pointed.
2. Writes the new roster from docs/hollow_choir_bosses.md -- three bosses and their adds --
   as unit rows only. They are not spawned (the new layout has no coordinates yet) and carry
   no triggers: the encounter logic is authored after the spells are signed off.

maps.data exists twice (editor + ClientDB) with identical contents; both are written.
"""

import argparse
import shutil
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-npc-designer/scripts"))
from proto_runtime import load_modules  # noqa: E402

EDITOR = ROOT / "data/editor/data"
CLIENTDB = ROOT / "data/client/ClientDB"

MAP_ID = 1
RETIRED_BOSSES = {81, 84, 85}
RETIRED_TRIGGERS = set(range(29, 51))
FACTION_HOSTILE_UNDEAD = 4
MODEL_UNDEAD_MALE = 17
MODEL_UNDEAD_FEMALE = 18
CLASS_WARRIOR, CLASS_MAGE, CLASS_DUNGEON_BOSS = 1, 2, 3

# Spells every creature in this project carries as its melee auto attack.
AUTO_ATTACK = 37

# --- New roster -------------------------------------------------------------------------------
# Elite multipliers are first-test values: on this unit class one knob scales health AND melee
# damage, so slow swing timers keep tank damage sane while health carries the fight length.
# Bosses keep the loot tables of the bosses they replace (28 holds the dungeon's epic).
BOSSES = [
    dict(id=86, name="Brother Oswin", subname="Keeper of the Vigil", level=12,
         model=MODEL_UNDEAD_MALE, elite=3.5, attack_time=2400, loot=29,
         gold=(1100, 1600), xp=420, armor=(160, 40.0), dmg_per_level=1.1,
         spells=[(251, 9500, 10500, 0.0, 8.0)]),
    dict(id=87, name="Sister Mereth", subname="The Mourning Voice", level=13,
         model=MODEL_UNDEAD_FEMALE, elite=3.8, attack_time=2800, loot=30,
         gold=(1300, 1900), xp=480, armor=(140, 34.0), dmg_per_level=1.0,
         spells=[(255, 18000, 18000, 0.0, 40.0)]),
    dict(id=88, name="Cantor Veyr", subname="The Hollow Choir", level=14,
         model=MODEL_UNDEAD_MALE, elite=4.2, attack_time=3000, loot=28,
         gold=(1900, 2700), xp=560, armor=(175, 42.0), dmg_per_level=1.2,
         spells=[(262, 25000, 25000, 0.0, 40.0)]),
]

# Adds give no XP and no loot: they are part of a boss's fight, not farmable trash.
ADDS = [
    dict(id=89, name="Risen Novice", level=11, model=MODEL_UNDEAD_MALE, cls=CLASS_WARRIOR,
         elite=1.2, attack_time=2000, armor=(90, 18.0)),
    dict(id=90, name="Mourning Chorister", level=12, model=MODEL_UNDEAD_FEMALE, cls=CLASS_MAGE,
         elite=0.8, attack_time=2400, armor=(60, 12.0)),
    dict(id=91, name="Hollow Chorister", level=12, model=MODEL_UNDEAD_MALE, cls=CLASS_MAGE,
         elite=1.0, attack_time=2400, armor=(60, 12.0)),
]

# Trash: four recurring types whose abilities rehearse the boss fights. Loot modules 20 (trash
# loot) and 18 (open world greens), as the crypt's earlier trash had.
TRASH_LOOT = [20, 18]
TRASH = [
    # Gravewarden: slow telegraphed frontal cleave (Warden's Cleave), the tank turns it away.
    dict(id=92, name="Gravewarden", level=11, model=MODEL_UNDEAD_MALE, cls=CLASS_WARRIOR,
         elite=2.0, attack_time=2400, armor=(120, 30.0), xp=260, gold=(60, 110),
         spells=[(266, 10000, 14000, 0.0, 0.0)]),
    # Mourning Cantor: ranged caster with an interruptible group-damage dirge.
    dict(id=93, name="Mourning Cantor", level=11, model=MODEL_UNDEAD_FEMALE, cls=CLASS_MAGE,
         elite=1.6, attack_time=2400, armor=(70, 14.0), xp=220, gold=(60, 110),
         spells=[(267, 15000, 20000, 0.0, 30.0), (242, 3000, 5000, 0.0, 25.0)]),
    # Candlebearer: weak melee; its death leaves a short-lived flame (trigger, Spilled Wax).
    dict(id=94, name="Candlebearer", level=11, model=MODEL_UNDEAD_MALE, cls=CLASS_WARRIOR,
         elite=1.0, attack_time=2000, armor=(80, 16.0), xp=120, gold=(30, 70), spells=[]),
    # Restless Novice: plain melee.
    dict(id=95, name="Restless Novice", level=11, model=MODEL_UNDEAD_MALE, cls=CLASS_WARRIOR,
         elite=1.4, attack_time=2000, armor=(90, 18.0), xp=160, gold=(40, 80), spells=[]),
]

ENCOUNTER_NAMES = {1: "Brother Oswin", 2: "Sister Mereth", 3: "Cantor Veyr"}


def load(message, path):
    msg = message()
    msg.ParseFromString(path.read_bytes())
    return msg


def make_unit(units_pb, spec, unit_class, xp, gold, loot):
    unit = units_pb.UnitEntry()
    unit.id = spec["id"]
    unit.name = spec["name"]
    unit.subname = spec.get("subname", "")
    unit.minlevel = unit.maxlevel = spec["level"]
    unit.factionTemplate = FACTION_HOSTILE_UNDEAD
    unit.maleModel = unit.femaleModel = spec["model"]
    unit.type = 0
    unit.family = 0
    unit.meleeattacktime = spec["attack_time"]
    unit.minlevelxp = unit.maxlevelxp = xp
    if gold:
        unit.minlootgold, unit.maxlootgold = gold
    if loot:
        unit.unitlootentry = loot
        unit.unitlootentries.append(loot)
    unit.creaturespells.add().spellid = AUTO_ATTACK
    unit.regeneration = 3
    unit.unitClassId = unit_class
    unit.damagePerLevel = spec.get("dmg_per_level", 1.0)
    unit.baseArmor, unit.armorPerLevel = spec["armor"]
    unit.eliteStatMultiplier = spec["elite"]
    unit.useStatBasedSystem = True
    return unit


def set_spells(unit, spells):
    """Creature spells (id, min cooldown, max cooldown, min range, max range), placed before
    the auto attack (37) so the AI weighs them first. Encounter abilities aimed at random
    players are cast by triggers instead: the AI only ever targets its victim."""
    auto_attack = list(unit.creaturespells)
    del unit.creaturespells[:]
    for spell_id, min_cd, max_cd, min_range, max_range in spells:
        entry = unit.creaturespells.add()
        entry.spellid = spell_id
        entry.priority = 100
        entry.mincooldown, entry.maxcooldown = min_cd, max_cd
        entry.minrange, entry.maxrange = min_range, max_range
    unit.creaturespells.extend(auto_attack)


def upsert(entries, new_entry):
    for i, entry in enumerate(entries):
        if entry.id == new_entry.id:
            # Trigger links belong to author_triggers.py; keep whatever it set.
            new_entry.triggers.extend(entry.triggers)
            entries[i].CopyFrom(new_entry)
            return "updated"
    entries.append(new_entry)
    return "added"


def check_no_dangling_triggers(mods):
    """Fail if anything outside the retired roster still names a retired trigger."""
    problems = []
    objects = load(mods["objects"].Objects, EDITOR / "objects.data")
    for obj in objects.entry:
        if set(obj.triggers) & RETIRED_TRIGGERS:
            problems.append(f"object {obj.id} {obj.name}")
    quests = load(mods["quests"].Quests, EDITOR / "quests.data")
    for quest in quests.entry:
        if (set(quest.starttriggers) | set(quest.failtriggers) | set(quest.rewardtriggers)) & RETIRED_TRIGGERS:
            problems.append(f"quest {quest.id} {quest.name}")
    areas = load(mods["area_triggers"].AreaTriggers, EDITOR / "area_triggers.data")
    for area in areas.entry:
        if {area.on_enter_trigger, area.on_exit_trigger} & RETIRED_TRIGGERS:
            problems.append(f"area trigger {area.id}")
    return problems


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    mods = load_modules(ROOT)
    import importlib
    mods["objects"] = importlib.import_module("objects_pb2")
    mods["area_triggers"] = importlib.import_module("area_triggers_pb2")

    units = load(mods["units"].Units, EDITOR / "units.data")
    triggers = load(mods["triggers"].Triggers, EDITOR / "triggers.data")
    maps = load(mods["maps"].Maps, EDITOR / "maps.data")

    # 1. Retire the old roster.
    removed_triggers = [t.id for t in triggers.entry if t.id in RETIRED_TRIGGERS]
    kept = [t for t in triggers.entry if t.id not in RETIRED_TRIGGERS]
    del triggers.entry[:]
    triggers.entry.extend(kept)

    for unit in units.entry:
        if set(unit.triggers) & RETIRED_TRIGGERS:
            remaining = [t for t in unit.triggers if t not in RETIRED_TRIGGERS]
            del unit.triggers[:]
            unit.triggers.extend(remaining)
            print(f"unit {unit.id} {unit.name}: triggers unlinked")

    dungeon = next(m for m in maps.entry if m.id == MAP_ID)
    spawns = [s for s in dungeon.unitspawns if s.unitentry not in RETIRED_BOSSES]
    removed_spawns = len(dungeon.unitspawns) - len(spawns)
    del dungeon.unitspawns[:]
    dungeon.unitspawns.extend(spawns)
    instance_triggers = [t for t in dungeon.instance_triggers if t not in RETIRED_TRIGGERS]
    del dungeon.instance_triggers[:]
    dungeon.instance_triggers.extend(instance_triggers)
    for encounter in dungeon.encounters:
        if encounter.id in ENCOUNTER_NAMES:
            encounter.name = ENCOUNTER_NAMES[encounter.id]

    print(f"triggers removed: {removed_triggers}")
    print(f"map {MAP_ID}: {removed_spawns} boss spawns removed, {len(spawns)} spawns left")

    problems = check_no_dangling_triggers(mods)
    if problems:
        sys.exit("still referencing retired triggers: " + ", ".join(problems))

    # 2. New roster.
    for spec in BOSSES:
        unit = make_unit(mods["units"], spec, CLASS_DUNGEON_BOSS, spec["xp"], spec["gold"], spec["loot"])
        set_spells(unit, spec["spells"])
        print(f"unit {spec['id']} {spec['name']}: {upsert(units.entry, unit)}")
    for spec in ADDS:
        unit = make_unit(mods["units"], spec, spec["cls"], 0, None, None)
        print(f"unit {spec['id']} {spec['name']}: {upsert(units.entry, unit)}")
    for spec in TRASH:
        unit = make_unit(mods["units"], spec, spec["cls"], spec["xp"], spec["gold"], None)
        unit.unitlootentries.extend(TRASH_LOOT)
        set_spells(unit, spec["spells"])
        print(f"unit {spec['id']} {spec['name']}: {upsert(units.entry, unit)}")

    if not args.apply:
        print("validated; pass --apply to write")
        return

    stamp = datetime.now().strftime("%Y%m%d%H%M%S")
    backup = ROOT / "generated/hollow_choir/backup" / stamp
    backup.mkdir(parents=True, exist_ok=True)
    writes = {
        EDITOR / "units.data": units,
        EDITOR / "triggers.data": triggers,
        EDITOR / "maps.data": maps,
        CLIENTDB / "maps.data": maps,
    }
    for path, message in writes.items():
        shutil.copy2(path, backup / f"{path.parent.name}_{path.name}")
        path.write_bytes(message.SerializeToString())
        print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
