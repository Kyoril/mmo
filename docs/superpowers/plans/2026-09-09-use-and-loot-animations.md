# Use Animations on Open Spells, and a Held Loot Pose — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the `OpenLock` spells play the rig's `UseStart`/`UseLoop`/`UseEnd` clips while casting, and make a looting character kneel in the `Loot` pose — held on its last frame — until the loot window closes or the character moves.

**Architecture:** Three independent slices. (A) is pure game data: the spell visualization kit system already supports one-shot → locked-loop → one-shot, so the Open spells only need correct kits and `visualization_id` links. (B) adds one logical animation slot (`ANIM_SLOT_LOOT`) and one `AnimationContext` flag, evaluated by the existing layered `AnimationController`; the held last frame falls out of `AnimationState`'s clamp behaviour for non-looping clips. (C) closes the loot dialog server-side on position-changing movement, which clears the already-replicated `unit_flags::Looting`.

**Tech Stack:** C++17, protobuf 2 (`proto_data` + mirrored `client_data` schemas), Python 3 with `google.protobuf` for data authoring, Catch2 + CTest for unit tests, the Lua E2E harness under `e2e/`, CMake + Visual Studio on Windows.

**Design spec:** [docs/superpowers/specs/2026-09-09-use-and-loot-animations-design.md](../specs/2026-09-09-use-and-loot-animations-design.md)

## Global Constraints

Every task's requirements implicitly include this section.

- Branch: `feature/use-and-loot-animations`. Never push to origin. Never commit to `develop`.
- Code style: Allman braces (every `{` and `}` on its own line), tabs for indentation, braces on every `if` even for single-line bodies, `m_camelCase` members, `PascalCase` methods, `camelCase` locals, `snake_case.cpp/.h` filenames.
- Every new or modified source file keeps the header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` as its first line.
- No exceptions (`SIMPLE_NO_EXCEPTIONS`). Use `ASSERT`, `VERIFY`, `UNREACHABLE`, and `DLOG`/`WLOG`/`ELOG` from `src/shared/base/macros.h`.
- **No `ProtocolVersion` bump in this plan.** No opcode is added, removed, renumbered or reordered; no packet payload changes; framing and the cipher are untouched. `tools/protocol_version_check.py` must stay green with no `--update` run.
- `animation_profiles.proto` and `spell_visualizations.proto` exist twice — `src/shared/proto_data/` (namespace `mmo.proto`) and `src/shared/client_data/` (namespace `mmo.proto_client`). **Every field number and every enum value must match between the two copies.** Edit both or neither.
- Game data lives in two trees that must agree: `data/editor/data/*.data` (editor/server) and `data/client/ClientDB/*.data` (client). Both are git submodules that already point ahead of what the superproject records; commit inside the submodule, and **do not** bump the superproject's submodule pointer without asking the user.
- No spell visualization kit may set `duration_ms`. `SpellVisualizationService::ApplyAnimationToActor` turns it into `playRate = clipLength / duration`, which time-warps the clip.

---

## File Structure

**Created:**

| Path | Responsibility |
|---|---|
| `tools/open_spell_visuals/author_open_visuals.py` | Authors visualizations 13, 14, 41 and the `visualization_id` links into both data trees. Validate-only by default. |
| `tools/tests/test_open_spell_visual_data.py` | Guards the Part A data invariants (links resolve, clips exist on both human rigs, no `duration_ms`, editor and ClientDB agree). |
| `e2e/scenarios/loot_animation_flag.lua` | E2E coverage for the `Looting` flag lifecycle, including the movement cancel. |

**Modified:**

| Path | Change |
|---|---|
| `src/shared/proto_data/animation_profiles.proto` | Add `ANIM_SLOT_LOOT = 17`. |
| `src/shared/client_data/animation_profiles.proto` | Mirror `ANIM_SLOT_LOOT = 17`. |
| `src/shared/game_client/animation/animation_binding.h` | `SlotCount` now derives from `ANIM_SLOT_LOOT`. |
| `src/shared/game_client/animation/animation_binding.cpp` | Bind the new slot to `"Loot"` in `builtinBase`. |
| `src/shared/game_client/animation/animation_context.h` | Add `bool looting`. |
| `src/shared/game_client/animation/animation_controller.h` | Add `bool m_lootPoseActive`. |
| `src/shared/game_client/animation/animation_controller.cpp` | Loot branch in `EvaluateLocomotion`. |
| `src/shared/game_client/game_unit_c.cpp` | Fill `ctx.looting` in `UpdateAnimation`. |
| `src/shared/game_client/spell_visualization_service.cpp` | Missing-clip `WLOG` → `DLOG`. |
| `src/world_server/player.cpp` | Close the loot dialog on position-changing movement. |
| `src/shared/bot_core/bot_realm_connector.h/.cpp` | `Loot(guid)` / `LootRelease(guid)` senders. |
| `src/e2e_client/scenario_engine.cpp` | Lua verbs `GetUnitFlags`, `LootUnit`, `ReleaseLoot`. |
| `e2e/README.md` | Document the three new verbs. |
| `data/editor/data/{spell_visualizations,spells}.data` | Part A data (submodule). |
| `data/client/ClientDB/{spell_visualizations,spells}.data` | Part A data (submodule). |

---

## Task 1: Open-spell Use animations (data)

Pure game data plus one log-level change. Delivers Part A of the spec.

**Files:**
- Create: `tools/open_spell_visuals/author_open_visuals.py`
- Create: `tools/tests/test_open_spell_visual_data.py`
- Modify: `src/shared/game_client/spell_visualization_service.cpp` (the `WLOG` at ~line 432)
- Data: `data/editor/data/spell_visualizations.data`, `data/editor/data/spells.data`, `data/client/ClientDB/spell_visualizations.data`, `data/client/ClientDB/spells.data`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: visualization ids `13` (Mining), `14` (Open), `41` (Open Door) present in both data trees; spells `39`, `112` link `visualization_id = 14`, spell `157` links `13`, spell `235` links `41`. No later task depends on these.

**Reference facts** (measured, do not re-derive):

| Clip | HumanMale | HumanFemale_V2 |
|---|---|---|
| `UseStart` | 0.567 s | 0.500 s |
| `UseLoop` | 0.700 s | 0.700 s |
| `UseEnd` | 1.367 s | 1.333 s |

Event key numbers in `kits_by_event`: `2` = `CASTING`, `3` = `CAST_SUCCEEDED`.

- [ ] **Step 1: Write the failing data test**

Create `tools/tests/test_open_spell_visual_data.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the Open-spell visualization data invariants.

The Open spells previously pointed at visualizations naming clips ("Open", "Mining") that
exist on no rig, and three of the four did not link a visualization at all. These tests
fail if that state returns. Skips rather than fails when protoc or the data submodules are
unavailable, so a partial checkout does not break the gate.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# spell id -> visualization id. Spells carrying an OpenLock (type 30) effect.
EXPECTED_LINKS = {39: 14, 112: 14, 157: 13, 235: 41}

# Rigs every player character currently uses. A clip named by an Open kit must exist on
# both, or half the player base sees nothing.
HUMAN_SKELETONS = [
    "data/client/Models/Character/Human/Male/HumanMale.skel",
    "data/client/Models/Character/Human/Female/HumanFemale_V2.skel",
]

OPEN_VISUALIZATION_IDS = {13, 14, 41}


def _clip_names(skeleton_path):
    """Animation clip names from a .skel: each MINA chunk holds a 4-byte size, then a
    uint8-prefixed name, then a float duration."""
    data = skeleton_path.read_bytes()
    names = set()
    index = 0
    while True:
        index = data.find(b"MINA", index)
        if index < 0:
            return names
        cursor = index + 8
        if cursor < len(data):
            length = data[cursor]
            if 0 < length < 40:
                raw = data[cursor + 1:cursor + 1 + length]
                if all(32 <= byte < 127 for byte in raw):
                    names.add(raw.decode())
        index += 4


def _load(schema_dir, protos, message_name, data_path):
    from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
    sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
    from proto_runtime import find_protoc

    with tempfile.TemporaryDirectory() as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())

    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)

    message = message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))
    return message.FromString(data_path.read_bytes())


class OpenSpellVisualDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / "data/editor/data/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/editor submodule is not checked out")
        try:
            editor_schema = ROOT / "src/shared/proto_data"
            client_schema = ROOT / "src/shared/client_data"
            cls.visuals = _load(editor_schema, ["spell_visualizations.proto"],
                                "mmo.proto.SpellVisualizations",
                                ROOT / "data/editor/data/spell_visualizations.data")
            cls.spells = _load(editor_schema, ["spells.proto"], "mmo.proto.Spells",
                               ROOT / "data/editor/data/spells.data")
            cls.client_visuals = _load(client_schema, ["spell_visualizations.proto"],
                                       "mmo.proto_client.SpellVisualizations",
                                       ROOT / "data/client/ClientDB/spell_visualizations.data")
            cls.client_spells = _load(client_schema, ["spells.proto"], "mmo.proto_client.Spells",
                                      ROOT / "data/client/ClientDB/spells.data")
        except (ImportError, FileNotFoundError, subprocess.CalledProcessError) as exc:
            # Missing protoc, missing protobuf, or an uninitialised submodule -- environment
            # problems the gate should skip past. A corrupt .data file must NOT land here:
            # this suite exists to catch exactly that, so it has to fail, not skip.
            raise unittest.SkipTest(f"cannot load proto data: {exc}")

        cls.kits = [(vis.id, kit)
                    for vis in cls.visuals.entry if vis.id in OPEN_VISUALIZATION_IDS
                    for kit_list in vis.kits_by_event.values() for kit in kit_list.kits]

    def test_every_open_spell_links_its_visualization(self):
        by_id = {spell.id: spell for spell in self.spells.entry}
        for spell_id, vis_id in EXPECTED_LINKS.items():
            spell = by_id.get(spell_id)
            self.assertIsNotNone(spell, f"spell {spell_id} is missing from spells.data")
            self.assertTrue(spell.HasField("visualization_id"),
                            f"spell {spell_id} ({spell.name}) has no visualization_id")
            self.assertEqual(spell.visualization_id, vis_id,
                             f"spell {spell_id} ({spell.name}) points at "
                             f"{spell.visualization_id}, expected {vis_id}")

    def test_every_open_visualization_exists(self):
        ids = {vis.id for vis in self.visuals.entry}
        for vis_id in OPEN_VISUALIZATION_IDS:
            self.assertIn(vis_id, ids, f"visualization {vis_id} is missing")

    def test_every_kit_names_a_clip_present_on_both_human_rigs(self):
        available = None
        for relative in HUMAN_SKELETONS:
            path = ROOT / relative
            if not path.is_file():
                self.skipTest(f"missing skeleton {relative}")
            names = _clip_names(path)
            available = names if available is None else (available & names)

        self.assertTrue(self.kits, "no Open-spell kits found")
        for vis_id, kit in self.kits:
            self.assertTrue(kit.animation_name,
                            f"visualization {vis_id}: kit has no animation_name")
            self.assertIn(kit.animation_name, available,
                          f"visualization {vis_id}: clip {kit.animation_name!r} is not on "
                          f"both human rigs")

    def test_no_kit_time_warps_its_animation(self):
        # duration_ms becomes playRate = clipLength / duration and speeds the clip up.
        for vis_id, kit in self.kits:
            self.assertFalse(kit.HasField("duration_ms"),
                             f"visualization {vis_id}: duration_ms time-warps the clip")

    def test_editor_and_client_datasets_agree(self):
        editor = {vis.id: vis.SerializeToString()
                  for vis in self.visuals.entry if vis.id in OPEN_VISUALIZATION_IDS}
        client = {vis.id: vis.SerializeToString()
                  for vis in self.client_visuals.entry if vis.id in OPEN_VISUALIZATION_IDS}
        self.assertEqual(editor.keys(), client.keys(),
                         "editor and ClientDB hold different Open visualization ids")
        for vis_id in editor:
            self.assertEqual(editor[vis_id], client[vis_id],
                             f"visualization {vis_id} differs between editor and ClientDB")

        editor_links = {spell.id: spell.visualization_id
                        for spell in self.spells.entry if spell.id in EXPECTED_LINKS}
        client_links = {spell.id: spell.visualization_id
                        for spell in self.client_spells.entry if spell.id in EXPECTED_LINKS}
        self.assertEqual(editor_links, client_links,
                         "spell visualization_id links differ between editor and ClientDB")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
python -m unittest tools.tests.test_open_spell_visual_data -v
```

Expected: FAIL. `test_every_open_spell_links_its_visualization` fails with "spell 39 (Open) has no visualization_id"; `test_every_open_visualization_exists` fails on 41; `test_every_kit_names_a_clip_present_on_both_human_rigs` fails on `'Open'` / `'Mining'`.

- [ ] **Step 3: Write the authoring script**

Create `tools/open_spell_visuals/author_open_visuals.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the Open-spell visualization kits (ids 13, 14, 41) and their spell links.

Writes both the editor dataset and the client ClientDB copy, for spell_visualizations.data
AND spells.data. Idempotent: re-running replaces the same ids rather than appending.

    python tools/open_spell_visuals/author_open_visuals.py            # validate only
    python tools/open_spell_visuals/author_open_visuals.py --apply    # write both trees

Design notes that are easy to undo by accident:

* No kit sets duration_ms. ApplyAnimationToActor computes
  playRate = clipLength / duration_ms, so any value time-warps the clip.
* 520 ms is where UseStart ends (0.567 s male, 0.500 s female). One number has to serve
  both rigs, so the loop kit and the door's UseEnd kit both wait 520 ms.
* Spell 235 "Open Door" is instant. WorldState::OnSpellStart gates START_CAST and CASTING
  on castTime > 0, so an instant spell only ever sees CAST_SUCCEEDED -- hence a separate
  visualization (41) that carries the whole sequence on that one event.
* Visualization 13 rides the Use clips as a placeholder until pickaxe animations exist.
  It keeps its id so the swap stays data-only.
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

OUT = ROOT / "generated/open_spell_visuals"

# Event ids: 2 = CASTING, 3 = CAST_SUCCEEDED.
CASTING, CAST_SUCCEEDED = "2", "3"

# Where UseStart ends. 0.567 s on HumanMale, 0.500 s on HumanFemale_V2.
USE_START_MS = 520

HUMAN_SKELETONS = [
    ROOT / "data/client/Models/Character/Human/Male/HumanMale.skel",
    ROOT / "data/client/Models/Character/Human/Female/HumanFemale_V2.skel",
]


def _require(condition, message):
    """Like assert, but survives ``python -O`` -- these guards gate writing game data, so
    they must not disappear when assertions are stripped."""
    if not condition:
        raise SystemExit(message)


def one_shot(clip, delay=0):
    kit = {"scope": "CASTER", "loop": False, "animation_name": clip}
    if delay:
        kit["delay_ms"] = delay
    return kit


def looped(clip, delay=0):
    kit = {"scope": "CASTER", "loop": True, "animation_name": clip}
    if delay:
        kit["delay_ms"] = delay
    return kit


def definitions():
    """(visualization id, name, spell ids that link it, kits_by_event)."""
    channelled = {
        CASTING: {"kits": [one_shot("UseStart"), looped("UseLoop", USE_START_MS)]},
        CAST_SUCCEEDED: {"kits": [one_shot("UseEnd")]},
    }
    return [
        (13, "Mining", [157], channelled),
        (14, "Open", [39, 112], channelled),
        # Instant: only CAST_SUCCEEDED ever fires, so it carries the whole sequence.
        (41, "Open Door", [235], {
            CAST_SUCCEEDED: {"kits": [one_shot("UseStart"),
                                      one_shot("UseEnd", USE_START_MS)]}}),
    ]


def clip_names(skeleton_path):
    """Animation clip names from a .skel: each MINA chunk holds a 4-byte size, then a
    uint8-prefixed name, then a float duration."""
    data = skeleton_path.read_bytes()
    names = set()
    index = 0
    while True:
        index = data.find(b"MINA", index)
        if index < 0:
            return names
        cursor = index + 8
        if cursor < len(data):
            length = data[cursor]
            if 0 < length < 40:
                raw = data[cursor + 1:cursor + 1 + length]
                if all(32 <= byte < 127 for byte in raw):
                    names.add(raw.decode())
        index += 4


def load_type(schema_dir, protos, message_name):
    with tempfile.TemporaryDirectory(prefix="open_vis_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


def upsert_visuals(dataset, drafts):
    by_id = {vis.id: vis for vis in dataset.entry}
    for draft in drafts:
        target = by_id.get(draft["id"])
        if target is None:
            target = dataset.entry.add()
        else:
            _require(target.name in (draft["name"], "Open", "Mining"),
                     f"visualization {draft['id']} is named {target.name!r}, refusing to "
                     f"overwrite it with {draft['name']!r}")
            target.Clear()
        json_format.ParseDict(draft, target)


def link_spells(dataset, mapping):
    by_id = {spell.id: spell for spell in dataset.entry}
    for spell_id, vis_id in mapping.items():
        spell = by_id.get(spell_id)
        _require(spell is not None, f"spell {spell_id} is missing from the dataset")
        spell.visualization_id = vis_id


def validate(visuals, spells, mapping, available_clips):
    ids = [vis.id for vis in visuals.entry]
    _require(len(ids) == len(set(ids)), "duplicate visualization ids")

    authored = {draft_id for draft_id, _, _, _ in definitions()}
    for vis in visuals.entry:
        if vis.id not in authored:
            continue
        for kit_list in vis.kits_by_event.values():
            for kit in kit_list.kits:
                _require(kit.animation_name,
                         f"visualization {vis.id}: kit has no animation_name")
                _require(kit.animation_name in available_clips,
                         f"visualization {vis.id}: clip {kit.animation_name!r} is not on "
                         f"both human rigs")
                _require(not kit.HasField("duration_ms"),
                         f"visualization {vis.id}: duration_ms time-warps the clip")
                _require(not kit.sounds and not kit.sound_ids,
                         f"visualization {vis.id}: Open kits carry no audio")

    by_id = {spell.id: spell for spell in spells.entry}
    for spell_id, vis_id in mapping.items():
        _require(by_id[spell_id].visualization_id == vis_id,
                 f"spell {spell_id} points at {by_id[spell_id].visualization_id}, "
                 f"expected {vis_id}")

    _require(visuals.IsInitialized(), "visualizations dataset is missing required fields")
    _require(spells.IsInitialized(), "spells dataset is missing required fields")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    available = None
    for skeleton in HUMAN_SKELETONS:
        _require(skeleton.is_file(), f"missing skeleton {skeleton}")
        names = clip_names(skeleton)
        available = names if available is None else (available & names)

    drafts, mapping = [], {}
    for vis_id, name, spell_ids, events in definitions():
        drafts.append({"id": vis_id, "name": name, "kits_by_event": events})
        for spell_id in spell_ids:
            mapping[spell_id] = vis_id

    editor_visuals_type = load_type(ROOT / "src/shared/proto_data",
                                    ["spell_visualizations.proto"],
                                    "mmo.proto.SpellVisualizations")
    editor_spells_type = load_type(ROOT / "src/shared/proto_data", ["spells.proto"],
                                   "mmo.proto.Spells")
    client_visuals_type = load_type(ROOT / "src/shared/client_data",
                                    ["spell_visualizations.proto"],
                                    "mmo.proto_client.SpellVisualizations")
    client_spells_type = load_type(ROOT / "src/shared/client_data", ["spells.proto"],
                                   "mmo.proto_client.Spells")

    paths = [
        ROOT / "data/editor/data/spell_visualizations.data",
        ROOT / "data/editor/data/spells.data",
        ROOT / "data/client/ClientDB/spell_visualizations.data",
        ROOT / "data/client/ClientDB/spells.data",
    ]
    types = [editor_visuals_type, editor_spells_type, client_visuals_type,
             client_spells_type]
    datasets = [message.FromString(path.read_bytes())
                for message, path in zip(types, paths)]

    for index in (0, 2):
        upsert_visuals(datasets[index], drafts)
    for index in (1, 3):
        link_spells(datasets[index], mapping)

    validate(datasets[0], datasets[1], mapping, available)
    validate(datasets[2], datasets[3], mapping, available)

    (OUT / "visualizations.json").write_text(json.dumps(drafts, indent=2) + "\n")

    if not args.apply:
        print(f"validated {len(drafts)} visualizations across {len(mapping)} spells")
        return

    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for index, path in enumerate(paths):
        shutil.copy2(path, backup / f"{index}_{path.name}")

    for dataset, path in zip(datasets, paths):
        path.write_bytes(dataset.SerializeToString())

    print(f"wrote {len(drafts)} visualizations and {len(mapping)} spell links "
          f"to both trees. Backup: {backup}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Validate without writing**

```bash
python tools/open_spell_visuals/author_open_visuals.py
```

Expected: `validated 3 visualizations across 4 spells` and exit 0. Any `SystemExit` message here means a clip name or spell id is wrong — fix it before applying.

- [ ] **Step 5: Apply the data**

```bash
python tools/open_spell_visuals/author_open_visuals.py --apply
```

Expected: `wrote 3 visualizations and 4 spell links to both trees. Backup: ...`

- [ ] **Step 6: Run the data test to verify it passes**

```bash
python -m unittest tools.tests.test_open_spell_visual_data -v
```

Expected: PASS, 5 tests, `OK`.

- [ ] **Step 7: Downgrade the missing-clip warning**

In `src/shared/game_client/spell_visualization_service.cpp`, inside `ApplyAnimationToActor`, replace:

```cpp
		AnimationState* animState = entity->GetAnimationState(animName);
		if (!animState)
		{
			WLOG("Animation '" << animName << "' not found on entity");
			return;
		}
```

with:

```cpp
		AnimationState* animState = entity->GetAnimationState(animName);
		if (!animState)
		{
			// A rig legitimately lacking a clip is not an error: the orc skeletons carry
			// none of the Use* clips the Open visualizations name, and a shared
			// visualization has to degrade quietly rather than log once per cast.
			DLOG("Animation '" << animName << "' not found on entity");
			return;
		}
```

- [ ] **Step 8: Build the client to verify the change compiles**

```bash
cmake --build build --config Debug -t mmo_client
```

Expected: build succeeds with no new warnings.

- [ ] **Step 9: Commit the submodule data**

```bash
git -C data/editor add data/spell_visualizations.data data/spells.data
git -C data/editor commit -m "data: Use animations on the Open spells"
git -C data/client add ClientDB/spell_visualizations.data ClientDB/spells.data
git -C data/client commit -m "data: Use animations on the Open spells"
```

- [ ] **Step 10: Commit the superproject files**

Do **not** `git add data/editor` or `git add data/client` — the submodule pointers stay as they are (see Global Constraints).

```bash
git add tools/open_spell_visuals/author_open_visuals.py tools/tests/test_open_spell_visual_data.py src/shared/game_client/spell_visualization_service.cpp
git commit -m "feat(spells): play the Use animation sequence on the Open spells"
```

---

## Task 2: The ANIM_SLOT_LOOT logical slot

Adds the slot and its default clip binding. Nothing consumes it yet; this task's deliverable is a client that still builds and behaves identically, with the slot available.

**Files:**
- Modify: `src/shared/proto_data/animation_profiles.proto:28`
- Modify: `src/shared/client_data/animation_profiles.proto:28`
- Modify: `src/shared/game_client/animation/animation_binding.h:64`
- Modify: `src/shared/game_client/animation/animation_binding.cpp:48` (end of `builtinBase`)

**Interfaces:**
- Consumes: nothing.
- Produces: `proto_client::ANIM_SLOT_LOOT` (enum value `17`), resolvable through `SlotBindingTable::Get(proto_client::ANIM_SLOT_LOOT)`, returning the entity's `"Loot"` clip or `nullptr`. Task 3 consumes this.

- [ ] **Step 1: Add the slot to the server-side proto**

In `src/shared/proto_data/animation_profiles.proto`, change:

```proto
	ANIM_SLOT_SPECIAL_IDLE = 16;
}
```

to:

```proto
	ANIM_SLOT_SPECIAL_IDLE = 16;
	ANIM_SLOT_LOOT = 17;
}
```

- [ ] **Step 2: Mirror the slot in the client proto**

In `src/shared/client_data/animation_profiles.proto`, make the exact same change. The two files must stay byte-identical in their enum values.

- [ ] **Step 3: Verify the two enums match**

```bash
diff <(grep "ANIM_SLOT_" src/shared/proto_data/animation_profiles.proto) <(grep "ANIM_SLOT_" src/shared/client_data/animation_profiles.proto)
```

Expected: no output (exit 0). Any output means the mirror is broken.

- [ ] **Step 4: Widen SlotCount**

In `src/shared/game_client/animation/animation_binding.h`, change:

```cpp
		static constexpr uint32 SlotCount = proto_client::ANIM_SLOT_SPECIAL_IDLE + 1;
```

to:

```cpp
		static constexpr uint32 SlotCount = proto_client::ANIM_SLOT_LOOT + 1;
```

This is load-bearing: `SlotBindingTable::m_bindings` is a `std::array<AnimationState*, SlotCount>` and `Get()` asserts the slot is in range, so forgetting it makes every loot lookup trip an `ASSERT`.

- [ ] **Step 5: Bind the slot to the "Loot" clip**

In `src/shared/game_client/animation/animation_binding.cpp`, in the `builtinBase` table, add a final entry after the `ANIM_SLOT_ATTACK` line:

```cpp
			{ proto_client::ANIM_SLOT_ATTACK, { "UnarmedAttack01" } },
			// No fallback candidate on purpose: a rig without a "Loot" clip must play its
			// normal idle rather than freeze in some unrelated pose.
			{ proto_client::ANIM_SLOT_LOOT, { "Loot" } },
		};
```

- [ ] **Step 6: Build the client and the editor**

```bash
cmake --build build --config Debug -t mmo_client mmo_edit
```

Expected: both targets build. The proto change regenerates through CMake; if the generated headers look stale, re-run `cmake -S . -B build` first.

- [ ] **Step 7: Build and run the full unit test suite**

```bash
cmake --build build --config Debug -t all_tests
```

```bash
cd build && ctest -C Debug --output-on-failure
```

Expected: all suites pass. This is a regression check — widening `SlotCount` touches an array every animated unit uses.

- [ ] **Step 8: Commit**

```bash
git add src/shared/proto_data/animation_profiles.proto src/shared/client_data/animation_profiles.proto src/shared/game_client/animation/animation_binding.h src/shared/game_client/animation/animation_binding.cpp
git commit -m "feat(anim): add the ANIM_SLOT_LOOT logical animation slot"
```

---

## Task 3: The held loot pose

Wires the slot to the replicated `Looting` flag. Delivers Part B of the spec.

**Files:**
- Modify: `src/shared/game_client/animation/animation_context.h` (after the `dead` member)
- Modify: `src/shared/game_client/animation/animation_controller.h` (private members, after `m_idleSeconds`)
- Modify: `src/shared/game_client/animation/animation_controller.cpp` (`EvaluateLocomotion`, after the `ctx.dead` branch)
- Modify: `src/shared/game_client/game_unit_c.cpp` (`UpdateAnimation`, after `ctx.dead = isDead;`)

**Interfaces:**
- Consumes: `proto_client::ANIM_SLOT_LOOT` and `SlotBindingTable::Get` from Task 2.
- Produces: `AnimationContext::looting` (a `bool`, default `false`). No later task consumes it.

- [ ] **Step 1: Add the context flag**

In `src/shared/game_client/animation/animation_context.h`, after the `dead` member:

```cpp
		/// True when the unit is dead (health or stand state).
		bool dead{false};

		/// True while the unit kneels in the loot pose: the replicated Looting unit flag is
		/// set AND the unit is standing still on the ground. The movement half is tested
		/// locally rather than waiting for the server to clear the flag, so the pose drops
		/// on the frame the player starts moving instead of a round trip later.
		bool looting{false};
	};
```

- [ ] **Step 2: Add the controller's entry-detection member**

In `src/shared/game_client/animation/animation_controller.h`, after `m_idleSeconds`:

```cpp
		/// Seconds the unit has been standing still; drives the special idle transition.
		float m_idleSeconds{0.0f};

		/// True while the loot pose owns the body. Used to detect the entry frame, where the
		/// clip is rewound and any playing one-shot is fast-forwarded out of the way.
		bool m_lootPoseActive{false};
	};
```

- [ ] **Step 3: Add the loot branch to EvaluateLocomotion**

In `src/shared/game_client/animation/animation_controller.cpp`, in `EvaluateLocomotion`, insert a branch between the `ctx.dead` case and the `m_pose.GetLock()` case:

```cpp
		if (ctx.dead)
		{
			single(m_bindings.Get(proto_client::ANIM_SLOT_DEATH));
			m_idleSeconds = 0.0f;
		}
		else if (ctx.looting)
		{
			// Ahead of the pose lock so looting while seated kneels and returns to sitting
			// afterwards. The clip does not loop, so AnimationState clamps it at its length
			// and it holds its last frame for as long as the loot window stays open.
			AnimationState* lootClip = m_bindings.Get(proto_client::ANIM_SLOT_LOOT);
			if (lootClip)
			{
				if (!m_lootPoseActive)
				{
					lootClip->SetLoop(false);
					lootClip->SetPlayRate(1.0f);
					lootClip->SetTimePosition(0.0f);

					// Finishing an Open cast on a chest fires the 1.37s UseEnd one-shot at the
					// same instant the loot window opens. Without this the character stands up
					// out of the chest and only then kneels.
					m_action.FastForwardCurrent();
					m_lootPoseActive = true;
				}

				single(lootClip);
			}

			m_idleSeconds = 0.0f;
		}
		else if (AnimationState* lock = m_pose.GetLock())
```

Then, immediately after the whole `if/else if` chain that `EvaluateLocomotion` uses to fill `clips`, and before it hands `clips` to the locomotion layer, clear the flag when the pose is not active:

```cpp
		if (!ctx.looting)
		{
			m_lootPoseActive = false;
		}
```

Place that statement directly before the call that pushes `clips`/`clipCount` into `m_locomotion` at the end of `EvaluateLocomotion`.

- [ ] **Step 4: Fill the flag from the replicated field**

In `src/shared/game_client/game_unit_c.cpp`, in `UpdateAnimation`, after `ctx.dead = isDead;`:

```cpp
		ctx.stealthed = HasStealthAura();
		ctx.dead = isDead;

		// The Looting unit flag is server-authoritative and clears a round trip after the
		// player starts moving, so the local movement test is what actually keeps a looting
		// character from sliding across the floor in a kneel. Recomputed every frame, so
		// there is no cached state to drift.
		ctx.looting = (Get<uint32>(object_fields::Flags) & unit_flags::Looting) != 0 &&
			!m_movementInfo.IsChangingPosition() && !ctx.swimming && !ctx.airborne;

		m_animationController->Update(ctx);
```

- [ ] **Step 5: Build the client**

```bash
cmake --build build --config Debug -t mmo_client
```

Expected: builds clean.

- [ ] **Step 6: Run the full unit test suite for regressions**

```bash
cmake --build build --config Debug -t all_tests
```

```bash
cd build && ctest -C Debug --output-on-failure
```

Expected: all suites pass. `SlotBindingTable::Rebuild` needs a live `Entity`, so there is no headless test for the loot slot itself; Task 4's E2E scenario covers the flag and Task 6 covers the visual.

- [ ] **Step 7: Commit**

```bash
git add src/shared/game_client/animation/animation_context.h src/shared/game_client/animation/animation_controller.h src/shared/game_client/animation/animation_controller.cpp src/shared/game_client/game_unit_c.cpp
git commit -m "feat(anim): kneel in a held Loot pose while the loot window is open"
```

---

## Task 4: E2E coverage for the Looting flag (the failing test)

Adds three verbs to the headless client and the scenario that exercises them. **The scenario is expected to FAIL at the end of this task** — the movement cancel it asserts does not exist until Task 5. That failure is the point.

**Files:**
- Modify: `src/shared/bot_core/bot_realm_connector.h` (public senders, near `void CheatKill();` at line 363)
- Modify: `src/shared/bot_core/bot_realm_connector.cpp` (near `BotRealmConnector::CheatKill` at line 2279)
- Modify: `src/e2e_client/scenario_engine.cpp` (helper lambdas near `luaGetObjectState`, registrations near line 1134)
- Create: `e2e/scenarios/loot_animation_flag.lua`
- Modify: `e2e/README.md` (API reference table)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces, for Lua scenarios:
  - `GetUnitFlags(guidString) -> number` — the unit's replicated `object_fields::Flags`, or `-1` for an unknown guid.
  - `LootUnit(guidString)` — sends `client_realm_packet::Loot` for that guid. Returns nothing.
  - `ReleaseLoot(guidString)` — sends `client_realm_packet::LootRelease` for that guid. Returns nothing.
  - C++: `BotRealmConnector::Loot(uint64 lootObjectGuid)` and `BotRealmConnector::LootRelease(uint64 lootObjectGuid)`.

**Reference facts:** `unit_flags::Lootable = 0x00000002`, `unit_flags::Looting = 0x00000004`. Creature entry `45` is "Forest Wolf", level 6, with unit loot entry 27 — the loot instance (and therefore the `Lootable` flag) is created whenever the creature has a loot entry configured, regardless of what the drop rolls produce, so this is deterministic.

- [ ] **Step 1: Add the packet senders to the bot connector header**

In `src/shared/bot_core/bot_realm_connector.h`, next to `void CheatKill();`:

```cpp
		void CheatKill();

		/// @brief Requests the loot window for a corpse or world object.
		/// @param lootObjectGuid Guid of the unit or object to loot.
		void Loot(uint64 lootObjectGuid);

		/// @brief Closes the loot window for a corpse or world object.
		/// @param lootObjectGuid Guid of the unit or object being looted.
		void LootRelease(uint64 lootObjectGuid);
```

- [ ] **Step 2: Implement the senders**

In `src/shared/bot_core/bot_realm_connector.cpp`, after `BotRealmConnector::CheatKill`:

```cpp
	void BotRealmConnector::Loot(const uint64 lootObjectGuid)
	{
		sendSinglePacket([lootObjectGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::Loot);
			packet << io::write<uint64>(lootObjectGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::LootRelease(const uint64 lootObjectGuid)
	{
		sendSinglePacket([lootObjectGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::LootRelease);
			packet << io::write<uint64>(lootObjectGuid);
			packet.Finish();
			});
	}
```

- [ ] **Step 3: Add the Lua helper functions**

In `src/e2e_client/scenario_engine.cpp`, in the same anonymous namespace as `luaGetObjectState`, add:

```cpp
		int64 luaGetUnitFlags(const std::string& guidStr)
		{
			const BotUnit* unit = findUnit(guidStr);
			return unit ? static_cast<int64>(unit->GetUnitFlags()) : -1;
		}

		void luaLootUnit(const std::string& guidStr)
		{
			g_runtime->session->GetRealm().Loot(guidFromString(guidStr));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("LootUnit", { { "guid", guidStr } });
			}
		}

		void luaReleaseLoot(const std::string& guidStr)
		{
			g_runtime->session->GetRealm().LootRelease(guidFromString(guidStr));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("ReleaseLoot", { { "guid", guidStr } });
			}
		}
```

`findUnit` is the file-local helper `luaGetHealth` and friends already use; it returns `nullptr` for an unknown guid, which is why the numeric query answers `-1`.

- [ ] **Step 4: Register the three verbs with Lua**

In `src/e2e_client/scenario_engine.cpp`, next to the existing `luabind::def_lambda("GetObjectState", ...)` registration:

```cpp
				luabind::def_lambda("GetObjectState", &luaGetObjectState),
				luabind::def_lambda("GetUnitFlags", &luaGetUnitFlags),
				luabind::def_lambda("LootUnit", &luaLootUnit),
				luabind::def_lambda("ReleaseLoot", &luaReleaseLoot),
```

Note the luabind comma gotcha: every entry in this chain ends with a comma except the last one in the list — keep the existing terminator where it is.

- [ ] **Step 5: Build the E2E client**

```bash
cmake --build build --config Debug -t e2e_client login_server realm_server world_server
```

Expected: all four targets build.

- [ ] **Step 6: Write the scenario**

Create `e2e/scenarios/loot_animation_flag.lua`:

```lua
-- Regression: the Looting unit flag drives the client's kneeling loot pose.
--
-- Three things must hold, and the client animation is only correct if all three do:
--   1. Opening a loot window sets unit_flags::Looting on the looting character.
--   2. Position-changing movement closes the window and clears the flag (otherwise the
--      character slides across the floor mid-kneel).
--   3. An explicit release clears it too.
--
-- Uses Forest Wolf (entry 45): it has a unit loot entry, and the server creates the loot
-- instance -- and with it the Lootable flag -- whenever an entry is configured, regardless
-- of what the drop chances roll. That makes the Lootable step deterministic.

local FOREST_WOLF = 45
local UNIT_FLAG_LOOTABLE = 2
local UNIT_FLAG_LOOTING = 4

local function hasFlag(guid, flag)
	local flags = GetUnitFlags(guid)
	Assert(flags >= 0, "GetUnitFlags returned -1 for guid " .. tostring(guid))
	return math.floor(flags / flag) % 2 == 1
end

local wolf = GM.CreateMonster(FOREST_WOLF)
Assert(IsAlive(wolf), "wolf should spawn alive")

TargetUnit(wolf)
GM.KillTarget()

Assert(WaitUntil(function() return not IsAlive(wolf) end, 10000, "wolf dies"),
	"wolf should be dead after GM.KillTarget")

Assert(WaitUntil(function() return hasFlag(wolf, UNIT_FLAG_LOOTABLE) end, 10000,
	"corpse becomes lootable"),
	"the corpse should carry unit_flags::Lootable after the kill")

-- 1. Opening the loot window flags the looter.
LootUnit(wolf)
Assert(WaitUntil(function() return hasFlag(Me(), UNIT_FLAG_LOOTING) end, 10000,
	"looter is flagged"),
	"unit_flags::Looting should be set on the character while the loot window is open")

Log("Looting flag set after LootUnit")

-- 2. Moving cancels it. Step a couple of units away from where the corpse is.
local x, y, z = GetPosX(Me()), GetPosY(Me()), GetPosZ(Me())
Assert(MoveTo(x + 3.0, y, z, 15000), "character should be able to step away from the corpse")

Assert(WaitUntil(function() return not hasFlag(Me(), UNIT_FLAG_LOOTING) end, 10000,
	"movement cancels looting"),
	"moving should close the loot window and clear unit_flags::Looting")

Log("Looting flag cleared by movement")

-- 3. An explicit release clears it too. Walk back into loot range first.
Assert(MoveTo(x, y, z, 15000), "character should be able to return to the corpse")

LootUnit(wolf)
Assert(WaitUntil(function() return hasFlag(Me(), UNIT_FLAG_LOOTING) end, 10000,
	"looter is flagged again"),
	"unit_flags::Looting should be set again on the second loot")

ReleaseLoot(wolf)
Assert(WaitUntil(function() return not hasFlag(Me(), UNIT_FLAG_LOOTING) end, 10000,
	"release clears looting"),
	"releasing the loot window should clear unit_flags::Looting")

Log("Looting flag cleared by explicit release")

GM.DestroyMonster(wolf)
```

- [ ] **Step 7: Run the scenario and verify it fails on the movement step**

```bash
powershell -File tools/e2e/e2e_run.ps1
```

Expected: FAIL. `loot_animation_flag` reports "moving should close the loot window and clear unit_flags::Looting". Every other scenario stays green. If it fails on an *earlier* assertion (the Lootable flag, or the first Looting flag), that is a harness problem in this task, not the missing feature — fix it here before moving on.

- [ ] **Step 8: Document the new verbs**

In `e2e/README.md`, in the "Queries" paragraph, add `GetUnitFlags(g)` to the numeric-query list (which already documents that numeric queries return `-1` for unknown units). In the "Actions" paragraph, add:

```
`LootUnit(g)` (opens the loot window on a corpse or world object; the character must
be within loot range), `ReleaseLoot(g)` (closes it),
```

- [ ] **Step 9: Commit**

```bash
git add src/shared/bot_core/bot_realm_connector.h src/shared/bot_core/bot_realm_connector.cpp src/e2e_client/scenario_engine.cpp e2e/scenarios/loot_animation_flag.lua e2e/README.md
git commit -m "test(e2e): cover the Looting unit flag lifecycle"
```

---

## Task 5: Movement cancels looting (server)

Makes Task 4's scenario pass. Delivers Part C of the spec.

**Files:**
- Modify: `src/world_server/player.cpp` (`Player::OnMovement`, after the stand-state block at ~line 2410)

**Interfaces:**
- Consumes: the E2E scenario from Task 4 as its test.
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Close the loot dialog on position-changing movement**

In `src/world_server/player.cpp`, in `Player::OnMovement`, directly after the closing brace of the stand-state block and before `VisibilityTile &tile = ...`:

```cpp
		// Starting to move, jumping or entering water while seated/sleeping stands the character
		// up (movement is client-authoritative, so the movement packet is the authoritative signal).
		if (m_character->IsAlive() && m_character->GetStandState() != unit_stand_state::Stand)
		{
			switch (opCode)
			{
			case game::client_realm_packet::MoveStartForward:
			case game::client_realm_packet::MoveStartBackward:
			case game::client_realm_packet::MoveStartStrafeLeft:
			case game::client_realm_packet::MoveStartStrafeRight:
			case game::client_realm_packet::MoveJump:
			case game::client_realm_packet::MoveStartSwim:
				m_character->SetStandState(unit_stand_state::Stand);
				break;
			default:
				break;
			}
		}

		// Moving out of the loot pose closes the window: the client kneels for as long as the
		// Looting flag is set, so a character that walks away while it is still set would slide
		// across the floor mid-kneel. Tested on the movement flags rather than the op code so a
		// heartbeat, a knockback or a fall cancels it too; turning in place deliberately does
		// not, since the character stays planted and a mouse-look must not cost anyone their loot.
		if (m_loot && info.IsChangingPosition())
		{
			CloseLootDialog();
		}

		VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
```

`CloseLootDialog()` already sends `LootReleaseResponse` (so the client's loot frame closes) and clears `unit_flags::Looting`, which replicates to everyone nearby.

- [ ] **Step 2: Build the world server**

```bash
cmake --build build --config Debug -t world_server
```

Expected: builds clean.

- [ ] **Step 3: Run the E2E suite and verify the scenario now passes**

```bash
powershell -File tools/e2e/e2e_run.ps1
```

Expected: exit 0, every scenario green including `loot_animation_flag`. Results land in `e2e/runtime/logs/summary.json`.

- [ ] **Step 4: Verify no protocol bump is needed**

```bash
python tools/protocol_version_check.py
```

Expected: exit 0, no complaint. Nothing on the wire changed — `unit_flags::Looting` and `object_fields::Flags` already existed and were already replicated.

`docs/protocol_versions.md` is a reference document, not a changelog — it records how bumps work and what enforces them, and has no per-change table. There is no bump here, so there is nothing to add to it. Do not edit it.

- [ ] **Step 5: Commit**

```bash
git add src/world_server/player.cpp
git commit -m "fix(world): close the loot window when the character moves"
```

---

## Task 6: Gate and visual verification

**Files:** none modified unless the gate or the visual pass turns something up.

**Interfaces:**
- Consumes: everything from Tasks 1-5.
- Produces: a green `tools/gate/last_report.json` matching HEAD.

- [ ] **Step 1: Run the local quality gate**

Invoke the `gate` skill (`/gate`), which runs `tools/gate/verify.ps1` — protocol check, Debug build, unit tests, E2E — and then reviews the branch diff.

Expected: green. `tools/gate/last_report.json` must be full, green and match HEAD before anything merges.

- [ ] **Step 2: Verify the Open animations in the real client**

Launch the client with the RunOnce auto-login path and confirm three things by eye or screenshot:

1. Casting Open on a chest: the character reaches out (`UseStart`), holds a working loop for the 5 s cast (`UseLoop`), then withdraws (`UseEnd`).
2. Interrupting that cast by walking: the loop drops immediately and the character runs normally — no `UseEnd`.
3. Using a door: a single reach-and-withdraw flourish, no loop.

- [ ] **Step 3: Verify the loot pose in the real client**

1. Kill a creature, open its loot: the character kneels and **holds** the last frame for as long as the window stays open, rather than snapping back to idle after 0.87 s.
2. Walk away: the window closes and the character runs normally, with no slide in the kneel.
3. Loot a chest right after an Open cast: the kneel takes over immediately rather than playing `UseEnd` first.
4. Watch a second character loot nearby, if one is available: they kneel too.

- [ ] **Step 4: Report and hand off**

Report the gate result and the visual findings to the user. Raise the two open questions explicitly rather than deciding them:

- Whether to bump the `data/client` and `data/editor` submodule pointers in the superproject (they were already ahead before this branch).
- Whether to merge with `/ship`.

---

## Self-Review

**Spec coverage.** Part A → Task 1. Part B → Tasks 2 and 3. Part C → Task 5. Protocol section → Task 5 step 4. Testing section: E2E → Tasks 4 and 5; data tests → Task 1; the spec's "no unit test for the loot slot, it needs a live Entity" note → Task 3 step 6; visual → Task 6. The spec's orc-rig WLOG downgrade → Task 1 step 7. The spec's submodule caution → Global Constraints and Task 1 steps 9-10.

**Deviation from the spec, deliberate:** the spec's E2E step 3 said the scenario would also assert "a `LootReleaseResponse` arrived". The bot connector registers no handler for that packet and adding one would be dead weight — the `Looting` flag is the contract the animation actually reads, so the scenario asserts on the flag alone. Coverage is unchanged.

**Placeholder scan.** No TBD/TODO. Every code step carries the code. The one judgement call left to the implementer is named explicitly and bounded (Task 4 step 3: match the neighbouring helper's unit-lookup method name rather than guess).

**Type consistency.** `ANIM_SLOT_LOOT` (Task 2) is consumed as `proto_client::ANIM_SLOT_LOOT` in Task 3. `AnimationContext::looting` is declared in Task 3 step 1 and assigned in step 4 with the same name. `m_lootPoseActive` is declared in step 2 and used in step 3. `BotRealmConnector::Loot` / `LootRelease` (Task 4 steps 1-2) are called as `GetRealm().Loot(...)` / `.LootRelease(...)` in step 3. `GetUnitFlags` / `LootUnit` / `ReleaseLoot` are registered in step 4 and called under those exact names in the scenario in step 6.
