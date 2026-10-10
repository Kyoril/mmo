# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the Hollow Choir encounter triggers (ids 53-76) and link them to their creatures.

    py -3 tools/hollow_choir/author_triggers.py            # validate only
    py -3 tools/hollow_choir/author_triggers.py --apply    # write triggers.data and units.data

Run after author_bosses.py, author_spells.py and author_spawns.py. Idempotent: triggers are
written by id, and each creature's trigger list is replaced by the one defined here.

What the triggers do and what the creature AI does on its own:

* The AI casts a creature spell at its victim whenever it is off cooldown: Oswin's Grave Strike
  (every ~10 s, at the tank), Mereth's Lament (18 s), Veyr's Dirge of the Grave (25 s). Those
  need no trigger.
* Abilities aimed at someone other than the victim are timers here: Guttering Candle and
  Dissonance under a random player, Silent Place under a random player who is not tanking. They go
  out through ApplyAura, which casts as a proc from the boss: a normal cast is refused while the
  boss is still casting or channeling (Grave Strike, Lament, Dirge), and the timer would be lost.
* Phase changes cancel whatever the boss is casting first, for the same reason. Found by the
  E2E scenario: Veyr's 60 % choir never rose because he was channeling the Dirge.
* Health thresholds raise the adds; the bosses' deaths open the seals (G1, G2).
* Summoned adds despawn when their boss resets or dies. The cleanup flag is an instance
  variable that is 1 outside a fight and 0 during it, so adds summoned by a GM testing the
  spells by hand (variable never written, so 0) are left alone.
"""

import argparse
import shutil
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-npc-designer/scripts"))
from google.protobuf.json_format import ParseDict  # noqa: E402
from proto_runtime import load_modules  # noqa: E402

EDITOR = ROOT / "data/editor/data"

# trigger_event
ON_SPAWN, ON_AGGRO, ON_KILLED, ON_RESET, ON_HEALTH_BELOW, ON_TIMER, ON_SUMMONED_DIED = 0, 2, 3, 8, 11, 22, 23
# trigger_actions
YELL, SAY, SET_OBJECT_STATE, CAST_SPELL, DELAY, CANCEL_CAST = 2, 1, 3, 6, 7, 11
DESPAWN, SET_ENCOUNTER, APPLY_AURA, REMOVE_AURA, SET_INSTANCE_VAR, BROADCAST, EMOTE = 21, 24, 29, 30, 31, 32, 23
# trigger_action_target
OWNER, NAMED_OBJECT, NAMED_CREATURE, RANDOM_PLAYER, RANDOM_PLAYER_NOT_VICTIM = 1, 4, 5, 7, 11
# trigger_spell_cast_target
AT_CASTER = 0
# trigger_flags
ABORT_ON_DEATH, ONLY_IN_COMBAT = 0x1, 0x2
# encounter_state
NOT_STARTED, IN_PROGRESS, DONE = 0, 1, 2

OSWIN, MERETH, VEYR = 86, 87, 88
RISEN_NOVICE, MOURNING_CHORISTER, HOLLOW_CHORISTER, CANDLEBEARER = 89, 90, 91, 94
ENCOUNTER = {OSWIN: 1, MERETH: 2, VEYR: 3}

# Instance variables. Cleanup flags: 1 = the boss is out of combat, its adds should leave.
CLEANUP = {OSWIN: 2001, MERETH: 2002, VEYR: 2003}
VEYR_FINALE = 2013

SEAL_G1 = "HollowChoir_G1"
SEALS_G2 = ["HollowChoir_G2_North", "HollowChoir_G2_South", "HollowChoir_G2_Corridor"]


def act(action, target=None, data=None, texts=None, name=None):
    entry = {"action": action}
    if target is not None:
        entry["target"] = target
    if name:
        entry["targetname"] = name
    if data:
        entry["data"] = data
    if texts:
        entry["texts"] = [texts]
    return entry


def yell(text):
    return act(YELL, OWNER, texts=text)


def broadcast(text):
    return act(BROADCAST, texts=text)


def var_equals(key, value):
    return {"operator": "Equal", "leftfunction": "InstanceVariable", "leftfunctiondata": [str(key)],
            "rightlong": str(value)}


def living_equals(entry, value):
    return {"operator": "Equal", "leftfunction": "LivingCreatureCount", "leftfunctiondata": [str(entry)],
            "rightlong": str(value)}


def trigger(id_, name, events, actions, flags=0, condition=None):
    entry = {"id": id_, "name": name, "actions": actions, "newevents": events}
    if flags:
        entry["flags"] = flags
    if condition:
        entry["condition"] = condition
    return entry


def ev(type_, *data):
    entry = {"type": type_}
    if data:
        entry["data"] = list(data)
    return entry


def fight_start(boss):
    return [act(SET_ENCOUNTER, data=[ENCOUNTER[boss], IN_PROGRESS]),
            act(SET_INSTANCE_VAR, data=[CLEANUP[boss], 0])]


def fight_end(boss, state):
    return [act(SET_ENCOUNTER, data=[ENCOUNTER[boss], state]),
            act(SET_INSTANCE_VAR, data=[CLEANUP[boss], 1])]


TRIGGERS = {
    # --- Brother Oswin -------------------------------------------------------------------------
    OSWIN: [
        trigger(53, "Oswin - On Aggro", [ev(ON_AGGRO)],
                [yell("Who disturbs the vigil? The dead are resting. You will rest with them.")]
                + fight_start(OSWIN)),
        trigger(54, "Oswin - Guttering Candle", [ev(ON_TIMER, 14000, 16000)],
                [act(APPLY_AURA, RANDOM_PLAYER, data=[253])],
                flags=ABORT_ON_DEATH | ONLY_IN_COMBAT),
        trigger(55, "Oswin - Last Vigil (50%)", [ev(ON_HEALTH_BELOW, 50)],
                [broadcast("Brother Oswin calls the dead from their biers!"),
                 yell("Rise, brothers. Keep the vigil with me."),
                 act(CANCEL_CAST, OWNER),
                 act(CAST_SPELL, OWNER, data=[252, AT_CASTER])],
                flags=ABORT_ON_DEATH),
        trigger(56, "Oswin - On Killed", [ev(ON_KILLED)],
                [yell("The candles... are going out..."),
                 act(SET_OBJECT_STATE, NAMED_OBJECT, data=[1], name=SEAL_G1),
                 broadcast("The seal to the nave breaks.")]
                + fight_end(OSWIN, DONE)),
        trigger(57, "Oswin - On Reset", [ev(ON_RESET)], fight_end(OSWIN, NOT_STARTED)),
    ],

    # --- Sister Mereth -------------------------------------------------------------------------
    MERETH: [
        trigger(61, "Mereth - On Aggro", [ev(ON_AGGRO)],
                [yell("Hush. Can you not hear them weeping?")] + fight_start(MERETH)),
        trigger(62, "Mereth - Silent Place", [ev(ON_TIMER, 15000, 19000)],
                [act(APPLY_AURA, RANDOM_PLAYER_NOT_VICTIM, data=[258])],
                flags=ABORT_ON_DEATH | ONLY_IN_COMBAT),
        trigger(63, "Mereth - Mourning Voices (65%)", [ev(ON_HEALTH_BELOW, 65)],
                [broadcast("Sister Mereth calls the mourning voices!"),
                 yell("Sing with me, sisters. Sing them down."),
                 act(CANCEL_CAST, OWNER),
                 act(CAST_SPELL, OWNER, data=[256, AT_CASTER]),
                 act(DELAY, data=[2000]),
                 act(APPLY_AURA, OWNER, data=[257])],
                flags=ABORT_ON_DEATH),
        trigger(64, "Mereth - Mourning Voices (30%)", [ev(ON_HEALTH_BELOW, 30)],
                [broadcast("Sister Mereth calls the mourning voices!"),
                 yell("Louder! Let the whole abbey grieve!"),
                 act(CANCEL_CAST, OWNER),
                 act(CAST_SPELL, OWNER, data=[256, AT_CASTER]),
                 act(DELAY, data=[2000]),
                 act(APPLY_AURA, OWNER, data=[257])],
                flags=ABORT_ON_DEATH),
        # The chorus protects her only while a chorister lives.
        trigger(65, "Mereth - Chorus Broken", [ev(ON_SUMMONED_DIED)],
                [act(REMOVE_AURA, OWNER, data=[257]),
                 act(EMOTE, OWNER, texts="falters as the last voice beside her breaks.")],
                condition=living_equals(MOURNING_CHORISTER, 0)),
        trigger(66, "Mereth - On Killed", [ev(ON_KILLED)],
                [yell("Silence... at last..."),
                 *[act(SET_OBJECT_STATE, NAMED_OBJECT, data=[1], name=seal) for seal in SEALS_G2],
                 broadcast("The Mourning Voice falls silent. The way to the apse lies open."),
                 act(YELL, NAMED_CREATURE, name="HollowChoir_Veyr",
                     texts="A voice is missing from my choir. Come, then. Take her place.")]
                + fight_end(MERETH, DONE)),
        trigger(67, "Mereth - On Reset", [ev(ON_RESET)],
                [act(REMOVE_AURA, OWNER, data=[257])] + fight_end(MERETH, NOT_STARTED)),
    ],

    # --- Cantor Veyr ---------------------------------------------------------------------------
    VEYR: [
        trigger(68, "Veyr - On Aggro", [ev(ON_AGGRO)],
                [yell("You are late for the service. The hymn has already begun."),
                 act(SET_INSTANCE_VAR, data=[VEYR_FINALE, 0])] + fight_start(VEYR)),
        trigger(69, "Veyr - Dissonance", [ev(ON_TIMER, 12000)],
                [act(APPLY_AURA, RANDOM_PLAYER, data=[260])],
                flags=ABORT_ON_DEATH | ONLY_IN_COMBAT, condition=var_equals(VEYR_FINALE, 0)),
        # Below 30 % Dissonance comes faster: no new mechanic, the old ones at once.
        trigger(70, "Veyr - Dissonance (Finale)", [ev(ON_TIMER, 8000)],
                [act(APPLY_AURA, RANDOM_PLAYER, data=[260])],
                flags=ABORT_ON_DEATH | ONLY_IN_COMBAT, condition=var_equals(VEYR_FINALE, 1)),
        trigger(71, "Veyr - The Choir Rises (60%)", [ev(ON_HEALTH_BELOW, 60)],
                [broadcast("The Hollow Choir rises!"),
                 yell("Voices! Lift me up!"),
                 act(CANCEL_CAST, OWNER),
                 act(CAST_SPELL, OWNER, data=[264, AT_CASTER])],
                flags=ABORT_ON_DEATH),
        trigger(72, "Veyr - Finale (30%)", [ev(ON_HEALTH_BELOW, 30)],
                [broadcast("Cantor Veyr begins the final verse!"),
                 yell("The final verse! All of you, SING!"),
                 act(SET_INSTANCE_VAR, data=[VEYR_FINALE, 1]),
                 act(CANCEL_CAST, OWNER),
                 act(CAST_SPELL, OWNER, data=[264, AT_CASTER])],
                flags=ABORT_ON_DEATH),
        trigger(73, "Veyr - On Killed", [ev(ON_KILLED)],
                [yell("The choir... is... hollow..."),
                 broadcast("Cantor Veyr is silenced. The Hollow Choir falls quiet.")]
                + fight_end(VEYR, DONE)),
        trigger(74, "Veyr - On Reset", [ev(ON_RESET)],
                [act(SET_INSTANCE_VAR, data=[VEYR_FINALE, 0])] + fight_end(VEYR, NOT_STARTED)),
    ],

    # --- Adds and trash ------------------------------------------------------------------------
    RISEN_NOVICE: [
        trigger(58, "Hollow Choir - Novice Cleanup", [ev(ON_TIMER, 3000)],
                [act(DESPAWN, OWNER)], condition=var_equals(CLEANUP[OSWIN], 1)),
    ],
    MOURNING_CHORISTER: [
        trigger(59, "Hollow Choir - Mourning Chorister Cleanup", [ev(ON_TIMER, 3000)],
                [act(DESPAWN, OWNER)], condition=var_equals(CLEANUP[MERETH], 1)),
    ],
    HOLLOW_CHORISTER: [
        trigger(60, "Hollow Choir - Hollow Chorister Cleanup", [ev(ON_TIMER, 3000)],
                [act(DESPAWN, OWNER)], condition=var_equals(CLEANUP[VEYR], 1)),
        # Each chorister keeps its own 6 s copy of Choral Resonance on Veyr fresh; when it dies
        # its copy runs out.
        trigger(75, "Hollow Chorister - Resonance", [ev(ON_SPAWN), ev(ON_TIMER, 4000)],
                [act(APPLY_AURA, NAMED_CREATURE, data=[265], name="HollowChoir_Veyr")],
                flags=ABORT_ON_DEATH),
    ],
    CANDLEBEARER: [
        # Cast by the corpse: Spilled Wax is castable while dead and aimed at the caster itself.
        trigger(76, "Candlebearer - Spilled Wax", [ev(ON_KILLED)],
                [act(CAST_SPELL, OWNER, data=[268, AT_CASTER])]),
    ],
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    mods = load_modules(ROOT)
    triggers = mods["triggers"].Triggers()
    triggers.ParseFromString((EDITOR / "triggers.data").read_bytes())
    units = mods["units"].Units()
    units.ParseFromString((EDITOR / "units.data").read_bytes())
    spells = mods["spells"].Spells()
    spells.ParseFromString((EDITOR / "spells.data").read_bytes())
    spell_ids = {s.id for s in spells.entry}
    by_id = {t.id: t for t in triggers.entry}

    ours = set()
    for unit_id, unit_triggers in TRIGGERS.items():
        for data in unit_triggers:
            entry = mods["triggers"].TriggerEntry()
            ParseDict(data, entry)
            for action in entry.actions:
                if action.action in (CAST_SPELL, APPLY_AURA, REMOVE_AURA) and action.data[0] not in spell_ids:
                    sys.exit(f"trigger {entry.id}: unknown spell {action.data[0]}")
            if entry.id in by_id:
                if not by_id[entry.id].name.startswith(("Oswin", "Mereth", "Veyr", "Hollow Ch", "Candlebearer")):
                    sys.exit(f"trigger id {entry.id} is taken by {by_id[entry.id].name!r}")
                by_id[entry.id].CopyFrom(entry)
            else:
                triggers.entry.append(entry)
                by_id[entry.id] = triggers.entry[-1]
            ours.add(entry.id)

    for unit in units.entry:
        if unit.id in TRIGGERS:
            del unit.triggers[:]
            unit.triggers.extend(t["id"] for t in TRIGGERS[unit.id])
            print(f"unit {unit.id} {unit.name}: triggers {list(unit.triggers)}")
    missing = set(TRIGGERS) - {u.id for u in units.entry}
    if missing:
        sys.exit(f"units missing: {sorted(missing)}")
    print(f"{len(ours)} triggers")

    if not args.apply:
        print("validated; pass --apply to write")
        return

    backup = ROOT / "generated/hollow_choir/backup" / datetime.now().strftime("%Y%m%d%H%M%S")
    backup.mkdir(parents=True, exist_ok=True)
    for path, message in ((EDITOR / "triggers.data", triggers), (EDITOR / "units.data", units)):
        shutil.copy2(path, backup / f"{path.parent.name}_{path.name}")
        path.write_bytes(message.SerializeToString())
        print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
