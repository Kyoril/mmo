# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the scout spell presentation: sound catalog entries, spell visualizations, and the
spell -> visualization links, in both the editor dataset and the client ClientDB copy.

    py -3.14 tools/scout_visuals/author_scout_visuals.py            # validate only
    py -3.14 tools/scout_visuals/author_scout_visuals.py --apply    # write all six files

Idempotent: sound entries are matched by id and visualizations by name, and keep the ids they
were first given, so re-running replaces rather than appends. Every dataset is parsed,
modified and validated before any file is written. Design doc: docs/scout_spell_visuals.md.

Like the mage script this one writes ``spells.data``, but only ``visualization_id``: scout
spells pointed at nothing, at the warrior's "Bloodrush" (4) or "Deep Wound" (6), or at the
pre-catalog "Wounding Strike" / "Ruthless Strike" entries (18, 19) that Twin Slash and Mark
Weakness borrowed. Those old entries are left in place, unreferenced.

Design rules that are easy to undo by accident (all validated below):

* No kit sets ``duration_ms`` -- it time-warps the clip (see warrior_spell_visuals.md).
* Kits use ``sound_ids`` and never ``sounds``.
* No kit loops: scouts have no cast bars. Looping *particles* (aura idles, the knife trail)
  need no kit flag -- the .hpar loops and aura removal / projectile impact tears it down.
* No sound on AURA_APPLIED / AURA_IDLE / AURA_TICK: AURA_APPLIED re-fires whenever a unit
  carrying the aura comes into view, and a bleed tick sound every 2 s is noise. Buff
  feedback with a sound sits on CAST_SUCCEEDED.
* Aura kits are TARGET-scoped (aura events pass the aura holder as the target).
* Blade ribbons sit on one-shot CAST_SUCCEEDED kits, so every one of them must carry an
  ``emit_duration``: an untimed ribbon outside the cast phase is never torn down.
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

OUT = ROOT / "generated/scout_visuals"
SOUND_DIR = "Sound/Spells/Scout/"
PARTICLE_DIR = "Particles/Scout/"

# Event ids from spell_visualizations.proto.
CAST, IMPACT, AURA, AURA_TICK, AURA_IDLE = "3", "4", "5", "7", "8"

# --- Sound catalog -------------------------------------------------------------------------
# id, name, files, volume, pitch_min, pitch_max. Strikes repeat constantly, so they get the
# widest pitch spread and sit below the big cooldowns; Quick Cut (off the GCD, spammed) is a
# two-file shuffle bag and the quietest.
SOUND_ENTRIES = [
    (180, "Scout - Wounding Strike", ["WoundingStrike.wav"], 0.85, 0.94, 1.06),
    (181, "Scout - Ruthless Strike", ["RuthlessStrike.wav"], 0.90, 0.95, 1.05),
    (182, "Scout - Quick Cut", ["QuickCut01.wav", "QuickCut02.wav"], 0.70, 0.92, 1.08),
    (183, "Scout - Twin Slash", ["TwinSlash.wav"], 0.85, 0.95, 1.05),
    (184, "Scout - Knockout", ["Knockout.wav"], 0.95, 0.97, 1.03),
    (185, "Scout - Throwing Knife Throw", ["ThrowingKnifeThrow.wav"], 0.80, 0.94, 1.06),
    (186, "Scout - Throwing Knife Impact", ["ThrowingKnifeImpact.wav"], 0.85, 0.94, 1.06),
    (187, "Scout - Mark Weakness", ["MarkWeakness.wav"], 0.85, 0.98, 1.02),
    (188, "Scout - Evasive Step", ["EvasiveStep.wav"], 0.80, 0.96, 1.04),
    (189, "Scout - Smoke and Mirrors", ["SmokeAndMirrors.wav"], 0.95, 0.98, 1.02),
    (190, "Scout - Sprint", ["Sprint.wav"], 0.80, 0.97, 1.03),
    (191, "Scout - Fever Pitch", ["FeverPitch.wav"], 0.90, 0.98, 1.02),
    (192, "Scout - Steady Hand", ["SteadyHand.wav"], 0.85, 0.98, 1.02),
]
SND = {name[len("Scout - "):]: sid for sid, name, *_ in SOUND_ENTRIES}

# --- Kit helpers -----------------------------------------------------------------------------

STEEL_LIGHT = {"r": 0.75, "g": 0.86, "b": 1.0}
STUN_LIGHT = {"r": 1.0, "g": 0.85, "b": 0.35}
MARK_LIGHT = {"r": 1.0, "g": 0.22, "b": 0.16}
ENERGY_LIGHT = {"r": 1.0, "g": 0.78, "b": 0.22}

# Measured from the Human rigs (.skel MINA chunks): Attack_1H_01 / Attack_1H_02 0.97 s,
# UnarmedAttack01 0.70 s (male) / 0.83 s (female), CastRelease 0.63 / 0.53 s. Instant melee
# IMPACT is raised at SpellGo, i.e. when the swing *starts*, so target impacts are delayed to
# about where the blade connects. These are feel values, confirmed in the client.
SWING_CONTACT_MS = 280
JAB_CONTACT_MS = 180


def light(colour, intensity, range_, fade_in=0.05, fade_out=0.4):
    return dict(colour, intensity=intensity, range=range_,
                fade_in_time=fade_in, fade_out_time=fade_out)


def anim(clip, sound=None):
    kit = {"scope": "CASTER", "loop": False, "animation_name": clip}
    if sound:
        kit["sound_ids"] = [SND[sound]]
    return kit


def fx(particle, sound=None, scope="CASTER", bone=None, delay=0, flash=None, tint=None):
    kit = {"scope": scope, "loop": False}
    if particle:
        kit["particles"] = [PARTICLE_DIR + particle + ".hpar"]
    if sound:
        kit["sound_ids"] = [SND[sound]]
    if bone:
        kit["attach_bone"] = bone
    if delay:
        kit["delay_ms"] = delay
    if flash:
        kit["light"] = flash
    if tint:
        kit["tint"] = tint
    return kit


def pulse(r, g, b, a, ms):
    """A self-removing tint pulse (ColorTint.duration_ms), not the kit's duration_ms."""
    return {"r": r, "g": g, "b": b, "a": a, "duration_ms": ms}


def blade(bone="hand_r", delay=120, emit=0.45, width=0.30, alpha=0.55):
    """A blade arc: a ribbon on the weapon hand for one swing. Timed (``emit_duration``), so it
    stops emitting after the swing and retires once its segments have faded. The beam sprite
    runs across the ribbon with soft tapered ends, so each arc reads as a crescent."""
    return {"scope": "CASTER", "loop": False, "attach_bone": bone, "delay_ms": delay,
            "ribbon_trail": {
                "material_name": "Particles/Particle_Beam.hmi",
                "initial_width": width, "final_width": 0.0,
                "initial_r": 0.96, "initial_g": 0.98, "initial_b": 1.0, "initial_a": alpha,
                "final_r": 0.68, "final_g": 0.82, "final_b": 0.98, "final_a": 0.0,
                "segment_lifetime": 0.18, "max_segments": 64,
                "segment_interval": 0.012, "emit_duration": emit}}


def strike(clip, sound, impact, contact=SWING_CONTACT_MS, blades=None, flash=None,
           tint=None):
    """A melee technique: the swing clip with its layered sound (whoosh first, the cut lands
    inside the mix), blade arcs on the weapon hand(s), and the impact on the target's chest
    timed to the swing's contact."""
    return {
        CAST: [anim(clip, sound)] + (blades or [blade()]),
        IMPACT: [fx(impact, scope="TARGET", bone="spine_03", delay=contact, flash=flash,
                    tint=tint)],
    }


def buff(burst, sound, idle, flash=None, tint=None, bone=None, idle_bone=None):
    """A self-buff: activation burst + sound on CAST_SUCCEEDED, a quiet looping idle while
    the aura lasts. No cast animation, so it never interrupts attacking."""
    return {
        CAST: [fx(burst, sound, bone=bone, flash=flash, tint=tint)],
        AURA_IDLE: [fx(idle, scope="TARGET", bone=idle_bone)],
    }


def definitions():
    """(visualization name suffix, spell ids, kits_by_event, projectiles)."""
    wound_tint = pulse(1.0, 0.25, 0.22, 0.35, 350)
    return [
        ("Wounding Strike", [76, 160],
         strike("Attack_1H_01", "Wounding Strike", "WoundingStrikeImpact", tint=wound_tint), []),
        ("Ruthless Strike", [77],
         strike("Attack_1H_02", "Ruthless Strike", "RuthlessStrikeImpact",
                blades=[blade(width=0.36, alpha=0.62)],
                flash=light(STEEL_LIGHT, 1.6, 4.0), tint=wound_tint), []),
        ("Quick Cut", [163],
         strike("UnarmedAttack01", "Quick Cut", "QuickCutImpact", contact=JAB_CONTACT_MS,
                blades=[blade(delay=60, emit=0.3, width=0.22, alpha=0.5)]), []),
        ("Twin Slash", [168],
         strike("Attack_1H_01", "Twin Slash", "TwinSlashImpact",
                blades=[blade(), blade("hand_l", delay=240, emit=0.4)], tint=wound_tint), []),
        # The bleed. Visualization 6 "Deep Wound" was an empty shell; Wounded gets its own.
        ("Wounded", [165], {
            AURA_TICK: [fx("WoundedTick", scope="TARGET", bone="spine_03",
                           tint=pulse(1.0, 0.25, 0.22, 0.3, 250))],
            AURA_IDLE: [fx("WoundedBleed", scope="TARGET", bone="spine_03")],
        }, []),
        ("Knockout", [161], {
            CAST: [anim("UnarmedAttack01", "Knockout"),
                   blade(delay=60, emit=0.3, width=0.24)],
            IMPACT: [fx("KnockoutImpact", scope="TARGET", bone="head", delay=JAB_CONTACT_MS,
                        flash=light(STUN_LIGHT, 2.0, 5.0, 0.05, 0.5))],
            AURA_IDLE: [fx("StunStars", scope="TARGET")],
        }, []),
        ("Throwing Knife", [167], {
            CAST: [anim("CastRelease", "Throwing Knife Throw"),
                   fx("ThrowRelease", bone="hand_r", delay=150)],
            IMPACT: [fx("ThrowingKnifeImpact", "Throwing Knife Impact", "TARGET", "spine_03")],
        }, [{"motion": "LINEAR", "trail_particle": PARTICLE_DIR + "ThrowingKnifeTrail.hpar",
             "face_movement": True, "spawn_bone": "hand_r"}]),
        ("Mark Weakness", [169], {
            CAST: [anim("CastRelease", "Mark Weakness")],
            IMPACT: [fx("MarkWeaknessApply", scope="TARGET",
                        flash=light(MARK_LIGHT, 1.8, 5.0, 0.05, 0.6))],
            AURA_IDLE: [fx("MarkWeaknessAura", scope="TARGET")],
        }, []),
        ("Evasive Step", [166],
         buff("EvasiveStepBurst", "Evasive Step", "EvasiveStepAura"), []),
        ("Smoke and Mirrors", [1426],
         buff("SmokeBomb", "Smoke and Mirrors", "SmokeMirrorsAura",
              flash=light(STEEL_LIGHT, 1.4, 6.0, 0.05, 0.6)), []),
        ("Sprint", [10], buff("SprintBurst", "Sprint", "SprintAura"), []),
        ("Fever Pitch", [1409],
         buff("FeverPitchBurst", "Fever Pitch", "FeverPitchAura",
              flash=light(ENERGY_LIGHT, 2.2, 6.0, 0.05, 0.7),
              tint=pulse(1.0, 0.8, 0.3, 0.4, 600)), []),
        ("Steady Hand", [1413],
         buff("SteadyHandFocus", "Steady Hand", "SteadyHandAura", bone="hand_r",
              idle_bone="hand_r", flash=light(STEEL_LIGHT, 1.4, 3.5, 0.1, 0.5)), []),
    ]


# --- Validation ------------------------------------------------------------------------------

def _require(condition, message):
    """Survives ``python -O``: these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def validate_visualizations(dataset, sound_ids):
    for vis in dataset.entry:
        if not vis.name.startswith("Scout - "):
            continue
        for event, kit_list in vis.kits_by_event.items():
            for kit in kit_list.kits:
                _require(not kit.HasField("duration_ms"),
                         f"{vis.name}: duration_ms time-warps the clip; leave it unset")
                _require(not kit.sounds, f"{vis.name}: scout kits use sound_ids")
                _require(not kit.loop, f"{vis.name}: scout kits never loop (event {event})")
                if event in (5, 7, 8):
                    _require(not kit.sound_ids,
                             f"{vis.name}: no sound on aura events -- they replay whenever a "
                             f"carrier comes into view, or every tick")
                    _require(kit.scope == 1, f"{vis.name}: aura kits must be TARGET-scoped")
                if event == 3:
                    _require(kit.scope == 0, f"{vis.name}: CastSucceeded has no target list")
                if kit.HasField("ribbon_trail"):
                    _require(kit.ribbon_trail.emit_duration > 0.0,
                             f"{vis.name}: a ribbon outside the cast phase needs emit_duration")
                for sound_id in kit.sound_ids:
                    _require(sound_id in sound_ids, f"{vis.name}: unknown sound id {sound_id}")
                for particle in kit.particles:
                    _require((ROOT / "data/client" / particle).is_file(),
                             f"{vis.name}: missing particle {particle}")
        for proj in vis.projectiles:
            _require((ROOT / "data/client" / proj.trail_particle).is_file(),
                     f"{vis.name}: missing trail {proj.trail_particle}")
    ids = [v.id for v in dataset.entry]
    _require(len(ids) == len(set(ids)), "duplicate visualization ids")
    _require(dataset.IsInitialized(), "visualizations dataset is missing required fields")


# --- Upserts ----------------------------------------------------------------------------------

def upsert_sounds(dataset):
    by_id = {e.id: e for e in dataset.entry}
    for sid, name, files, volume, pitch_min, pitch_max in SOUND_ENTRIES:
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
        entry.looped = False
        entry.stream = False
        entry.volume = volume
        entry.pitch_min = pitch_min
        entry.pitch_max = pitch_max
        entry.min_distance = 5.0
        entry.max_distance = 30.0
    _require(len({e.id for e in dataset.entry}) == len(dataset.entry), "duplicate sound ids")
    _require(dataset.IsInitialized(), "sounds dataset is missing required fields")


FIRST_VISUALIZATION_ID = 100


def build_drafts(existing_visuals):
    existing = {v.name: v.id for v in existing_visuals.entry}
    taken = set(existing.values())
    # New entries are allocated from the scout block (100+), skipping ids other classes hold,
    # so the ids stay stable when the datasets are rebuilt from a develop that gained entries.
    next_id = FIRST_VISUALIZATION_ID
    drafts, links = [], {}
    for suffix, spell_ids, events, projectiles in definitions():
        name = "Scout - " + suffix
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
    10: "Sprint", 76: "Wounding Strike", 160: "Wounding Strike", 77: "Ruthless Strike",
    161: "Knockout", 163: "Quick Cut", 165: "Wounded", 166: "Evasive Step",
    167: "Throwing Knife", 168: "Twin Slash", 169: "Mark Weakness", 1409: "Fever Pitch",
    1413: "Steady Hand", 1426: "Smoke and Mirrors",
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
    # ClientDB sounds and spells are byte copies of the editor files (the client mirror tests
    # compare bytes); re-serialising them through the client schema reorders fields. The
    # visualizations are serialised per tree, like the mage and cleric scripts do.
    data["sounds_client"] = data["sounds_editor"]
    data["spells_client"] = data["spells_editor"]
    for key, (path, _) in targets.items():
        path.write_bytes(data[key].SerializeToString())
    print(f"wrote sounds, visualizations and spell links to both datasets. Backup: {backup}")


if __name__ == "__main__":
    main()
