# The Haven Road — quests 78–94 (levels 6–10)

Added 2026-11-12. Before this, every quest from level 6 to 10 sat east of the river, around the
Kingsroad Waypost, the Mirewater and the Barrowfield. West of the Old Stone Bridge there was a
road, an inn, two farms, a harbour and a populated town — and nothing to do. Haven's only quests
were its two "welcome" errands.

This content fills that stretch. It runs from the Old Stone Bridge down the Haven Road into Haven,
and it gives Haven a story of its own. The Mirewater and Barrowfield quests are unchanged; they are
now the northern branch, and the Haven Road is the western one.

- Authoring guide for the mechanics used here: the quest skill
  (`.agents/skills/mmo-quest-creator/references/quest-authoring-patterns.md`).
- Regression test: `e2e/scenarios/haven_road_quests.lua` plays every mechanic below against a real
  server stack.
- World facts added by this content: `docs/world/bible.md`, sections 3.11, 3.13, 4, 5 and 6.

## The story

Someone is moving the King's stolen goods down the river. The player meets the pieces one at a time
and only later sees that they fit together:

1. A "tollmaster" squats on the Old Stone Bridge (the ring's eyes on the road).
2. Somebody tries to take the Watch's dispatch off the courier on the Haven Road.
3. A farmer's fields are haunted — because someone buried lures in them, to make him sell the land
   by the river.
4. Boats without lanterns put in at a landing above the harbour and sink crates in the river.
5. The manifest in their skiff names a buyer: "the Chandler".
6. Haven's market knows a chandler who buys no tallow. Confronted, he runs for the Trading Basin.

The ring is **the Thorn**: the Forest Bandits' black-thorn armband (q24) was only ever its lowest
rung. Its purse was Ambrose Tallow, chandler in Haven's lower court.

## Where it plays

| Place | Atlas id | Level | Who is there |
|---|---|---|---|
| Crossroads Inn | `crossroads_inn` | 6–7 | Road-Warden Petra Lowell, Pip Wickham, Waykeeper Tomas Reedley (soul binder), Marta Coldwell (innkeeper, food and drink) |
| Old Stone Bridge, east end | `old_stone_bridge` | 7 | Tollmaster Brask |
| Haven Road | `haven_road_lamps` (new) | 6–7 | the lamp posts, the ambush |
| Wainbrook Farm | `wainbrook_farm` (new) | 7–8 | Farmer Odell and Goodwife Hesper Wainbrook; Stubble Boars; Furrow Dusklings by night |
| Thistledown Farm | `thistledown_farm` (new) | 8 | Widow Annis Thistledown; Old Rootsnout |
| River Valley cottage | `valley_cottage` (new) | 7–9 | Old Tam Carrow (ferryman); Valley Wolves |
| Thornhand Landing | `thornhand_landing` (new) | 8–9 | Bosun Kerrick, Thornhand Smugglers |
| Gate Island | `gate_island` (new) | 8–9 | Harbourmaster Lysa Fairwind |
| Haven, lower court | `haven` | 9–10 | Ambrose Tallow; the traders; Watch-Captain Corwin Ashby |
| Haven Trading Basin, north quay | `haven_trading_basin` | 10 | Ambrose Tallow and his enforcers |

The new atlas places are placeholders with a question for the user each. Nothing was added to or
changed in the terrain or the placed props.

## The quests

| Id | Name | Lvl | From → to | What the player does |
|---|---|---|---|---|
| 78 | Beyond the Old Stone Bridge | 6 | Aldric Vane (Waypost) → Petra Lowell | Breadcrumb across the river. |
| 79 | The King's Toll | 7 | Petra → Petra | **Talk** to Tollmaster Brask. Refuse the toll and he and two thugs attack; bluff him and he either runs (credit without a fight) or calls the bluff. |
| 80 | Lamps for the Haven Road | 7 | Pip Wickham → Odell Wainbrook | Light eight road lanterns south of the inn. Walks the player to the farm. |
| 81 | The Haven Dispatch | 7 | Petra → Lysa Fairwind | **Timed** courier run (4 minutes) to Gate Island. Waylayers ambush the courier on the road — and only a courier. |
| 82 | Pigs in the Corn | 7 | Hesper → Hesper | Offer turnips to four strayed sows (item with a spell) and **lead them back** to the farm. |
| 83 | The Scarecrow Watch | 8 | Odell → Odell | Light the watch-fire; hold it through two waves of dusklings; kill Harrow, the Field-Haunt. |
| 84 | A Token in the Ashes | 8 | item (Harrow's loot) → Odell | The Scorched Thorn Token **starts a quest** when used. |
| 85 | Root and Branch | 8 | Odell → Odell | Dig out five buried dusk-lures. One in three wakes a duskling. |
| 86 | Cider for Haven | 8 | Hesper → Rosalind Tull (Haven inn) | Delivery; the first reason to walk into Haven. |
| 87 | Lights on the Water | 9 | Old Tam → Lysa | **Overhear** the smugglers from the rise west of their landing, then take the manifest from their skiff. |
| 88 | Sunk Cost | 9 | Old Tam → Lysa | **Dive** for five crates on the river bed. |
| 89 | Wanted: Old Rootsnout | 8 | Annis Thistledown → Annis | A named boar in her field. |
| 90 | The Chandler's Customers | 9 | Lysa → Corwin Ashby | **Question** three traders in Haven's lower court; each tells a different piece. |
| 91 | A Quiet Word | 10 | Corwin → Corwin | Confront Tallow at his stall. He bolts for the north gate. |
| 92 | Low Tide at the Basin | 10 | Corwin → Corwin | Bring Tallow down on the north quay of the Trading Basin and take the Thorn Ledger. Rare reward, choice of three. |
| 93 | Stones Out of Tune | 8 | Magister Calloran → Calloran | Touch the resonance shards at Haven's stone, the Crossroads Inn and the Briarwatch camp. A walk back along the whole road. |
| 94 | Names for the Chapel | 10 | Osric Dawnmere (Barrowfront) → Mother Agatha Brightwell | After the Barrow-King (q53): carry the roll of the dead to Haven. Ties the northern branch to the town. |

Chains: 78 → 79 and 81; 83 → (token) 84 → 85; 87 → 90 → 91 → 92; 53 → 94. Everything else is free.

One change to existing quests: Haven's two welcome errands, 73 "Welcome to Haven" and 74 "Coin and
Keeping", are offered from level 6 instead of 8, because the dispatch run and the cider delivery
bring players through the north gate that early.

Two of the seventeen are plain kills (89, and 83's final target). Total quest experience is 15,300.

### Ambient life

- **Night in the fields.** Furrow Dusklings roam the Wainbrook fields from 21:00 to 06:00 and are
  gone by day (`Farmlands - Duskling Night Routine`, the same pattern as Haven's day and night).
  No quest depends on them.
- Stubble Boars in both farms' fields, Valley Wolves in the River Valley, seven smugglers and their
  bosun at the landing, three enforcers at the basin.

## Mechanics, and how they are built

### Talking a creature into a fight — `SetFaction` (new trigger action 40)

Tollmaster Brask is a friendly npc with a gossip menu. Its two options are offered only to a player
on quest 79 (gossip option condition). Picking one runs a trigger on Brask:

- **Refuse:** yell, `SetFaction` to the bandit faction with *attack triggering unit*, then
  `SetSpawnState` for his two thugs (inactive spawners, `Despawn On Reset`).
- **Bluff:** `RandomTrigger` between "he runs" (`QuestKillCredit` for his own entry, `MoveTo`,
  `Despawn`) and "he calls it" (the refuse path).

`SetFaction` data: `<faction template id>` (0 = back to the entry's own), `[<attack triggering
unit 0/1>]`. The override lasts for one fight: it ends by itself when the creature resets
(evades) or respawns, so the next player finds someone to talk to again.

Order matters: set the faction **before** spawning hostile helpers, or they attack the creature
while it is still friendly.

### Leading creatures somewhere — `DeliverFollowers` (new trigger action 41)

A sow hit by the turnip spell (`OnSpellHit`) follows the player (`SetFollowTarget`, as escorts do).
The area trigger around Hesper runs `DeliverFollowers`: every living creature of the given entry
that follows the triggering player within the radius grants a quest kill credit for its entry,
stops following, emotes the optional text and despawns.

Data: `<creature entry>`, `[<radius, default 15>]`, `[<despawn delay ms, default 3000>]`; text:
optional emote. This is the general "bring N of them back" objective: strayed livestock, rescued
prisoners, lost children.

**One follower belongs to one player.** The turnip spell's second effect applies a
`PreventAuraApplication` aura (`src/shared/game/aura.h`) to the sow for five minutes. While it is
on her, every aura-applying cast anyone else aims at her fails with "Invalid target": a second
player cannot offer their own turnip and walk off with a sow - and the progress she carries - that
someone else is already leading. The aura is visible, so her buff frame shows that she is taken
before anyone wastes a turnip on her. The mark has to expire, because a player who never hands her
in leaves her claimed; five minutes is long enough to lead her home and short enough that an
abandoned sow becomes everyone's again.

Three things this quest needed that were not there:

- **The `Dummy` spell effect did nothing at all**, not even tell triggers about the cast. A spell
  whose only effect is `Dummy` now records its targets as hit, so `OnSpellHit` fires. That is what
  a quest item "used on a creature" is: an item with a dummy spell and a trigger on the creature.
- **A faction for livestock** (faction 13, template 13 `Livestock, Friendly`): friendly to players
  and townsfolk and nobody's enemy. With the townsfolk faction the sows were attacked by every boar
  in the field (creatures attack npcs of an enemy faction as readily as players).
- Give the radius room. The server's position of a following creature trails what the client
  shows by a few metres; the sows use 25.

### Events only for players on the quest — `QuestStatus` (new trigger condition function)

`QuestStatus(questId)` is the quest status of the player who raised the trigger
(`quest_status::Type`: 0 rewarded, 1 complete, 2 unavailable, 3 incomplete, 4 available; -1 when no
player is involved). The overheard scene compares it with 3, so walking past with no quest does
not play it.

A quest **without objectives is complete (1) from the moment it is accepted** and never incomplete.
The road ambush belongs to such a quest (the timed delivery), so its condition is
`QuestStatus == 3 or QuestStatus == 1`: "the quest is in the log".

### A dialogue with pages — gossip actions on sub menus

A gossip option of type `GossipMenu` opens another page. Until now the server refused every option
picked on such a page, because it only accepted the npc's root menu. It now accepts every page that
can be reached from the root through options whose conditions the player meets
(`FindReachableGossipMenu`, `src/shared/game_server/gossip_menu_graph.h`) and still nothing else.

The traders of quest 90 use it: main page → "What can you tell me about Ambrose Tallow?" → the
account → "Thank you" (a `Trigger` option) grants the credit. Their main page carries the quest
condition on the **menu** itself, so without the quest their vendor window opens directly, as before.

Credit for "talked to X" is a `QuestKillCredit` for a unit entry that is never spawned
(`Wainwright's Account`); its name is what the quest log shows.

### Summon at the owner with a despawn time

`SummonCreature` took explicit coordinates as soon as it had four data values, so "at the owner,
despawn after a minute" could not be written. All-zero coordinates now mean "at the origin".

### Quest objects no longer crash the world server

`QuestObject`s existed as a type but no live content used one. Using one removed it from the world
while the Open spell's effect still held it, which is a use-after-free (an assertion in Debug). A
used quest object is now disabled at once and removed on the next tick, and the Open effect keeps
its target alive across the use. Found by the E2E scenario on the first lantern.

### Other patterns used (existing mechanics)

- Players use every world object through the **Open spell: a 5 second cast that cannot be started
  in combat**. Lighting a lantern, digging a lure and opening a crate all take that long, and the
  boar next to a lure has to die first. Plan counts with that in mind.
- A scene meant to be overheard uses `Emote` actions, not `Say`: an emote carries 50 m, a say 25 m,
  and creatures notice a player at about 20 m. The smugglers "growl" and "mutter" their lines.
- Hostile creatures are kept 24 m plus their roaming radius away from townsfolk, and off the Haven
  Road. The authoring script asserts the first; a quest giver in a permanent brawl helps nobody.
- Object use credit on `QuestObject`s (lanterns, lures, shards). Three *different* object entries
  with a count of one each make "touch these three stones" unfarmable from a single spawn.
- A `QuestObject` that starts an event: the watch-fire has a quest gate but **no** object counter,
  so it stays usable while the quest is incomplete and a wiped player can light it again. Creatures
  that must give quest credit come from an inactive spawner (`SetSpawnState`); the waves are
  `SummonCreature` (summons grant neither experience nor credit).
- Timed quest with a source item (81), item-started quest (84), exploration credit after a scene
  (87, 91), chests under water (88).

## Verification

- Unit tests: `game_server_tests "[faction]"` (the override, and that it ends on reset) and
  `"[gossip]"` (which pages of a dialogue are reachable).
- E2E: `haven_road_quests.lua`. It also checks the negatives: no thugs and no ambush without the
  quest, no credit through another trader's page, no reward before the last objective.
- `tools/world/lint.py` and `tools/world/report.py`: no new errors from this content. The six
  sunken crates are baselined as "in water" — that is where they belong. The report learned that a
  `QuestKillCredit` or `DeliverFollowers` trigger is a valid source for a kill objective.
- `tools/tests/test_haven_road_data.py` guards the wiring outside `quests.data`: givers and enders,
  chains, gated objects and their spawns, the gossip conditions, the inactive event spawners, the
  ambush condition, the sows' faction, and a German text in the Ihr form for every string.
- `tools/xp_audit.py`: 173 % coverage of the experience needed for level 10 (was 135 %).
- The E2E client gained `SelectGossipOption`, `UseItem`, `TakeLootItem` and
  `FindNearestUnitByEntry` (see `e2e/README.md`).

## Known gaps and open questions

- **The terrain west of the river is being reworked.** The main checkout held uncommitted terrain
  and prop changes for exactly these pages while this was authored. Every position was resolved
  against both states and only spots where the two agree were used (river-bed crates: within 1.2 m).
  After the terrain work lands, run `tools/world/lint.py --map 0` once more.
- **Zones.** The farms and the river valley have no zone painted, and the Crossroads Inn still
  counts as Briarwatch March. The quests work without, but the quest log and the map show no area
  name there.
- **Props.** There is no pen at Wainbrook Farm (the sows are led "back to Hesper"), no boat at the
  landing other than the skiff object, and no stall for Tallow. Dressing these places is a separate
  pass for the world-dresser.
- **The Crossroads Inn's interior.** Marta Coldwell stands behind the bar, at the spot Old Harbin
  has in the same building in Oakenshire. Not checked in the client.
- **Visuals.** The quest objects reuse existing meshes (road lantern, brazier, root, boat, a small
  rune stone). Lighting a lantern has no flame effect yet.
- **Locales.** English and German (Ihr/Euch) are written. French and Russian fall back to English.
- **Not played in the real client.** The E2E client has no quest dialogue UI: the item-started
  quest (84), the gossip texts and the timed quest's countdown are verified on the server side only.
- **Bluffing Brask** is a coin toss by design. If it should depend on something (level, class, an
  item), that is a condition on the two random triggers.
