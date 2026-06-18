<required_reading>
Read these reference files now:

1. `references/npc-data-surface.md`
2. `references/npc-runtime-semantics.md`
3. `references/npc-json-format.md`
4. `references/npc-authoring-workflow.md`
</required_reading>

<process>
1. Pick the closest existing combat creature and clone it whenever possible, especially for stat-system, spell, or loot patterns.
2. Choose the faction template first because it determines whether the unit is friendly, neutral, or hostile to players and other units.
3. Default to `useStatBasedSystem = true` and select a real `unitClassId` from live data unless the request is explicitly legacy.
4. Decide whether the creature is passive, neutral, or aggressive through faction setup, movement, triggers, and combat spell choices rather than through prose alone.
5. Configure combat abilities with existing spell IDs. Confirm range, passives, and proc behavior against the spell system. If the creature needs new abilities, invoke `mmo-spell-designer`.
6. Configure loot through `unit.unitlootentry` and the linked `loot_entry`. Validate every referenced item ID. If the loot table needs new items, invoke `mmo-item-designer`.
7. Use `script_name` only when the name is actually registered in the current C++ runtime. Otherwise leave it empty and rely on default AI plus triggers and spells.
8. Validate the draft with `scripts/validate_npc_json.py`.
</process>

<success_criteria>
- Faction, stat-system, spell, and loot choices are anchored to live project data.
- Unknown combat script names were not invented.
- Spell and loot dependencies use confirmed existing IDs or were created through the dedicated item or spell skills first.
- The JSON validates cleanly before apply.
</success_criteria>
