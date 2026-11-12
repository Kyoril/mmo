# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the acolyte spell presentation: sound catalog entries, spell visualizations, and the
spell -> visualization links, in both the editor dataset and the client ClientDB copy.

    py -3.14 tools/acolyte_visuals/author_acolyte_visuals.py            # validate only
    py -3.14 tools/acolyte_visuals/author_acolyte_visuals.py --apply    # write all six files

Idempotent: sound entries are matched by id and visualizations by name, and keep the ids they
were first given, so re-running replaces rather than appends. Every dataset is parsed,
modified and validated before any file is written. Design doc: docs/acolyte_spell_visuals.md.

Like the mage and scout scripts this one writes ``spells.data``, but only
``visualization_id``. Acolyte spells pointed at shared pre-catalog entries: "Shadow Bolt" (16,
also used by creature spells), "Fear" (15), "Veil of Shadows" (2, borrowed by Embrace of
Dread), the default visualization (9) or the mage's "Arcane Pulse" (55). Those entries are left
in place for their other users. The talent-built spells (Leeching Rot, Embrace of Dread and the
kit overhaul's talent actives and procs, ``TALENT_SPELLS``) carry the same ids in
``tools/talent_trees/author_talent_trees.py``.

Design rules that are easy to undo by accident (all validated below):

* No kit sets ``duration_ms`` -- it time-warps the clip (see warrior_spell_visuals.md).
* Kits use ``sound_ids`` and never ``sounds``.
* Only CASTING and CHANNELING kits may loop (the cast-bar and channel loops); every other
  looping *particle* (trails, aura idles, ground zones) is torn down by projectile impact,
  aura removal or the zone's end.
* No sound on AURA_APPLIED / AURA_IDLE / AURA_TICK: AURA_APPLIED re-fires whenever a unit
  carrying the aura comes into view, and a DoT tick sound every 2 s is noise. Buff feedback
  with a sound sits on CAST_SUCCEEDED or IMPACT.
* Aura kits are TARGET-scoped (aura events pass the aura holder as the target).
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

OUT = ROOT / "generated/acolyte_visuals"
SOUND_DIR = "Sound/Spells/Acolyte/"
PARTICLE_DIR = "Particles/Acolyte/"

# Event ids from spell_visualizations.proto.
CASTING, CAST, IMPACT, AURA, AURA_TICK, AURA_IDLE = "2", "3", "4", "5", "7", "8"
CHANNELING, GROUND_ACTIVE, GROUND_EXPIRED = "9", "10", "11"

# --- Sound catalog -------------------------------------------------------------------------
# id, name, files, looped, volume, pitch_min, pitch_max. Ids 280-291 (240-252 scout, 260-263
# the town bells; the gap leaves room). Shadowbolt is the rotation filler, so its impact is a
# two-file shuffle bag with the widest pitch spread; Leeching Rot and the DoT ticks are silent.
SOUND_ENTRIES = [
    (280, "Acolyte - Shadow Channel", ["ShadowChannelLoop.wav"], True, 0.55, 1.0, 1.0),
    (281, "Acolyte - Blight Channel", ["BlightChannelLoop.wav"], True, 0.55, 1.0, 1.0),
    (282, "Acolyte - Shadowbolt Cast", ["ShadowboltCast.wav"], False, 0.80, 0.95, 1.05),
    (283, "Acolyte - Shadowbolt Impact", ["ShadowboltImpact01.wav", "ShadowboltImpact02.wav"],
     False, 0.90, 0.92, 1.08),
    (284, "Acolyte - Corrupting Touch", ["CorruptingTouch.wav"], False, 0.85, 0.95, 1.05),
    (285, "Acolyte - Fear Cast", ["FearCast.wav"], False, 0.70, 0.98, 1.02),
    (286, "Acolyte - Fear", ["Fear.wav"], False, 0.90, 0.96, 1.04),
    (287, "Acolyte - Veil of Shadows", ["VeilOfShadows.wav"], False, 0.80, 0.98, 1.02),
    (288, "Acolyte - Embrace of Dread", ["EmbraceOfDread.wav"], False, 1.00, 0.98, 1.02),
    (289, "Acolyte - Umbral Bloom", ["UmbralBloom.wav"], False, 1.00, 0.97, 1.03),
    (290, "Acolyte - Gloaming Trance", ["GloamingTrance.wav"], False, 0.75, 0.98, 1.02),
    (291, "Acolyte - Void Resurgence", ["VoidResurgence.wav"], False, 0.85, 0.98, 1.02),
    # Kit overhaul (2026-11-02): ids 432+ (424-431 are the Duskling's voice lines and ritual).
    # Rot Field's cast bar reuses the Blight Channel loop; procs and ticks stay silent.
    (432, "Acolyte - Curse of Frailty", ["CurseFrailty.wav"], False, 0.80, 0.96, 1.04),
    (433, "Acolyte - Drain Channel", ["DrainChannelLoop.wav"], True, 0.55, 1.0, 1.0),
    (434, "Acolyte - Curse of the Mire", ["CurseMire.wav"], False, 0.80, 0.96, 1.04),
    (435, "Acolyte - Wasting Plague", ["WastingPlague.wav"], False, 0.85, 0.95, 1.05),
    (436, "Acolyte - Call Gloomlings", ["CallGloomlings.wav"], False, 0.85, 0.97, 1.03),
    (437, "Acolyte - Rot Field", ["RotFieldCast.wav"], False, 0.90, 0.97, 1.03),
    (438, "Acolyte - Tangled Tongue", ["TangledTongue.wav"], False, 0.80, 0.96, 1.04),
    (439, "Acolyte - Wail of the Lost", ["WailOfTheLost.wav"], False, 0.95, 0.98, 1.02),
    (440, "Acolyte - Empower Duskling", ["EmpowerDuskling.wav"], False, 0.80, 0.97, 1.03),
    (441, "Acolyte - Brood Horror", ["BroodHorror.wav"], False, 1.00, 0.98, 1.02),
    (442, "Acolyte - Gloom Tether", ["GloomTether.wav"], False, 0.75, 0.95, 1.05),
]
SND = {name[len("Acolyte - "):]: sid for sid, name, *_ in SOUND_ENTRIES}

# --- Kit helpers -----------------------------------------------------------------------------

VOID_LIGHT = {"r": 0.80, "g": 0.26, "b": 1.0}
BLIGHT_LIGHT = {"r": 0.50, "g": 0.95, "b": 0.25}
DREAD_LIGHT = {"r": 1.0, "g": 0.18, "b": 0.30}

# Fear's base cast time is 2.0 s and its cast sound is a reversed swell peaking at 0.98 s:
# started 1.0 s into the bar it ends on the release. Dread Presence cuts the cast to 1.0 s,
# so with the talent the delayed kit is dropped at cast end and only the channel plays.
FEAR_SWELL_DELAY_MS = 1000


def light(colour, intensity, range_, fade_in=0.05, fade_out=0.45):
    return dict(colour, intensity=intensity, range=range_,
                fade_in_time=fade_in, fade_out_time=fade_out)


def channel(particle, sound, colour):
    """A looping cast-bar kit: CastLoop + hand particles + channel loop sound + hand light."""
    return {"scope": "CASTER", "loop": True, "animation_name": "CastLoop",
            "attach_bone": "hand_r", "particles": [PARTICLE_DIR + particle + ".hpar"],
            "sound_ids": [SND[sound]], "light": light(colour, 1.3, 4.5, 0.3, 0.4)}


def release(sound=None, particle=None):
    """CastRelease, optionally with a sound and a flare on the casting hand."""
    kit = {"scope": "CASTER", "loop": False, "animation_name": "CastRelease"}
    if sound:
        kit["sound_ids"] = [SND[sound]]
    if particle:
        kit["particles"] = [PARTICLE_DIR + particle + ".hpar"]
        kit["attach_bone"] = "hand_r"
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


def definitions():
    """(visualization name suffix, spell ids, kits_by_event, projectiles)."""
    shadow_channel = channel("ShadowHandChannel", "Shadow Channel", VOID_LIGHT)
    return [
        ("Shadowbolt", [23, 74], {
            CASTING: [shadow_channel],
            CAST: [release("Shadowbolt Cast", "ShadowRelease")],
            IMPACT: [fx("ShadowboltImpact", "Shadowbolt Impact", "TARGET", "spine_03",
                        flash=light(VOID_LIGHT, 2.6, 6.0), tint=pulse(0.70, 0.45, 1.0, 0.35, 300))],
        }, [{"motion": "LINEAR", "trail_particle": PARTICLE_DIR + "ShadowboltTrail.hpar",
             "face_movement": True, "spawn_bone": "hand_r",
             "light": light(VOID_LIGHT, 2.4, 8.0, 0.1, 0.3)}]),
        ("Corrupting Touch", [24], {
            CASTING: [channel("BlightHandChannel", "Blight Channel", BLIGHT_LIGHT)],
            CAST: [release()],
            IMPACT: [fx("CorruptionApply", "Corrupting Touch", "TARGET", "spine_03",
                        flash=light(BLIGHT_LIGHT, 2.0, 5.0, 0.05, 0.6),
                        tint=pulse(0.60, 1.0, 0.45, 0.35, 450))],
            AURA_TICK: [fx("CorruptionTick", scope="TARGET", bone="spine_03",
                           tint=pulse(0.65, 1.0, 0.5, 0.25, 250))],
            AURA_IDLE: [fx("CorruptionAura", scope="TARGET")],
        }, []),
        ("Fear", [73], {
            CASTING: [channel("DreadHandChannel", "Shadow Channel", DREAD_LIGHT),
                      fx(None, "Fear Cast", delay=FEAR_SWELL_DELAY_MS)],
            CAST: [release()],
            IMPACT: [fx("FearApply", "Fear", "TARGET", "head",
                        flash=light(DREAD_LIGHT, 2.2, 5.0, 0.05, 0.6))],
            AURA_IDLE: [fx("FearAura", scope="TARGET")],
        }, []),
        ("Veil of Shadows", [38], {
            CAST: [fx("VeilBurst", "Veil of Shadows", flash=light(VOID_LIGHT, 1.6, 6.0, 0.1, 0.8))],
            AURA_IDLE: [fx("VeilAura", scope="TARGET")],
        }, []),
        ("Umbral Bloom", [1309], {
            CAST: [release(), fx("UmbralBloom", "Umbral Bloom",
                                 flash=light(VOID_LIGHT, 3.2, 12.0, 0.05, 0.8))],
            IMPACT: [fx(None, scope="TARGET", tint=pulse(0.70, 0.45, 1.0, 0.35, 300))],
        }, []),
        ("Embrace of Dread", [1328], {
            CAST: [fx("EmbraceBurst", "Embrace of Dread",
                      flash=light(DREAD_LIGHT, 3.0, 9.0, 0.05, 1.0),
                      tint=pulse(1.0, 0.35, 0.45, 0.45, 700))],
            AURA_IDLE: [fx("EmbraceAura", scope="TARGET")],
        }, []),
        ("Gloaming Trance", [1306], {
            CAST: [fx("GloamingProc", "Gloaming Trance", bone="hand_r",
                      flash=light(VOID_LIGHT, 1.5, 4.0, 0.05, 0.5))],
            AURA_IDLE: [fx("GloamingAura", scope="TARGET", bone="hand_r")],
        }, []),
        # Leeching Rot heals the caster on every Corrupting Touch tick: no sound, small motes.
        ("Leeching Rot", [1314], {
            IMPACT: [fx("LeechingRotHeal", scope="TARGET", bone="spine_03")],
        }, []),
        ("Void Resurgence", [119, 120, 121], {
            CAST: [fx("VoidResurgenceBurst", "Void Resurgence",
                      flash=light(VOID_LIGHT, 1.8, 6.0, 0.2, 0.8))],
            AURA_IDLE: [fx("VoidResurgenceAura", scope="TARGET")],
        }, []),
    ] + kit_overhaul_definitions()


def kit_overhaul_definitions():
    """The 2026-11-02 kit overhaul: curses, Drain Essence, Wasting Plague, Rot Field, the
    summons and the Summoner/Blight/Dread talent actives and procs. Every curse idles with its
    own particle so players can tell them apart; procs and ticks are silent."""
    return [
        ("Curse of Frailty", [1330], {
            CAST: [release()],
            IMPACT: [fx("FrailtyApply", "Curse of Frailty", "TARGET", "spine_03",
                        flash=light(VOID_LIGHT, 1.8, 5.0, 0.05, 0.6))],
            AURA_IDLE: [fx("FrailtyAura", scope="TARGET")],
        }, []),
        # Channeled: the server sends SpellStart, ChannelStart and SpellGo in the same tick, so
        # the channel lives on CHANNELING (held until the channel ends) and there is no
        # CastRelease, which would cut into the loop at channel start.
        ("Drain Essence", [1331], {
            CHANNELING: [channel("DrainChannel", "Drain Channel", VOID_LIGHT)],
        }, []),
        # Every tick of the channel: motes torn out of the victim. Silent (one every second).
        ("Drain Essence Tick", [1332], {
            IMPACT: [fx("DrainBeamMotes", scope="TARGET", bone="spine_03")],
        }, []),
        ("Curse of the Mire", [1333], {
            CAST: [release()],
            IMPACT: [fx("MireApply", "Curse of the Mire", "TARGET",
                        flash=light(BLIGHT_LIGHT, 1.6, 5.0, 0.05, 0.6))],
            AURA_IDLE: [fx("MireAura", scope="TARGET")],
        }, []),
        # Also cast by Venomous Backlash (1358) on a melee attacker.
        ("Wasting Plague", [1334], {
            CAST: [release()],
            IMPACT: [fx("PlagueApply", "Wasting Plague", "TARGET", "spine_03",
                        flash=light(BLIGHT_LIGHT, 1.8, 5.0, 0.05, 0.6),
                        tint=pulse(0.60, 1.0, 0.45, 0.35, 400))],
            AURA_TICK: [fx(None, scope="TARGET", tint=pulse(0.65, 1.0, 0.5, 0.25, 250))],
            AURA_IDLE: [fx("PlagueAura", scope="TARGET")],
        }, []),
        ("Call Gloomlings", [1335], {
            CAST: [release(), fx("GloomlingSummon", "Call Gloomlings",
                                 flash=light(VOID_LIGHT, 2.2, 7.0, 0.05, 0.8))],
        }, []),
        # The zone lies on the ground for 8 s (12 s with Creeping Rot): the field loops at the
        # zone's position until SpellZoneEnd and fades out on expiry.
        ("Rot Field", [1336], {
            CASTING: [channel("BlightHandChannel", "Blight Channel", BLIGHT_LIGHT)],
            CAST: [release("Rot Field")],
            GROUND_ACTIVE: [fx("RotFieldGround", flash=light(BLIGHT_LIGHT, 1.4, 9.0, 0.6, 1.0))],
            GROUND_EXPIRED: [fx("RotFieldTick", flash=light(BLIGHT_LIGHT, 1.2, 8.0, 0.05, 1.0))],
        }, []),
        ("Rot Field Tick", [1337], {
            IMPACT: [fx("RotFieldTick", scope="TARGET", bone="spine_03")],
        }, []),
        # The aura sits on the pet: the burst plays at the caster, the empowerment idles on
        # the Duskling (aura events pass the aura holder as the target).
        ("Empower Duskling", [1345], {
            CAST: [release("Empower Duskling", "ShadowRelease")],
            IMPACT: [fx(None, scope="TARGET", tint=pulse(0.70, 0.45, 1.0, 0.40, 500))],
            AURA_IDLE: [fx("EmpowerAura", scope="TARGET")],
        }, []),
        # Gloom Tether's proc summon: a proc, so no CastRelease, but a summon worth hearing.
        ("Gloom Tether", [1347], {
            CAST: [fx("GloomlingSummon", "Gloom Tether", flash=light(VOID_LIGHT, 1.6, 5.0, 0.05, 0.6))],
        }, []),
        # Shared Night heals the pet on every Drain Essence tick: silent motes.
        ("Shared Night", [1349], {
            IMPACT: [fx("SharedNightHeal", scope="TARGET", bone="spine_03")],
        }, []),
        ("Brood Horror", [1352], {
            CAST: [release(), fx("BroodSummon", "Brood Horror",
                                 flash=light(VOID_LIGHT, 3.0, 10.0, 0.05, 1.0))],
        }, []),
        # Creeping Rot's slow, applied on every Rot Field tick: the mire idle, silent.
        ("Creeping Rot", [1357], {
            AURA_IDLE: [fx("MireAura", scope="TARGET")],
        }, []),
        ("Tangled Tongue", [1360], {
            CAST: [release()],
            IMPACT: [fx(None, "Tangled Tongue", "TARGET", "head",
                        flash=light(DREAD_LIGHT, 1.6, 4.0, 0.05, 0.6))],
            AURA_IDLE: [fx("TongueAura", scope="TARGET")],
        }, []),
        # Grasping Dread's 2 s root: the tendrils are a one-shot that runs out with the root,
        # so they sit on IMPACT rather than replaying whenever the victim comes into view.
        ("Grasping Dread", [1362], {
            IMPACT: [fx("RootTendrils", scope="TARGET")],
        }, []),
        # Fleeing victims carry Fear's skull idle so a wail reads like a Fear.
        ("Wail of the Lost", [1363], {
            CAST: [release(), fx("WailBurst", "Wail of the Lost",
                                 flash=light(DREAD_LIGHT, 2.6, 9.0, 0.05, 0.8),
                                 tint=pulse(1.0, 0.35, 0.45, 0.35, 500))],
            AURA_IDLE: [fx("FearAura", scope="TARGET")],
        }, []),
        ("Lingering Dread", [1365], {
            AURA_IDLE: [fx("HealingBlight", scope="TARGET")],
        }, []),
    ]


# --- Validation ------------------------------------------------------------------------------

def _require(condition, message):
    """Survives ``python -O``: these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def validate_visualizations(dataset, sound_ids):
    for vis in dataset.entry:
        if not vis.name.startswith("Acolyte - "):
            continue
        for event, kit_list in vis.kits_by_event.items():
            for kit in kit_list.kits:
                _require(not kit.HasField("duration_ms"),
                         f"{vis.name}: duration_ms time-warps the clip; leave it unset")
                _require(not kit.sounds, f"{vis.name}: acolyte kits use sound_ids")
                _require(not kit.loop or event in (2, 9),
                         f"{vis.name}: only CASTING/CHANNELING kits may loop (event {event})")
                if event in (5, 7, 8):
                    _require(not kit.sound_ids,
                             f"{vis.name}: no sound on aura events -- they replay whenever a "
                             f"carrier comes into view, or every tick")
                    _require(kit.scope == 1, f"{vis.name}: aura kits must be TARGET-scoped")
                if event in (2, 3, 9):
                    _require(kit.scope == 0, f"{vis.name}: cast events have no target list")
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


FIRST_VISUALIZATION_ID = 130


def build_drafts(existing_visuals):
    existing = {v.name: v.id for v in existing_visuals.entry}
    taken = set(existing.values())
    # New entries are allocated from the acolyte block (130+), skipping ids other classes
    # hold, so the ids stay stable when the datasets are rebuilt from a develop that gained
    # entries.
    next_id = FIRST_VISUALIZATION_ID
    drafts, links = [], {}
    for suffix, spell_ids, events, projectiles in definitions():
        name = "Acolyte - " + suffix
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
    23: "Shadowbolt", 74: "Shadowbolt", 24: "Corrupting Touch", 73: "Fear",
    38: "Veil of Shadows", 1309: "Umbral Bloom", 1328: "Embrace of Dread",
    1306: "Gloaming Trance", 1314: "Leeching Rot",
    119: "Void Resurgence", 120: "Void Resurgence", 121: "Void Resurgence",
    1330: "Curse of Frailty", 1331: "Drain Essence", 1332: "Drain Essence",
    1333: "Curse of the Mire", 1334: "Wasting Plague", 1335: "Call Gloomlings",
    1336: "Rot Field", 1337: "Rot Field", 1345: "Empower Duskling", 1347: "Gloom Tether",
    1349: "Shared Night", 1352: "Brood Horror", 1357: "Creeping Rot", 1360: "Tangled Tongue",
    1362: "Grasping Dread", 1363: "Wail of the Lost", 1365: "Lingering Dread",
}

# The ids the talent authoring script must carry for the spells it builds (validated so the
# two scripts cannot drift apart).
TALENT_SPELLS = (1314, 1328, 1345, 1347, 1349, 1352, 1357, 1360, 1362, 1363, 1365)


def check_talent_script(links):
    source = (ROOT / "tools/talent_trees/author_talent_trees.py").read_text(encoding="utf-8")
    for spell_id in TALENT_SPELLS:
        marker = f'"visualization_id": {links[spell_id]},  # Acolyte - '
        _require(marker in source,
                 f"author_talent_trees.py: spell {spell_id} must carry {marker!r}")


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
        check_talent_script(links)
        return

    check_talent_script(links)
    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir(parents=True)
    for key, (path, _) in targets.items():
        shutil.copy2(path, backup / f"{key}_{path.name}")
    # ClientDB files are byte copies of the editor files (the client mirror tests compare
    # bytes): serialise each editor message once and write the same blob to both trees.
    data["sounds_client"] = data["sounds_editor"]
    data["vis_client"] = data["vis_editor"]
    data["spells_client"] = data["spells_editor"]
    blobs = {key: data[key].SerializeToString()
             for key in ("sounds_editor", "vis_editor", "spells_editor")}
    for key, (path, _) in targets.items():
        path.write_bytes(blobs[key.split("_")[0] + "_editor"])
    print(f"wrote sounds, visualizations and spell links to both datasets. Backup: {backup}")


if __name__ == "__main__":
    main()
