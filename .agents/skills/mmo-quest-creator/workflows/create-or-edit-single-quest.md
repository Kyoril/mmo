<required_reading>
Read these reference files now:
1. `references/quest-data-surface.md`
2. `references/quest-runtime-semantics.md`
3. `references/quest-json-format.md`
4. `references/quest-authoring-patterns.md`
</required_reading>

<process>
1. Start from a live reference quest of the same gameplay shape and export it, or create a fresh JSON document using the schema in `references/quest-json-format.md`.
2. Author the quest row itself:
   - title, internal name, narrative text, and objective text
   - level gates, class or race restrictions, and prerequisite chain fields
   - runtime-sensitive flags such as `StayAlive`, `Repeatable`, `Daily`, `Weekly`, `Exploration`, and `AutoRewarded`
   - `timelimit` when the quest should fail after a fixed number of seconds, including while the player is offline
   - objective requirements, keeping within the four-requirement runtime limit
   - reward XP, money, items, optional items, spells, or spell casts
3. Author the wiring in the same draft:
   - `providers.unit_ids` and `providers.object_ids`
   - `enders.unit_ids` and `enders.object_ids`
   - `gated_object_ids` for world objects that should only be usable while the quest is active
4. If the quest needs new supporting content, stop guessing and delegate:
   - new questgiver or target NPC: `mmo-npc-designer`
   - new quest item or reward item: `mmo-item-designer`
   - new source, reward, or objective spell: `mmo-spell-designer`
5. Validate with `scripts/validate_quest_json.py`.
6. Apply with `scripts/apply_quest_json.py` only after validation passes.
7. Re-inspect the resulting quest to verify the row and the wiring both landed.
</process>

<success_criteria>
This workflow is complete when:

- The quest row, provider wiring, and ender wiring all exist in one validated draft.
- Referenced items, spells, creatures, objects, classes, races, skills, triggers, and area triggers were confirmed from live data.
- The quest is reachable and turn-in capable after apply, not just present in `quests.data`.
</success_criteria>
