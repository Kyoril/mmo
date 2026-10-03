# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the mage spell presentation: sound catalog entries, spell visualizations, and the
spell -> visualization links, in both the editor dataset and the client ClientDB copy.

    py -3.14 tools/mage_visuals/author_mage_visuals.py            # validate only
    py -3.14 tools/mage_visuals/author_mage_visuals.py --apply    # write all six files

Idempotent: sound entries and visualizations are matched by name and keep the ids they
were first given, so re-running replaces rather than appends. Every dataset is parsed,
modified and validated before any file is written.

Unlike the warrior script this one *does* write ``spells.data``: most mage spells pointed
at shared entries (8 "Default Projectile", 9 "Default Spell Visualization", which clerics and
creatures use too) or at nothing at all, so each mage spell is repointed at its own
"Mage - ..." entry. Only ``visualization_id`` is changed. The superseded mage-only entries
1, 3, 10 and 24 are left in place, unreferenced.

Design rules that are easy to undo by accident:

* No kit sets ``duration_ms`` -- it time-warps the clip (see warrior_spell_visuals.md).
* Kits use ``sound_ids`` and never ``sounds``.
* A CASTING kit may loop (anim + channel sound); everything else is a one-shot kit. Looping
  *particles* need no kit flag: the .hpar loops and the engine tears it down on cast end,
  projectile impact or aura removal.
* Buff feedback with a sound goes on CAST_SUCCEEDED / IMPACT, never on AURA_APPLIED:
  AURA_APPLIED also fires whenever a unit carrying the aura comes into view, so a sound
  there replays for every buffed player who walks past.
* AURA_IDLE kits are scoped to the aura holder. A spell with auras on both caster and target
  (Fire Barrage) must not have one, or the target gets the caster's effect.
"""

import argparse
import shutil
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
sys.path.insert(0, str(ROOT / "tools/warrior_visuals"))
from author_visuals import load_type  # noqa: E402

OUT = ROOT / "generated/mage_visuals"
SOUND_DIR = "Sound/Spells/Mage/"
PARTICLE_DIR = "Particles/Mage/"

# Event ids from spell_visualizations.proto.
CASTING, CAST, IMPACT, AURA, AURA_IDLE, CHANNELING = "2", "3", "4", "5", "8", "9"

# Measured from the Human rigs: CastRelease is 0.53 s (female) / 0.63 s (male). Orcs have
# neither CastLoop nor CastRelease; the service skips the animation and still plays the rest.

# --- Sound catalog -------------------------------------------------------------------------
# id, name, files, looped, volume, pitch_min, pitch_max
# Impacts that repeat constantly (Frostbolt, Fireball, Fire Barrage) get the widest pitch
# range; Fire Barrage also fires three impacts per wave, so it sits quietest.
SOUND_ENTRIES = [
    (82, "Mage - Frost Channel", ["FrostChannel.wav"], True, 0.55, 1.0, 1.0),
    (83, "Mage - Fire Channel", ["FireChannel.wav"], True, 0.55, 1.0, 1.0),
    (84, "Mage - Arcane Channel", ["ArcaneChannel.wav"], True, 0.55, 1.0, 1.0),
    (85, "Mage - Frostbolt Cast", ["FrostboltCast.wav"], False, 0.85, 0.95, 1.05),
    (86, "Mage - Frostbolt Impact", ["FrostboltImpact01.wav", "FrostboltImpact02.wav"], False, 0.90, 0.92, 1.08),
    (87, "Mage - Ice Lance Cast", ["IceLanceCast.wav"], False, 0.85, 0.95, 1.05),
    (88, "Mage - Ice Lance Impact", ["IceLanceImpact.wav"], False, 0.90, 0.95, 1.05),
    (89, "Mage - Frost Nova", ["FrostNova.wav"], False, 1.00, 0.98, 1.02),
    (90, "Mage - Frost Armor", ["FrostArmor.wav"], False, 0.80, 0.98, 1.02),
    (91, "Mage - Chilled", ["Chilled.wav"], False, 0.70, 0.94, 1.06),
    (92, "Mage - Frostburn", ["Frostburn.wav"], False, 0.80, 0.96, 1.04),
    (93, "Mage - Fireball Cast", ["FireballCast.wav"], False, 0.85, 0.95, 1.05),
    (94, "Mage - Fireball Impact", ["FireballImpact01.wav", "FireballImpact02.wav"], False, 0.90, 0.92, 1.08),
    (95, "Mage - Fire Blast", ["FireBlast.wav"], False, 1.00, 0.96, 1.04),
    (96, "Mage - Fire Barrage Shot", ["FireBarrageShot.wav"], False, 0.60, 0.90, 1.10),
    (97, "Mage - Fire Barrage Impact", ["FireBarrageImpact.wav"], False, 0.55, 0.88, 1.12),
    (98, "Mage - Arcane Intellect", ["ArcaneIntellect.wav"], False, 0.80, 0.98, 1.02),
    (99, "Mage - Sleep", ["Sleep.wav"], False, 0.80, 0.98, 1.02),
    (100, "Mage - Arcane Disruption", ["ArcaneDisruption.wav"], False, 0.90, 0.96, 1.04),
    (101, "Mage - Arcane Pulse", ["ArcanePulse.wav"], False, 1.00, 0.98, 1.02),
    (102, "Mage - Conjure Manastone", ["ManastoneConjure.wav"], False, 0.85, 0.98, 1.02),
]
SND = {name[len("Mage - "):]: sid for sid, name, *_ in SOUND_ENTRIES}

# --- Kit helpers -----------------------------------------------------------------------------

FROST_LIGHT = {"r": 0.35, "g": 0.75, "b": 1.0}
FIRE_LIGHT = {"r": 1.0, "g": 0.55, "b": 0.18}
ARCANE_LIGHT = {"r": 0.80, "g": 0.45, "b": 1.0}


def light(colour, intensity, range_, fade_in=0.15, fade_out=0.5):
    return dict(colour, intensity=intensity, range=range_,
                fade_in_time=fade_in, fade_out_time=fade_out)


def channel(particle, sound, colour, animation="CastLoop"):
    """A looping cast kit: loop animation + hand particles + channel loop sound + hand light.
    Used for CASTING (cast bar) and CHANNELING (held from ChannelStart to the channel's end)."""
    return {"scope": "CASTER", "loop": True, "animation_name": animation,
            "attach_bone": "hand_r", "particles": [PARTICLE_DIR + particle + ".hpar"],
            "sound_ids": [SND[sound]], "light": light(colour, 1.3, 4.5, 0.3, 0.4)}


def release(sound=None):
    kit = {"scope": "CASTER", "loop": False, "animation_name": "CastRelease"}
    if sound:
        kit["sound_ids"] = [SND[sound]]
    return kit


def fx(particle, sound=None, scope="CASTER", bone=None, delay=0, flash=None):
    kit = {"scope": scope, "loop": False, "particles": [PARTICLE_DIR + particle + ".hpar"]}
    if sound:
        kit["sound_ids"] = [SND[sound]]
    if bone:
        kit["attach_bone"] = bone
    if delay:
        kit["delay_ms"] = delay
    if flash:
        kit["light"] = flash
    return kit


def projectile(trail, colour, intensity=2.5, range_=9.0, **extra):
    proj = {"motion": "LINEAR", "trail_particle": PARTICLE_DIR + trail + ".hpar",
            "face_movement": True, "spawn_bone": "hand_r",
            "light": light(colour, intensity, range_, 0.1, 0.3)}
    proj.update(extra)
    return proj


def definitions():
    """(visualization name suffix, spell ids, kits_by_event, projectiles)."""
    frost_channel = channel("FrostHandChannel", "Frost Channel", FROST_LIGHT)
    fire_channel = channel("FireHandChannel", "Fire Channel", FIRE_LIGHT)  # Fireball only
    arcane_channel = channel("ArcaneHandChannel", "Arcane Channel", ARCANE_LIGHT)

    # Fire Barrage: three arcing comets per wave, spread left / high / right, like the
    # original entry 10 -- its sphere meshes are replaced by the particle trail.
    barrage = [
        projectile("FireBarrageTrail", FIRE_LIGHT, 1.6, 6.0, motion="ARC", arc_height=1.4, arc_width=2.2),
        projectile("FireBarrageTrail", FIRE_LIGHT, 1.6, 6.0, motion="ARC", arc_height=3.2),
        projectile("FireBarrageTrail", FIRE_LIGHT, 1.6, 6.0, motion="ARC", arc_height=1.4, arc_width=-2.2),
    ]

    return [
        ("Frostbolt", [3, 18, 158], {
            CASTING: [frost_channel],
            CAST: [release("Frostbolt Cast")],
            IMPACT: [fx("FrostboltImpact", "Frostbolt Impact", "TARGET", "spine_03",
                        flash=light(FROST_LIGHT, 2.5, 6.0, 0.05, 0.45))],
            # Slowed: a cold tint while the snare lasts (removed with the aura).
            AURA: [{"scope": "TARGET", "loop": False,
                    "tint": {"r": 0.55, "g": 0.75, "b": 1.0, "a": 1.0}}],
        }, [projectile("FrostboltTrail", FROST_LIGHT, 3.0, 10.0)]),
        ("Ice Lance", [187], {
            CAST: [release("Ice Lance Cast")],
            IMPACT: [fx("IceLanceImpact", "Ice Lance Impact", "TARGET", "spine_03",
                        flash=light(FROST_LIGHT, 2.0, 5.0, 0.05, 0.35))],
        }, [projectile("IceLanceTrail", FROST_LIGHT, 2.0, 7.0)]),
        ("Frostburn", [5], {
            CASTING: [frost_channel],
            CAST: [release()],
            IMPACT: [fx("ChilledBurst", "Frostburn", "TARGET")],
            AURA_IDLE: [fx("FrostburnVeil", scope="TARGET")],
        }, []),
        ("Frost Nova", [68], {
            CAST: [release(), fx("FrostNova", "Frost Nova",
                                 flash=light(FROST_LIGHT, 3.0, 12.0, 0.05, 0.8))],
            AURA: [{"scope": "TARGET", "loop": False,
                    "tint": {"r": 0.45, "g": 0.70, "b": 1.0, "a": 1.0}}],
            AURA_IDLE: [fx("FrozenRoot", scope="TARGET")],
        }, []),
        ("Frost Armor", [6], {
            CAST: [release(), fx("FrostArmorBurst", "Frost Armor",
                                 flash=light(FROST_LIGHT, 1.7, 8.0, 0.1, 0.8))],
            AURA_IDLE: [fx("FrostArmorAura", scope="TARGET")],
        }, []),
        ("Chilled", [22], {
            AURA: [fx("ChilledBurst", "Chilled", "TARGET"),
                   {"scope": "TARGET", "loop": False,
                    "tint": {"r": 0.60, "g": 0.80, "b": 1.0, "a": 1.0}}],
        }, []),
        ("Fireball", [4], {
            CASTING: [fire_channel],
            CAST: [release("Fireball Cast")],
            IMPACT: [fx("FireballImpact", "Fireball Impact", "TARGET", "spine_03",
                        flash=light(FIRE_LIGHT, 3.0, 7.0, 0.05, 0.5))],
            AURA_IDLE: [fx("FireballBurn", scope="TARGET")],
        }, [projectile("FireballTrail", FIRE_LIGHT, 3.0, 10.0)]),
        ("Fire Blast", [7], {
            CAST: [release(), fx("FireBlastHand", bone="hand_r")],
            IMPACT: [fx("FireBlastImpact", "Fire Blast", "TARGET",
                        flash=light(FIRE_LIGHT, 3.5, 8.0, 0.05, 0.6))],
        }, []),
        # Fire Barrage is channeled: the server sends SpellStart, ChannelStart and SpellGo in
        # the same tick, so a CASTING kit would be torn down the frame it spawned. The barrage
        # reads through its waves instead: every triggered 152 cast flares the hand.
        # Channeled: CHANNELING kits run from ChannelStart until the channel ends, across the
        # SpellGos of 150 itself and of every 152 wave. "Channel" is a forward-facing hold pose;
        # no CastRelease, which would cut into it at channel start.
        ("Fire Barrage", [150], {
            CHANNELING: [channel("FireHandChannel", "Fire Channel", FIRE_LIGHT, "Channel")],
        }, []),
        ("Fire Barrage Projectile", [152], {
            CAST: [fx("FireBlastHand", "Fire Barrage Shot", bone="hand_r")],
            IMPACT: [fx("FireBarrageImpact", "Fire Barrage Impact", "TARGET", "spine_03")],
        }, barrage),
        ("Arcane Intellect", [11], {
            CAST: [release()],
            IMPACT: [fx("ArcaneIntellectBurst", "Arcane Intellect", "TARGET",
                        flash=light(ARCANE_LIGHT, 1.5, 6.0, 0.1, 0.8))],
        }, []),
        ("Sleep", [72], {
            CASTING: [arcane_channel],
            CAST: [release()],
            IMPACT: [fx("SleepApply", "Sleep", "TARGET", "head")],
            AURA_IDLE: [fx("SleepAura", scope="TARGET")],
        }, []),
        ("Arcane Disruption", [75], {
            CAST: [release("Arcane Disruption"), fx("ArcaneDisruptionCast", bone="hand_r")],
            IMPACT: [fx("ArcaneDisruptionImpact", scope="TARGET", bone="head",
                        flash=light(ARCANE_LIGHT, 2.0, 5.0, 0.05, 0.4))],
        }, []),
        ("Arcane Pulse", [198], {
            CAST: [release(), fx("ArcanePulse", "Arcane Pulse",
                                 flash=light(ARCANE_LIGHT, 3.0, 10.0, 0.05, 0.7))],
        }, []),
        ("Conjure Lesser Manastone", [124], {
            CASTING: [arcane_channel],
            CAST: [release(), fx("ManastoneConjure", "Conjure Manastone",
                                 flash=light(ARCANE_LIGHT, 2.0, 6.0, 0.1, 0.6))],
        }, []),
    ]


# --- Validation ------------------------------------------------------------------------------

# Chilled is the deliberate exception to "no sound on aura events": it lasts 5 s, it is the
# attacker's only feedback that Frost Armor bit, and AURA_APPLIED is the only event it has.
AURA_SOUND_EXCEPTIONS = {"Mage - Chilled"}


def _require(condition, message):
    """Survives ``python -O``: these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def validate_visualizations(dataset, sound_ids):
    for vis in dataset.entry:
        if not vis.name.startswith("Mage - "):
            continue
        has_aura_idle = 8 in vis.kits_by_event
        for event, kit_list in vis.kits_by_event.items():
            for kit in kit_list.kits:
                _require(not kit.HasField("duration_ms"),
                         f"{vis.name}: duration_ms time-warps the clip; leave it unset")
                _require(not kit.sounds, f"{vis.name}: mage kits use sound_ids")
                _require(not kit.loop or event in (2, 9),
                         f"{vis.name}: only CASTING/CHANNELING kits may loop (event {event})")
                if event in (5, 8) and vis.name not in AURA_SOUND_EXCEPTIONS:
                    _require(not kit.sound_ids,
                             f"{vis.name}: no sound on aura events -- they replay whenever a "
                             f"carrier comes into view")
                if event == 3:
                    _require(kit.scope == 0, f"{vis.name}: CastSucceeded has no target list")
                if event in (5, 8):
                    # Aura events pass the aura holder as the target. A CASTER-scoped aura kit
                    # would be keyed on the caster, which neither cast end (skips aura-bound
                    # effects) nor aura removal (cleans the holder) ever tears down.
                    _require(kit.scope == 1, f"{vis.name}: aura kits must be TARGET-scoped")
                for sound_id in kit.sound_ids:
                    _require(sound_id in sound_ids, f"{vis.name}: unknown sound id {sound_id}")
                for particle in kit.particles:
                    _require((ROOT / "data/client" / particle).is_file(),
                             f"{vis.name}: missing particle {particle}")
        for proj in vis.projectiles:
            _require((ROOT / "data/client" / proj.trail_particle).is_file(),
                     f"{vis.name}: missing trail {proj.trail_particle}")
        _require(not (has_aura_idle and vis.name == "Mage - Fire Barrage"),
                 "Fire Barrage auras sit on caster and target; an AURA_IDLE kit hits both")
    ids = [v.id for v in dataset.entry]
    _require(len(ids) == len(set(ids)), "duplicate visualization ids")
    _require(dataset.IsInitialized(), "visualizations dataset is missing required fields")


# --- Upserts ----------------------------------------------------------------------------------

def upsert_sounds(dataset):
    by_id = {e.id: e for e in dataset.entry}
    for sid, name, files, looped, volume, pitch_min, pitch_max in SOUND_ENTRIES:
        entry = by_id.get(sid)
        if entry is None:
            entry = dataset.entry.add()
        else:
            _require(entry.name == name, f"sound id {sid} is taken by {entry.name!r}")
        entry.Clear()
        entry.id = sid
        entry.name = name
        entry.files.extend(SOUND_DIR + f for f in files)
        entry.category = 0          # SOUND_EFFECTS
        entry.is_3d = True
        entry.looped = looped
        entry.stream = False
        entry.volume = volume
        entry.pitch_min = pitch_min
        entry.pitch_max = pitch_max
        entry.min_distance = 5.0
        entry.max_distance = 30.0
    _require(len({e.id for e in dataset.entry}) == len(dataset.entry), "duplicate sound ids")
    _require(dataset.IsInitialized(), "sounds dataset is missing required fields")


FIRST_VISUALIZATION_ID = 42


def build_drafts(existing_visuals):
    from google.protobuf import json_format  # noqa: F401  (imported for callers)
    existing = {v.name: v.id for v in existing_visuals.entry}
    taken = set(existing.values())
    # New entries are allocated from the mage block (42+), skipping ids other classes hold, so
    # the ids stay stable when the datasets are rebuilt from a develop that gained entries.
    next_id = FIRST_VISUALIZATION_ID
    drafts, links = [], {}
    for suffix, spell_ids, events, projectiles in definitions():
        name = "Mage - " + suffix
        vis_id = existing.get(name)
        if vis_id is None:
            while next_id in taken:
                next_id += 1
            vis_id = next_id
            taken.add(vis_id)
        draft = {"id": vis_id, "name": name,
                 "kits_by_event": {k: {"kits": v} for k, v in events.items()}}
        if projectiles:
            draft["projectiles"] = projectiles
        drafts.append(draft)
        for spell_id in spell_ids:
            links[spell_id] = vis_id
    return drafts, links


def upsert_visuals(dataset, drafts):
    from google.protobuf import json_format
    by_name = {v.name: v for v in dataset.entry}
    for draft in drafts:
        target = by_name.get(draft["name"])
        if target is None:
            target = dataset.entry.add()
        else:
            _require(target.id == draft["id"],
                     f"{draft['name']} exists with id {target.id}, expected {draft['id']}")
            target.Clear()
        json_format.ParseDict(draft, target)


def link_spells(dataset, links, expected_names):
    by_id = {s.id: s for s in dataset.entry}
    for spell_id, vis_id in links.items():
        spell = by_id.get(spell_id)
        _require(spell is not None, f"spell {spell_id} missing")
        _require(spell.name == expected_names[spell_id],
                 f"spell {spell_id} is {spell.name!r}, expected {expected_names[spell_id]!r}")
        spell.visualization_id = vis_id


EXPECTED_SPELLS = {
    3: "Frostbolt", 18: "Frostbolt", 158: "Frostbolt", 187: "Ice Lance", 5: "Frostburn",
    68: "Frost Nova", 6: "Frost Armor", 22: "Chilled", 4: "Fireball", 7: "Fire Blast",
    150: "Fire Barrage", 152: "Fire Barrage Projectile", 11: "Arcane Intellect", 72: "Sleep",
    75: "Arcane Disruption", 198: "Arcane Pulse", 124: "Conjure Lesser Manastone",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    for _, name, files, *_ in SOUND_ENTRIES:
        for f in files:
            _require((ROOT / "data/client" / SOUND_DIR / f).is_file(),
                     f"{name}: missing sound file {SOUND_DIR + f}")

    editor = ROOT / "data/editor/data"
    client = ROOT / "data/client/ClientDB"
    proto_data = ROOT / "src/shared/proto_data"
    client_data = ROOT / "src/shared/client_data"
    targets = {
        "sounds_editor": (editor / "sounds.data",
                          load_type(proto_data, ["sounds.proto"], "mmo.proto.Sounds")),
        "sounds_client": (client / "sounds.data",
                          load_type(client_data, ["sounds.proto"], "mmo.proto_client.Sounds")),
        "vis_editor": (editor / "spell_visualizations.data",
                       load_type(proto_data, ["spell_visualizations.proto"],
                                 "mmo.proto.SpellVisualizations")),
        "vis_client": (client / "spell_visualizations.data",
                       load_type(client_data, ["spells.proto", "spell_visualizations.proto"],
                                 "mmo.proto_client.SpellVisualizations")),
        "spells_editor": (editor / "spells.data",
                          load_type(proto_data, ["spells.proto"], "mmo.proto.Spells")),
        "spells_client": (client / "spells.data",
                          load_type(client_data, ["spells.proto"], "mmo.proto_client.Spells")),
    }
    data = {key: msg_type.FromString(path.read_bytes())
            for key, (path, msg_type) in targets.items()}

    upsert_sounds(data["sounds_editor"])
    upsert_sounds(data["sounds_client"])
    sound_ids = {e.id for e in data["sounds_editor"].entry}

    drafts, links = build_drafts(data["vis_editor"])
    client_drafts, _ = build_drafts(data["vis_client"])
    _require([d["id"] for d in drafts] == [d["id"] for d in client_drafts],
             "editor and client visualization datasets have diverged")
    for key in ("vis_editor", "vis_client"):
        upsert_visuals(data[key], drafts)
        validate_visualizations(data[key], sound_ids)
    for key in ("spells_editor", "spells_client"):
        link_spells(data[key], links, EXPECTED_SPELLS)

    if not args.apply:
        print(f"validated {len(SOUND_ENTRIES)} sound entries, {len(drafts)} visualizations, "
              f"{len(links)} spell links")
        for d in drafts:
            print(f"  {d['id']:3} {d['name']}")
        return

    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir(parents=True)
    for key, (path, _) in targets.items():
        shutil.copy2(path, backup / f"{key}_{path.name}")
    for key, (path, _) in targets.items():
        path.write_bytes(data[key].SerializeToString())
    print(f"wrote sounds, visualizations and spell links to both datasets. Backup: {backup}")


if __name__ == "__main__":
    main()
