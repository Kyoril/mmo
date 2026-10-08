# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the Hollow Choir boss spells (251-265) in the editor dataset and the ClientDB copy.

    py -3 tools/hollow_choir/author_spells.py            # validate only
    py -3 tools/hollow_choir/author_spells.py --apply    # write both spells.data files

Idempotent: spells are written by id, replacing what is there. Design: docs/hollow_choir_bosses.md.

Each spell can be tested on its own with `learnspell <id>` and a cast at a hostile NPC. The zone
spells (Guttering Candle, Silent Place, Dissonance) then mark the ground under that NPC.

Rules that are easy to break by accident:

* Zone ticks and detonations (254, 259, 261) are cast by the boss on players it may not see:
  the zone lies where the player *was*, often behind a pillar. They ignore line of sight.
* The zone spells themselves (253, 258, 260) also ignore line of sight, so the encounter can
  aim them at any player in the room.
* Kicks stop every cast unless the spell opts out. Grave Strike and the summons are answered by
  positioning, not by an interrupt, so they carry CannotBeInterrupted; Lament and Dirge of the
  Grave are the interruptible ones.
* Dirge of the Grave is channeled: its channel length is `casttime`, its caster aura lasts
  `duration`, and the periodic trigger on that aura deals the damage. The two must match.
"""

import argparse
import shutil
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-npc-designer/scripts"))
from proto_runtime import load_modules  # noqa: E402

EDITOR_SPELLS = ROOT / "data/editor/data/spells.data"
CLIENT_SPELLS = ROOT / "data/client/ClientDB/spells.data"
ICONS = ROOT / "data/client"

# spell_effects (src/shared/game/spell.h)
SCHOOL_DAMAGE, APPLY_AURA, PERSISTENT_AREA_AURA, SUMMON = 2, 6, 24, 25
# spell_effect_targets (src/shared/game/spell_target_map.h)
CASTER, TARGET_ENEMY, SOURCE_AREA_ENEMY, CONE_ENEMY = 0, 5, 9, 16
# aura_type (src/shared/game/aura.h)
PERIODIC_TRIGGER_SPELL, MOD_DECREASE_SPEED, MOD_DAMAGE_DONE_PCT, MOD_DAMAGE_TAKEN_PCT = 9, 15, 23, 24
# spell schools
PHYSICAL, FIRE, SHADOW = 0, 2, 5
# attributes
CHANNELED = 0x00000001
NEGATIVE = 0x04000000
IGNORE_LOS = 1 << 7
CANNOT_BE_INTERRUPTED = 1 << 11
# spell_interrupt_flags
INTERRUPT = 0x04
# ranges.data
RANGE_SELF, RANGE_INFINITE, RANGE_35, RANGE_MELEE = 0, 1, 2, 6

# creatures
RISEN_NOVICE, MOURNING_CHORISTER, HOLLOW_CHORISTER = 89, 90, 91


def effect(type_, target=CASTER, **fields):
    return dict(type=type_, targeta=target, **fields)


def spell(id_, name, level, school, description, icon, visualization, effects,
          cast_ms=0, duration=0, attributes=(0, 0), interrupt=0, range_type=RANGE_SELF,
          aura_text=None, stack_amount=None):
    return dict(id=id_, name=name, level=level, school=school, description=description,
                icon=icon, visualization=visualization, effects=effects, cast_ms=cast_ms,
                duration=duration, attributes=attributes, interrupt=interrupt,
                range_type=range_type, aura_text=aura_text, stack_amount=stack_amount)


SPELLS = [
    # --- Brother Oswin -------------------------------------------------------------------------
    spell(251, "Grave Strike", 12, PHYSICAL,
          "Brings the broken censer down in a crushing arc, dealing $s0 physical damage to "
          "everyone in front of the caster.",
          "Interface/Icons/Spells/T_Icon_BloodCombat_12.htex", 80,
          [effect(SCHOOL_DAMAGE, CONE_ENEMY, basepoints=104, diesides=22, radius=8.0, miscvalueb=100)],
          cast_ms=1500, attributes=(NEGATIVE, CANNOT_BE_INTERRUPTED), range_type=RANGE_MELEE),
    spell(252, "Last Vigil", 12, SHADOW,
          "Calls two novices up from their biers to keep the vigil with the caster.",
          "Interface/Icons/Spells/T_Icon_Unholy_40.htex", 81,
          [effect(SUMMON, CASTER, basepoints=2, summonunit=RISEN_NOVICE, radius=6.0)],
          cast_ms=2000, attributes=(0, CANNOT_BE_INTERRUPTED)),
    spell(253, "Guttering Candle", 12, FIRE,
          "Sets a guttering candle flame on the ground beneath the target. After 2 sec it flares, "
          "dealing $254s0 fire damage to everyone still standing in it.",
          "Interface/Icons/Spells/T_Icon_Fire_40.htex", 82,
          [effect(PERSISTENT_AREA_AURA, TARGET_ENEMY, radius=3.5, amplitude=2000, triggerspell=254)],
          duration=2000, attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_35),
    spell(254, "Guttering Candle", 12, FIRE,
          "The candle flares, dealing $s0 fire damage.",
          "Interface/Icons/Spells/T_Icon_Fire_40.htex", 83,
          [effect(SCHOOL_DAMAGE, TARGET_ENEMY, basepoints=78, diesides=18)],
          attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_INFINITE),

    # --- Sister Mereth -------------------------------------------------------------------------
    spell(255, "Lament", 13, SHADOW,
          "A lament for the unburied, dealing $s0 shadow damage to every enemy within 40 yards.",
          "Interface/Icons/Spells/T_Icon_Shadow_30.htex", 84,
          [effect(SCHOOL_DAMAGE, SOURCE_AREA_ENEMY, basepoints=48, diesides=12, radius=40.0)],
          cast_ms=3000, attributes=(NEGATIVE, IGNORE_LOS), interrupt=INTERRUPT),
    spell(256, "Mourning Voices", 13, SHADOW,
          "Calls two Mourning Choristers to opposite sides of the hall.",
          "Interface/Icons/Spells/T_Icon_Shadow_45.htex", 85,
          [effect(SUMMON, CASTER, basepoints=2, summonunit=MOURNING_CHORISTER, radius=14.0)],
          cast_ms=2000, attributes=(0, CANNOT_BE_INTERRUPTED)),
    spell(257, "Mourning Chorus", 13, SHADOW,
          "The choristers' grief shrouds the caster, reducing damage taken by $s0%.",
          "Interface/Icons/Spells/T_Icon_Shadow_45.htex", 86,
          [effect(APPLY_AURA, CASTER, aura=MOD_DAMAGE_TAKEN_PCT, basepoints=-40)],
          aura_text="Damage taken reduced by 40%."),
    spell(258, "Silent Place", 13, SHADOW,
          "Hushes the ground beneath the target for 12 sec. Anyone standing in the silence takes "
          "$259s0 shadow damage every second and is slowed by 50%.",
          "Interface/Icons/Spells/T_Icon_Shadow_60.htex", 87,
          [effect(PERSISTENT_AREA_AURA, TARGET_ENEMY, radius=4.5, amplitude=1000, triggerspell=259)],
          duration=12000, attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_35),
    spell(259, "Silent Place", 13, SHADOW,
          "The silence deals $s0 shadow damage and slows movement by 50%.",
          "Interface/Icons/Spells/T_Icon_Shadow_60.htex", 88,
          [effect(SCHOOL_DAMAGE, TARGET_ENEMY, basepoints=14, diesides=6),
           effect(APPLY_AURA, TARGET_ENEMY, aura=MOD_DECREASE_SPEED, basepoints=-50)],
          duration=1500, attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_INFINITE,
          aura_text="Movement slowed by 50%."),

    # --- Cantor Veyr ---------------------------------------------------------------------------
    spell(260, "Dissonance", 14, SHADOW,
          "Strikes a discordant note beneath the target. After 2 sec a wave of sound bursts from "
          "it, dealing $261s0 shadow damage to everyone still standing in it.",
          "Interface/Icons/Spells/T_Icon_Arcane_20.htex", 89,
          [effect(PERSISTENT_AREA_AURA, TARGET_ENEMY, radius=4.0, amplitude=2000, triggerspell=261)],
          duration=2000, attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_35),
    spell(261, "Dissonance", 14, SHADOW,
          "A wave of discordant sound deals $s0 shadow damage.",
          "Interface/Icons/Spells/T_Icon_Arcane_20.htex", 90,
          [effect(SCHOOL_DAMAGE, TARGET_ENEMY, basepoints=88, diesides=20)],
          attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_INFINITE),
    spell(262, "Dirge of the Grave", 14, SHADOW,
          "Conducts the hollow choir for 5 sec, dealing $263s0 shadow damage to every enemy "
          "within 40 yards each second.",
          "Interface/Icons/Spells/T_Icon_Unholy_80.htex", 91,
          [effect(APPLY_AURA, CASTER, aura=PERIODIC_TRIGGER_SPELL, amplitude=1000, triggerspell=263, targetb=CASTER)],
          cast_ms=5000, duration=5000, attributes=(CHANNELED | NEGATIVE, 0), interrupt=INTERRUPT),
    spell(263, "Dirge of the Grave", 14, SHADOW,
          "The dirge deals $s0 shadow damage.",
          "Interface/Icons/Spells/T_Icon_Unholy_80.htex", 92,
          [effect(SCHOOL_DAMAGE, SOURCE_AREA_ENEMY, basepoints=16, diesides=6, radius=40.0)],
          attributes=(NEGATIVE, IGNORE_LOS)),
    spell(264, "The Choir Rises", 14, SHADOW,
          "Raises two Hollow Choristers from the dark of the apse.",
          "Interface/Icons/Spells/T_Icon_Unholy_100.htex", 93,
          [effect(SUMMON, CASTER, basepoints=2, summonunit=HOLLOW_CHORISTER, radius=10.0)],
          cast_ms=2000, attributes=(0, CANNOT_BE_INTERRUPTED)),
    spell(265, "Choral Resonance", 14, SHADOW,
          "Each living chorister swells the caster's voice, increasing damage done by $s0% per "
          "stack.",
          "Interface/Icons/Spells/T_Icon_Unholy_100.htex", 94,
          [effect(APPLY_AURA, CASTER, aura=MOD_DAMAGE_DONE_PCT, basepoints=12)],
          aura_text="Damage done increased.", stack_amount=4),
]


def build(spells_pb, spec):
    entry = spells_pb.SpellEntry()
    entry.id = spec["id"]
    entry.name = spec["name"]
    entry.attributes.extend(spec["attributes"])
    for index, fields in enumerate(spec["effects"]):
        eff = entry.effects.add()
        eff.index = index
        for key, value in fields.items():
            setattr(eff, key, value)
    entry.cooldown = 0
    entry.casttime = spec["cast_ms"]
    entry.cost = 0
    entry.maxlevel = entry.baselevel = entry.spelllevel = spec["level"]
    entry.spellSchool = spec["school"]
    entry.facing = 1
    entry.duration = spec["duration"]
    entry.interruptflags = spec["interrupt"]
    entry.rangetype = spec["range_type"]
    entry.classmask = 0
    entry.rank = 0
    entry.description = spec["description"]
    entry.icon = spec["icon"]
    if spec["aura_text"]:
        entry.auratext = spec["aura_text"]
    if spec["stack_amount"]:
        entry.stackamount = spec["stack_amount"]
    entry.visualization_id = spec["visualization"]
    # Same as every other creature spell in this project: no global cooldown, no school lockout.
    entry.cooldownflags = 3
    return entry


def validate(spells, units):
    by_id = {s.id: s for s in spells.entry}
    for spec in SPELLS:
        icon = ICONS / spec["icon"]
        if not icon.is_file():
            sys.exit(f"spell {spec['id']}: icon {spec['icon']} does not exist")
        for fields in spec["effects"]:
            trigger = fields.get("triggerspell")
            if trigger and trigger not in by_id:
                sys.exit(f"spell {spec['id']}: trigger spell {trigger} does not exist")
            summon = fields.get("summonunit")
            if summon and summon not in units:
                sys.exit(f"spell {spec['id']}: summoned unit {summon} does not exist")
            if fields["type"] == PERSISTENT_AREA_AURA:
                if spec["duration"] <= 0 or fields.get("radius", 0) <= 0:
                    sys.exit(f"spell {spec['id']}: a zone needs a duration and a radius")
        if spec["attributes"][0] & CHANNELED and spec["cast_ms"] != spec["duration"]:
            sys.exit(f"spell {spec['id']}: channel length and aura duration differ")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    mods = load_modules(ROOT)
    spells = mods["spells"].Spells()
    spells.ParseFromString(EDITOR_SPELLS.read_bytes())
    units = mods["units"].Units()
    units.ParseFromString((ROOT / "data/editor/data/units.data").read_bytes())

    if EDITOR_SPELLS.read_bytes() != CLIENT_SPELLS.read_bytes():
        sys.exit("editor and ClientDB spells.data differ; reconcile them before authoring")

    for spec in SPELLS:
        entry = build(mods["spells"], spec)
        for i, existing in enumerate(spells.entry):
            if existing.id == entry.id:
                spells.entry[i].CopyFrom(entry)
                break
        else:
            spells.entry.append(entry)
        print(f"spell {spec['id']} {spec['name']}")

    validate(spells, {u.id for u in units.entry})

    if not args.apply:
        print("validated; pass --apply to write")
        return

    backup = ROOT / "generated/hollow_choir/backup" / datetime.now().strftime("%Y%m%d%H%M%S")
    backup.mkdir(parents=True, exist_ok=True)
    data = spells.SerializeToString()
    for path in (EDITOR_SPELLS, CLIENT_SPELLS):
        shutil.copy2(path, backup / f"{path.parent.name}_{path.name}")
        path.write_bytes(data)
        print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
