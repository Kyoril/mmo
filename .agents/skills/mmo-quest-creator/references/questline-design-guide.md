<overview>
Design guidance for building WoW-quality zones and questlines with this engine's quest system.
Derived from an analysis of all 64 TBC Ghostlands quests (the reference zone for hub-driven
levelling content) mapped against this repository's runtime. Use this when designing a new zone's
quest content or extending an existing area, not for mechanical wiring details (see the other
references for that).
</overview>

<quest_goal_taxonomy>
The full vocabulary of quest goals observed in a classic WoW levelling zone, with the way each is
built in THIS engine:

1. **Delivery / handoff** — bring an item or message to another NPC.
   Build: `srcitemid` grants the item on accept; turn-in NPC in `end_quests`; the item is listed as
   a required item so it is consumed on turn-in. No counters needed for pure "speak to" handoffs.
2. **Travel breadcrumb** — "speak with X at Y". Low XP (10-25% of a standard quest), no item
   rewards. Build: empty requirements, different provider and ender.
3. **Kill quest** — 1-3 creature counters, 6-12 kills each. Build: `creatureid + creaturecount`
   requirements. Use `UnitEntry.killcredit` to let several creature variants share one counter.
4. **Named / elite kill** — kill one named target; often drops a proof item ("head") instead of
   using a kill counter, which doubles as the turn-in requirement. Build: either a kill counter on
   the named unit, or a 100% drop quest item + item requirement (prefer the item for finale bosses:
   it reads better and survives group credit questions). Tag with `suggestedplayers >= 2` and
   quest type Elite for group content.
5. **Collect from kills** — 1-4 item counters fed by creature drops. Drop rates 25-80%; higher
   count = higher drop rate, never both low.
6. **Collect from world objects** — ground-spawn containers ("supplies", "plans", "stones").
   Build: quest-gated `QuestObject`/Chest objects (`ObjectEntry.requiredquest`) with loot entries
   containing the quest item; item requirement drives the counter.
7. **Use object N times** — "burn 10 remains", "poison the 3 food racks". Build: object-use
   requirement (`objectid + objectcount`, `spellcast` 0) — credit is granted natively when the
   player uses the object. Distinct named racks = one requirement row per object entry (max 4).
   QuestObjects despawn on use and respawn on the spawner's delay, so place enough spawns for
   the required count plus contention (rule of thumb: 1.5-2x the count, short respawn).
8. **Cast spell on target** — use a quest item's spell on a creature or object. Build:
   `objectid + objectcount + spellcast` requirement; credit via the spell-cast path.
9. **Exploration / investigate** — "investigate the ruins", possibly combined with kill counters.
   Build: set the `Exploration` quest flag, add a `text` requirement row for the quest log line,
   and fire the `QuestExplorationCredit` trigger action from an area trigger (or any trigger).
   This credits only the exploration objective; remaining counters still have to be completed.
   `QuestEventOrExploration` (force-completes ALL objectives) is only for pure scripted completion.
10. **Escort / rescue** — protect an NPC walking a route, or free captives.
    Build (escort, route-driven): start via gossip or `OnQuestAccept` trigger on the escortee;
    chain `MoveTo` actions with `OnReachedTriggeredTarget` events for the route; final waypoint
    fires `QuestEventOrExploration` (or `QuestExplorationCredit`); an `OnKilled` trigger on the
    escortee fires `QuestFailQuest` so the quest fails when the NPC dies.
    Build (escort, follow-driven): fire `SetFollowTarget` from the `OnQuestAccept`/gossip trigger
    so the NPC walks WITH the player instead of leading; complete via an area trigger at the
    destination (`QuestExplorationCredit`) and release the escortee from the quest's
    `rewardtriggers` (`ClearFollowTarget` + `Despawn` on the named spawn — never from the area
    trigger itself, which fires for every passerby); same `OnKilled` → `QuestFailQuest` fail
    wiring. Live example: quest `56` (see quest-authoring-patterns.md). Use follow-driven escorts
    for "take me to X" flows and route-driven ones for "protect me while I walk my path" flows.
    Spawn the escortee outside ambient aggro range — the danger belongs on the route.
    Build (rescue captives): each captive is a gossip NPC whose `OnGossipAction` trigger fires
    `QuestKillCredit` for a hidden credit unit, then plays a walk-off + despawn sequence.
11. **Summon boss by ritual** — use an item/object at a location, boss spawns, kill it.
    Build: `QuestObject` with an `OnInteraction` trigger firing `SummonCreature`; the kill counter
    (or dropped proof item) is the actual requirement. Gate the object with `requiredquest`.
12. **Repeatable turn-in ("More X")** — a follow-up version of a collect quest with `Repeatable`
    flag, zero XP, and the same requirements, unlocked by `prevquestid` on the one-shot version.
    The classic use is reputation grinding; until the reputation runtime exists, use these for
    item/money turn-ins sparingly.
13. **Wanted poster** — quest offered by a world object. Build: put the quest id in
    `ObjectEntry.quests`. Pairs naturally with type-4 elite hunts.
14. **Item-started quest** — a dropped item opens a quest offer when used ("should be taken to...").
    Build: set `ItemEntry.questentry` to the quest id; drop the item from relevant creatures
    (make it a quality-white quest item, 100% one-per-player if it's a chain hook). List the item
    as a required item of the quest so it is consumed on turn-in. The client shows
    "This Item Begins a Quest" automatically.
</quest_goal_taxonomy>

<zone_structure>
How a Ghostlands-style zone hangs together:

- **Hub-and-spoke**: a central quest hub offers 2-4 concurrent quests that all point into the SAME
  sub-area, so one outing completes several quests. Return trips are batched: the player leaves
  with a full log and returns with everything done.
- **Forward hubs**: as the player levels through the zone, new hubs open closer to the endgame
  area (Tranquillien → Sanctum of the Sun → Deatholme gates). Breadcrumb quests connect hubs.
- **Zone villain arc**: establish a named villain early through flavor text (Dar'Khan), let
  separate chains each uncover a piece (intel chain, sabotage chain, war chain), and converge them
  on one finale kill quest, followed by a "victory lap" turn-in at the capital with a big
  one-time reward.
- **Chapter chains**: 2-4 quests each, `prevquestid`-linked, one theme per chain (one village, one
  threat). Multiple chains run in parallel; a chain never blocks the whole hub.
- **Difficulty band**: quest level minus required level stays small (2-6). Elite/group quests cap
  the zone (wanted posters, the villain) and sit 1-2 levels above the surrounding solo content.
- **Race/class forks**: when two audiences need different flavor for the same beat, author two
  quests gated by `requiredraces`/`requiredclasses` pointing at the same follow-up. Both forks
  can name the same follow-up via `nextquestid` — rewarding either one unlocks it. For forks the
  same character could otherwise take twice, put both in the same positive `exclusivegroup`.
</zone_structure>

<reward_pacing>
Observed Ghostlands reward bands, normalized to a standard solo kill/collect quest of the same
level = 1.0x XP:

| Quest kind | XP | Money | Items |
|---|---|---|---|
| Travel breadcrumb | 0.1-0.4x | little or none | none |
| Standard kill/collect | 1.0x | small | none or 1 utility consumable |
| Chain finale / multi-objective | 1.4-1.6x | medium | choice of 2-3 greens or whites |
| Elite / wanted / boss | 1.6-2.0x | large | choice of 3-4, one per armor class or role |
| Repeatable "More X" | 0 | none | small consumable bundle |

Rules of thumb:
- Choice rewards appear only on chain finales and elite quests; standard quests give at most a
  fixed reward. A choice list should serve different classes (one caster, one melee, one ranged
  piece), not different power levels.
- Never reward grey items; dedicated white quest items exist for that (see quest-item-quality rule).
- The zone finale rewards the best item of the zone plus a capital turn-in follow-up whose reward
  is honor/flavor (title, emote, trinket), not raw power.
- Timed/StayAlive modifiers justify roughly a half-band bump, not more.
</reward_pacing>

<objective_writing>
- Objectives text names the exact target, the count, the location, and the turn-in NPC with
  location ("Slay 10 X and 10 Y on the Dead Scar, then return to Z in Tranquillien.").
- Use the `text` field of a requirement row for event objectives so the log shows a line like
  "Investigate An'daroth" next to the counters.
- Multi-counter quests: 2 counters is the comfortable default, 3 for "war effort" quests, 4 is the
  hard engine limit — reserve it for showcase quests (four lieutenants, four weapon types).
- Order requirement rows: creature counters first, then object counters, then items. The client
  assigns quest-log counter display slots in that order.
- Distinct-named single collects ("Stone of Light" + "Stone of Flame") read better than "2 Stones"
  and give each sub-location a purpose. Each is its own requirement row with count 1.
</objective_writing>

<ghostlands_case_study>
Zone snapshot (TBC Ghostlands, levels 9-21, 64 quests): one central hub (Tranquillien) + two
specialist hubs (Farstrider Enclave: trolls/plague, Sanctum of the Sun: villain war) + one entry
breadcrumb from the previous zone + one capital victory lap. Chains: village-cleansing
(3 quests + elite capstone), villain-intel (journal item → 2 handoffs → ziggurats collect →
breadcrumb), spy-sabotage (kill+investigate → steal plans from objects → delivery → deactivate
crystal), lake (collect medallions → summon & kill elemental), plague (2 kill quests → potion
handoff → rescue 3 captives), troll war (2 attack waves → weapon collect x4 → elite kill),
Deatholme endgame (war effort 3-counter → 4 lieutenants → destroy 8 eyes → kill villain).
Roughly: 40% kill/collect, 20% delivery/breadcrumb, 15% object interaction, 10% elite/boss,
8% scripted (escort/rescue/summon), 7% repeatable.
</ghostlands_case_study>

<engine_gaps_to_respect>
Known limits that still constrain design (verify before assuming they changed):

- **Reputation**: `QuestEntry.rewardreputations` and faction base-rep exist in data only; there is
  no runtime reputation standing. Do not build rep-reward loops yet; keep repeatables item/money
  based. When designing a zone meant for a future rep faction, still author the rewardreputations
  rows so content is ready.
- **starttriggers**: still unused by the runtime; quest-accept scripting goes through the
  questgiver unit's `OnQuestAccept` trigger.
- **AutoRewarded + choice rewards**: incompatible — auto-rewarded quests must use fixed rewards.
- **Negative exclusive groups** ("all of group must be completed"): not supported; only positive
  mutually-exclusive groups are enforced.
- Kill counters and object counters share the same four `QuestField.counters` bytes: max 4
  counter-based objectives per quest, values cap at 255.
</engine_gaps_to_respect>
