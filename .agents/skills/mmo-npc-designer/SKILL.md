---
name: mmo-npc-designer
description: Inspects, designs, validates, exports, and applies MMO creature and NPC data for F:\mmo using the live protobuf project files. Use when creating or editing questgivers, vendors, trainers, civilians, hostile creatures, bosses, loot-bearing enemies, faction setups, gossip-driven NPCs, or creature map spawns.
---

<essential_principles>
Treat `data/editor/data/*.data` as the authoring source of truth, `src/shared/proto_data/*.proto` as the schema contract, and the world-server creature, AI, loot, gossip, and faction code as the runtime contract.

Do not infer interactable behavior from `UnitEntry.npcflags` alone. Runtime NPC flags are recomputed from linked data such as `trainerentry`, `vendorentry`, `gossip_menus`, `quests`, and `end_quests`.

Do not guess IDs for factions, faction templates, models, spells, triggers, items, gossip menus, trainers, vendors, or loot tables. Inspect live project data first.

When a creature needs new items for loot or vendor stock, invoke `mmo-item-designer` and then wire the confirmed item IDs back into the NPC data.

When a creature needs new active abilities, passives, proc auras, or trainer-taught spells, invoke `mmo-spell-designer` and then wire the confirmed spell IDs back into the NPC data.

Default to the stat-based creature system unless the request is explicitly cloning legacy behavior. Most live units in this repository already use `useStatBasedSystem = true`.
</essential_principles>

<objective>
Create or edit MMO creatures and NPCs as data instead of code. This skill is designed for the actual creature pipeline in this repository: `units.data` for the base unit row, `unit_loot.data` for drops, `vendors.data` and `trainers.data` for services, `gossip_menus.data` plus `conditions.data` and `triggers.data` for interaction logic, `maps.data` for spawns, and `faction_templates.data` for hostility and friendliness.

The skill supports both major authoring modes in this project: interactable NPCs such as questgivers, vendors, trainers, binders, and gossip-driven civilians; and combat creatures such as neutral wildlife, aggressive mobs, dungeon enemies, scripted bosses, and loot-bearing elites.
</objective>

<quick_start>
Inspect live NPC data and related catalogs first:

```powershell
python .agents/skills/mmo-npc-designer/scripts/inspect_npc_catalog.py --project-root F:\mmo --unit-id 10 --pretty
```

Clone an existing creature or NPC into an editable JSON draft:

```powershell
python .agents/skills/mmo-npc-designer/scripts/export_npc_json.py --project-root F:\mmo --unit-id 10 --output F:\mmo\generated\npcs\warrior_trainer.json
```

Validate the draft against live project data:

```powershell
python .agents/skills/mmo-npc-designer/scripts/validate_npc_json.py F:\mmo\generated\npcs\warrior_trainer.json --project-root F:\mmo
```

Apply the validated draft back into project data:

```powershell
python .agents/skills/mmo-npc-designer/scripts/apply_npc_json.py F:\mmo\generated\npcs\warrior_trainer.json --project-root F:\mmo --backup
```

Apply spawn updates from the same JSON only when intended:

```powershell
python .agents/skills/mmo-npc-designer/scripts/apply_npc_json.py F:\mmo\generated\npcs\warrior_trainer.json --project-root F:\mmo --apply-spawns --backup
```
</quick_start>

<routing>
Route directly from the user request. Do not stop to ask which workflow to use unless the request is genuinely ambiguous.

- Requests to inspect, summarize, compare, or clone an existing NPC or creature: follow `workflows/inspect-or-clone.md`.
- Requests for vendors, trainers, questgivers, binders, gossip NPCs, or mixed service NPCs: follow `workflows/create-interactable-npc.md`.
- Requests for mobs, beasts, humanoid enemies, elites, dungeon creatures, bosses, scripted enemies, or loot-bearing combatants: follow `workflows/create-combat-creature.md`.
- Requests that include placement, respawns, patrols, or map-specific spawn edits: also follow `workflows/apply-and-spawn-npc.md`.

If the creature needs both interaction and combat behavior, combine the relevant workflows instead of forcing it into one category.
</routing>

<reference_guides>
Read these references as directed by the active workflow:

- `references/npc-data-surface.md`
- `references/npc-runtime-semantics.md`
- `references/npc-json-format.md`
- `references/npc-authoring-workflow.md`
</reference_guides>

<workflows_index>
| Workflow | Purpose |
|----------|---------|
| `workflows/inspect-or-clone.md` | Inspect live NPC data, dependencies, and current spawns before editing. |
| `workflows/create-interactable-npc.md` | Author questgivers, vendors, trainers, gossip NPCs, and mixed service NPCs. |
| `workflows/create-combat-creature.md` | Author hostile, neutral, passive, elite, or scripted combat creatures with loot and spells. |
| `workflows/apply-and-spawn-npc.md` | Validate, apply, and optionally place or update creature spawns on maps. |
</workflows_index>

<success_criteria>
This skill is being used correctly when:

- The agent inspected live unit, faction, model, loot, spell, gossip, and spawn data before proposing IDs.
- Interactable behavior is driven by linked trainer, vendor, gossip, and quest data instead of guessed `npcflags`.
- Hostility decisions are anchored to live faction templates and factions.
- Combat spell, auto-attack, stat-system, loot, trigger, and script choices are checked against the runtime behavior in this repository.
- Every JSON draft validates before apply.
- Item and spell dependencies are delegated to `mmo-item-designer` and `mmo-spell-designer` when new supporting data is actually needed.
</success_criteria>
