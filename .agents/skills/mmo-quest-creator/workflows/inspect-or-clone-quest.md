<required_reading>
Read these reference files now:
1. `references/quest-data-surface.md`
2. `references/quest-runtime-semantics.md`
3. `references/quest-json-format.md`
</required_reading>

<process>
1. Inspect the target quest with `scripts/inspect_quest_catalog.py`. Read the quest row, provider units or objects, turn-in units or objects, gated world objects, related triggers, and area triggers before making any change proposal.
2. If the user is editing an existing quest, export it to JSON with `scripts/export_quest_json.py` and modify that draft instead of rebuilding the quest shape from memory.
3. Compare the target quest with the closest live examples by objective type:
   - kill quest: quest `1`, `5`, `6`, or `10`
   - item collection quest: quest `2`, `9`, or `19`
   - handoff quest: quest `3`, `4`, `7`, or `21`
   - scripted trigger completion: quest `8` or `22`
   - area-trigger exploration completion: quest `20`
4. Call out the real dependency graph before editing:
   - which NPC or object offers the quest
   - which NPC or object ends it
   - which items, creatures, objects, spells, skills, races, or classes it references
   - which trigger or area-trigger path completes it if it is scripted or exploratory
5. Export to JSON when the next step is an actual edit, fix, or extension. Keep the exported draft authoritative for wiring by updating its provider, ender, gated object, trigger, and area-trigger sections instead of planning to patch those by hand later.
</process>

<success_criteria>
This workflow is complete when:

- The live quest row and its wiring were inspected first.
- The agent identified the closest existing content pattern instead of treating the quest as novel by default.
- The edit path is grounded in an exported JSON draft or a clearly described new JSON document shape.
</success_criteria>
