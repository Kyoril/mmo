<required_reading>
Read these reference files now:

1. `references/npc-data-surface.md`
2. `references/npc-runtime-semantics.md`
3. `references/npc-json-format.md`
</required_reading>

<process>
1. Inspect the live creature and its dependencies with `scripts/inspect_npc_catalog.py`.
2. If the request is an edit, export the closest existing unit to JSON with `scripts/export_npc_json.py` instead of authoring from a blank sheet.
3. Review linked faction template, loot entry, vendor entry, trainer entry, gossip menus, triggers, conditions, quests, creature spells, and current map spawns before making changes.
4. If the request implies new item or spell content that does not already exist, invoke `mmo-item-designer` or `mmo-spell-designer` first and then continue with the confirmed IDs.
5. Only after the live data has been inspected should the workflow branch into interactable-NPC or combat-creature authoring.
</process>

<success_criteria>
- The closest live unit was inspected or cloned.
- Every linked dependency was surfaced before edits started.
- The draft is anchored to real project IDs instead of guesses.
</success_criteria>
