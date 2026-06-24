---
name: mmo-spell-designer
description: Inspects, designs, validates, exports, and applies MMO spell data for F:\\mmo using the live protobuf project files. Use when creating a new spell, editing an existing spell, reviewing aura or proc behavior, checking race or class restrictions, or tracing spell dependencies such as items, visualizations, categories, proficiencies, and starting spell sources.
---

<objective>
Create or edit MMO spells as data instead of code. This skill uses the live project files under `data/editor/data/` as the source of truth, reads the spell protobuf schema and related catalogs, and helps an agent produce valid spell definitions that match the runtime behavior implemented by the server.

The skill is optimized for the actual spell system in this repository: passive spells, item `OnEquip` spells, multi-effect spells, auras, periodic effects, proc-triggered spells, class and race masks, reagent requirements, visualization references, stacking behavior, trainer-taught spells, and talent-granted spells.
</objective>

<quick_start>
Inspect the live spell and its dependencies first:

```powershell
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root F:\mmo --spell-id 1337 --pretty
```

Clone an existing spell into an editable JSON draft:

```powershell
python .agents/skills/mmo-spell-designer/scripts/export_spell_json.py --project-root F:\mmo --spell-id 1337 --output F:\mmo\generated\spells\my_spell.json
```

Validate the draft against live data before applying it:

```powershell
python .agents/skills/mmo-spell-designer/scripts/validate_spell_json.py F:\mmo\generated\spells\my_spell.json --project-root F:\mmo
```

Apply the validated draft back into `data/editor/data/spells.data` with an automatic backup:

```powershell
python .agents/skills/mmo-spell-designer/scripts/apply_spell_json.py F:\mmo\generated\spells\my_spell.json --project-root F:\mmo --backup
```

Grant the resulting spell to a class at a chosen level:

```powershell
python .agents/skills/mmo-spell-designer/scripts/grant_class_spell.py --project-root F:\mmo --class-name Scout --spell-id 168 --level 10
```

Inspect class trainers and their current teachable spells:

```powershell
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root F:\mmo --section trainers --class-name Scout --pretty
```

Add or update a trainer spell entry so players can learn the new spell from a trainer:

```powershell
python .agents/skills/mmo-spell-designer/scripts/upsert_trainer_spell.py --project-root F:\mmo --trainer-id 42 --spell-id 168 --cost 1500 --required-level 10
```

Inspect class talent tabs and current talent placements:

```powershell
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root F:\mmo --section talents --class-name Scout --pretty
```

Create or update a talent entry that points at the new rank spell:

```powershell
python .agents/skills/mmo-spell-designer/scripts/upsert_talent_entry.py --project-root F:\mmo --talent-id 900 --tab-id 3 --row 1 --column 2 --rank-spell-id 169
```

Add or remove spell ranks on an existing talent entry without rebuilding the whole talent manually:

```powershell
python .agents/skills/mmo-spell-designer/scripts/edit_talent_ranks.py --project-root F:\mmo --talent-id 900 --add-rank-spell-id 170
python .agents/skills/mmo-spell-designer/scripts/edit_talent_ranks.py --project-root F:\mmo --talent-id 900 --remove-rank-spell-id 169
```

Create and import a new spell icon from generated PNG art:

```powershell
powershell -ExecutionPolicy Bypass -File .agents/skills/mmo-item-designer/scripts/normalize_item_icon.ps1 `
  -InputPath C:\Users\RobinKlimonow\.codex\generated_images\<session>\<image>.png `
  -OutputPath F:\mmo\generated\spells\my_spell_icon.png

python .agents/skills/mmo-spell-designer/scripts/import_spell_icon.py `
  F:\mmo\generated\spells\my_spell_icon.png `
  F:\mmo\data\client\Interface\Icons\Spells\My_Spell_Icon.htex
```
</quick_start>

<context>
- Treat `data/editor/data/*.data` as authoritative for authoring.
- Treat `src/shared/proto_data/*.proto` as the schema contract.
- Treat `src/shared/game_server/spells/` and `src/shared/game_server/objects/` as the runtime semantics contract.
- Prefer cloning the closest existing spell instead of authoring from a blank sheet when the request resembles an existing spell family or aura pattern.
</context>

<process>
1. Read `references/spell-data-surface.md` and `references/spell-runtime-semantics.md`.
2. Inspect live project data with `scripts/inspect_spell_catalog.py`. Confirm spell IDs, item IDs, class and race restrictions, proficiencies, categories, stacking categories, visualization IDs, reagent items, linked trigger spells, trainer availability, and any talent references.
3. For edits, export the closest existing spell to JSON with `scripts/export_spell_json.py` and modify the draft. For new spells, either export a similar spell as a template or author a fresh JSON wrapper with the same field names as `SpellEntry`.
4. Validate the draft with `scripts/validate_spell_json.py`. Fix every reported problem before applying.
5. Apply the JSON draft with `scripts/apply_spell_json.py` only after validation passes.
6. If the spell is class-granted, add or update the class spell grant with `scripts/grant_class_spell.py`.
7. If the spell should be trainer-taught, inspect `trainers.data` and update the target trainer with `scripts/upsert_trainer_spell.py` or `scripts/remove_trainer_spell.py`.
8. If the spell is talent-granted, inspect `talents.data` / `talent_tabs.data` and add or update the talent with `scripts/upsert_talent_entry.py` or adjust ranks with `scripts/edit_talent_ranks.py`.
9. When a spell needs a brand-new icon, generate square source art with `image_gen`, normalize it to `128x128`, and import it into `data/client/Interface/Icons/Spells/` with `scripts/import_spell_icon.py`. Prefer fully opaque icons unless transparency is intentionally part of the design.
10. After applying, inspect the resulting spell again and verify the critical references, effect wiring, icon path, trainer teachability, and talent placement if applicable.
</process>

<validation>
- Never invent referenced IDs. Confirm them from live project data.
- Validate every draft before applying it.
- For new icon work, the final shipped spell icon must come from `image_gen` output normalized to `128x128`, not from copied project art or a placeholder.
- For trainer work, verify trainer ID, class linkage, cost, level gates, and optional skill requirements from live data before writing.
- For talent work, verify the talent tab, row, column, and every rank spell ID from live data before writing.
- When a spell uses `ApplyAura` or `ApplyAreaAura`, verify the aura type, targets, duration, amplitude, proc fields, and removal conditions together.
- For `aura_type::ModStat` and `aura_type::ModStatPct`, treat `effect.miscvaluea` as the spell editor stat index from `spell_editor_window.cpp` and `GameUnitS::GetUnitModByStat`, not as `proto::StatConstants.StatType`. The correct mapping is `0=Stamina`, `1=Strength`, `2=Agility`, `3=Intellect`, `4=Spirit`.
- When a spell is passive or item-driven, verify how it becomes active at runtime instead of assuming player manual casting.
- When a spell triggers another spell, verify both the trigger spell reference and the proc conditions.
</validation>

<anti_patterns>
<pitfall name="ignoring_runtime_semantics">
Do not treat `spells.proto` as sufficient by itself. Field validity is only half the problem; the runtime handlers in `spell_effects.cpp`, `aura_effect.cpp`, `aura_container.cpp`, `single_cast_state.cpp`, and unit equip or learn flows determine whether a data design actually behaves as intended.
</pitfall>

<pitfall name="invented_references">
Do not guess class masks, race masks, reagent items, visualization IDs, or trigger spell IDs. Inspect the live catalogs first.
</pitfall>

<pitfall name="blank_slate_design">
Do not design complicated proc or aura spells from scratch unless no close reference exists. Clone the nearest live spell and change the minimal set of fields.
</pitfall>

<pitfall name="passive_vs_manual_confusion">
Do not mark a spell passive unless it should automatically activate when the unit learns it or is recreated. Item `OnEquip` casts are also automatic, but they are driven by item spell triggers rather than the passive attribute.
</pitfall>
</anti_patterns>

<reference_guides>
Read these references before substantial spell work:

- `references/spell-data-surface.md`
- `references/spell-runtime-semantics.md`
- `references/spell-authoring-workflow.md`
</reference_guides>

<success_criteria>
This skill is being used correctly when:

- The agent inspected live spell data before proposing IDs or masks.
- Spell drafts use the real `SpellEntry` field names and valid references.
- Validation passes before any apply step.
- New spell icons are normalized to `128x128`, imported as `.htex`, and assigned via the spell's `icon` field using an asset-root-relative path.
- Trainer and talent changes are anchored to the real `trainers.data`, `talents.data`, and `talent_tabs.data` entries instead of guessed IDs.
- Passive, item-driven, aura, periodic, and proc semantics were checked against the runtime behavior in this repository.
- The resulting spell can be iterated as data without touching C++ code unless the requested effect is genuinely unsupported by the current runtime handlers.
</success_criteria>
