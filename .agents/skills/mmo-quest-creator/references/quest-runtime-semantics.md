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
- spell-cast-on-object credit calls `OnQuestSpellCastCredit` (requirements with `objectid` + `spellcast`)
- using a world object calls `OnQuestObjectUseCredit`, which increments requirements whose
  `objectid` matches the object entry and whose `spellcast` is 0 — "use object N times" is native
- trigger action `QuestExplorationCredit` calls `OnQuestExploration`, which sets the quest's
  exploration credit WITHOUT completing its other objectives (the quest completes only once the
  remaining counters are also met)
- trigger action `QuestEventOrExploration` calls `CompleteQuest`, which force-fills ALL counters —
  use it only for pure scripted completion, never for mixed objective quests

Every successful `GameWorldObjectS::Use` (chest looted, door used, quest object used) also fires
the object's `OnInteraction` triggers: base `ObjectEntry.triggers` plus the per-spawn
`ObjectSpawnEntry.trigger_id` override. The world object type `QuestObject` exists specifically
for credit-and-trigger interactables with no other behavior.
</progression>

<fulfillment>
`FulfillsQuestRequirements` checks:

- the quest is in the active quest map
- `Exploration` quests require `explored == true` (checked even when the quest has zero
  requirement rows, so a pure exploration quest no longer auto-completes at accept)
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
- `QuestEventOrExploration`: calls `CompleteQuest(questId)` on a player target (force-fills all counters)
- `QuestExplorationCredit`: grants only the exploration/event credit of the quest to a player target
- `QuestFailQuest`: fails the quest for a player target if it is in their quest log (escort death
  and similar failure conditions); `failtriggers` of the quest fire as usual

Useful trigger events:

- `OnQuestAccept`
- `OnKilled`
- `OnSpellHit`
- `OnGossipAction`
- `OnSpawn`
- `OnReachedTriggeredTarget`
- `OnInteraction` (world objects: fired on every successful object use)

Area triggers call their linked `on_enter_trigger`, which can then complete or advance a quest.
World objects execute their own triggers (`ObjectEntry.triggers` + per-spawn
`ObjectSpawnEntry.trigger_id`) with the object as trigger owner and the using player as the
triggering unit, so `TriggeringUnit`-targeted quest actions work from object interaction.
</trigger_actions>

<important_caveats>
Non-obvious runtime caveats that matter when authoring:

- `QuestEntry.starttriggers` are currently unused by runtime code.
- `AutoRewarded` is exposed in data and editor UI, but the normal quest runtime still does not auto-turn-in completed quests purely from that flag.
- `rewardspellcast` is cast in `GamePlayerS::RewardQuest`, and the NPC reward handler also attempts to cast it again. Verify live behavior before depending on visible one-shot spell rewards.
- `QuestEntry.rewardreputations` and faction base-rep data exist in schema only — there is no
  runtime reputation standing yet, so rep rewards do nothing at present.
- `exclusivegroup` and `nextquestid` are not enforced by `GetQuestStatus`; model mutually
  exclusive quests through race/class gates or `prevquestid` ancestry.
- Object-use credit is only granted on a SUCCESSFUL use: quest-gated objects stop being usable
  once their specific requirement is met, which naturally caps farming. Ungated objects (doors)
  can be used repeatedly, but counters cap at the required count.
</important_caveats>

<item_started_quests>
`ItemEntry.questentry` links an item to the quest it starts. Runtime behavior:

- Using the item (client sends UseItem even without an on-use spell when `startquestid` is set)
  makes the world server answer with the quest details offer if the quest status is `Available`.
- Accepting uses the item guid as quest giver guid; the server validates the item is in the
  player's inventory and its entry starts that quest.
- The item tooltip shows the localized "This Item Begins a Quest" line automatically.
- List the item as a required item of the quest so it is removed on turn-in; otherwise the player
  keeps a dead quest-starter in their bags.
</item_started_quests>

<client_support>
The client quest system now exposes `GetQuestLogTimeLeft(questId)` so UI can display timed-quest countdowns using the local reconstructed deadline.
</client_support>

<lua_hooks>
Creature Lua scripts can also react to quest flow:

- `OnQuestAccept`
- `OnQuestComplete`

See `data/scripts/example_npc.lua` and `src/world_server/lua_script_mgr.cpp`.
</lua_hooks>
