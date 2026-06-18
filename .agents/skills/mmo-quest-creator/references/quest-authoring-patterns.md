<overview>
This file captures the live authoring patterns already present in the repository and a few grounded heuristics for reward and chain design.
</overview>

<live_patterns>
<pattern name="simple_kill_quest">
Quest `1` `The Boar Problem`, quest `5` `Boars in the Thickets`, quest `6` `The Alpha of the Herd`, and quest `10` `Rat Troubles` show the baseline kill-quest pattern:

- provider and ender on the same NPC
- one `creatureid + creaturecount` requirement
- text tuned to geography and stakes
- modest XP and money scaling by level
</pattern>

<pattern name="item_collection_with_gated_object">
Quest `2` `Gathering Healing Herbs` and quest `9` `A Crate Too Far` show the current object-gated collection pattern:

- quest requires a normal item count
- the item is sourced from quest-gated world objects
- the world objects use `requiredquest`
- the quest itself does not need an object counter
</pattern>

<pattern name="handoff_or_notification">
Quest `3`, `4`, `7`, and `21` are delivery or trainer handoff quests:

- no counter-based requirement rows
- narrative text moves the player to a new NPC
- a different unit ends the quest
- these quests are ideal for chain transitions or class splits
</pattern>

<pattern name="scripted_completion">
Quest `8` and quest `22` use display-text-only objectives and depend on triggers for the real gameplay logic:

- the quest row communicates the objective to the player
- triggers handle the gameplay sequence
- completion happens through a trigger path that calls `CompleteQuest`
</pattern>

<pattern name="area_trigger_completion">
Quest `20` demonstrates region discovery by linking an area trigger to a trigger row that completes the quest.
</pattern>

<pattern name="multi_item_turn_in">
Quest `19` demonstrates the current multi-objective item turn-in pattern:

- two item requirements
- no exotic scripting
- modest reward scaling for a low-level civic or labor quest
</pattern>

<pattern name="timed_or_failure_sensitive_quest">
Timed quests and `StayAlive` quests should be authored with explicit failure narration and recovery intent:

- use `timelimit` only when the failure state is part of the content, not as a generic urgency decoration
- if failure matters to story flow, wire `failtriggers`
- remember that a timed quest can fail after objectives are complete but before turn-in
- `StayAlive` now really fails on player death, so only use it when the content can tolerate that friction
</pattern>

<pattern name="repeatable_quest">
Repeatable content now supports three variants:

- `Repeatable`: instantly available again after reward
- `Daily`: available again after the configured daily reset
- `Weekly`: available again after the configured weekly reset

Prefer `Repeatable` for training, testing, or endlessly farmable civic tasks. Prefer `Daily` or `Weekly` only when you actually want a global cadence.
</pattern>
</live_patterns>

<reward_guidance>
The quest editor's current XP helper in `src/mmo_edit/editor_windows/quest_editor_window.cpp` is heuristic, not authoritative. It uses:

- a level-based base percent of the next-level XP budget
- a coarse difficulty multiplier from `suggestedplayers`
- rounding to the nearest 5 XP

Use it as a starting point, not as the final truth. Compare against nearby live quests at the same level band.
</reward_guidance>

<reward_examples>
Observed live XP examples:

- level `1` simple kill quest: `175` XP
- level `1` basic gather quest: `250` XP
- level `3` larger kill quest: `300` to `400` XP
- level `5` final solo kill quest: `650` XP
- level `10` scripted class challenge: `1145` XP

Observed live item reward patterns:

- small consumable stacks for early collection quests
- one standout equipment reward for a capstone quest beat
- no reward choice items in the current live data yet, but the schema supports them
</reward_examples>

<chain_guidance>
When extending a questline:

- use `prevquestid` for hard unlock order
- use handoff quests to move players between hubs, trainers, or thematic beats
- use `nextchainquestid` when you want the same questgiver to immediately surface the follow-up after turn-in
- keep each step's reward proportional to the player's travel, combat, and scripting friction
- change the quest texture between steps instead of repeating the same kill count on a stronger mob without narrative or mechanical change
</chain_guidance>

<anti_patterns>
<pitfall name="quest_row_only">
Do not stop after creating `QuestEntry`. A quest without providers or enders is dead data.
</pitfall>

<pitfall name="fake_exploration">
Do not set `Exploration` and assume the engine will detect discovery automatically. Wire an area trigger or a scripted completion trigger.
</pitfall>

<pitfall name="object_counter_assumption">
Do not design a "click this object five times" quest unless you also provide a spell-cast or explicit trigger path that actually increments or completes it.
</pitfall>

<pitfall name="unused_starttriggers">
Do not put important start behavior into `starttriggers` and assume it will fire. Use provider unit triggers on `OnQuestAccept` instead.
</pitfall>

<pitfall name="auto_reward_assumption">
Do not author a quest as if `AutoRewarded` guarantees automatic turn-in. Verify the runtime path or add explicit custom handling.
</pitfall>
</anti_patterns>
