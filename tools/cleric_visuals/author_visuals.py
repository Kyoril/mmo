# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the cleric spell visualizations and point every cleric spell at them.

Writes ``spell_visualizations.data`` **and** ``spells.data`` (only the ``visualization_id``
field of the listed spells) in both the editor dataset and the client ClientDB copy.
Unlike the warrior script this one owns the spell -> visualization link: every cleric spell
pointed at the shared "Default Spell Visualization" (9) or at nothing, so there was no
existing link to preserve.

    python tools/cleric_visuals/author_visuals.py            # validate only
    python tools/cleric_visuals/author_visuals.py --apply    # write all four datasets

The kits use every visualization feature the client implements, per event:

* **Casting** (cast-time spells): the ``CastLoop`` animation, a looping cast sound, a looping
  hand particle with a point light on ``hand_r``, a ribbon trail off the casting hand and a
  soft body glow (tint). All of it is cast-phase and torn down by CastSucceeded/CancelCast.
* **CastSucceeded**: ``CastRelease`` plus a release burst and flash light at the hand.
* **Impact** (target): ground-anchored particles at the root, a chest-height kit carrying the
  light and a timed tint pulse (``ColorTint.duration_ms``), and the impact sound.
* **AuraApplied / AuraIdle / AuraTick**: one-shot apply bursts, looping idle particles that
  live exactly as long as the aura, and per-tick pulses with their own sound and tint.

Design notes that are easy to undo by accident:

* No kit sets ``duration_ms`` on an animation -- it time-warps the clip (warrior lesson).
* Kits use ``sound_ids`` (catalog entries, see author_sounds.py), never ``sounds``.
* Tints outside the cast phase always carry ``duration_ms``: a timed pulse removes itself,
  whereas a plain tint on an Impact target is never removed by anything.
* Looping particle files may only appear in Casting and AuraIdle kits -- those are the two
  phases the service tears down itself. tools/tests/test_cleric_visual_data.py enforces it.
* Ribbon trails only in the cast phase: nothing retires a ribbon spawned by any other event.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, json_format, message_factory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

OUT = ROOT / "generated/cleric_visuals"
PARTICLE_DIR = "Particles/Cleric/"

# Event ids (proto SpellVisualEvent).
CASTING, CAST, IMPACT, AURA, TICK, IDLE = "2", "3", "4", "5", "7", "8"

# Sound catalog ids authored by author_sounds.py.
SND_CAST_LOOP = 120
SND_FIRE_CAST_LOOP = 121
SND_RES_CHANNEL = 122
SND_RELEASE = 123
SND_HEALING_LIGHT = 124
SND_SMITE = 125
SND_HOLY_FIRE = 126
SND_HOLY_FIRE_TICK = 127
SND_DIVINE_VITALITY = 128
SND_RENEWING_LIGHT = 129
SND_RENEWING_LIGHT_TICK = 130
SND_HEALING_AURA = 131
SND_PROTECTIVE_AURA = 132
SND_FAITHWARD = 133
SND_RESURRECTION = 134

# Colours (linear RGB) shared by lights, ribbons and tints.
GOLD = (1.00, 0.84, 0.48)
WHITE_GOLD = (1.00, 0.95, 0.78)
AMBER = (1.00, 0.66, 0.28)
LIFE = (0.82, 1.00, 0.62)
WARD = (0.70, 0.84, 1.00)

# New cleric visualizations are numbered from here (unless one with the same name already
# exists). The gap leaves room for the mage set authored in parallel on
# feature/mage-spell-visuals, which also allocates from the old maximum (42).
FIRST_NEW_VIS_ID = 70

# Visualization 21 "Resurrection" already exists (empty) and spell 179 points at it; it is
# reused under its id rather than orphaned.
RESURRECTION_VIS_ID = 21


def _require(condition, message):
    """Like assert, but survives ``python -O`` -- these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def particle(name):
    return PARTICLE_DIR + name + ".hpar"


def light(colour, intensity, rng, fade_in=0.15, fade_out=0.6):
    return {"r": colour[0], "g": colour[1], "b": colour[2], "intensity": intensity,
            "range": rng, "fade_in_time": fade_in, "fade_out_time": fade_out}


def tint(colour, alpha, duration_ms=None):
    t = {"r": colour[0], "g": colour[1], "b": colour[2], "a": alpha}
    if duration_ms:
        t["duration_ms"] = duration_ms
    return t


def kit(scope="CASTER", **fields):
    k = {"scope": scope, "loop": False}
    k.update(fields)
    return k


def cast_phase(hand_particle, loop_sound, colour, ribbon=True, extra=()):
    """Casting-event kits: animation + loop sound + hand glow/light + body glow + ribbon."""
    kits = [kit(animation_name="CastLoop", loop=True, sound_ids=[loop_sound],
                particles=[particle(hand_particle)], attach_bone="hand_r",
                light=light(colour, 1.4, 4.5, fade_in=0.3, fade_out=0.4),
                tint=tint(colour, 0.22))]
    if ribbon:
        kits.append(kit(attach_bone="hand_r", ribbon_trail={
            "initial_width": 0.10, "final_width": 0.0,
            "initial_r": colour[0], "initial_g": colour[1], "initial_b": colour[2],
            "initial_a": 0.55,
            "final_r": colour[0], "final_g": colour[1], "final_b": colour[2], "final_a": 0.0,
            "segment_lifetime": 0.35, "max_segments": 48}))
    kits.extend(extra)
    return {"kits": kits}


def release(colour, sound=None):
    """CastSucceeded kits: the release gesture plus a burst and flash at the casting hand."""
    burst = kit(particles=[particle("HolyRelease")], attach_bone="hand_r",
                light=light(colour, 2.2, 5.0, fade_in=0.05, fade_out=0.35))
    if sound:
        burst["sound_ids"] = [sound]
    return {"kits": [kit(animation_name="CastRelease"), burst]}


def definitions():
    """(visualization name, fixed id or None, spell ids, kits_by_event)."""
    return [
        ("Cleric - Healing Light", None, [12, 61], {
            CASTING: cast_phase("HolyHandGlow", SND_CAST_LOOP, GOLD),
            CAST: release(GOLD, SND_RELEASE),
            IMPACT: {"kits": [
                kit("TARGET", particles=[particle("HealingLightImpact")],
                    sound_ids=[SND_HEALING_LIGHT]),
                kit("TARGET", attach_bone="spine_03",
                    light=light(GOLD, 2.6, 6.0, fade_in=0.1, fade_out=0.9),
                    tint=tint(GOLD, 0.75, 900))]}}),
        ("Cleric - Smite", None, [13, 57], {
            CASTING: cast_phase("SmiteGather", SND_CAST_LOOP, WHITE_GOLD),
            CAST: release(WHITE_GOLD),
            IMPACT: {"kits": [
                kit("TARGET", particles=[particle("SmiteImpact")], sound_ids=[SND_SMITE]),
                kit("TARGET", particles=[particle("SmiteFlash")], attach_bone="spine_03",
                    light=light(WHITE_GOLD, 4.0, 8.0, fade_in=0.03, fade_out=0.5),
                    tint=tint(WHITE_GOLD, 0.9, 400))]}}),
        ("Cleric - Holy Fire", None, [58], {
            CASTING: cast_phase("HolyFireGather", SND_FIRE_CAST_LOOP, AMBER, extra=[
                kit(particles=[particle("HolyFireGather")], attach_bone="hand_l")]),
            CAST: release(AMBER),
            IMPACT: {"kits": [
                kit("TARGET", particles=[particle("HolyFireImpact")],
                    sound_ids=[SND_HOLY_FIRE]),
                kit("TARGET", attach_bone="spine_03",
                    light=light(AMBER, 3.5, 8.0, fade_in=0.05, fade_out=1.0),
                    tint=tint(AMBER, 0.8, 700))]},
            IDLE: {"kits": [
                kit("TARGET", particles=[particle("HolyFireBurn")],
                    light=light(AMBER, 0.8, 3.5, fade_in=0.4, fade_out=0.6))]},
            TICK: {"kits": [
                kit("TARGET", particles=[particle("HolyFireTick")], attach_bone="spine_03",
                    sound_ids=[SND_HOLY_FIRE_TICK], tint=tint(AMBER, 0.6, 350))]}}),
        ("Cleric - Divine Vitality", None, [20], {
            CAST: release(GOLD),
            IMPACT: {"kits": [
                kit("TARGET", particles=[particle("DivineVitalityImpact")],
                    sound_ids=[SND_DIVINE_VITALITY]),
                kit("TARGET", attach_bone="spine_03",
                    light=light(GOLD, 2.4, 6.0, fade_in=0.2, fade_out=1.2),
                    tint=tint(GOLD, 0.7, 1200))]}}),
        ("Cleric - Renewing Light", None, [170], {
            CAST: release(LIFE),
            IMPACT: {"kits": [
                kit("TARGET", particles=[particle("RenewingLightImpact")],
                    sound_ids=[SND_RENEWING_LIGHT]),
                kit("TARGET", attach_bone="spine_03",
                    light=light(LIFE, 1.8, 5.0, fade_in=0.15, fade_out=0.9),
                    tint=tint(LIFE, 0.5, 800))]},
            IDLE: {"kits": [kit("TARGET", particles=[particle("RenewingLightIdle")])]},
            TICK: {"kits": [
                kit("TARGET", particles=[particle("RenewingLightTick")],
                    sound_ids=[SND_RENEWING_LIGHT_TICK], tint=tint(LIFE, 0.45, 500))]}}),
        ("Cleric - Healing Aura", None, [55], {
            CAST: {"kits": [
                kit(animation_name="CastRelease"),
                kit(particles=[particle("HealingAuraActivate")], sound_ids=[SND_HEALING_AURA]),
                kit(attach_bone="spine_03", light=light(GOLD, 2.5, 10.0, fade_in=0.2, fade_out=1.2),
                    tint=tint(GOLD, 0.5, 900))]},
            AURA: {"kits": [kit("TARGET", particles=[particle("HealingAuraApply")])]},
            IDLE: {"kits": [kit("TARGET", particles=[particle("HealingAuraIdle")])]}}),
        ("Cleric - Protective Aura", None, [65], {
            CAST: {"kits": [
                kit(animation_name="CastRelease"),
                kit(particles=[particle("ProtectiveAuraActivate")],
                    sound_ids=[SND_PROTECTIVE_AURA]),
                kit(attach_bone="spine_03", light=light(WARD, 2.5, 10.0, fade_in=0.2, fade_out=1.2),
                    tint=tint(WARD, 0.5, 900))]},
            AURA: {"kits": [kit("TARGET", particles=[particle("ProtectiveAuraApply")])]},
            IDLE: {"kits": [kit("TARGET", particles=[particle("ProtectiveAuraIdle")])]}}),
        ("Cleric - Faithward", None, [99, 100, 101], {
            AURA: {"kits": [
                kit("TARGET", particles=[particle("FaithwardApply")], attach_bone="spine_03",
                    sound_ids=[SND_FAITHWARD],
                    light=light(WARD, 2.0, 5.0, fade_in=0.05, fade_out=0.6),
                    tint=tint(WARD, 0.6, 700))]},
            IDLE: {"kits": [kit("TARGET", particles=[particle("FaithwardIdle")],
                                attach_bone="spine_03")]}}),
        ("Cleric - Resurrection", RESURRECTION_VIS_ID, [179], {
            CASTING: cast_phase("HolyHandGlow", SND_RES_CHANNEL, GOLD, extra=[
                kit(particles=[particle("ResurrectionChannel")])]),
            CAST: release(GOLD),
            IMPACT: {"kits": [
                kit("TARGET", particles=[particle("ResurrectionImpact")],
                    sound_ids=[SND_RESURRECTION]),
                kit("TARGET", attach_bone="spine_03",
                    light=light(WHITE_GOLD, 4.0, 10.0, fade_in=0.25, fade_out=1.6),
                    tint=tint(WHITE_GOLD, 0.8, 2000))]}}),
    ]


def load_type(schema_dir, protos, message_name):
    with tempfile.TemporaryDirectory(prefix="cleric_vis_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for f in file_set.file:
        pool.Add(f)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


def looping_particles():
    """The allow-list of particle files that may loop (cleric_common.LOOPING_EFFECTS)."""
    sys.path.insert(0, str(ROOT / "tools/particle_gen/recipes"))
    import cleric_common
    return {PARTICLE_DIR + name for name in cleric_common.LOOPING_EFFECTS}


def validate(dataset, sound_ids, allowed_looping):
    """Everything that must hold before any dataset is written."""
    for vis in dataset.entry:
        if not vis.name.startswith("Cleric - "):
            continue
        for event, kit_list in vis.kits_by_event.items():
            for k in kit_list.kits:
                _require(not k.HasField("duration_ms"),
                         f"{vis.name}: duration_ms time-warps the clip; leave it unset")
                _require(not k.sounds, f"{vis.name}: cleric kits use sound_ids, never sounds")
                for sound_id in k.sound_ids:
                    _require(sound_id in sound_ids, f"{vis.name}: unknown sound id {sound_id}")
                for path in k.particles:
                    _require((ROOT / "data/client" / path).is_file(),
                             f"{vis.name}: missing particle {path}")
                    if path in allowed_looping:
                        _require(event in (2, 8),
                                 f"{vis.name}: looping {path} outside Casting/AuraIdle leaks")
                if k.loop:
                    _require(event == 2, f"{vis.name}: only the cast loop kit may loop")
                if k.HasField("ribbon_trail"):
                    _require(event == 2, f"{vis.name}: ribbon trails only in the cast phase")
                if k.HasField("tint") and event != 2:
                    _require(k.tint.duration_ms > 0,
                             f"{vis.name}: a tint outside the cast phase needs duration_ms")
                if event in (2, 3):
                    _require(k.scope == 0, f"{vis.name}: cast events have no target list")
    ids = [v.id for v in dataset.entry]
    _require(len(ids) == len(set(ids)), "duplicate visualization ids")
    _require(dataset.IsInitialized(), "visualizations dataset is missing required fields")


def upsert_visuals(dataset, drafts):
    by_id = {v.id: v for v in dataset.entry}
    for draft in drafts:
        target = by_id.get(draft["id"])
        if target is None:
            target = dataset.entry.add()
        target.Clear()
        json_format.ParseDict(draft, target)


def relink_spells(dataset, mapping):
    by_id = {s.id: s for s in dataset.entry}
    for spell_id, vis_id in mapping.items():
        _require(spell_id in by_id, f"spell {spell_id} missing from a spells dataset")
        by_id[spell_id].visualization_id = vis_id


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    editor_dir = ROOT / "src/shared/proto_data"
    client_dir = ROOT / "src/shared/client_data"
    paths = {
        "editor_vis": ROOT / "data/editor/data/spell_visualizations.data",
        "client_vis": ROOT / "data/client/ClientDB/spell_visualizations.data",
        "editor_spells": ROOT / "data/editor/data/spells.data",
        "client_spells": ROOT / "data/client/ClientDB/spells.data",
    }
    types = {
        "editor_vis": load_type(editor_dir, ["spell_visualizations.proto"], "mmo.proto.SpellVisualizations"),
        "client_vis": load_type(client_dir, ["spells.proto", "spell_visualizations.proto"],
                                "mmo.proto_client.SpellVisualizations"),
        "editor_spells": load_type(editor_dir, ["spells.proto"], "mmo.proto.Spells"),
        "client_spells": load_type(client_dir, ["spells.proto"], "mmo.proto_client.Spells"),
    }
    data = {key: types[key].FromString(paths[key].read_bytes()) for key in paths}

    sounds_type = load_type(editor_dir, ["sounds.proto"], "mmo.proto.Sounds")
    sound_ids = {e.id for e in
                 sounds_type.FromString((ROOT / "data/editor/data/sounds.data").read_bytes()).entry}

    existing = {v.name: v.id for v in data["editor_vis"].entry}
    next_id = max(max(v.id for v in data["editor_vis"].entry) + 1, FIRST_NEW_VIS_ID)
    spells_by_id = {s.id: s for s in data["editor_spells"].entry}

    drafts, mapping = [], {}
    for name, fixed_id, spell_ids, events in definitions():
        vis_id = fixed_id or existing.get(name)
        if vis_id is None:
            vis_id = next_id
            next_id += 1
        drafts.append({"id": vis_id, "name": name, "kits_by_event": events})
        suffix = name.split(" - ", 1)[1]
        for spell_id in spell_ids:
            _require(spells_by_id[spell_id].name == suffix,
                     f"spell {spell_id} is named {spells_by_id[spell_id].name!r}, expected {suffix!r}")
            mapping[spell_id] = vis_id

    allowed_looping = looping_particles()
    for key in ("editor_vis", "client_vis"):
        upsert_visuals(data[key], drafts)
        validate(data[key], sound_ids, allowed_looping)
    for key in ("editor_spells", "client_spells"):
        relink_spells(data[key], mapping)

    (OUT / "visualizations.json").write_text(json.dumps(drafts, indent=2) + "\n")

    if not args.apply:
        print(f"validated {len(drafts)} visualizations across {len(mapping)} spells: "
              + ", ".join(f"{s}->{v}" for s, v in sorted(mapping.items())))
        return

    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for key, path in paths.items():
        shutil.copy2(path, backup / f"{key}_{path.name}")
    for key, path in paths.items():
        path.write_bytes(data[key].SerializeToString())

    print(f"wrote {len(drafts)} visualizations and relinked {len(mapping)} spells in both "
          f"datasets. Backup: {backup}")


if __name__ == "__main__":
    main()
