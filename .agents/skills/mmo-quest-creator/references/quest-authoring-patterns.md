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

<pattern name="escort_follow_quest">
Quest `56` `Out of the Barrowfield` is the canonical follow-driven escort:

- the escortee (unit `80`, named spawn `Barrowfield - Surveyor Wick Farrow`) is itself the
  questgiver; quest flags combine `Exploration` + `AutoRewarded`, with one text-only
  requirement row for the log line
- an `OnQuestAccept` trigger on the escortee fires `Say` + `SetFollowTarget`, so the NPC
  follows the accepting player
- an area trigger at the destination fires `QuestExplorationCredit` for the triggering
  player only (safe for passersby — it is a no-op without the quest)
- the quest's `rewardtriggers` handle the goodbye: `Say` + `ClearFollowTarget` + `Delay` +
  `Despawn` on the named spawn (reward triggers run with the rewarded player as context,
  so they never fire for uninvolved players)
- an `OnKilled` trigger on the escortee fires `QuestFailQuest` targeting `NearestPlayer`

Placement rules learned the hard way: spawn the escortee OUTSIDE ambient aggro range of
nearby hostiles (roughly 25+ units), or wandering mobs kill it on a respawn loop with no
player involved. The danger belongs on the escort ROUTE, not at the pickup point.

Known limitations: a second player accepting the quest mid-escort re-targets the follow to
themselves (there is no "is already being escorted" gate yet), and the `NearestPlayer` fail
target has no range cap — if the escorter runs far ahead when the escortee dies, the fail can
attribute to whichever player happens to be nearest in the instance (harmless unless that
player also carries the quest).
</pattern>

<pattern name="talk_a_creature_into_a_fight">
Quest `79` `The King's Toll` (Tollmaster Brask, unit `129`): a friendly npc the player talks to,
who turns hostile depending on what is said.

- the npc is friendly and carries a gossip menu; its options have a gossip **condition** on the
  quest being incomplete, so players without the quest never see them
- each option is of type `Trigger`; an `OnGossipAction` trigger on the npc answers it
- "it comes to blows": `SetFaction` with a hostile faction template and *attack triggering unit*,
  then `SetSpawnState` for helpers kept on inactive, non-respawning spawners whose entry carries
  `Despawn On Reset` (trigger `17`). Set the faction FIRST - helpers that spawn next to a still
  friendly npc attack it
- "it does not": `QuestKillCredit` for the npc's own entry, `MoveTo`, `Despawn`. The quest
  requirement is a plain kill of the npc, so both endings complete it
- a coin toss between two outcomes is a `RandomTrigger` over two triggers

The faction override ends by itself when the creature resets or respawns.
</pattern>

<pattern name="lead_creatures_back">
Quest `82` `Pigs in the Corn`: "bring N of them home" without an escort per creature.

- the quest hands out an item (`srcitemid`) whose spell has a single `Dummy` effect on a friendly
  target; the creatures carry an `OnSpellHit` trigger for that spell that runs `SetFollowTarget`
- an area trigger at the destination runs `DeliverFollowers` for the creature entry: every one that
  follows the triggering player within the radius gives a quest kill credit and despawns
- the requirement is `creatureid + creaturecount` for that entry
- the creatures need a faction that is friendly to players (or the spell cannot target them) and
  that no hostile creature nearby counts as an enemy (or they are killed on the way): faction
  template `13` `Livestock, Friendly`
- give the radius room (25): the server's position of a follower trails the client's view
</pattern>

<pattern name="event_only_for_players_on_the_quest">
Quest `81` (ambush on the road) and `87` (a scene to overhear): an area trigger whose trigger has a
**condition** on the `QuestStatus` function, so it does nothing for passers-by.

- `QuestStatus(questId) == 3` (incomplete) for a quest with open objectives
- a quest WITHOUT objectives is complete (`1`) from the moment it is accepted; its events need
  `QuestStatus == 3 OR QuestStatus == 1`
- flag the trigger `OnlyOneInstance`
- for an overheard scene use `Emote` actions (50 m) rather than `Say` (25 m): the player has to
  stand outside aggro range (about 20 m), which is nearly out of earshot of a say
- grant the credit with `QuestExplorationCredit` after the last line; the context keeps the
  triggering player across `Delay` actions
</pattern>

<pattern name="question_npcs_with_dialogue_pages">
Quest `90` `The Chandler's Customers`: "ask these three people".

- each npc gets a main gossip menu with a `GossipMenu` option that opens a page with the answer,
  and on that page a `Trigger` option; an `OnGossipAction` trigger for `[page id, option id]`
  grants `QuestKillCredit` for a unit entry that is never spawned (its name is the log line:
  `Wainwright's Account`)
- options on sub pages are accepted by the server only when the page can be reached from the
  npc's menu through options whose conditions the player meets
- for a vendor, put the quest condition on the MENU (`GossipMenuEntry.conditionId`): without the
  quest the menu is not offered at all and the vendor window opens directly, as before
</pattern>

<pattern name="object_starts_an_event">
Quest `83` `The Scarecrow Watch`: using an object starts a fight in waves.

- a `QuestObject` with `requiredquest` but NO object counter in the quest: it stays usable while
  the quest is incomplete, so a player who died can start the event again
- its `OnInteraction` trigger (`OnlyOneInstance`) summons the waves with `SummonCreature` and
  `Delay`, then switches on the inactive spawner of the named creature with `SetSpawnState`
- summons grant no experience, loot or quest credit: whatever the quest counts must come from a
  spawner
- the object is gone after its use (quest objects are consumed), so the trigger continues without
  an owner; give `SummonCreature` explicit coordinates
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

<pitfall name="object_counter_spellcast_mixup">
Object-use counters are native (`objectid + objectcount`, `spellcast` 0 — credited on plain use). But a requirement WITH a `spellcast` id is only credited by casting that spell on the object; plain use does nothing for it. Pick the shape that matches the design.
</pitfall>

<pitfall name="unused_starttriggers">
Do not put important start behavior into `starttriggers` and assume it will fire. Use provider unit triggers on `OnQuestAccept` instead.
</pitfall>

<pitfall name="auto_reward_choice_items">
Do not combine `AutoRewarded` with choice rewards — the runtime ignores the flag and falls back to manual turn-in (the validator rejects this combination). For auto-rewarded quests granting fixed reward ITEMS, still wire an ender NPC so a full-bags player is not stuck until the login-time retry.
</pitfall>

<pitfall name="open_spell_needs_peace">
Every world object is used through the Open spell: a 5 second cast that cannot be started in
combat. Do not place a quest object inside the aggro range of a hostile spawn unless killing it
first is the point, and do not ask for twenty of them.
</pitfall>

<pitfall name="hostiles_next_to_townsfolk">
Creatures attack npcs of an enemy faction as readily as players. A hostile spawn within about 24 m
(plus its roaming radius) of a quest giver starts a brawl nobody asked for, and livestock with the
townsfolk faction is eaten by the first boar.
</pitfall>

<pitfall name="escortee_in_aggro_range">
Do not spawn an escort NPC within ambient aggro range of hostile spawns. It will be killed by wandering mobs on a respawn loop with no player involved, and its death trigger will fire pointlessly.
</pitfall>
</anti_patterns>
