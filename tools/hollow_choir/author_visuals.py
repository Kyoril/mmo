# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the Hollow Choir boss spell presentation: sound catalog entries, spell visualizations
and the spell -> visualization links, in both the editor dataset and the client ClientDB copy.

    py -3 tools/hollow_choir/author_visuals.py            # validate only
    py -3 tools/hollow_choir/author_visuals.py --apply    # write all six files

Idempotent: sound entries and visualizations are written by id, so re-running replaces rather
than appends. Every dataset is parsed, modified and validated before any file is written.
Particles come from tools/particle_gen/recipes/hollow_choir_*.py, sounds from
tools/sfx_gen/recipes/hollow_choir.py. Design: docs/hollow_choir_bosses.md.

Rules carried over from the class visual passes (see tools/mage_visuals/author_mage_visuals.py):

* No kit sets ``duration_ms`` -- it time-warps the animation clip.
* Kits use ``sound_ids``, never ``sounds``.
* A kit's ``loop`` flag applies to its sound as well as its animation, so a looping pose and a
  one-shot sound are two kits.
* Only CASTING, CHANNELING and GROUND_ACTIVE kits may loop; everything else is a one-shot.
* No sound on aura events: AURA_APPLIED replays whenever a carrier comes into view. Choral
  Resonance therefore sounds on its CAST_SUCCEEDED (the boss casting it on himself).
* Aura kits are TARGET-scoped (the aura holder), CAST_SUCCEEDED kits CASTER-scoped.
* Per-player impacts only exist for unit-targeted spells. The caster-centred AoEs (Lament,
  the Dirge pulse) have none, so their release effect has to carry the hit on its own.
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

SOUND_DIR = "Sound/Spells/HollowChoir/"
PARTICLE_DIR = "Particles/HollowChoir/"

# Event ids from spell_visualizations.proto.
CASTING, CAST, IMPACT, AURA, AURA_IDLE, CHANNELING = "2", "3", "4", "5", "8", "9"
GROUND_ACTIVE, GROUND_EXPIRED = "10", "11"
LOOPABLE_EVENTS = {2, 9, 10}

# --- Sound catalog -------------------------------------------------------------------------
# id, name, file, looped, volume
# Boss sounds carry across a whole room: the warnings (Candle Mark, Dissonance Mark) are what a
# player reacts to, so they get the widest audible range and full volume.
SOUND_ENTRIES = [
    (140, "Hollow Choir - Grave Strike Windup", "Oswin_GraveStrikeWindup.wav", False, 0.90),
    (141, "Hollow Choir - Grave Strike Impact", "Oswin_GraveStrikeImpact.wav", False, 1.00),
    (142, "Hollow Choir - Last Vigil Cast", "Oswin_LastVigilCast.wav", False, 0.90),
    (143, "Hollow Choir - Guttering Candle Mark", "Oswin_CandleMark.wav", False, 1.00),
    (144, "Hollow Choir - Guttering Candle Flare", "Oswin_CandleFlare.wav", False, 1.00),
    (145, "Hollow Choir - Lament Cast", "Mereth_LamentCast.wav", False, 0.95),
    (146, "Hollow Choir - Lament Release", "Mereth_LamentRelease.wav", False, 1.00),
    (147, "Hollow Choir - Mourning Voices Cast", "Mereth_VoicesCast.wav", False, 0.90),
    (148, "Hollow Choir - Silent Place Mark", "Mereth_SilentMark.wav", False, 1.00),
    (149, "Hollow Choir - Silent Place Loop", "Mereth_SilentLoop.wav", True, 0.60),
    (150, "Hollow Choir - Dissonance Mark", "Veyr_DissonanceMark.wav", False, 1.00),
    (151, "Hollow Choir - Dissonance Wave", "Veyr_DissonanceWave.wav", False, 1.00),
    (152, "Hollow Choir - Dirge Loop", "Veyr_DirgeLoop.wav", True, 0.80),
    (153, "Hollow Choir - Choir Rises Cast", "Veyr_ChoirRisesCast.wav", False, 0.95),
    (154, "Hollow Choir - Resonance Hum", "Veyr_ResonanceHum.wav", False, 0.80),
]
SND = {name[len("Hollow Choir - "):]: sid for sid, name, *_ in SOUND_ENTRIES}

# --- Kit helpers -----------------------------------------------------------------------------

CANDLE = {"r": 1.0, "g": 0.62, "b": 0.22}
TEAL = {"r": 0.35, "g": 0.95, "b": 0.90}
INDIGO = {"r": 0.45, "g": 0.35, "b": 1.0}
MAGENTA = {"r": 1.0, "g": 0.30, "b": 0.85}
VIOLET = {"r": 0.70, "g": 0.35, "b": 1.0}


def light(colour, intensity, range_, fade_in=0.15, fade_out=0.5):
    return dict(colour, intensity=intensity, range=range_,
                fade_in_time=fade_in, fade_out_time=fade_out)


def pose(animation):
    """A looping cast pose, held until the cast or channel ends. Carries no sound."""
    return {"scope": "CASTER", "loop": True, "animation_name": animation}


def anim(animation):
    return {"scope": "CASTER", "loop": False, "animation_name": animation}


def fx(particle=None, sound=None, scope="CASTER", bone=None, flash=None, loop=False):
    kit = {"scope": scope, "loop": loop}
    if particle:
        kit["particles"] = [PARTICLE_DIR + particle + ".hpar"]
    if sound:
        kit["sound_ids"] = [SND[sound]]
    if bone:
        kit["attach_bone"] = bone
    if flash:
        kit["light"] = flash
    return kit


# id, name, spell ids, kits_by_event
VISUALIZATIONS = [
    # --- Brother Oswin ---------------------------------------------------------------------
    (80, "Hollow Choir - Grave Strike", [251], {
        CASTING: [pose("CastLoop"),
                  fx("Oswin_GraveStrikeWindup", "Grave Strike Windup", flash=light(CANDLE, 1.2, 6.0, 1.2, 0.2))],
        CAST: [anim("Attack_1H_02"),
               fx("Oswin_GraveStrikeImpact", "Grave Strike Impact", flash=light(CANDLE, 2.5, 9.0, 0.05, 0.5))],
    }),
    (81, "Hollow Choir - Last Vigil", [252], {
        CASTING: [pose("ChannelUp"), fx("Oswin_LastVigilCast", "Last Vigil Cast")],
        CAST: [anim("EmoteRoar"), fx("Oswin_LastVigilRelease")],
    }),
    (82, "Hollow Choir - Guttering Candle", [253], {
        CAST: [anim("CastRelease")],
        GROUND_ACTIVE: [fx("Oswin_CandleGround", "Guttering Candle Mark", flash=light(CANDLE, 2.0, 7.0, 1.8, 0.2))],
        GROUND_EXPIRED: [fx("Oswin_CandleFlare", "Guttering Candle Flare", flash=light(CANDLE, 4.0, 11.0, 0.05, 0.7))],
    }),
    (83, "Hollow Choir - Guttering Candle Burn", [254], {
        IMPACT: [fx("Oswin_CandleBurn", scope="TARGET")],
    }),

    # --- Sister Mereth ---------------------------------------------------------------------
    (84, "Hollow Choir - Lament", [255], {
        CASTING: [pose("CastLoop"),
                  fx("Mereth_LamentCast", "Lament Cast", flash=light(TEAL, 1.6, 8.0, 1.5, 0.3))],
        CAST: [anim("CastRelease"),
               fx("Mereth_LamentRelease", "Lament Release", flash=light(TEAL, 3.0, 14.0, 0.05, 0.9))],
    }),
    (85, "Hollow Choir - Mourning Voices", [256], {
        CASTING: [pose("ChannelUp"), fx("Mereth_VoicesCast", "Mourning Voices Cast")],
        CAST: [anim("EmoteCry")],
    }),
    (86, "Hollow Choir - Mourning Chorus", [257], {
        AURA_IDLE: [fx("Mereth_ChorusVeil", scope="TARGET")],
    }),
    (87, "Hollow Choir - Silent Place", [258], {
        CAST: [anim("CastRelease")],
        GROUND_ACTIVE: [fx("Mereth_SilentGround", "Silent Place Loop", flash=light(INDIGO, 1.5, 7.0, 0.6, 1.0), loop=True),
                        fx(sound="Silent Place Mark")],
    }),
    (88, "Hollow Choir - Silent Place Pulse", [259], {
        IMPACT: [fx("Mereth_SilentPulse", scope="TARGET")],
    }),

    # --- Cantor Veyr -----------------------------------------------------------------------
    (89, "Hollow Choir - Dissonance", [260], {
        CAST: [anim("CastRelease")],
        GROUND_ACTIVE: [fx("Veyr_DissonanceGround", "Dissonance Mark", flash=light(MAGENTA, 1.8, 7.0, 1.8, 0.2))],
        GROUND_EXPIRED: [fx("Veyr_DissonanceWave", "Dissonance Wave", flash=light(MAGENTA, 4.0, 11.0, 0.05, 0.6))],
    }),
    (90, "Hollow Choir - Dissonance Hit", [261], {
        IMPACT: [fx("Veyr_DissonanceHit", scope="TARGET")],
    }),
    (91, "Hollow Choir - Dirge of the Grave", [262], {
        CHANNELING: [{"scope": "CASTER", "loop": True, "animation_name": "ChannelUp",
                      "particles": [PARTICLE_DIR + "Veyr_DirgeChannel.hpar"],
                      "sound_ids": [SND["Dirge Loop"]], "light": light(VIOLET, 2.0, 12.0, 0.4, 0.6)}],
    }),
    (93, "Hollow Choir - The Choir Rises", [264], {
        CASTING: [pose("ChannelUp"), fx("Veyr_ChoirRisesCast", "Choir Rises Cast")],
        CAST: [anim("EmoteShout"), fx("Veyr_ChoirRisesRelease", flash=light(MAGENTA, 2.5, 9.0, 0.05, 0.6))],
    }),
    (94, "Hollow Choir - Choral Resonance", [265], {
        CAST: [fx(sound="Resonance Hum")],
        AURA_IDLE: [fx("Veyr_ResonanceAura", scope="TARGET")],
    }),
]

# Spells deliberately left without a visualization: the Dirge pulse fires every second from
# the boss himself, and the channel's CHANNELING kit already carries the whole picture.
NO_VISUALIZATION = {263}

EXPECTED_SPELLS = {
    251: "Grave Strike", 252: "Last Vigil", 253: "Guttering Candle", 254: "Guttering Candle",
    255: "Lament", 256: "Mourning Voices", 257: "Mourning Chorus", 258: "Silent Place",
    259: "Silent Place", 260: "Dissonance", 261: "Dissonance", 262: "Dirge of the Grave",
    263: "Dirge of the Grave", 264: "The Choir Rises", 265: "Choral Resonance",
}


def _require(condition, message):
    """Survives ``python -O``: these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def validate_visualizations(dataset, sound_ids):
    for vis in dataset.entry:
        if not vis.name.startswith("Hollow Choir - "):
            continue
        for event, kit_list in vis.kits_by_event.items():
            for kit in kit_list.kits:
                _require(not kit.HasField("duration_ms"),
                         f"{vis.name}: duration_ms time-warps the clip; leave it unset")
                _require(not kit.sounds, f"{vis.name}: kits use sound_ids")
                _require(not kit.loop or event in LOOPABLE_EVENTS,
                         f"{vis.name}: only CASTING/CHANNELING/GROUND_ACTIVE kits may loop (event {event})")
                if event in (5, 8):
                    _require(not kit.sound_ids, f"{vis.name}: no sound on aura events")
                    _require(kit.scope == 1, f"{vis.name}: aura kits must be TARGET-scoped")
                if event == 3:
                    _require(kit.scope == 0, f"{vis.name}: CastSucceeded has no target list")
                for sound_id in kit.sound_ids:
                    _require(sound_id in sound_ids, f"{vis.name}: unknown sound id {sound_id}")
                for particle in kit.particles:
                    _require((ROOT / "data/client" / particle).is_file(),
                             f"{vis.name}: missing particle {particle}")
    ids = [v.id for v in dataset.entry]
    _require(len(ids) == len(set(ids)), "duplicate visualization ids")
    _require(dataset.IsInitialized(), "visualizations dataset is missing required fields")


def upsert_sounds(dataset):
    by_id = {e.id: e for e in dataset.entry}
    for sid, name, file_name, looped, volume in SOUND_ENTRIES:
        entry = by_id.get(sid)
        if entry is None:
            entry = dataset.entry.add()
        else:
            _require(entry.name == name, f"sound id {sid} is taken by {entry.name!r}")
        entry.Clear()
        entry.id = sid
        entry.name = name
        entry.files.append(SOUND_DIR + file_name)
        entry.category = 0          # SOUND_EFFECTS
        entry.is_3d = True
        entry.looped = looped
        entry.stream = False
        entry.volume = volume
        entry.pitch_min = 0.97
        entry.pitch_max = 1.03
        entry.min_distance = 8.0
        entry.max_distance = 50.0
    _require(len({e.id for e in dataset.entry}) == len(dataset.entry), "duplicate sound ids")
    _require(dataset.IsInitialized(), "sounds dataset is missing required fields")


def upsert_visuals(dataset):
    from google.protobuf import json_format
    by_id = {v.id: v for v in dataset.entry}
    for vis_id, name, _, events in VISUALIZATIONS:
        target = by_id.get(vis_id)
        if target is None:
            target = dataset.entry.add()
        else:
            _require(target.name == name, f"visualization id {vis_id} is taken by {target.name!r}")
            target.Clear()
        json_format.ParseDict({"id": vis_id, "name": name,
                               "kits_by_event": {k: {"kits": v} for k, v in events.items()}}, target)


def link_spells(dataset):
    by_id = {s.id: s for s in dataset.entry}
    for spell_id, name in EXPECTED_SPELLS.items():
        spell = by_id.get(spell_id)
        _require(spell is not None, f"spell {spell_id} missing")
        _require(spell.name == name, f"spell {spell_id} is {spell.name!r}, expected {name!r}")
        if spell_id in NO_VISUALIZATION:
            spell.ClearField("visualization_id")
    for vis_id, _, spell_ids, _ in VISUALIZATIONS:
        for spell_id in spell_ids:
            by_id[spell_id].visualization_id = vis_id


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    for _, name, file_name, *_ in SOUND_ENTRIES:
        _require((ROOT / "data/client" / SOUND_DIR / file_name).is_file(),
                 f"{name}: missing sound file {SOUND_DIR + file_name}")

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

    for key in ("sounds_editor", "sounds_client"):
        upsert_sounds(data[key])
    sound_ids = {e.id for e in data["sounds_editor"].entry}
    for key in ("vis_editor", "vis_client"):
        upsert_visuals(data[key])
        validate_visualizations(data[key], sound_ids)
    link_spells(data["spells_editor"])
    # The ClientDB copy must stay byte-identical to the editor file (tools/tests check this), and
    # the client schema serializes the same entries differently, so it gets the editor bytes.
    data["spells_client"] = targets["spells_client"][1].FromString(data["spells_editor"].SerializeToString())
    print(f"{len(SOUND_ENTRIES)} sounds, {len(VISUALIZATIONS)} visualizations, "
          f"{len(EXPECTED_SPELLS)} spells checked")

    if not args.apply:
        print("validated; pass --apply to write")
        return

    backup = ROOT / "generated/hollow_choir/backup" / datetime.now().strftime("%Y%m%d%H%M%S")
    backup.mkdir(parents=True, exist_ok=True)
    for key, (path, _) in targets.items():
        shutil.copy2(path, backup / f"{path.parent.name}_{path.name}")
        payload = data["spells_editor"] if key == "spells_client" else data[key]
        path.write_bytes(payload.SerializeToString())
        print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
