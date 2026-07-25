<required_reading>
Read these reference files now:
1. `references/quest-runtime-semantics.md`
2. `references/quest-data-surface.md`
3. `references/quest-json-format.md`
4. `references/quest-authoring-patterns.md`
</required_reading>

<process>
1. Decide which runtime progression/completion path the quest actually uses:
   - area trigger entering a region → `QuestExplorationCredit` (partial credit) or
     `QuestEventOrExploration` (full scripted completion)
   - provider or encounter unit trigger on `OnQuestAccept`, `OnKilled`, `OnSpellHit`, or another event
   - plain object-use credit through `objectid + objectcount` (spellcast 0), optionally with the
     object's `OnInteraction` triggers for extra scripted behavior (say lines, summons, spawn state)
   - spell-cast-on-object credit through `spellcast + objectid`
   - escort/failure conditions through `QuestFailQuest` (e.g. `OnKilled` trigger on the escortee)
   - fail or reward triggers after state changes
   - custom Lua or C++ code only when data-driven paths are insufficient
2. Author or export the quest JSON draft and add any required:
   - `attached_triggers`
   - `attached_area_triggers`
   - `gated_object_ids`
3. Keep the runtime caveats in mind:
   - `starttriggers` are not executed by the current runtime
   - the `Exploration` flag alone detects nothing; some trigger must fire `QuestExplorationCredit`
     or `QuestEventOrExploration`
   - mixed quests (counters + investigate objective) must use `QuestExplorationCredit`, because
     `QuestEventOrExploration` force-fills every counter
4. Prefer the live patterns already present in the repository:
   - quest `8` for trigger-driven heal-credit flow
   - quest `20` for area-trigger completion
   - quest `22` for scripted arena progression and completion trigger
5. Validate the entire draft, including attached trigger and area-trigger rows.
6. Apply and then re-inspect the linked trigger rows to verify that all referenced IDs now exist and still point at the intended quest.
</process>

<success_criteria>
This workflow is complete when:

- The scripted or exploration quest has a real completion path in current runtime terms.
- Every trigger and area-trigger reference exists and points at the intended quest.
- The design does not depend on unimplemented or unused quest fields.
</success_criteria>
