# Bot swarm roadmap

Where the swarm bots go from "grind and chat" to "level, gear up, trade and run the Hollow
Choir as a group". Status as of 2026-10-19, on branch `feature/bot-swarm-ai-chat`.

## Where we start

- **AI architecture is ready for this.** `bot_ai` is a utility engine with name-registered
  triggers, actions, multipliers and strategies (`BotAiRegistry`), one engine per state
  (non-combat, combat, dead). Strategies are literal tables in C++ today; the registry was
  built so they can come from data.
- **Static game data is already in every bot.** The full `proto::Project` is loaded once per
  swarm, with items (quality, level, stats, sell price), talents, trainers, vendors, quests,
  quest givers, area triggers and loot tables. Bots do not need query packets for any of it.
- **The servers implement everything on the wire.** Vendors, trade, trainers, talents, quests,
  looting, loot rolls, area triggers and mail all work for the game client. What is missing is
  on the bot client side (`bot_core`) and in the bot AI.
- **What bots do today:** grind (pick spot, fight, rest, revive), follow and assist a grouped
  player, chat. They never loot, sell, buy, equip, train, pick talents, quest, trade or enter
  instances. The spell rotation is damage-only and ignores class and role: no heals, no taunts,
  no interrupts, no dodging ground zones.
- **Server gaps:** repair has no packet (the `Inventory::RepairAllItems` logic exists, but no
  opcode or handler calls it). There is no auction house, no raid conversion and no target
  marks.

## Phases

Each phase ships on its own, is verified live against the dev stack and goes through the
gate. Later phases depend on earlier ones as noted.

### Phase 1: Inventory, looting, selling (foundation)
Everything after this needs the bot to know what it carries.

- Track the bot's own bags and slots: player `InvSlot`/`PackSlot` fields, bag `NumSlots`/`Slot_N`,
  item `Contained`/`Durability`. Today these fields are parsed and thrown away.
- Loot: parse `LootResponse`, send `AutoStoreLootItem` and `LootMoney`, and add a
  "lootable corpse nearby" trigger and action to the grind strategy.
- Vendor: when bags are full or nearly full, travel to the nearest vendor (from the vendor
  data and unit spawns), sell grey items and junk with `SellItem`, buy food and drink with
  `BuyItem`.
- Equip upgrades: a gear scorer from proto item stats, armor type proficiency, required level
  and class; `AutoEquipItem` already exists.
- Repair is left out until the server has a repair packet (separate, small server task).

### Phase 2: Spreading out and levelling properly
- Grind spot choice weighted by level band and by how many bots already work a spot, so the
  swarm spreads over the world instead of clustering around the starting town.
- Trainers: walk to the class trainer on level-up, buy new spells (`TrainerBuySpell`).
- Talents: spend points from a per-role talent build (`LearnTalent`).
- Rotation becomes role-aware: heals and buffs for clerics, taunt for warriors, interrupts
  for everyone who has one.

### Phase 3: Questing
- Quest log from the player's `QuestLogSlot` fields; accept, progress and turn-in packets.
- Objectives come from quest data (kill creature X, collect item Y, use object Z); givers and
  turn-in NPCs from `UnitEntry.quests` / `end_quests` and their spawns.
- A levelling path as data: `e2e/scenarios/quest_path_1_to_10.lua` already lists the 31
  quests from 1 to 10 in order, ready to be turned into a bot quest route.

### Phase 4: Dungeon groups (the Hollow Choir)
- Roles: tank (warrior, Protection talents, Provoke and Shield Slam, faces bosses away),
  healer (cleric, Healing Light, Renewing Light, Healing Aura, healing by missing health and
  threat), DPS (mage, acolyte, scout).
- Group formation: a leader bot (or a player) gathers a tank, a healer and three DPS of level 10,
  travels to the entrance (area trigger 4 at (141, 32, 345) on map 0), and enters with
  `AreaTriggerTriggered`.
- Combat in a group: assist the tank's target, wait for the tank to pull, stay out of the
  tank's frontal cone, leave ground zones (zones arrive as `SpellZoneStart`), interrupt
  interruptible casts, switch to adds.
- Loot rolls: `StartLootRoll` and `LootRoll`; need on usable upgrades for the bot's own role,
  greed otherwise, pass on what nobody can use. Short chat lines about loot ("gz", "need for
  the staff, ok?").
- **Tactics as data.** One file per dungeon with its pull order, packs, bosses, priority
  targets, abilities to interrupt, zones to avoid and positioning (tank spot, ranged spot),
  read by generic AI. The Hollow Choir file is the first one; the mechanics list is already
  known (see below).

### Phase 5: Crafting and trading
- Tailoring: learn from the profession trainer, keep cloth from loot, craft recipes the bot can
  use or sell, sell the rest.
- Trades: the full trade packet set (initiate, add item, set money, accept). Bots offer crafted
  bags or gear to players who ask, and can ask players or other bots for items they need
  ("anyone selling a bag?"), with prices from item sell prices.
- Chat ties it together: the chat model can propose a trade the same way it proposes an invite
  today (a tag in its answer that the host turns into a real action).

## Hollow Choir: what a bot group must handle

From the spell, unit and trigger data (all implemented in data, no scripts):

| Boss | Must handle |
|---|---|
| Brother Oswin (lvl 12) | Grave Strike: frontal cone, uninterruptible, so the tank faces him away. Guttering Candle: circle on a random player, move out within 2 s. At 50 %: 2 Risen Novices for the tank to pick up. |
| Sister Mereth (lvl 13) | Lament: 3 s group AoE cast, interrupt it. Silent Place: 12 s slowing zone under a non-tank, move out. At 65 % and 30 %: 2 Mourning Choristers that make her take 40 % less damage, switch to them. |
| Cantor Veyr (lvl 14) | Dirge of the Grave: 5 s channel, interrupt it. Dissonance: circle, 2 s delay, move out (more often below 30 %). At 60 % and 30 %: 2 Hollow Choristers that buff him, kill them. Small apse, pull carefully. |

Trash to respect: Gravewarden (frontal cleave), Mourning Cantor (interruptible group AoE and
bolt), Candlebearer (leaves a fire zone on death).

Interrupts available at 10: warrior Skullbash (15 s), mage Arcane Disruption (25 s). Clerics,
acolytes and scouts have none, so a group needs a warrior and a mage, or a rotation of the two.

## Decisions (2026-10-19)

1. **Order:** 1 → 2 → 4 → 3 → 5. The dungeon comes before questing, since grinding already
   reaches level 10.
2. **Behaviour data** (talent builds, quest routes, dungeon tactics) lives in JSON files under
   `data/bots/`, read by the swarm.
3. **Repair** is left for a separate server task; bots live without it for now.
