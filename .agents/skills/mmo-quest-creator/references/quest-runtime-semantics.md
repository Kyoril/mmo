<overview>
These notes describe how the current runtime evaluates quest availability, progress, completion, rewarding, and scripted behavior. Use them as the contract when designing or debugging quest content.
</overview>

<availability>
`GamePlayerS::GetQuestStatus` in `src/shared/game_server/objects/game_player_s.cpp` determines normal availability.

It currently checks:

- whether the quest is already rewarded
- whether a daily or weekly rewarded quest is still locked until its reset time
- whether the quest exists
- `maxlevel`
- required race and class masks
- `prevquestid`
- `minlevel`

It does not currently enforce every possible schema field. In particular, `requiredskill` is still marked TODO in the runtime, so do not assume that field is fully enforced without verifying the surrounding gameplay path.
</availability>

<acceptance>
`GamePlayerS::AcceptQuest` currently does these things:

- refuses any quest that is not `Available`
- grants `srcitemid` items immediately if configured
- casts `srcspell` on the player if configured
- sets a quest timer from `timelimit`
- places the quest in the quest log
- auto-completes only if `FulfillsQuestRequirements` is already true

Timed quests now persist their absolute deadline across logout. On login, `InitializeQuestTimers()` rearms countdowns and immediately fails any timed quest whose deadline already passed while the player was offline.

Quest acceptance from an NPC also raises `trigger_event::OnQuestAccept` on the questgiver unit in `src/world_server/player_npc_handlers.cpp`. This is the live data-driven hook used today for quest-start scripting.
</acceptance>

<progression>
The runtime updates quest progress through these paths:

- creature death calls `OnQuestKillCredit`
- inventory changes call `OnQuestItemAddedCredit` and `OnQuestItemRemovedCredit`
- spell-cast-on-object credit calls `OnQuestSpellCastCredit`
- trigger action `QuestEventOrExploration` calls `CompleteQuest`

There is no native generic "use world object N times" increment path in `GameWorldObjectS::Use`. If a requirement uses `objectid`, progress currently comes from spell-cast credit or forced completion triggers.
</progression>

<fulfillment>
`FulfillsQuestRequirements` checks:

- the quest is in the active quest map
- `Exploration` quests require `explored == true`
- creature counters meet `creaturecount`
- object counters meet `objectcount`
- current inventory counts meet `itemcount`

`sourceid` and `sourcecount` are handled through the item-credit path, but they do not appear separately in quest counters or the editor UI.
</fulfillment>

<completion_and_reward>
`GamePlayerS::CompleteQuest` force-sets remaining creature and object counters to their target values, marks the quest complete, and updates the quest log.

`GamePlayerS::RewardQuest`:

- validates and grants optional choice rewards and guaranteed rewards
- removes required quest items from inventory
- scales XP down when the player is higher level than the quest
- grants money
- removes `srcitemid` items
- teaches `rewardspell`
- casts `rewardspellcast`
- fires `rewardtriggers`
- removes the quest from the quest log and marks it rewarded

`failtriggers` are also executed on quest failure.

Repeatability now behaves in three different ways:

- plain `Repeatable`: the quest becomes immediately available again after reward
- `Daily`: the quest is treated as rewarded until the next global daily reset time
- `Weekly`: the quest is treated as rewarded until the next global weekly reset time
</completion_and_reward>

<failure_paths>
The runtime now has two important failure paths beyond manual abandonment:

- timed quests fail when their countdown expires, even if objectives were already complete but the quest was not yet turned in
- `StayAlive` quests fail when the player dies, through `FailQuestsOnDeath()`
</failure_paths>

<trigger_actions>
Quest-relevant trigger actions in `src/world_server/trigger_handler.cpp`:

- `QuestKillCredit`: grants kill credit for a unit entry to a player target
- `QuestEventOrExploration`: calls `CompleteQuest(questId)` on a player target

Useful trigger events:

- `OnQuestAccept`
- `OnKilled`
- `OnSpellHit`
- `OnGossipAction`
- `OnSpawn`
- `OnReachedTriggeredTarget`

Area triggers call their linked `on_enter_trigger`, which can then complete or advance a quest.
</trigger_actions>

<important_caveats>
Non-obvious runtime caveats that matter when authoring:

- `QuestEntry.starttriggers` are currently unused by runtime code.
- `GamePlayerS::OnQuestExploration` is still TODO, so exploration quests need a trigger path that reaches `CompleteQuest`.
- The current quest editor window does not expose object-use or spell-cast-on-object requirement fields even though the runtime supports them.
- `AutoRewarded` is exposed in data and editor UI, but the normal quest runtime still does not auto-turn-in completed quests purely from that flag.
- `rewardspellcast` is cast in `GamePlayerS::RewardQuest`, and the NPC reward handler also attempts to cast it again. Verify live behavior before depending on visible one-shot spell rewards.
</important_caveats>

<client_support>
The client quest system now exposes `GetQuestLogTimeLeft(questId)` so UI can display timed-quest countdowns using the local reconstructed deadline.
</client_support>

<lua_hooks>
Creature Lua scripts can also react to quest flow:

- `OnQuestAccept`
- `OnQuestComplete`

See `data/scripts/example_npc.lua` and `src/world_server/lua_script_mgr.cpp`.
</lua_hooks>
