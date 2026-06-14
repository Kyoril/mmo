---
name: mmo-quest-creator
description: Inspects, designs, validates, exports, and applies MMO quest data for F:/mmo using the live protobuf project files. Use when creating or editing single quests, building or extending quest chains, wiring questgivers and turn-in NPCs, authoring exploration or scripted quest flows, balancing quest rewards, or fixing broken quest dependencies.
---

<essential_principles>
Treat `data/editor/data/quests.data` as only one part of quest authoring. A quest is not reachable or completable until its provider and turn-in wiring are also correct in `units.data` or `objects.data`, and many advanced flows also require `triggers.data`, `area_triggers.data`, or quest-gated world objects in `objects.data`.

Do not rely on `QuestEntry.starttriggers`. The field exists in `quests.proto`, but the runtime currently does not execute it anywhere. Quest-start behavior in live content is driven through questgiver unit triggers using `OnQuestAccept` or by custom Lua/C++ logic.

Do not assume the `Exploration` quest flag is self-sufficient. `GamePlayerS::OnQuestExploration` is currently a TODO. Exploration and scripted completion in this project work when an area trigger or another trigger path eventually fires `QuestEventOrExploration`, which calls `CompleteQuest`.

Do not model pure object-use quests as if the engine had a native "use object N times" counter. The current runtime increments object objective counters through `OnQuestSpellCastCredit` for spell-cast-on-object requirements, or through an explicit `CompleteQuest` trigger/script path. If a quest requirement uses `objectid` without a matching spell/trigger path, validate the design carefully.

Default to cloning the closest live quest and editing the minimal set of fields instead of inventing quest data from scratch. Existing quests in this repository already cover handoff quests, kill quests, item collection quests, multi-objective turn-ins, trigger-completed scripted quests, and area-trigger exploration quests.

When a quest needs new supporting units, items, or spells, invoke `mmo-npc-designer`, `mmo-item-designer`, or `mmo-spell-designer` first, then wire the confirmed IDs back into the quest draft.
</essential_principles>

<objective>
Create or edit MMO quests as data instead of code. This skill is designed for the actual quest system in this repository: `quests.data` for the quest row itself, `units.data` and `objects.data` for provider and turn-in wiring, `objects.data` for quest-gated interactables, `triggers.data` plus `area_triggers.data` for scripted or exploration completion, and the runtime behavior in `game_player_s.cpp`, `player_npc_handlers.cpp`, `player.cpp`, and `trigger_handler.cpp`.

The skill supports both isolated quests and multi-step questlines. It can be used to create new quests, extend existing chains, fix broken reachability or turn-in wiring, rebalance rewards, and diagnose why a quest fails to progress at runtime.
</objective>

<quick_start>
Inspect a live quest and its dependencies first:

```powershell
python .agents/skills/mmo-quest-creator/scripts/inspect_quest_catalog.py --project-root F:/mmo --quest-id 22 --pretty
```

Clone an existing quest into an editable JSON draft:

```powershell
python .agents/skills/mmo-quest-creator/scripts/export_quest_json.py --project-root F:/mmo --quest-id 22 --output F:/mmo/generated/quests/lessons_in_steel.json
```

Validate the draft against live project data before applying it:

```powershell
python .agents/skills/mmo-quest-creator/scripts/validate_quest_json.py F:/mmo/generated/quests/lessons_in_steel.json --project-root F:/mmo
```

Apply the validated draft back into quest, linkage, trigger, and area-trigger data with backups:

```powershell
python .agents/skills/mmo-quest-creator/scripts/apply_quest_json.py F:/mmo/generated/quests/lessons_in_steel.json --project-root F:/mmo --backup
```
</quick_start>

<routing>
Route directly from the user request.

- Requests to inspect, summarize, debug, compare, or clone an existing quest: follow `workflows/inspect-or-clone-quest.md`.
- Requests to create or edit one quest, including provider or turn-in fixes: follow `workflows/create-or-edit-single-quest.md`.
- Requests to build, extend, or rebalance a quest chain, class questline, or campaign slice: follow `workflows/build-or-extend-questline.md`.
- Requests that involve exploration triggers, scripted completion, healing or spell-cast credit, wave encounters, or area-trigger flows: also follow `workflows/wire-scripted-or-exploration-quest.md`.

If the quest combines ordinary objectives with scripted completion, combine the relevant workflows instead of forcing the request into one bucket.
</routing>

<reference_guides>
Read these references as directed by the active workflow:

- `references/quest-data-surface.md`
- `references/quest-runtime-semantics.md`
- `references/quest-json-format.md`
- `references/quest-authoring-patterns.md`
</reference_guides>

<workflows_index>
| Workflow | Purpose |
|----------|---------|
| `workflows/inspect-or-clone-quest.md` | Inspect live quest data, dependencies, wiring, and nearby examples before editing. |
| `workflows/create-or-edit-single-quest.md` | Author or revise one quest row plus its provider, turn-in, and reward wiring. |
| `workflows/build-or-extend-questline.md` | Build or extend a multi-step questline with chain dependencies and reward pacing. |
| `workflows/wire-scripted-or-exploration-quest.md` | Wire triggers, area triggers, spell-cast credit, object gating, and scripted completion paths. |
</workflows_index>

<success_criteria>
This skill is being used correctly when:

- The agent inspected live quest, provider, turn-in, item, trigger, and area-trigger data before proposing IDs or flow changes.
- The quest draft includes provider and ender wiring instead of assuming `quests.data` alone makes the quest available.
- Exploration and scripted quests are backed by a real completion path that the current runtime executes.
- The draft respects the current quest runtime constraints, especially the four-objective counter limit and the lack of native object-use counters.
- Validation passes before any apply step.
- Supporting NPC, item, and spell dependencies are confirmed from live project data or delegated to the dedicated skills.
</success_criteria>
