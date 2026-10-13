# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Briarwatch March creature retune and Fireball rank 2 (see
docs/superpowers/specs/2026-10-12-briarwatch-balance-talent-ranks-design.md).

    py tools/briarwatch_balance/author_briarwatch_balance.py            # show the changes only
    py tools/briarwatch_balance/author_briarwatch_balance.py --apply    # write editor data + ClientDB

Idempotent: re-running sets the same values. ClientDB receives a byte copy of the editor spells
blob, exactly what mmo_edit's ExportToClient does.

Easy to undo by accident:
* Fireball rank 2 keeps rank 1's familyflags (8): the fire talents select spells by family flags.
* Rank 2's baseid (4) is what ties it to the Fireball talent on server and client; without it the
  rank would stay usable after a talent reset. Spell 4 itself stays rank 0 / baseid 0: it belongs to
  the talent tree script, which gives single-rank talent spells no rank line.
"""

import argparse
import importlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / "data/editor/data"
CLIENT_DB = ROOT / "data/client/ClientDB"
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

FIREBALL, FIREBALL_R2 = 4, 1427
MAGE_TRAINER = 1
SHADOWBOLT = 180

# unit id: field overrides (spec, Part 1)
UNITS = {
    67: {"minmeleedmg": 3.5, "damagePerLevel": 0.75},                                # Duskfang Stalker
    48: {"eliteStatMultiplier": 1.0, "minmeleedmg": 4.0, "damagePerLevel": 0.75},    # Kingsroad Cutthroat
    49: {"eliteStatMultiplier": 1.0},                                                # Kingsroad Hexer
    66: {"eliteStatMultiplier": 1.0, "minmeleedmg": 3.0, "damagePerLevel": 0.7},     # Roadside Marauder
    50: {"eliteStatMultiplier": 1.0, "minmeleedmg": 4.0, "damagePerLevel": 0.75},    # Marshland Ambusher
    51: {"eliteStatMultiplier": 1.0},                                                # Marshland Hexer
    55: {"minmeleedmg": 4.0, "damagePerLevel": 0.75},                                # Bog Rat
    56: {"minmeleedmg": 4.0, "damagePerLevel": 0.75},                                # Mire Prowler
    57: {"eliteStatMultiplier": 1.0, "minmeleedmg": 3.0, "damagePerLevel": 0.7},     # Mirewater Poacher
}
HEXERS = (49, 51)
HEXER_BOLT_COOLDOWN = (5000, 7000)

_MODULES = {}


def find_any_protoc():
    try:
        return find_protoc(ROOT)
    except FileNotFoundError:
        common = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute",
                                 "--git-common-dir"], capture_output=True, text=True, check=True)
        return find_protoc(Path(common.stdout.strip()).parent)


def load_modules():
    if not _MODULES:
        out = Path(tempfile.mkdtemp(prefix="briarwatch_proto_"))
        proto_dir = ROOT / "src/shared/proto_data"
        subprocess.run([str(find_any_protoc()), f"-I{proto_dir}", f"--python_out={out}",
                        *sorted(p.name for p in proto_dir.glob("*.proto"))], check=True, cwd=proto_dir)
        sys.path.insert(0, str(out))
        for name in ("units", "spells", "trainers"):
            _MODULES[name] = importlib.import_module(f"{name}_pb2")
    return _MODULES


def read(module, container, file_name):
    message = getattr(module, container)()
    message.ParseFromString((EDITOR / file_name).read_bytes())
    return message


def by_id(container, entry_id):
    for entry in container.entry:
        if entry.id == entry_id:
            return entry
    return None


def retune_units(units, log):
    for unit_id, fields in UNITS.items():
        unit = by_id(units, unit_id)
        assert unit is not None, f"unit {unit_id} missing"
        for field, value in fields.items():
            old = getattr(unit, field)
            if abs(old - value) > 1e-6:
                log.append(f"unit {unit_id} {unit.name}: {field} {old:g} -> {value:g}")
            setattr(unit, field, value)
        if unit_id in HEXERS:
            spells = [s for s in unit.creaturespells if s.spellid == SHADOWBOLT]
            assert spells, f"unit {unit_id} has no Shadowbolt"
            for spell in spells:
                if (spell.mincooldown, spell.maxcooldown) != HEXER_BOLT_COOLDOWN:
                    log.append(f"unit {unit_id} {unit.name}: Shadowbolt cooldown "
                               f"{spell.mincooldown}/{spell.maxcooldown} -> {HEXER_BOLT_COOLDOWN}")
                spell.mincooldown, spell.maxcooldown = HEXER_BOLT_COOLDOWN


def author_fireball(spells, log):
    # Rank 1 is the Fireball talent spell, owned by tools/talent_trees/author_talent_trees.py, which
    # keeps single-rank talent spells at rank 0 / baseid 0. Rank 2's baseid alone ties it to the talent.
    rank1 = by_id(spells, FIREBALL)
    assert rank1 is not None and rank1.name == "Fireball"

    rank2 = by_id(spells, FIREBALL_R2)
    if rank2 is None:
        rank2 = spells.entry.add()
        log.append(f"spell {FIREBALL_R2}: created Fireball rank 2")
    elif rank2.name != "Fireball":
        raise SystemExit(f"spell {FIREBALL_R2} exists and is not Fireball: {rank2.name}")
    before = rank2.SerializePartialToString()
    rank2.CopyFrom(rank1)
    rank2.id = FIREBALL_R2
    rank2.rank = 2
    rank2.baseid = FIREBALL
    rank2.casttime = 1800
    rank2.cost = 45
    rank2.spelllevel = 6
    rank2.baselevel = 6
    rank2.maxlevel = 10
    direct, periodic = rank2.effects[0], rank2.effects[1]
    assert direct.type == 2 and periodic.type == 6 and periodic.aura == 13
    direct.basepoints = 29
    direct.diesides = 9
    direct.pointsperlevel = 0.6
    # Rank 1 ticks for 3 + 0.621/level up to 5 at its cap (level 5); 6 continues that curve at
    # level 6, so rank 2 never ticks for less than rank 1.
    periodic.basepoints = 6
    if before and before != rank2.SerializeToString():
        log.append(f"spell {FIREBALL_R2}: updated Fireball rank 2")


def add_trainer_entry(trainers, log):
    trainer = by_id(trainers, MAGE_TRAINER)
    assert trainer is not None
    entry = next((s for s in trainer.spells if s.spell == FIREBALL_R2), None)
    if entry is None:
        entry = trainer.spells.add()
        log.append(f"trainer {MAGE_TRAINER}: teaches {FIREBALL_R2} at level 6 for 450")
    elif (entry.spellcost, entry.reqlevel) != (450, 6):
        log.append(f"trainer {MAGE_TRAINER}: {FIREBALL_R2} cost/level -> 450/6")
    entry.spell = FIREBALL_R2
    entry.spellcost = 450
    entry.reqlevel = 6


def plan_changes():
    """Reads the editor data, applies the design in memory and returns (log, units, spells, trainers).

    An empty log means the data on disk already matches the design.
    """
    modules = load_modules()
    units = read(modules["units"], "Units", "units.data")
    spells = read(modules["spells"], "Spells", "spells.data")
    trainers = read(modules["trainers"], "Trainers", "trainers.data")

    log = []
    retune_units(units, log)
    author_fireball(spells, log)
    add_trainer_entry(trainers, log)
    return log, units, spells, trainers


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    log, units, spells, trainers = plan_changes()
    print("\n".join(log) if log else "no changes")

    if args.apply:
        (EDITOR / "units.data").write_bytes(units.SerializeToString())
        (EDITOR / "spells.data").write_bytes(spells.SerializeToString())
        (EDITOR / "trainers.data").write_bytes(trainers.SerializeToString())
        shutil.copyfile(EDITOR / "spells.data", CLIENT_DB / "spells.data")
        print("written")


if __name__ == "__main__":
    main()
