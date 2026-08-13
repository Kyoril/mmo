# Sevrin Wax, the Coffinwright — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a level 12 undead necromancer dungeon boss to map 1 with three phases and two add types, authored entirely as game data — no C++ combat script, no Lua.

**Architecture:** Seven new spells, twelve new triggers, three new units, one loot table, and one map edit. Phase state lives in world-instance variables because `SetPhase` needs a combat script the boss deliberately does not have. Scripted moments are `Delay` chains on `OnAggro`/`OnHealthDroppedBelow`; recurring ones are `OnTimer` triggers.

**Tech Stack:** Binary protobuf data under `data/editor/data/`, authored via the JSON draft → validate → apply scripts in `.agents/skills/mmo-{npc,spell}-designer/scripts/`. One new applier script for triggers and map encounters, which no existing tool can write. Lua E2E scenario under `e2e/scenarios/`.

**Design spec:** [2026-08-12-crypt-necromancer-boss-design.md](../specs/2026-08-12-crypt-necromancer-boss-design.md). Read it before starting — it records the fourteen engine constraints this design is built around, each with a file:line source.

## Global Constraints

- **Branch:** main-repo work happens on `feature/crypt-necromancer-boss`. Never push to origin. Merge to `develop` only via `/gate` then `/ship`.
- **`data/editor` and `data/client` are separate git submodules**, not directories of this repo (`data/editor` → `mmo-editor-data`, `data/client` → `mmo-data`). Both sit on `master` and were clean at the start of this work. This has three consequences:
  - Every data commit runs **inside** the submodule: `git -C data/editor commit …`, `git -C data/client commit …`. A `git add data/editor/...` from the main repo does not stage the file contents — at best it stages a pointer bump, which reviews as an opaque one-line `Subproject commit` change.
  - **Stage only the exact files you touched.** Never `git add -A` or `git add .` in either submodule.
  - The main repo's submodule pointers are bumped **once**, in Task 7, matching this repo's convention (`git log`: "Update data/client and data/editor submodule refs (…)"). Do not bump them per task.
- **Review packages for data tasks are generated inside the submodule.** For Tasks 2–5 the controller runs `review-package` with the submodule as the working directory, because a main-repo diff would show nothing but a pointer.
- **Project root is `H:\mmo`.** Pass `--project-root H:/mmo` (forward slashes) to every skill script. The skill docs say `F:\mmo` — that is wrong for this checkout, and backslashes fail to resolve in the Bash tool.
- **Every new spell must be `cost: 0`.** Creature spell casts pay power cost (`single_cast_state.cpp:695-725`) and unit class 3 has a 50 mana pool.
- **Every new spell must be `classmask: 0`** — these are creature abilities, not class spells.
- **Task order is load-bearing.** `validate_npc_json.py:151-153` rejects a unit whose `triggers` list names a trigger that does not exist yet. Spells and triggers must be applied before units.
- **New spells need a dual write.** `data/editor/data/spells.data` is the source; `data/client/ClientDB/spells.data` is a straight file copy of it (`main_window.cpp:942-1010`, `ExportToClient`). The same applies to `maps.data`. Without the copy the client cannot render the spells.
- **All player-facing text is localized in four locales:** 1 = de, 2 = en, 3 = fr, 4 = ru. English also stays in the base field. This covers unit `name_loc`/`subname_loc`, spell `name_loc`/`description_loc`/`auratext_loc`, and trigger `texts_loc`.
- **Code style** for the one Python file added: tabs for indentation, `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` header, stdlib `unittest` only (no pytest), matching `tools/tests/test_protocol_version_check.py`.
- **No wire format change**, so no `ProtocolVersion` bump. `python tools/protocol_version_check.py` must still pass.
- **IDs are fixed by the spec** and must not drift: spells 236–242, triggers 29–40, units 81–83, loot entry 28, map 1 encounter slot 1, instance variables 1001 (phase) and 1002 (boss alive).

### Reference values used throughout

| Thing | Value |
|---|---|
| Effect target `Caster` / `TargetEnemy` / `SourceAreaEnemy` | 0 / 5 / 9 |
| Effect type `SchoolDamage` / `ApplyAura` | 2 / 6 |
| Aura `PeriodicDamage` / `ModDecreaseSpeed` / `ModAttackSpeed` / `ModDamageDonePct` / `ModDamageTakenPct` | 13 / 15 / 3 / 23 / 24 |
| `rangetype` 30 units / melee | 5 / 6 |
| `spellSchool` Shadow | 5 |
| `visualization_id` Shadow Bolt / Fear / Enrage / Immunity / Default | 16 / 15 / 12 / 17 / 9 |
| Faction template `Undead Dungeon` | 4 |
| Model `PLAYER - Undead Human Male` | 17 |
| Unit class Warrior / Mage / Dungeon Boss | 1 / 2 / 3 |
| Trigger event `OnSpawn`/`OnAggro`/`OnKilled`/`OnReset`/`OnHealthDroppedBelow`/`OnAllPlayersDead`/`OnTimer` | 0 / 2 / 3 / 8 / 11 / 19 / 22 |
| Trigger action `Say`/`Yell`/`CastSpell`/`Delay`/`StopAutoAttack`/`CancelCast`/`SetCombatMovement`/`Emote`/`Despawn` | 1 / 2 / 6 / 7 / 10 / 11 / 9 / 23 / 21 |
| Trigger action `SetEncounterState`/`SummonCreature`/`ModifyThreat`/`ApplyAura`/`RemoveAura`/`SetInstanceVariable`/`BroadcastMessage` | 24 / 25 / 27 / 29 / 30 / 31 / 32 |
| Trigger target `OwningObject`/`NamedCreature`/`RandomPlayer` | 1 / 5 / 7 |
| Trigger flags `AbortOnOwnerDeath` / `OnlyInCombat` | 1 / 2 |
| Encounter state `NotStarted`/`InProgress`/`Done`/`Fail` | 0 / 1 / 2 / 3 |
| Boss home / left arch / right arch / apse | (-24,1,0) / (-12,1,-9) / (-12,1,9) / (-27,1,0) |

Scratch directory for JSON drafts: `generated/encounters/` (create it; it is a build output directory, not committed).

---

### Task 1: Encounter data applier

No existing tool can write `triggers.data` on its own. `apply_quest_json.py` supports an `attached_triggers` list but hard-requires `doc["quest"]` (`apply_quest_json.py:85`), and nothing writes `MapEntry.encounters` or `instance_triggers` at all. This task adds that tool, with tests, before any data is authored.

**Files:**
- Create: `.agents/skills/mmo-npc-designer/scripts/apply_encounter_json.py`
- Create: `tools/tests/test_apply_encounter_json.py`

**Interfaces:**
- Consumes: `npc_catalog_lib.find_project_root`, `load_catalog_bundle`, `parse_message_dict` (all already exported by `.agents/skills/mmo-npc-designer/scripts/npc_catalog_lib.py`).
- Produces: a CLI — `python .agents/skills/mmo-npc-designer/scripts/apply_encounter_json.py <path> --project-root H:/mmo [--backup] [--dry-run]`, and three module-level functions the test drives directly: `replace_or_append_by_id(container, message) -> str`, `apply_encounter(map_entry, encounter_message) -> str`, `apply_instance_triggers(map_entry, trigger_ids) -> str`.

Input document shape:

```json
{
  "format": "mmo-encounter",
  "version": 1,
  "triggers": [ { "id": 29, "name": "...", "actions": [], "newevents": [] } ],
  "map_encounters": [ { "map_id": 1, "encounter": { "id": 1, "name": "..." } } ],
  "map_instance_triggers": [ { "map_id": 1, "trigger_ids": [37] } ]
}
```

- [ ] **Step 1: Create the scratch and draft directory**

```bash
mkdir -p generated/encounters
```

- [ ] **Step 2: Write the failing test**

Create `tools/tests/test_apply_encounter_json.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for .agents/skills/mmo-npc-designer/scripts/apply_encounter_json.py.

This applier is the only way trigger rows and map encounter slots reach the binary
project data. Its failure modes are quiet: appending a duplicate id instead of updating
one, or dropping an instance trigger, both produce a file that still loads and a boss
that simply never fires. Those cases are pinned down here.

    python tools/tests/test_apply_encounter_json.py
"""

import importlib.util
import os
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPTS_DIR = os.path.join(REPO_ROOT, ".claude", "skills", "mmo-npc-designer", "scripts")
MODULE_PATH = os.path.join(SCRIPTS_DIR, "apply_encounter_json.py")

sys.path.insert(0, SCRIPTS_DIR)
_spec = importlib.util.spec_from_file_location("apply_encounter_json", MODULE_PATH)
aej = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(aej)

from proto_runtime import load_modules  # noqa: E402

MODULES = load_modules(REPO_ROOT)


class ReplaceOrAppendById(unittest.TestCase):
	def test_appends_a_new_id(self):
		triggers = MODULES["triggers"].Triggers()
		entry = MODULES["triggers"].TriggerEntry(id=29, name="Sevrin Wax - On Aggro")
		self.assertEqual(aej.replace_or_append_by_id(triggers.entry, entry), "created")
		self.assertEqual(len(triggers.entry), 1)

	def test_updates_an_existing_id_in_place(self):
		# A second apply of the same draft must not leave two rows with id 29 behind:
		# the loader keys triggers by id, so a duplicate silently shadows the real one.
		triggers = MODULES["triggers"].Triggers()
		triggers.entry.add(id=29, name="old name")
		entry = MODULES["triggers"].TriggerEntry(id=29, name="new name")
		self.assertEqual(aej.replace_or_append_by_id(triggers.entry, entry), "updated")
		self.assertEqual(len(triggers.entry), 1)
		self.assertEqual(triggers.entry[0].name, "new name")


class ApplyEncounter(unittest.TestCase):
	def test_adds_an_encounter_slot(self):
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		encounter = MODULES["maps"].EncounterEntry(id=1, name="Sevrin Wax, the Coffinwright")
		self.assertEqual(aej.apply_encounter(map_entry, encounter), "created")
		self.assertEqual(len(map_entry.encounters), 1)

	def test_updates_an_existing_slot_by_id(self):
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		map_entry.encounters.add(id=1, name="placeholder")
		encounter = MODULES["maps"].EncounterEntry(id=1, name="Sevrin Wax, the Coffinwright")
		self.assertEqual(aej.apply_encounter(map_entry, encounter), "updated")
		self.assertEqual(len(map_entry.encounters), 1)
		self.assertEqual(map_entry.encounters[0].name, "Sevrin Wax, the Coffinwright")


class ApplyInstanceTriggers(unittest.TestCase):
	def test_adds_missing_trigger_ids(self):
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		self.assertEqual(aej.apply_instance_triggers(map_entry, [37]), "added 37")
		self.assertEqual(list(map_entry.instance_triggers), [37])

	def test_is_idempotent(self):
		# Re-applying the draft must not grow the list; a duplicated instance trigger
		# would run the wipe handler twice per wipe.
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		map_entry.instance_triggers.append(37)
		self.assertEqual(aej.apply_instance_triggers(map_entry, [37]), "unchanged")
		self.assertEqual(list(map_entry.instance_triggers), [37])

	def test_preserves_unrelated_ids(self):
		map_entry = MODULES["maps"].MapEntry(id=1, name="Test Dungeon", directory="Test")
		map_entry.instance_triggers.append(99)
		aej.apply_instance_triggers(map_entry, [37])
		self.assertEqual(sorted(map_entry.instance_triggers), [37, 99])


if __name__ == "__main__":
	unittest.main(verbosity=2)
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
python tools/tests/test_apply_encounter_json.py
```

Expected: FAIL — `FileNotFoundError` or `ModuleNotFoundError` for `apply_encounter_json.py`, because the module does not exist yet.

- [ ] **Step 4: Write the applier**

Create `.agents/skills/mmo-npc-designer/scripts/apply_encounter_json.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Apply encounter JSON (trigger rows, map encounter slots, instance triggers) into project data.

The NPC applier owns units, loot and spawns; the quest applier can carry trigger rows but
only alongside a quest. Neither can write a boss encounter's triggers or a map's encounter
slots on their own, which is what this script is for.

Cross-references are intentionally not validated: an encounter's triggers routinely name
creature entries that the NPC applier has not created yet, and forcing the opposite order
would make a unit's trigger list unresolvable instead. Apply triggers first, units second.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from npc_catalog_lib import (
	find_project_root,
	load_catalog_bundle,
	parse_message_dict,
)


def replace_or_append_by_id(container, message) -> str:
	for index, entry in enumerate(container):
		if entry.id == message.id:
			container[index].CopyFrom(message)
			return "updated"
	container.add().CopyFrom(message)
	return "created"


def apply_encounter(map_entry, encounter_message) -> str:
	return replace_or_append_by_id(map_entry.encounters, encounter_message)


def apply_instance_triggers(map_entry, trigger_ids) -> str:
	added = []
	for trigger_id in trigger_ids:
		if trigger_id not in list(map_entry.instance_triggers):
			map_entry.instance_triggers.append(trigger_id)
			added.append(trigger_id)
	if not added:
		return "unchanged"
	return "added " + ", ".join(str(value) for value in added)


def backup_file(path: Path) -> None:
	shutil.copy2(path, path.with_suffix(path.suffix + ".bak"))


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("path")
	parser.add_argument("--project-root", default=None)
	parser.add_argument("--backup", action="store_true")
	parser.add_argument("--dry-run", action="store_true")
	args = parser.parse_args()

	project_root = find_project_root(args.project_root)
	doc = json.loads(Path(args.path).read_text(encoding="utf-8"))

	if doc.get("format") != "mmo-encounter":
		print("ERROR: expected \"format\": \"mmo-encounter\"")
		return 1

	modules, catalogs, indexes = load_catalog_bundle(project_root)
	data_root = project_root / "data" / "editor" / "data"
	touched: list[Path] = []
	actions: list[str] = []

	for trigger_data in doc.get("triggers", []):
		message = parse_message_dict(modules["triggers"].TriggerEntry, trigger_data)
		actions.append(f"trigger {message.id}: {replace_or_append_by_id(catalogs['triggers'].entry, message)}")
		if data_root / "triggers.data" not in touched:
			touched.append(data_root / "triggers.data")

	for wrapper in doc.get("map_encounters", []):
		map_entry = indexes["maps"][wrapper["map_id"]]
		message = parse_message_dict(modules["maps"].EncounterEntry, wrapper["encounter"])
		actions.append(f"encounter {message.id} on map {map_entry.id}: {apply_encounter(map_entry, message)}")
		if data_root / "maps.data" not in touched:
			touched.append(data_root / "maps.data")

	for wrapper in doc.get("map_instance_triggers", []):
		map_entry = indexes["maps"][wrapper["map_id"]]
		result = apply_instance_triggers(map_entry, wrapper["trigger_ids"])
		actions.append(f"instance triggers on map {map_entry.id}: {result}")
		if data_root / "maps.data" not in touched:
			touched.append(data_root / "maps.data")

	if not touched:
		print("Nothing to do: document contained no triggers, encounters or instance triggers")
		return 1

	if args.dry_run:
		for action in actions:
			print(f"DRY-RUN: {action}")
		for path in touched:
			print(f"DRY-RUN: would write {path}")
		return 0

	if args.backup:
		for path in touched:
			backup_file(path)

	if data_root / "triggers.data" in touched:
		(data_root / "triggers.data").write_bytes(catalogs["triggers"].SerializeToString())
	if data_root / "maps.data" in touched:
		(data_root / "maps.data").write_bytes(catalogs["maps"].SerializeToString())

	for action in actions:
		print(f"OK: {action}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
python tools/tests/test_apply_encounter_json.py
```

Expected: PASS — `OK` with 7 tests run.

- [ ] **Step 6: Commit**

```bash
git add .agents/skills/mmo-npc-designer/scripts/apply_encounter_json.py tools/tests/test_apply_encounter_json.py
git commit -m "feat(tools): add an applier for trigger rows and map encounter slots"
```

---

### Task 2: The seven encounter spells

**Files:**
- Create: `generated/encounters/spell_236_grave_bolt.json` … `spell_242_dirge.json` (7 drafts)
- Modify: `data/editor/data/spells.data`
- Modify: `data/client/ClientDB/spells.data`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: spell ids **236** Grave Bolt, **237** Wasting Rot, **238** Gravewax Nova, **239** Coffin Nail, **240** Rite of Rising, **241** Unhallowed Fervor, **242** Dirge of the Hollow Choir. Tasks 3 and 4 reference these ids.

- [ ] **Step 1: Confirm 236–242 are free**

```bash
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root H:/mmo --section spells --limit 1000 | python -c "import sys,json; ids={s['id'] for s in json.load(sys.stdin)['spells']}; print('max', max(ids)); print('collisions', sorted(ids & set(range(236,243))))"
```

Expected: `max 235` and `collisions []`. If anything collides, stop and renumber consistently across this plan.

- [ ] **Step 2: Write the four damage spells**

`generated/encounters/spell_236_grave_bolt.json`:

```json
{
  "format": "mmo-spell",
  "version": 1,
  "spell": {
    "id": 236,
    "name": "Grave Bolt",
    "attributes": [0, 0],
    "effects": [
      { "index": 0, "type": 2, "basepoints": 46, "diesides": 12, "targeta": 5 }
    ],
    "cooldown": "0",
    "casttime": 2000,
    "cost": 0,
    "maxlevel": 12,
    "baselevel": 12,
    "spelllevel": 12,
    "speed": 28.0,
    "spellSchool": 5,
    "facing": 1,
    "interruptflags": 5,
    "rangetype": 5,
    "classmask": 0,
    "rank": 0,
    "description": "Hurls a clot of grave-dark at the enemy, dealing $s0 shadow damage.",
    "icon": "Interface/Icons/Spells/T_Icon_Shadow_01.htex",
    "visualization_id": 16,
    "cooldownflags": 3,
    "name_loc": [
      { "locale": 1, "value": "Grabesblitz" },
      { "locale": 2, "value": "Grave Bolt" },
      { "locale": 3, "value": "Trait de tombe" },
      { "locale": 4, "value": "Могильная стрела" }
    ],
    "description_loc": [
      { "locale": 1, "value": "Schleudert einen Klumpen Grabesdunkel auf den Feind und verursacht $s0 Schattenschaden." },
      { "locale": 2, "value": "Hurls a clot of grave-dark at the enemy, dealing $s0 shadow damage." },
      { "locale": 3, "value": "Projette un caillot de ténèbres sépulcrales sur l'ennemi, infligeant $s0 points de dégâts d'ombre." },
      { "locale": 4, "value": "Швыряет в противника комок могильной тьмы, нанося $s0 ед. урона от тьмы." }
    ]
  }
}
```

`generated/encounters/spell_237_wasting_rot.json` — a DoT. `amplitude` is the tick interval in ms, and `basepoints` on a `PeriodicDamage` aura is the per-tick amount:

```json
{
  "format": "mmo-spell",
  "version": 1,
  "spell": {
    "id": 237,
    "name": "Wasting Rot",
    "attributes": [0, 0],
    "effects": [
      { "index": 0, "type": 6, "basepoints": 15, "targeta": 5, "aura": 13, "amplitude": 3000 }
    ],
    "cooldown": "0",
    "casttime": 0,
    "cost": 0,
    "maxlevel": 12,
    "baselevel": 12,
    "spelllevel": 12,
    "spellSchool": 5,
    "facing": 1,
    "duration": 18000,
    "rangetype": 5,
    "classmask": 0,
    "rank": 0,
    "description": "The flesh sloughs, dealing $s0 shadow damage every 3 seconds for $D.",
    "icon": "Interface/Icons/Spells/T_Icon_Shadow_02.htex",
    "auratext": "Rotting away.",
    "visualization_id": 9,
    "cooldownflags": 3,
    "name_loc": [
      { "locale": 1, "value": "Zehrende Fäulnis" },
      { "locale": 2, "value": "Wasting Rot" },
      { "locale": 3, "value": "Pourriture rongeante" },
      { "locale": 4, "value": "Изнуряющая гниль" }
    ],
    "description_loc": [
      { "locale": 1, "value": "Das Fleisch löst sich und verursacht alle 3 Sekunden $s0 Schattenschaden für $D." },
      { "locale": 2, "value": "The flesh sloughs, dealing $s0 shadow damage every 3 seconds for $D." },
      { "locale": 3, "value": "La chair se détache, infligeant $s0 points de dégâts d'ombre toutes les 3 secondes pendant $D." },
      { "locale": 4, "value": "Плоть отслаивается, нанося $s0 ед. урона от тьмы каждые 3 секунды в течение $D." }
    ],
    "auratext_loc": [
      { "locale": 1, "value": "Verfault." },
      { "locale": 2, "value": "Rotting away." },
      { "locale": 3, "value": "En putréfaction." },
      { "locale": 4, "value": "Разлагается." }
    ]
  }
}
```

`generated/encounters/spell_238_gravewax_nova.json` — self-centred AoE. Both effects use `targeta: 9` (`SourceAreaEnemy`) with an explicit `radius`. Leave `rangetype` unset so the creature spell entry's `maxrange` of 0 makes the AI treat it as a melee-range ability:

```json
{
  "format": "mmo-spell",
  "version": 1,
  "spell": {
    "id": 238,
    "name": "Gravewax Nova",
    "attributes": [0, 0],
    "effects": [
      { "index": 0, "type": 2, "basepoints": 66, "diesides": 20, "targeta": 9, "radius": 8.0 },
      { "index": 1, "type": 6, "basepoints": -40, "targeta": 9, "radius": 8.0, "aura": 15 }
    ],
    "cooldown": "0",
    "casttime": 1500,
    "cost": 0,
    "maxlevel": 12,
    "baselevel": 12,
    "spelllevel": 12,
    "spellSchool": 5,
    "duration": 6000,
    "interruptflags": 5,
    "classmask": 0,
    "rank": 0,
    "description": "Molten grave-wax bursts outward, dealing $s0 shadow damage and slowing movement by $s1% for $D.",
    "icon": "Interface/Icons/Spells/T_Icon_Shadow_03.htex",
    "auratext": "Movement speed reduced by $s1%.",
    "visualization_id": 9,
    "cooldownflags": 3,
    "name_loc": [
      { "locale": 1, "value": "Grabeswachs-Nova" },
      { "locale": 2, "value": "Gravewax Nova" },
      { "locale": 3, "value": "Nova de cire sépulcrale" },
      { "locale": 4, "value": "Взрыв могильного воска" }
    ],
    "description_loc": [
      { "locale": 1, "value": "Geschmolzenes Grabeswachs bricht nach außen, verursacht $s0 Schattenschaden und verlangsamt die Bewegung um $s1 % für $D." },
      { "locale": 2, "value": "Molten grave-wax bursts outward, dealing $s0 shadow damage and slowing movement by $s1% for $D." },
      { "locale": 3, "value": "De la cire sépulcrale en fusion jaillit, infligeant $s0 points de dégâts d'ombre et réduisant la vitesse de déplacement de $s1% pendant $D." },
      { "locale": 4, "value": "Расплавленный могильный воск разлетается, нанося $s0 ед. урона от тьмы и замедляя передвижение на $s1% на $D." }
    ],
    "auratext_loc": [
      { "locale": 1, "value": "Bewegungsgeschwindigkeit um $s1 % verringert." },
      { "locale": 2, "value": "Movement speed reduced by $s1%." },
      { "locale": 3, "value": "Vitesse de déplacement réduite de $s1%." },
      { "locale": 4, "value": "Скорость передвижения снижена на $s1%." }
    ]
  }
}
```

`generated/encounters/spell_239_coffin_nail.json` — melee-range strike, `rangetype: 6`:

```json
{
  "format": "mmo-spell",
  "version": 1,
  "spell": {
    "id": 239,
    "name": "Coffin Nail",
    "attributes": [0, 0],
    "effects": [
      { "index": 0, "type": 2, "basepoints": 36, "diesides": 10, "targeta": 5 }
    ],
    "cooldown": "0",
    "casttime": 0,
    "cost": 0,
    "maxlevel": 12,
    "baselevel": 12,
    "spelllevel": 12,
    "spellSchool": 5,
    "facing": 1,
    "rangetype": 6,
    "classmask": 0,
    "rank": 0,
    "description": "Drives an iron nail home, dealing $s0 shadow damage.",
    "icon": "Interface/Icons/Spells/T_Icon_Shadow_04.htex",
    "visualization_id": 9,
    "cooldownflags": 3,
    "name_loc": [
      { "locale": 1, "value": "Sargnagel" },
      { "locale": 2, "value": "Coffin Nail" },
      { "locale": 3, "value": "Clou de cercueil" },
      { "locale": 4, "value": "Гробовой гвоздь" }
    ],
    "description_loc": [
      { "locale": 1, "value": "Treibt einen Eisennagel ein und verursacht $s0 Schattenschaden." },
      { "locale": 2, "value": "Drives an iron nail home, dealing $s0 shadow damage." },
      { "locale": 3, "value": "Enfonce un clou de fer, infligeant $s0 points de dégâts d'ombre." },
      { "locale": 4, "value": "Вгоняет железный гвоздь, нанося $s0 ед. урона от тьмы." }
    ]
  }
}
```

- [ ] **Step 3: Write the two self-auras and the acolyte bolt**

`generated/encounters/spell_240_rite_of_rising.json` — the phase channel. `basepoints: -90` on `ModDamageTakenPct` (aura 24) is a 90% damage reduction; the multiplier is clamped at 0 in `game_unit_s.cpp:3584`, so this cannot invert. `duration` is 12000, deliberately longer than either channel so that the trigger's `RemoveAura` is what normally ends it and the duration is only a safety net:

```json
{
  "format": "mmo-spell",
  "version": 1,
  "spell": {
    "id": 240,
    "name": "Rite of Rising",
    "attributes": [0, 0],
    "effects": [
      { "index": 0, "type": 6, "basepoints": -90, "targeta": 0, "aura": 24 }
    ],
    "cooldown": "0",
    "casttime": 0,
    "cost": 0,
    "maxlevel": 12,
    "baselevel": 12,
    "spelllevel": 12,
    "spellSchool": 5,
    "duration": 12000,
    "classmask": 0,
    "rank": 0,
    "description": "Wrapped in the Litany, taking $s0% less damage for $D.",
    "icon": "Interface/Icons/Spells/T_Icon_Shadow_05.htex",
    "auratext": "Shielded by the Litany.",
    "visualization_id": 17,
    "cooldownflags": 0,
    "name_loc": [
      { "locale": 1, "value": "Ritus der Auferstehung" },
      { "locale": 2, "value": "Rite of Rising" },
      { "locale": 3, "value": "Rite du relèvement" },
      { "locale": 4, "value": "Обряд восставания" }
    ],
    "description_loc": [
      { "locale": 1, "value": "In die Litanei gehüllt, erleidet $s0 % weniger Schaden für $D." },
      { "locale": 2, "value": "Wrapped in the Litany, taking $s0% less damage for $D." },
      { "locale": 3, "value": "Enveloppé dans la Litanie, subit $s0% de dégâts en moins pendant $D." },
      { "locale": 4, "value": "Окутан Литанией, получает на $s0% меньше урона в течение $D." }
    ],
    "auratext_loc": [
      { "locale": 1, "value": "Von der Litanei geschützt." },
      { "locale": 2, "value": "Shielded by the Litany." },
      { "locale": 3, "value": "Protégé par la Litanie." },
      { "locale": 4, "value": "Защищён Литанией." }
    ]
  }
}
```

`generated/encounters/spell_241_unhallowed_fervor.json` — permanent phase 3 enrage. `duration: -1` is not valid here; use a duration long enough to outlast any fight (600000, matching how spell 159 `Immunity` handles the same problem):

```json
{
  "format": "mmo-spell",
  "version": 1,
  "spell": {
    "id": 241,
    "name": "Unhallowed Fervor",
    "attributes": [0, 0],
    "effects": [
      { "index": 0, "type": 6, "basepoints": 30, "targeta": 0, "aura": 23 },
      { "index": 1, "type": 6, "basepoints": 30, "targeta": 0, "aura": 3 }
    ],
    "cooldown": "0",
    "casttime": 0,
    "cost": 0,
    "maxlevel": 12,
    "baselevel": 12,
    "spelllevel": 12,
    "spellSchool": 5,
    "duration": 600000,
    "classmask": 0,
    "rank": 0,
    "description": "Damage dealt increased by $s0% and attack speed increased by $s1%.",
    "icon": "Interface/Icons/Spells/T_Icon_Shadow_06.htex",
    "auratext": "Damage and attack speed increased.",
    "visualization_id": 12,
    "cooldownflags": 0,
    "name_loc": [
      { "locale": 1, "value": "Ungeweihter Eifer" },
      { "locale": 2, "value": "Unhallowed Fervor" },
      { "locale": 3, "value": "Ferveur impie" },
      { "locale": 4, "value": "Неосвящённое рвение" }
    ],
    "description_loc": [
      { "locale": 1, "value": "Verursachter Schaden um $s0 % und Angriffsgeschwindigkeit um $s1 % erhöht." },
      { "locale": 2, "value": "Damage dealt increased by $s0% and attack speed increased by $s1%." },
      { "locale": 3, "value": "Dégâts infligés augmentés de $s0% et vitesse d'attaque augmentée de $s1%." },
      { "locale": 4, "value": "Наносимый урон увеличен на $s0%, скорость атаки увеличена на $s1%." }
    ],
    "auratext_loc": [
      { "locale": 1, "value": "Schaden und Angriffsgeschwindigkeit erhöht." },
      { "locale": 2, "value": "Damage and attack speed increased." },
      { "locale": 3, "value": "Dégâts et vitesse d'attaque augmentés." },
      { "locale": 4, "value": "Урон и скорость атаки увеличены." }
    ]
  }
}
```

`generated/encounters/spell_242_dirge.json` — the acolyte's filler. Ranged, so `DetermineCombatBehavior` classes the acolyte as a caster and it stands and sings instead of charging:

```json
{
  "format": "mmo-spell",
  "version": 1,
  "spell": {
    "id": 242,
    "name": "Dirge of the Hollow Choir",
    "attributes": [0, 0],
    "effects": [
      { "index": 0, "type": 2, "basepoints": 22, "diesides": 8, "targeta": 5 }
    ],
    "cooldown": "0",
    "casttime": 2500,
    "cost": 0,
    "maxlevel": 11,
    "baselevel": 11,
    "spelllevel": 11,
    "speed": 24.0,
    "spellSchool": 5,
    "facing": 1,
    "interruptflags": 5,
    "rangetype": 4,
    "classmask": 0,
    "rank": 0,
    "description": "A grinding note of the choir, dealing $s0 shadow damage.",
    "icon": "Interface/Icons/Spells/T_Icon_Shadow_07.htex",
    "visualization_id": 16,
    "cooldownflags": 3,
    "name_loc": [
      { "locale": 1, "value": "Klagelied des Hohlen Chors" },
      { "locale": 2, "value": "Dirge of the Hollow Choir" },
      { "locale": 3, "value": "Complainte du chœur creux" },
      { "locale": 4, "value": "Плач Пустого хора" }
    ],
    "description_loc": [
      { "locale": 1, "value": "Ein schleifender Ton des Chors, der $s0 Schattenschaden verursacht." },
      { "locale": 2, "value": "A grinding note of the choir, dealing $s0 shadow damage." },
      { "locale": 3, "value": "Une note grinçante du chœur, infligeant $s0 points de dégâts d'ombre." },
      { "locale": 4, "value": "Скрежещущая нота хора, наносящая $s0 ед. урона от тьмы." }
    ]
  }
}
```

- [ ] **Step 4: Validate all seven drafts**

```bash
for f in generated/encounters/spell_2*.json; do echo "== $f"; python .agents/skills/mmo-spell-designer/scripts/validate_spell_json.py "$f" --project-root H:/mmo || exit 1; done
```

Expected: each prints `OK` (or validates with no `ERROR:` lines). Fix any reported error before continuing — do not apply a failing draft.

- [ ] **Step 5: Apply all seven**

```bash
for f in generated/encounters/spell_2*.json; do python .agents/skills/mmo-spell-designer/scripts/apply_spell_json.py "$f" --project-root H:/mmo --backup || exit 1; done
```

- [ ] **Step 6: Mirror to the client database**

`apply_spell_json.py` writes only the server copy. The editor's `ExportToClient` is a straight file copy, so this reproduces it exactly:

```bash
cp data/editor/data/spells.data data/client/ClientDB/spells.data
```

- [ ] **Step 7: Verify the applied spells read back**

```bash
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root H:/mmo --spell-id 240 --pretty
```

Expected: `Rite of Rising`, one effect of type 6 with `aura: 24` and `basepoints: -90`, `cost` absent or 0, `duration: 12000`.

- [ ] **Step 8: Commit**

```bash
git -C data/editor add data/spells.data
git -C data/editor commit -m "Add the seven Sevrin Wax encounter spells"
git -C data/client add ClientDB/spells.data
git -C data/client commit -m "Export Sevrin Wax encounter spells to ClientDB"
```

---

### Task 3: The twelve encounter triggers

Applied before the units, because `validate_npc_json.py:151-153` rejects a unit referencing a trigger that does not exist.

**Files:**
- Create: `generated/encounters/triggers_sevrin_wax.json`
- Modify: `data/editor/data/triggers.data`

**Interfaces:**
- Consumes: spells 236–242 (Task 2); `apply_encounter_json.py` (Task 1). References units 81/82/83, which do not exist yet — the applier deliberately does not validate that.
- Produces: trigger ids 29–40. Task 4 lists 29–36 on unit 81, 39 on unit 82, and 38/39/40 on unit 83. Task 5 registers 37 as a map instance trigger.

`TriggerAction` fields: `action` (id), `target`, `targetname`, `texts` (index 0 = English), `data` (positional), `texts_loc`. `TriggerEvent` is `{ "type": N, "data": [...] }`. A `condition` is a `TriggerCondition` — `leftfunction` with `leftfunctiondata`, an `operator`, and `rightlong`.

- [ ] **Step 1: Confirm 29–40 are free**

```bash
python -c "
import sys; from pathlib import Path
sys.path.insert(0, '.agents/skills/mmo-npc-designer/scripts')
from proto_runtime import load_modules
m = load_modules(Path('H:/mmo'))
t = m['triggers'].Triggers(); t.ParseFromString(Path('data/editor/data/triggers.data').read_bytes())
ids = {e.id for e in t.entry}
print('max', max(ids)); print('collisions', sorted(ids & set(range(29, 41))))
"
```

Expected: `max 28` and `collisions []`.

- [ ] **Step 2: Write the trigger document**

Create `generated/encounters/triggers_sevrin_wax.json`. This is the whole encounter script; every `Yell`/`Say`/`BroadcastMessage` carries all four locales.

```json
{
  "format": "mmo-encounter",
  "version": 1,
  "triggers": [
    {
      "id": 29,
      "name": "Sevrin Wax - On Aggro",
      "newevents": [ { "type": 2 } ],
      "actions": [
        { "action": 31, "data": [1001, 1] },
        { "action": 31, "data": [1002, 1] },
        { "action": 24, "data": [1, 1] },
        {
          "action": 2, "target": 1,
          "texts": ["You'll wake them. You'll wake them all, and I have only just finished the sealing."],
          "texts_loc": [
            { "locale": 1, "value": "Ihr werdet sie wecken. Ihr werdet sie alle wecken, und ich habe die Versiegelung eben erst beendet." },
            { "locale": 3, "value": "Vous allez les réveiller. Vous allez tous les réveiller, et je viens à peine d'achever le scellement." },
            { "locale": 4, "value": "Вы разбудите их. Вы разбудите их всех, а я только что закончил запечатывание." }
          ]
        }
      ]
    },
    {
      "id": 30,
      "name": "Sevrin Wax - Rite of Rising (Phase 2)",
      "flags": 1,
      "newevents": [ { "type": 11, "data": [65] } ],
      "actions": [
        {
          "action": 32, "data": [1],
          "texts": ["Sevrin Wax begins the Rite of Rising!"],
          "texts_loc": [
            { "locale": 1, "value": "Sevrin Wax beginnt den Ritus der Auferstehung!" },
            { "locale": 3, "value": "Sevrin Wax entame le Rite du relèvement !" },
            { "locale": 4, "value": "Севрин Вакс начинает Обряд восставания!" }
          ]
        },
        { "action": 31, "data": [1001, 2] },
        {
          "action": 2, "target": 1,
          "texts": ["Sing with me! SING!"],
          "texts_loc": [
            { "locale": 1, "value": "Singt mit mir! SINGT!" },
            { "locale": 3, "value": "Chantez avec moi ! CHANTEZ !" },
            { "locale": 4, "value": "Пойте со мной! ПОЙТЕ!" }
          ]
        },
        { "action": 11, "target": 1 },
        { "action": 10, "target": 1 },
        { "action": 9, "target": 1, "data": [0] },
        { "action": 29, "target": 1, "data": [240] },
        { "action": 25, "target": 1, "data": [83, -12, 1, -9, 300000, 0] },
        { "action": 25, "target": 1, "data": [83, -12, 1, 9, 300000, 0] },
        { "action": 7, "data": [3000] },
        { "action": 25, "target": 1, "data": [82, -27, 1, 0, 120000, 1] },
        { "action": 7, "data": [5000] },
        { "action": 30, "target": 1, "data": [240] },
        { "action": 9, "target": 1, "data": [1] },
        {
          "action": 2, "target": 1,
          "texts": ["Now. Now you hear the choir."],
          "texts_loc": [
            { "locale": 1, "value": "Jetzt. Jetzt hört ihr den Chor." },
            { "locale": 3, "value": "Maintenant. Maintenant vous entendez le chœur." },
            { "locale": 4, "value": "Теперь. Теперь вы слышите хор." }
          ]
        }
      ]
    },
    {
      "id": 31,
      "name": "Sevrin Wax - Unhallowed (Phase 3)",
      "flags": 1,
      "newevents": [ { "type": 11, "data": [30] } ],
      "actions": [
        {
          "action": 32, "data": [1],
          "texts": ["Sevrin Wax casts off the wax and the weeping!"],
          "texts_loc": [
            { "locale": 1, "value": "Sevrin Wax wirft das Wachs und die Klage von sich!" },
            { "locale": 3, "value": "Sevrin Wax rejette la cire et les pleurs !" },
            { "locale": 4, "value": "Севрин Вакс сбрасывает воск и плач!" }
          ]
        },
        { "action": 31, "data": [1001, 3] },
        {
          "action": 2, "target": 1,
          "texts": ["Then I will join the choir myself!"],
          "texts_loc": [
            { "locale": 1, "value": "Dann trete ich dem Chor selbst bei!" },
            { "locale": 3, "value": "Alors je rejoindrai le chœur moi-même !" },
            { "locale": 4, "value": "Тогда я сам вступлю в хор!" }
          ]
        },
        { "action": 11, "target": 1 },
        { "action": 10, "target": 1 },
        { "action": 9, "target": 1, "data": [0] },
        { "action": 29, "target": 1, "data": [240] },
        { "action": 25, "target": 1, "data": [83, -12, 1, -9, 300000, 0] },
        { "action": 25, "target": 1, "data": [83, -12, 1, 9, 300000, 0] },
        { "action": 25, "target": 1, "data": [82, -27, 1, 0, 120000, 1] },
        { "action": 25, "target": 1, "data": [82, -12, 1, -9, 120000, 1] },
        { "action": 7, "data": [6000] },
        { "action": 30, "target": 1, "data": [240] },
        { "action": 9, "target": 1, "data": [1] },
        { "action": 29, "target": 1, "data": [241] }
      ]
    },
    {
      "id": 32,
      "name": "Sevrin Wax - Husk Wave (Left)",
      "flags": 2,
      "newevents": [ { "type": 22, "data": [25000, 32000] } ],
      "condition": { "operator": 3, "leftfunction": 11, "leftfunctiondata": [1001], "rightlong": 1 },
      "actions": [
        {
          "action": 1, "target": 1,
          "texts": ["Lie back down."],
          "texts_loc": [
            { "locale": 1, "value": "Legt euch wieder hin." },
            { "locale": 3, "value": "Recouchez-vous." },
            { "locale": 4, "value": "Ложитесь обратно." }
          ]
        },
        { "action": 25, "target": 1, "data": [82, -12, 1, -9, 120000, 1] }
      ]
    },
    {
      "id": 33,
      "name": "Sevrin Wax - Husk Wave (Right)",
      "flags": 2,
      "newevents": [ { "type": 22, "data": [25000, 32000] } ],
      "condition": { "operator": 3, "leftfunction": 11, "leftfunctiondata": [1001], "rightlong": 2 },
      "actions": [
        { "action": 25, "target": 1, "data": [82, -12, 1, 9, 120000, 1] }
      ]
    },
    {
      "id": 34,
      "name": "Sevrin Wax - Coffin Call",
      "flags": 2,
      "newevents": [ { "type": 22, "data": [30000, 40000] } ],
      "condition": { "operator": 3, "leftfunction": 11, "leftfunctiondata": [1001], "rightlong": 1 },
      "actions": [
        {
          "action": 2, "target": 1,
          "texts": ["You. I have a box your size."],
          "texts_loc": [
            { "locale": 1, "value": "Du. Ich habe eine Kiste in deiner Größe." },
            { "locale": 3, "value": "Toi. J'ai une boîte à ta taille." },
            { "locale": 4, "value": "Ты. У меня есть ящик твоего размера." }
          ]
        },
        { "action": 27, "target": 7, "data": [100000] }
      ]
    },
    {
      "id": 35,
      "name": "Sevrin Wax - On Killed",
      "newevents": [ { "type": 3 } ],
      "actions": [
        {
          "action": 2, "target": 1,
          "texts": ["...the song... goes on without me..."],
          "texts_loc": [
            { "locale": 1, "value": "...das Lied... geht ohne mich weiter..." },
            { "locale": 3, "value": "...le chant... continue sans moi..." },
            { "locale": 4, "value": "...песнь... продолжается без меня..." }
          ]
        },
        { "action": 24, "data": [1, 2] },
        { "action": 31, "data": [1002, 0] },
        { "action": 31, "data": [1001, 0] }
      ]
    },
    {
      "id": 36,
      "name": "Sevrin Wax - On Reset",
      "newevents": [ { "type": 8 } ],
      "actions": [
        { "action": 31, "data": [1001, 0] },
        { "action": 31, "data": [1002, 1] },
        { "action": 24, "data": [1, 0] }
      ]
    },
    {
      "id": 37,
      "name": "Crypt - Encounter Wipe",
      "newevents": [ { "type": 19 } ],
      "actions": [
        {
          "action": 2, "target": 5, "targetname": "CryptBoss_SevrinWax",
          "texts": ["Rest now. I'll seal you myself."],
          "texts_loc": [
            { "locale": 1, "value": "Ruht nun. Ich versiegle euch selbst." },
            { "locale": 3, "value": "Reposez-vous. Je vous scellerai moi-même." },
            { "locale": 4, "value": "Отдыхайте. Я сам вас запечатаю." }
          ]
        },
        { "action": 24, "data": [1, 3] },
        { "action": 31, "data": [1002, 0] }
      ]
    },
    {
      "id": 38,
      "name": "Choirbound Acolyte - Raise Husk",
      "newevents": [ { "type": 22, "data": [15000] } ],
      "condition": { "operator": 0, "leftfunction": 11, "leftfunctiondata": [1002], "rightlong": 1 },
      "actions": [
        { "action": 25, "target": 1, "data": [82, -15, 1, 0, 120000, 1] }
      ]
    },
    {
      "id": 39,
      "name": "Boss Add - Cleanup When Boss Dead",
      "newevents": [ { "type": 22, "data": [10000] } ],
      "condition": { "operator": 0, "leftfunction": 11, "leftfunctiondata": [1002], "rightlong": 0 },
      "actions": [
        { "action": 21, "target": 1 }
      ]
    },
    {
      "id": 40,
      "name": "Choirbound Acolyte - On Spawn",
      "newevents": [ { "type": 0 } ],
      "actions": [
        {
          "action": 23, "target": 1,
          "texts": ["begins a low, grinding hymn."],
          "texts_loc": [
            { "locale": 1, "value": "beginnt eine tiefe, schleifende Hymne." },
            { "locale": 3, "value": "entonne une hymne grave et grinçante." },
            { "locale": 4, "value": "начинает низкий скрежещущий гимн." }
          ]
        }
      ]
    }
  ]
}
```

**Why trigger 38 summons at a fixed point rather than at the acolyte.** `SummonCreature`
reads explicit coordinates only when `action.data_size() >= 4` (`trigger_handler.cpp:1408`);
otherwise it takes the resolved target's position. Summoning at the acolyte's own position
therefore means passing fewer than four values, which leaves no room for the despawn timer
at index 4 or the attack-nearest flag at index 5 — the husks would never expire and would
stand inert until attacked.

The fixed point `(-15, 1, 0)` is mid-nave, 9 units east of the boss: the dead claw up out
of the floor where the fight is actually happening. Both acolytes share this trigger, which
is why the point is in the nave rather than in either aisle — it reads correctly regardless
of which acolyte is singing, and the husks arrive at the players instead of pathing in from
a side room.

The `bossAlive == 1` condition stops acolytes raising husks in the 10s window between the
boss dying and trigger 39 despawning them.

**Interval is the first tuning dial.** Two live acolytes at 15s each is a husk every 7.5s,
and each husk has 390 HP. That is meant to be urgent. If it is overwhelming, raise the
interval before weakening the husks.

- [ ] **Step 3: Apply the triggers**

```bash
python .agents/skills/mmo-npc-designer/scripts/apply_encounter_json.py generated/encounters/triggers_sevrin_wax.json --project-root H:/mmo --backup
```

Expected: twelve `OK: trigger NN: created` lines.

- [ ] **Step 4: Verify the triggers read back**

```bash
python -c "
import sys; from pathlib import Path
sys.path.insert(0, '.agents/skills/mmo-npc-designer/scripts')
from proto_runtime import load_modules
m = load_modules(Path('H:/mmo'))
t = m['triggers'].Triggers(); t.ParseFromString(Path('data/editor/data/triggers.data').read_bytes())
for e in t.entry:
    if 29 <= e.id <= 40:
        print(e.id, repr(e.name), 'flags', e.flags, 'events', [(v.type, list(v.data)) for v in e.newevents], 'actions', len(e.actions), 'cond', e.HasField('condition'))
"
```

Expected: twelve rows. Triggers 30 and 31 both have 15 actions, with events `(11, [65])` and `(11, [30])` respectively; triggers 32, 33, 34, 38 and 39 report `cond True`; trigger 30 has `flags 1` and triggers 32/33/34 have `flags 2`.

The `1002 = 0` on trigger 37 is what makes adds clean themselves up after a **wipe**, not just after a kill. Without it, a wipe leaves the acolytes singing to an empty room and raising husks until their own 120–300s despawn timers expire, which is exactly the kind of warm-instance leftover that breaks the next E2E scenario.

- [ ] **Step 5: Commit**

```bash
git -C data/editor add data/triggers.data
git -C data/editor commit -m "Add the Sevrin Wax encounter triggers"
```

---

### Task 4: The boss, the two adds, and the loot table

**Files:**
- Create: `generated/encounters/unit_82_wax_sealed_husk.json`
- Create: `generated/encounters/unit_83_choirbound_acolyte.json`
- Create: `generated/encounters/unit_81_sevrin_wax.json`
- Modify: `data/editor/data/units.data`
- Modify: `data/editor/data/unit_loot.data`

**Interfaces:**
- Consumes: spells 236–242 (Task 2), triggers 29–40 (Task 3).
- Produces: unit ids **81** Sevrin Wax, **82** Wax-Sealed Husk, **83** Choirbound Acolyte, and loot entry **28**. Task 5 spawns unit 81; the triggers already summon 82 and 83.

Two traps this task has to work around:

1. **The applier only writes `unit_loot.data` when the legacy `unitlootentry` field is set** (`apply_npc_json.py:86`). The runtime prefers `unitlootentries` and only falls back to the legacy field when that list is empty (`creature_ai_death_state.cpp:226-237`). So set **both** — `unitlootentry: 28` to make the applier write the table, and `unitlootentries: [28]` as the field the runtime actually reads.
2. **Adds must be authored before the boss** only in the sense that all three must exist before Task 5; the order below just keeps each validation self-contained.

- [ ] **Step 1: Write the husk**

`generated/encounters/unit_82_wax_sealed_husk.json`:

```json
{
  "format": "mmo-npc",
  "version": 1,
  "unit": {
    "id": 82,
    "name": "Wax-Sealed Husk",
    "subname": "",
    "minlevel": 11,
    "maxlevel": 11,
    "factionTemplate": 4,
    "maleModel": 17,
    "femaleModel": 17,
    "type": 0,
    "family": 0,
    "walkspeed": 0.7,
    "runspeed": 6.0,
    "unitClassId": 1,
    "useStatBasedSystem": true,
    "eliteStatMultiplier": 1.0,
    "baseArmor": 205,
    "armorPerLevel": 48.0,
    "damagePerLevel": 1.0,
    "meleeattacktime": 2400,
    "regeneration": 3,
    "minlevelxp": 0,
    "maxlevelxp": 0,
    "creaturespells": [ { "spellid": 37 } ],
    "triggers": [39],
    "name_loc": [
      { "locale": 1, "value": "Wachsversiegelte Hülle" },
      { "locale": 2, "value": "Wax-Sealed Husk" },
      { "locale": 3, "value": "Dépouille scellée à la cire" },
      { "locale": 4, "value": "Запечатанная воском оболочка" }
    ]
  }
}
```

`minlevelxp`/`maxlevelxp` are 0 and there is no loot entry on purpose: the acolytes summon these indefinitely, so any reward would be farmable.

- [ ] **Step 2: Write the acolyte**

`generated/encounters/unit_83_choirbound_acolyte.json`:

```json
{
  "format": "mmo-npc",
  "version": 1,
  "unit": {
    "id": 83,
    "name": "Choirbound Acolyte",
    "subname": "",
    "minlevel": 11,
    "maxlevel": 11,
    "factionTemplate": 4,
    "maleModel": 17,
    "femaleModel": 17,
    "type": 0,
    "family": 0,
    "unitClassId": 2,
    "useStatBasedSystem": true,
    "eliteStatMultiplier": 1.0,
    "baseArmor": 175,
    "armorPerLevel": 42.0,
    "damagePerLevel": 1.0,
    "meleeattacktime": 2000,
    "regeneration": 3,
    "minlevelxp": 0,
    "maxlevelxp": 0,
    "creaturespells": [
      { "spellid": 242, "priority": 100, "mincooldown": 3000, "maxcooldown": 5000, "minrange": 0.0, "maxrange": 25.0 },
      { "spellid": 37 }
    ],
    "triggers": [38, 39, 40],
    "name_loc": [
      { "locale": 1, "value": "Chorgebundener Akolyth" },
      { "locale": 2, "value": "Choirbound Acolyte" },
      { "locale": 3, "value": "Acolyte lié au chœur" },
      { "locale": 4, "value": "Прикованный к хору послушник" }
    ]
  }
}
```

- [ ] **Step 3: Write the boss with its loot table**

`generated/encounters/unit_81_sevrin_wax.json`. The rotation is balanced 2 ranged / 2 melee-range so `DetermineCombatBehavior` (`creature_ai_combat_state.cpp:1233-1265`) resolves to Melee — a Caster would back away from the tank and drag the fight into the aisles.

Loot group 1 holds the four crypt blues at `dropchance: 0.0`. A group whose weighted candidates are all empty falls through to the equal-chance pool and picks exactly one (`loot_instance.cpp:140-151`), so every kill yields exactly one of the four.

```json
{
  "format": "mmo-npc",
  "version": 1,
  "unit": {
    "id": 81,
    "name": "Sevrin Wax, the Coffinwright",
    "subname": "Warden of the Hollow Choir",
    "minlevel": 12,
    "maxlevel": 12,
    "factionTemplate": 4,
    "maleModel": 17,
    "femaleModel": 17,
    "type": 0,
    "family": 0,
    "unitClassId": 3,
    "useStatBasedSystem": true,
    "eliteStatMultiplier": 2.0,
    "baseArmor": 175,
    "armorPerLevel": 42.0,
    "damagePerLevel": 1.2,
    "meleeattacktime": 2000,
    "regeneration": 3,
    "minlevelxp": 520,
    "maxlevelxp": 520,
    "minlootgold": 1800,
    "maxlootgold": 2600,
    "mainhandweapon": 119,
    "unitlootentry": 28,
    "unitlootentries": [28],
    "triggers": [29, 30, 31, 32, 33, 34, 35, 36],
    "creaturespells": [
      { "spellid": 236, "priority": 100, "mincooldown": 4000, "maxcooldown": 6000, "minrange": 0.0, "maxrange": 30.0 },
      { "spellid": 237, "priority": 100, "mincooldown": 14000, "maxcooldown": 18000, "minrange": 0.0, "maxrange": 30.0 },
      { "spellid": 239, "priority": 100, "mincooldown": 10000, "maxcooldown": 12000, "minrange": 0.0, "maxrange": 0.0 },
      { "spellid": 238, "priority": 100, "mincooldown": 20000, "maxcooldown": 26000, "minrange": 0.0, "maxrange": 0.0 },
      { "spellid": 37 }
    ],
    "name_loc": [
      { "locale": 1, "value": "Sevrin Wax, der Sargmacher" },
      { "locale": 2, "value": "Sevrin Wax, the Coffinwright" },
      { "locale": 3, "value": "Sevrin Wax, le Faiseur de cercueils" },
      { "locale": 4, "value": "Севрин Вакс, Гробовщик" }
    ],
    "subname_loc": [
      { "locale": 1, "value": "Wächter des Hohlen Chors" },
      { "locale": 2, "value": "Warden of the Hollow Choir" },
      { "locale": 3, "value": "Gardien du chœur creux" },
      { "locale": 4, "value": "Хранитель Пустого хора" }
    ]
  },
  "loot_entry": {
    "id": 28,
    "name": "Sevrin Wax, the Coffinwright",
    "minmoney": 1800,
    "maxmoney": 2600,
    "groups": [
      {
        "definitions": [
          { "item": 116, "mincount": 1, "maxcount": 1, "dropchance": 0.0, "isactive": true },
          { "item": 117, "mincount": 1, "maxcount": 1, "dropchance": 0.0, "isactive": true },
          { "item": 118, "mincount": 1, "maxcount": 1, "dropchance": 0.0, "isactive": true },
          { "item": 119, "mincount": 1, "maxcount": 1, "dropchance": 0.0, "isactive": true }
        ]
      },
      {
        "definitions": [
          { "item": 136, "mincount": 1, "maxcount": 1, "dropchance": 12.0, "isactive": true },
          { "item": 135, "mincount": 1, "maxcount": 1, "dropchance": 18.0, "isactive": true },
          { "item": 140, "mincount": 1, "maxcount": 2, "dropchance": 45.0, "isactive": true }
        ]
      }
    ]
  }
}
```

- [ ] **Step 4: Validate all three drafts**

```bash
for f in generated/encounters/unit_8*.json; do echo "== $f"; python .agents/skills/mmo-npc-designer/scripts/validate_npc_json.py "$f" --project-root H:/mmo || exit 1; done
```

Expected: no `ERROR:` lines. If `unit.triggers contains unknown trigger NN` appears, Task 3 did not apply — go back and finish it.

- [ ] **Step 5: Apply all three (without spawns — Task 5 handles placement)**

```bash
python .agents/skills/mmo-npc-designer/scripts/apply_npc_json.py generated/encounters/unit_82_wax_sealed_husk.json --project-root H:/mmo --backup
python .agents/skills/mmo-npc-designer/scripts/apply_npc_json.py generated/encounters/unit_83_choirbound_acolyte.json --project-root H:/mmo --backup
python .agents/skills/mmo-npc-designer/scripts/apply_npc_json.py generated/encounters/unit_81_sevrin_wax.json --project-root H:/mmo --backup
```

- [ ] **Step 6: Verify the computed stats match the spec**

```bash
python .agents/skills/mmo-npc-designer/scripts/inspect_npc_catalog.py --project-root H:/mmo --unit-id 81 --pretty
```

Expected: `Sevrin Wax, the Coffinwright`, faction template name `Undead Dungeon`, unit class name `Dungeon Boss`, `loot_entry_name` resolving to the new table, and the four creature spells plus Dodge.

Then confirm the derived health, which the spec predicts as 2670 / 390 / 237:

```bash
python -c "
import sys; from pathlib import Path
sys.path.insert(0, '.agents/skills/mmo-npc-designer/scripts')
from proto_runtime import load_modules
m = load_modules(Path('H:/mmo'))
D = Path('data/editor/data')
u = m['units'].Units(); u.ParseFromString((D/'units.data').read_bytes())
c = m['unit_classes'].UnitClasses(); c.ParseFromString((D/'unit_classes.data').read_bytes())
classes = {e.id: e for e in c.entry}
for e in u.entry:
    if e.id in (81, 82, 83):
        k = classes[e.unitClassId]
        base = k.levelbasevalues[e.minlevel - 1]
        mult = e.eliteStatMultiplier
        # The engine seeds strength and agility from stamina, so every stat total is stamina.
        stats = {0: base.stamina, 1: base.stamina, 2: base.stamina, 3: base.intellect, 4: base.spirit}
        hp = int(base.health * mult)
        for s in k.healthStatSources:
            hp += int((max(int(stats[s.statId] * mult), 20) - 20) * s.factor)
        print(e.id, e.name, 'level', e.minlevel, 'HP', hp)
"
```

Expected: `81 ... HP 2670`, `82 ... HP 390`, `83 ... HP 237`. A mismatch means an `eliteStatMultiplier` or `unitClassId` typo.

- [ ] **Step 7: Verify the loot group yields exactly one blue**

```bash
python -c "
import sys; from pathlib import Path
sys.path.insert(0, '.agents/skills/mmo-npc-designer/scripts')
from proto_runtime import load_modules
m = load_modules(Path('H:/mmo'))
l = m['unit_loot'].UnitLoot(); l.ParseFromString(Path('data/editor/data/unit_loot.data').read_bytes())
for e in l.entry:
    if e.id == 28:
        for i, g in enumerate(e.groups):
            print('group', i, [(d.item, d.dropchance) for d in g.definitions])
"
```

Expected: group 0 lists items 116–119 all at `0.0` (the equal-chance pool, so exactly one is taken), group 1 lists the weighted extras.

- [ ] **Step 8: Commit**

```bash
git -C data/editor add data/units.data data/unit_loot.data
git -C data/editor commit -m "Add Sevrin Wax, his two add types, and his loot table"
```

---

### Task 5: Map 1 — encounter slot, instance trigger, boss spawn

**Files:**
- Create: `generated/encounters/map1_sevrin_wax.json`
- Modify: `data/editor/data/maps.data`
- Modify: `data/client/ClientDB/maps.data`

**Interfaces:**
- Consumes: unit 81 (Task 4), trigger 37 (Task 3), `apply_encounter_json.py` (Task 1).
- Produces: named creature spawner `CryptBoss_SevrinWax` on map 1, which trigger 37 resolves via the `NamedCreature` target, and encounter slot 1, which the `SetEncounterState` actions address.

- [ ] **Step 1: Write the encounter slot and instance trigger document**

`generated/encounters/map1_sevrin_wax.json`:

```json
{
  "format": "mmo-encounter",
  "version": 1,
  "map_encounters": [
    { "map_id": 1, "encounter": { "id": 1, "name": "Sevrin Wax, the Coffinwright" } }
  ],
  "map_instance_triggers": [
    { "map_id": 1, "trigger_ids": [37] }
  ]
}
```

- [ ] **Step 2: Apply it**

```bash
python .agents/skills/mmo-npc-designer/scripts/apply_encounter_json.py generated/encounters/map1_sevrin_wax.json --project-root H:/mmo --backup
```

Expected: `OK: encounter 1 on map 1: created` and `OK: instance triggers on map 1: added 37`.

- [ ] **Step 3: Add the spawn section to the boss draft**

Append a `spawns` section to `generated/encounters/unit_81_sevrin_wax.json`, as a sibling of `unit` and `loot_entry`. `replace_mode: "by_name"` makes re-application idempotent. This is a **fragment**, not a whole document — paste it inside the existing top-level object:

```json
  "spawns": [
    {
      "map_id": 1,
      "replace_mode": "by_name",
      "spawn": {
        "name": "CryptBoss_SevrinWax",
        "unitentry": 81,
        "respawn": true,
        "respawndelay": 300000,
        "positionx": -24.0,
        "positiony": 1.0,
        "positionz": 0.0,
        "rotation": 0.0,
        "maxcount": 1,
        "isactive": true,
        "movement": 0,
        "locations": [
          { "positionx": -24.0, "positiony": 1.0, "positionz": 0.0, "rotation": 0.0 }
        ]
      }
    }
  ]
```

Both the legacy positional fields and `locations` are set: the proto marks `positionx/y/z` as `required` and obsolete at the same time, so the message will not serialize without them.

- [ ] **Step 4: Re-validate and apply with spawns**

```bash
python .agents/skills/mmo-npc-designer/scripts/validate_npc_json.py generated/encounters/unit_81_sevrin_wax.json --project-root H:/mmo
python .agents/skills/mmo-npc-designer/scripts/apply_npc_json.py generated/encounters/unit_81_sevrin_wax.json --project-root H:/mmo --apply-spawns --backup
```

Expected: `spawn on map 1: created`.

- [ ] **Step 5: Mirror maps.data to the client database**

`maps` is in the editor's shared-manager export list, so the client copy must track it:

```bash
cp data/editor/data/maps.data data/client/ClientDB/maps.data
```

- [ ] **Step 6: Verify the map wiring**

```bash
python -c "
import sys; from pathlib import Path
sys.path.insert(0, '.agents/skills/mmo-npc-designer/scripts')
from proto_runtime import load_modules
m = load_modules(Path('H:/mmo'))
mm = m['maps'].Maps(); mm.ParseFromString(Path('data/editor/data/maps.data').read_bytes())
for e in mm.entry:
    if e.id == 1:
        print('encounters', [(c.id, c.name) for c in e.encounters])
        print('instance_triggers', list(e.instance_triggers))
        for s in e.unitspawns:
            if s.name == 'CryptBoss_SevrinWax':
                print('spawn', s.name, s.unitentry, s.positionx, s.positiony, s.positionz, 'rot', s.rotation, 'movement', s.movement)
"
```

Expected: `encounters [(1, 'Sevrin Wax, the Coffinwright')]`, `instance_triggers [37]`, and the spawn at `-24.0 1.0 0.0`.

- [ ] **Step 7: Commit**

```bash
git -C data/editor add data/maps.data
git -C data/editor commit -m "Place Sevrin Wax in the crypt and register his encounter slot"
git -C data/client add ClientDB/maps.data
git -C data/client commit -m "Export map 1 encounter slot and boss spawn to ClientDB"
```

---

### Task 6: E2E scenario

**Files:**
- Create: `e2e/scenarios/crypt_boss_sevrin_wax.lua`

**Interfaces:**
- Consumes: everything from Tasks 2–5. Uses the scenario API documented in `e2e/README.md`.
- Produces: nothing other tasks depend on.

The scenario targets the failure modes that would otherwise ship silently: the damage-reduction aura being applied but never removed (the one soft-lock in the design), each health threshold firing, the acolytes appearing, and summons actually disappearing after the kill.

Two constraints from `e2e/README.md` and the isolation notes: scenarios start on map 0 with a fresh GM level 3 character, and a scenario must not leave the shared instance dirty for the next one. Deliberately **do not** over-level the character — a single hit large enough to cross 65% and 30% at once fires both phase triggers, which is the edge case the spec describes.

- [ ] **Step 1: Write the scenario**

Create `e2e/scenarios/crypt_boss_sevrin_wax.lua`:

```lua
-- Sevrin Wax, the Coffinwright: verifies the data-only three-phase encounter.
--
-- The parts worth asserting are the ones that fail quietly: a Rite that applies its
-- damage-reduction aura and never removes it would leave the boss unkillable, and adds
-- that outlive their summoner would pile up in the instance forever.

local BOSS_ENTRY = 81
local HUSK_ENTRY = 82
local ACOLYTE_ENTRY = 83
local RITE_OF_RISING = 240
local UNHALLOWED_FERVOR = 241

-- Boss home, from the design spec.
local BOSS_X, BOSS_Y, BOSS_Z = -24.0, 1.0, 0.0

GM.Worldport(1, 5.0, 1.0, 0.0, 3.14)
Assert(WaitUntil(function() return FindUnitByEntry(BOSS_ENTRY) ~= nil end, 15000, "boss spawns"),
	"Sevrin Wax should be spawned in the crypt")

local boss = FindUnitByEntry(BOSS_ENTRY)
Assert(GetLevel(boss) == 12, "boss should be level 12, got " .. tostring(GetLevel(boss)))

local maxHealth = GetMaxHealth(boss)
Log("boss max health = " .. tostring(maxHealth))
Assert(maxHealth > 2000 and maxHealth < 3500,
	"boss health should be in the tuned band, got " .. tostring(maxHealth))

-- He must be at his home position, not wandering: the summon coordinates in the
-- triggers are absolute, so a misplaced boss puts every add in the wrong room.
Assert(math.abs(GetPosX(boss) - BOSS_X) < 2.0 and math.abs(GetPosZ(boss) - BOSS_Z) < 2.0,
	"boss should stand at his home position")

-- Level up enough to survive a 2670 HP boss plus add pressure. Tripping the
-- double-threshold edge case would need a single ~940 damage hit, which is far outside
-- this game's damage scale at any level (the boss itself hits for 38-55), so a high
-- level is safe here. If he dies too fast to observe the phases, lower this.
GM.LevelUp(19)
GM.SetSpeed(2.0)

Assert(MoveTo(BOSS_X + 6.0, BOSS_Y, BOSS_Z, 30000), "should reach the boss")
TargetUnit(boss)
StartAttack(boss)

Assert(WaitUntil(function() return GetHealth(boss) < maxHealth end, 20000, "boss takes damage"),
	"the boss should enter combat and take damage")

-- Phase 2: the Rite fires at 65% and the choir arrives.
Assert(WaitUntil(function() return GetHealth(boss) <= maxHealth * 0.65 end, 120000, "boss reaches 65%"),
	"the boss should be brought below 65% health")

Assert(WaitUntil(function() return FindUnitByEntry(ACOLYTE_ENTRY) ~= nil end, 15000, "acolytes arrive"),
	"the Rite of Rising should summon Choirbound Acolytes")

-- The aura must come off again. A Rite that never ends is the one soft-lock in the design.
Assert(WaitUntil(function() return not HasAura(boss, RITE_OF_RISING) end, 20000, "Rite ends"),
	"Rite of Rising must be removed when the channel finishes")

-- The acolytes keep raising husks while they live.
Assert(WaitUntil(function() return FindUnitByEntry(HUSK_ENTRY) ~= nil end, 30000, "husks are raised"),
	"a Wax-Sealed Husk should be raised during the fight")

-- Phase 3.
Assert(WaitUntil(function() return GetHealth(boss) <= maxHealth * 0.30 end, 180000, "boss reaches 30%"),
	"the boss should be brought below 30% health")

Assert(WaitUntil(function() return HasAura(boss, UNHALLOWED_FERVOR) end, 20000, "boss enrages"),
	"Unhallowed Fervor should be applied in phase 3")

Assert(WaitUntil(function() return not HasAura(boss, RITE_OF_RISING) end, 20000, "second Rite ends"),
	"Rite of Rising must be removed after the phase 3 channel too")

-- Kill him.
Assert(WaitUntil(function() return not IsAlive(boss) end, 180000, "boss dies"),
	"the boss should die")
StopAttack()

-- Trigger 39 despawns leftover summons within ~10s of the boss dying.
Assert(WaitUntil(function()
		return FindUnitByEntry(HUSK_ENTRY) == nil and FindUnitByEntry(ACOLYTE_ENTRY) == nil
	end, 30000, "summons clean up"),
	"husks and acolytes must despawn after the boss dies")

-- Leave the shared stack clean for the next scenario.
GM.SetSpeed(1.0)
GM.Worldport(0, 0.0, 0.0, 0.0, 0.0)
Log("crypt_boss_sevrin_wax: done")
```

- [ ] **Step 2: Build what the harness needs**

```bash
cmake --build build -t e2e_client login_server realm_server world_server --config Debug
```

Expected: build succeeds. No C++ changed in this plan, so this should be a no-op unless the tree was already dirty.

- [ ] **Step 3: Run just this scenario**

```bash
powershell -File tools/e2e/e2e_run.ps1 -Scenario crypt_boss_sevrin_wax
```

Requires `$env:MMO_E2E_MYSQL_PASSWORD` set in the shell first. Expected: exit code 0, and `e2e/runtime/logs/summary.json` reporting the scenario as passed.

If it fails, read the JSONL transcript in `e2e/runtime/logs/` to see which assertion tripped. The most likely first failures and their causes:
- *boss spawns* times out → the spawn or map mirror from Task 5 did not apply.
- *acolytes arrive* times out → check the world server log for `TRIGGER_ACTION_SUMMON_CREATURE: Unknown creature entry 83`, meaning Task 4 did not apply before the fight.
- *Rite ends* times out → the `RemoveAura` action in trigger 30 is wrong; verify its `data` is `[240]` and its `target` is `1`.
- *boss reaches 65%* times out → he is regenerating faster than the test character deals damage, or `DetermineCombatBehavior` resolved to Caster and he is kiting. Check his distance from the player over time in the transcript.

- [ ] **Step 4: Run the full suite to confirm no cross-scenario damage**

```bash
powershell -File tools/e2e/e2e_run.ps1
```

Expected: exit code 0. A failure in a *different* scenario means this one left the instance dirty — check the `Worldport` back to map 0 and that no summons survived.

- [ ] **Step 5: Commit**

```bash
git add e2e/scenarios/crypt_boss_sevrin_wax.lua
git commit -m "test(e2e): cover the Sevrin Wax encounter phases and add cleanup"
```

---

### Task 7: Verification pass and documentation

**Files:**
- Modify: `docs/superpowers/specs/2026-08-12-crypt-necromancer-boss-design.md` (status line and any tuning corrections)
- Modify: `docs/protocol_versions.md` — **only if** the check below reports a problem

- [ ] **Step 1: Confirm nothing on the wire moved**

```bash
python tools/protocol_version_check.py
```

Expected: exit 0 with no problems. This plan adds no opcode and changes no payload, so a failure here means something unintended was touched.

- [ ] **Step 2: Run the XP audit**

```bash
python tools/xp_audit.py
```

Expected: the level bands stay at or above 120%. The boss adds 520 XP at level 12 and the adds deliberately add none. If the audit now reports the level 11–12 band as over-rich, reduce the boss XP rather than giving the adds any.

- [ ] **Step 3: Run the Python tool tests**

```bash
python tools/tests/test_apply_encounter_json.py
```

Expected: PASS, 7 tests.

- [ ] **Step 4: Confirm the client and server data agree**

```bash
python -c "
import hashlib
from pathlib import Path
for name in ('spells.data', 'maps.data'):
    a = hashlib.sha256(Path('data/editor/data', name).read_bytes()).hexdigest()
    b = hashlib.sha256(Path('data/client/ClientDB', name).read_bytes()).hexdigest()
    print(name, 'match' if a == b else 'MISMATCH')
"
```

Expected: both report `match`. A mismatch means a mirror step was skipped and the client will not see the new data.

- [ ] **Step 5: Manual play pass**

Launch the client and verify the four things data cannot assert. Record the outcome in the spec, correcting the spec if reality differs:

1. **He faces the entrance.** `rotation: 0.0` was inferred from the existing map 1 guard spawns; if he faces the apse wall, set `rotation` to `3.14159` and re-apply.
2. **He does not kite.** The rotation was balanced 2 ranged / 2 melee to force Melee behaviour. If he backs away from the tank, add a third melee-range spell entry.
3. **The Rite reads as a distinct moment** — the raid warning lands, he visibly stops, and the acolytes are findable in the aisles.
4. **Fight length is reasonable** at 2670 HP. Adjust `eliteStatMultiplier` if not.

- [ ] **Step 6: Bump the data submodule pointers in the main repo**

Tasks 2–5 committed inside the submodules; the main repo still points at the pre-encounter commits. Bump both now, in one commit, matching this repo's convention:

```bash
git -C data/editor log --oneline -1
git -C data/client log --oneline -1
git add data/editor data/client
git commit -m "Update data submodule refs (Sevrin Wax crypt boss: spells, triggers, units, loot, spawn)"
```

Verify the pointers now match the submodule heads:

```bash
git submodule status data/editor data/client
```

Expected: both lines start with a space (in sync), not `+` (pointer differs from checkout).

- [ ] **Step 7: Update the spec status and commit**

Change the spec's `Status:` line from `approved design, not yet implemented` to `implemented` and note any tuning value that changed during the play pass.

```bash
git add docs/superpowers/specs/2026-08-12-crypt-necromancer-boss-design.md
git commit -m "docs(spec): record the implemented Sevrin Wax tuning values"
```

- [ ] **Step 8: Run the gate**

Run `/gate`. It runs the protocol check, a Debug build, the unit tests and the full E2E suite, then reviews the branch diff. Do not merge without a green, HEAD-matching report; `/ship` refuses otherwise.

---

## Notes for the implementer

**`generated/` is not committed.** The JSON drafts are working files; only the resulting `.data` files and the two new source files are committed. Keep the drafts around until the play pass is done, since re-tuning means editing and re-applying them.

**Every apply script takes `--backup`.** It writes `<file>.data.bak` beside the original. Several `.bak` files already exist in `data/editor/data/` from previous sessions; do not commit new ones.

**If a step fails partway through a task**, the `.data` files are already partially written. Restore from the `.bak` file for that step, fix the draft, and re-apply — the appliers are id-keyed and idempotent, so re-running a corrected draft updates in place rather than duplicating.

---

## Correction log (found during execution)

- **Skill script paths are `.agents/skills/…`, not `.claude/skills/…`.** `.gitignore:67`
  ignores `.claude/*` with only four files force-tracked; the tracked home for these
  scripts is `.agents/skills/`, which is also what the skill's own documentation uses.
  The two trees are byte-identical mirrors on disk, so commands work from either path,
  but only the `.agents/` copy is committed. All paths in this plan were rewritten.
- **The Task 1 test code as originally written had a bug:** it called
  `load_modules(REPO_ROOT)` with a `str`, but `load_modules` does `project_root / "src"`,
  which raises `TypeError` on a string. Fixed by wrapping in `Path(...)`.
