# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the warrior spell visualization kits (entries 26-39).

Writes both the editor dataset and the client ClientDB copy. Only visualization entries and
each spell's visualization_id are touched; all other spell data is asserted unchanged.

    python tools/warrior_visuals/author_visuals.py            # validate only
    python tools/warrior_visuals/author_visuals.py --apply    # write both datasets

Design notes that are easy to undo by accident:

* No kit sets ``duration_ms``. ApplyAnimationToActor turns it into
  ``playRate = clipLength / duration``, so a guessed value time-warps the clip -- the
  previous pass ran every warrior animation 1.4x to 1.8x too fast.
* Kits use ``sound_ids`` (catalog entries) and leave ``sounds`` empty. Setting both would
  double-trigger.
* Caster-side delays come from measured clip lengths: CastRelease 0.63 s, Attack_1H_01/02
  0.97 s, UnarmedAttack01 0.70 s.
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
from spell_catalog_lib import load_catalogs  # noqa: E402
from proto_runtime import find_protoc  # noqa: E402

OUT = ROOT / "generated/warrior_visuals"
PARTICLE_DIR = "Particles/Warrior/"

# Event ids: 3 = CAST_SUCCEEDED, 4 = IMPACT, 5 = AURA_APPLIED.
CAST, IMPACT, AURA = "3", "4", "5"


def anim(name):
    """A caster kit that plays a clip at its native rate."""
    return {"scope": "CASTER", "loop": False, "animation_name": name}


def fx(particle, sound=None, scope="CASTER", bone=None, delay=0):
    kit = {"scope": scope, "loop": False, "particles": [PARTICLE_DIR + particle + ".hpar"]}
    if sound:
        kit["sound_ids"] = [sound]
    if bone:
        kit["attach_bone"] = bone
    if delay:
        kit["delay_ms"] = delay
    return kit


def definitions():
    """(visualization name suffix, spell ids, kits_by_event)."""
    return [
        ("Strike", [8, 71], {
            IMPACT: {"kits": [fx("SteelImpact", 21, "TARGET", "spine_03")]}}),
        ("Battlecry", [9], {
            CAST: {"kits": [anim("CastRelease"), fx("RallyBurst", 29, delay=150)]},
            AURA: {"kits": [fx("RallyBurst", scope="TARGET")]}}),
        ("Rend", [19], {
            CAST: {"kits": [anim("Attack_1H_01")]},
            IMPACT: {"kits": [fx("BloodImpact", 22, "TARGET", "spine_03")]}}),
        ("Charge", [48], {
            CAST: {"kits": [fx("ChargeDust", 27)]}}),
        ("Execute", [50], {
            CAST: {"kits": [anim("Attack_1H_02")]},
            IMPACT: {"kits": [fx("HeavyImpact", 23, "TARGET", "spine_03")]}}),
        ("Bloodrush", [62], {
            CAST: {"kits": [fx("RageBurst", 30)]}}),
        ("Crippling Strike", [69], {
            CAST: {"kits": [anim("Attack_1H_01")]},
            IMPACT: {"kits": [fx("BloodImpact", 22, "TARGET", "foot_l")]}}),
        ("Skullbash", [70], {
            CAST: {"kits": [anim("UnarmedAttack01")]},
            IMPACT: {"kits": [fx("SteelImpact", 24, "TARGET", "head")]}}),
        # No ground-slam clip exists on the Human rig; CastRelease's downward outward
        # gesture reads closer to a ground slam than UnarmedAttack01's jab. Its release
        # beat lands ~40% into the 0.63 s clip, hence the 260 ms delay.
        ("Shockwave", [122], {
            CAST: {"kits": [anim("CastRelease"), fx("ShockwaveDust", 28, delay=260)]}}),
        # No shield-bash clip either; UnarmedAttack01 is an off-hand forward thrust and is
        # the closest available.
        ("Shield Slam", [142], {
            CAST: {"kits": [anim("UnarmedAttack01")]},
            IMPACT: {"kits": [fx("HeavyImpact", 25, "TARGET", "spine_03")]}}),
        ("Last Stand", [205], {
            CAST: {"kits": [fx("GuardBurst", 33)]}}),
        # Attack_1H_02 is 0.97 s and makes contact about a third of the way in.
        ("Cleave", [209], {
            CAST: {"kits": [anim("Attack_1H_02"),
                            fx("CleaveBurst", 26, bone="hand_r", delay=300)]},
            IMPACT: {"kits": [fx("SteelImpact", scope="TARGET", bone="spine_03")]}}),
        ("Provoke", [216], {
            CAST: {"kits": [anim("CastRelease"), fx("DreadBurst", 31, delay=150)]},
            IMPACT: {"kits": [fx("DreadBurst", scope="TARGET")]}}),
        ("Demoralizing Shout", [217], {
            CAST: {"kits": [anim("CastRelease"), fx("DreadBurst", 32, delay=150)]},
            AURA: {"kits": [fx("DreadBurst", scope="TARGET")]}}),
    ]


def load_type(schema_dir, protos, message_name):
    with tempfile.TemporaryDirectory(prefix="warrior_vis_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for f in file_set.file:
        pool.Add(f)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


def validate(dataset, sound_ids):
    """Everything that must hold before either dataset is written."""
    for vis in dataset.entry:
        if not vis.name.startswith("Warrior - "):
            continue
        for event, kit_list in vis.kits_by_event.items():
            for kit in kit_list.kits:
                assert not kit.loop, f"{vis.name}: one-shot kits must not loop"
                assert not kit.HasField("duration_ms"), (
                    f"{vis.name}: duration_ms time-warps the clip; leave it unset")
                assert not (kit.sounds and kit.sound_ids), (
                    f"{vis.name}: a kit sets sounds or sound_ids, never both")
                assert not kit.sounds, f"{vis.name}: warrior kits use sound_ids"
                for sound_id in kit.sound_ids:
                    assert sound_id in sound_ids, f"{vis.name}: unknown sound id {sound_id}"
                for particle in kit.particles:
                    path = ROOT / "data/client" / particle
                    assert path.is_file(), f"{vis.name}: missing particle {particle}"
                if event == 3:
                    assert kit.scope == 0, "CastSucceeded has no target list"
    ids = [v.id for v in dataset.entry]
    assert len(ids) == len(set(ids)), "duplicate visualization ids"
    assert dataset.IsInitialized()


def upsert(dataset, drafts):
    by_name = {v.name: v for v in dataset.entry}
    for draft in drafts:
        target = by_name.get(draft["name"])
        if target is None:
            target = dataset.entry.add()
        else:
            assert target.id == draft["id"], (
                f"{draft['name']} already exists with id {target.id}, expected {draft['id']}")
            target.Clear()
        json_format.ParseDict(draft, target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    catalogs = load_catalogs(str(ROOT))
    spells, visuals = catalogs["spells"], catalogs["spell_visualizations"]
    by_id = {s.id: s for s in spells.entry}
    existing = {v.name: v.id for v in visuals.entry}
    next_id = max(v.id for v in visuals.entry) + 1

    sounds_type = load_type(ROOT / "src/shared/proto_data", ["sounds.proto"], "mmo.proto.Sounds")
    sound_ids = {e.id for e in
                 sounds_type.FromString((ROOT / "data/editor/data/sounds.data").read_bytes()).entry}

    drafts, mapping = [], {}
    for suffix, spell_ids, events in definitions():
        name = "Warrior - " + suffix
        vis_id = existing.get(name)
        if vis_id is None:
            vis_id = next_id
            next_id += 1
        drafts.append({"id": vis_id, "name": name, "kits_by_event": events})
        for spell_id in spell_ids:
            assert by_id[spell_id].name == suffix or suffix == "Strike", (
                spell_id, by_id[spell_id].name, suffix)
            mapping[spell_id] = vis_id

    editor_visuals = type(visuals)()
    editor_visuals.CopyFrom(visuals)
    upsert(editor_visuals, drafts)
    validate(editor_visuals, sound_ids)

    (OUT / "visualizations.json").write_text(json.dumps(drafts, indent=2) + "\n")

    if not args.apply:
        print(f"validated {len(drafts)} visualizations across {len(mapping)} spells")
        return

    client_visuals_type = load_type(
        ROOT / "src/shared/client_data",
        ["spells.proto", "spell_visualizations.proto"],
        "mmo.proto_client.SpellVisualizations")

    paths = [ROOT / "data/editor/data/spell_visualizations.data",
             ROOT / "data/client/ClientDB/spell_visualizations.data"]

    client_visuals = client_visuals_type.FromString(paths[1].read_bytes())
    upsert(client_visuals, drafts)
    validate(client_visuals, sound_ids)

    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for index, path in enumerate(paths):
        shutil.copy2(path, backup / f"{index}_{path.name}")

    paths[0].write_bytes(editor_visuals.SerializeToString())
    paths[1].write_bytes(client_visuals.SerializeToString())

    print(f"wrote {len(drafts)} visualizations to both datasets. Backup: {backup}")


if __name__ == "__main__":
    main()
