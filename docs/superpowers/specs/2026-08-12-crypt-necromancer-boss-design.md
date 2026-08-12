# Sevrin Wax, the Coffinwright — data-only dungeon boss

Date: 2026-08-12
Status: approved design, not yet implemented

## Goal

A level 12 undead necromancer dungeon boss for the crypt (map 1, `Test Dungeon`),
authored **entirely as game data** — `units.data`, `spells.data`, `triggers.data`,
`unit_loot.data`, `maps.data` — with no C++ combat script and no Lua. The encounter
must have three distinct phases, two supporting add types, and it must drop the four
crypt items that already exist in `items.data`.

Non-goal: new art. The encounter uses only existing models and the existing room
geometry.

## Fiction

Sevrin Wax was the crypt's embalmer. He sealed the dead in grave-wax and sang them
down — the Litany. Something in the lower vaults taught him the song runs backwards.
He believes he is still doing his job: the dead are restless, so he tends them. He
resents the players for waking them, then delights in what they have made possible.

The name and theme are derived from the loot that already exists: *Gravewax Buckler*,
*Coffin Nail Dagger*, *Moth-Eaten Ritual Robe*, *Bone-Etched Staff*.

## Engine constraints this design is built around

These were verified against the current code, not assumed. Each one shaped a decision.

| Constraint | Source | Consequence |
|---|---|---|
| `SetPhase` routes through `GameCreatureS::SetCombatPhase`, which fails without a combat script | `game_creature_s.h:181`, `trigger_handler.cpp:730` | Phase state lives in instance variables, never in `SetPhase` |
| Spell power cost is enforced for creatures unless the cast is a proc | `single_cast_state.cpp:695-725` | Every boss/add ability is authored with `cost = 0` (unit class 3 has only 50 mana) |
| `UnitSpellEntry.target` is never read; creature rotation always targets the current victim | `creature_ai_combat_state.cpp:1186-1220` | Adds cannot buff or heal the boss through their rotation; add support goes through triggers |
| `SetInstanceVariable` writes an absolute value — there is no increment | `trigger_handler.cpp:1631-1644` | No mechanic may depend on counting live adds |
| A loot **group** yields at most one item | `loot_instance.cpp:118-152` | All four blues share one equal-chance group → exactly one drops per kill |
| `RandomPlayer` re-resolves independently on every action | `trigger_handler.cpp:1286-1292` | "Mark player X, then hit player X" is impossible; Coffin Call is a pure threat swap |
| `OnHealthDroppedBelow` is edge-triggered and skipped at 0% health | `game_creature_s.cpp:356-385` | Phase transitions fire exactly once per crossing and never on the killing blow |
| Trigger flags are re-checked before *every* action, including after a `Delay` | `trigger_handler.cpp:93-101` | Scripted chains self-cancel on owner death; `OnlyInCombat` must be omitted from the Rite chains |
| A trigger that aborts mid-chain never calls `NotifyTriggerEnded` | `trigger_handler.cpp:194-197` | `OnlyOneInstance` is not used on any abort-prone chain, or it would latch permanently |
| `CreatureSpawner::SetState(false)` stops respawn but does not despawn live creatures | `creature_spawner.cpp:179-195` | Adds are summons with an explicit despawn path, not spawner-driven |
| Summons outlive their summoner | `trigger_handler.cpp:1443-1460` | Cleanup is explicit: a `bossAlive` instance variable plus a self-despawn timer on each add |
| `ModDamageTakenPct` clamps the multiplier at 0 | `game_unit_s.cpp:3584` | A -100 aura is safe, but -90 is chosen for failure tolerance (see Rite of Rising) |
| `texts_loc` is honoured by `Say`, `Yell`, `Emote` **and** `BroadcastMessage` | `trigger_handler.cpp:322,354,385,1669` | All player-facing encounter text is localizable |
| `spells.data` exists in both `data/editor/data/` and `data/client/ClientDB/` | `data/client/ClientDB/project.txt` | New spells need a dual write or the client cannot render them |

No wire format changes, so **no `ProtocolVersion` bump** is required.

## The room

Map 1 (`Test Dungeon`, `instancetype: DUNGEON`), the columned nave at y=1. Ground
level is near-solid wall with tall windows; the side aisles are reachable through
exactly one archway per side, at x=-12. Players enter the map at (5, 1, 0) via
trigger 13 (`Teleport To Dungeon`).

```
        x=-27  -24   -21  -18  -15  -12   -9   -6   -3    0    3    5
                                    +---+  arch
  z=-6  ============================+   +===========================
  z= 0   apse   BOSS                                        [door]  <- players enter
  z= 6  ============================+   +===========================
                                    +---+  arch
```

Positions used by the encounter:

| Name | Position | Role |
|---|---|---|
| Boss home | (-24, 1, 0) | Stationary, faces east down the nave |
| Left arch | (-12, 1, -9) | Add entry, left aisle |
| Right arch | (-12, 1, 9) | Add entry, right aisle |
| Apse | (-27, 1, 0) | Add entry behind the boss |

`SummonCreature` coordinates are integers (`GetActionData` returns `int32`), which all
of the above already are.

## Units

Next free unit id is 81 (current max 80).

### 81 — Sevrin Wax, the Coffinwright

| Field | Value | Rationale |
|---|---|---|
| `subname` | "Warden of the Hollow Choir" | |
| `minlevel` / `maxlevel` | 12 / 12 | As requested |
| `factionTemplate` | 4 (`Undead Dungeon`) | Existing undead hostile template |
| `maleModel` / `femaleModel` | 17 (`PLAYER - Undead Human Male Model`) | A `.char` model, so it renders equipped weapons |
| `unitClassId` | 3 (`Dungeon Boss`) | |
| `useStatBasedSystem` | true | |
| `eliteStatMultiplier` | 2.0 | Yields ~2670 HP at level 12; Thaldrik (lvl 10) has 1025, Barrow-King Morthas 840 |
| `mainhandweapon` | 119 (`Bone-Etched Staff`) | He wields the staff he drops |
| `baseArmor` / `armorPerLevel` | 175 / 42 | Matches `Gravebound Cultist`, the nearest caster analogue |
| `damagePerLevel` | 1.2 | Matches Barrow-King Morthas |
| `meleeattacktime` | 2000 | |
| `regeneration` | 3 | Project default |
| `rank` | *(not set)* | `UnitEntry.rank` is read nowhere in the engine — verified by grep across `game_server`, `game_client` and `mmo_client`. Setting it would be decoration; left unset until something consumes it |
| `minlevelxp` / `maxlevelxp` | 520 / 520 | Scaled up from the 450 that both level-10 bosses give; confirm with `tools/xp_audit.py` |
| `unitlootentries` | [28] | New loot table |
| `triggers` | [29, 30, 31, 32, 33, 34, 35, 36] | See trigger table |

**Combat behaviour must resolve to Melee.** `DetermineCombatBehavior` picks Caster only
when ranged spells strictly outnumber melee-range spells. A Caster boss backs away from
the tank and would drag the fight into the aisles. The rotation below is therefore
balanced 2 ranged / 2 melee-range so he closes and staff-fights, punctuated by casts.
**This must be verified in play** — if he still kites, add a third melee-range entry.

`creaturespells`:

| Spell | Priority | Cooldown | Range | Notes |
|---|---|---|---|---|
| 236 Grave Bolt | 100 | 4–6s | 0–30 | Ranged filler, interruptible |
| 237 Wasting Rot | 100 | 14–18s | 0–30 | Ranged DoT on victim |
| 239 Coffin Nail | 100 | 10–12s | melee | Melee-range strike |
| 238 Gravewax Nova | 100 | 20–26s | melee (self-centred) | AoE; `maxrange` left at 0 so it counts as melee |
| 37 Dodge | 100 | — | — | Existing generic creature defensive |

### 82 — Wax-Sealed Husk

Level 11, `factionTemplate` 4, model 17, `unitClassId` 1 (Warrior),
`eliteStatMultiplier` 1.0 → ~390 HP. Melee only, no abilities beyond Dodge.
`walkspeed`/`runspeed` reduced so it shambles. `minlevelxp`/`maxlevelxp` **0** and no
loot entry — the boss summons these indefinitely, so they must not be farmable.
`triggers`: [39] (cleanup only).

### 83 — Choirbound Acolyte

Level 11, `factionTemplate` 4, model 17, `unitClassId` 2 (Mage),
`eliteStatMultiplier` 1.0 → ~237 HP. Deliberately squishy. `minlevelxp`/`maxlevelxp` 0,
no loot. `creaturespells`: 242 Dirge of the Hollow Choir (ranged, so it stands and
casts rather than charging). `triggers`: [38, 39, 40].

## Spells

Next free spell id is 236 (current max 235). All are `cost = 0`, `classmask = 0`,
`spellSchool = 5` (Shadow), and must be written to **both** `data/editor/data/spells.data`
and `data/client/ClientDB/spells.data`.

| Id | Name | Shape | Detail |
|---|---|---|---|
| 236 | Grave Bolt | `SchoolDamage`, `TargetEnemy` | 2.0s cast, 30yd, interruptflags for pushback + interrupt |
| 237 | Wasting Rot | `ApplyAura` / `PeriodicDamage` (13) | Instant, 30yd, 18s, ~3s tick |
| 238 | Gravewax Nova | `SchoolDamage` + `ApplyAura` / `ModDecreaseSpeed` (15), both `SourceAreaEnemy` | 1.5s cast, radius ~8, slow ~40% for 6s |
| 239 | Coffin Nail | `WeaponDamage` or `SchoolDamage`, `TargetEnemy` | Instant, melee range |
| 240 | Rite of Rising | `ApplyAura` / `ModDamageTakenPct` (24), `Caster` | basepoints **-90**, duration 12s (longer than the channel, so `RemoveAura` always ends it early) |
| 241 | Unhallowed Fervor | `ApplyAura` / `ModDamageDonePct` (23) + `ModAttackSpeed` (3), `Caster` | Permanent for the fight; ~+30% damage |
| 242 | Dirge of the Hollow Choir | `SchoolDamage`, `TargetEnemy` | 2.5s cast, 25yd, weak — the acolyte's filler |

Damage targets, to be trued up against existing level 9–12 spells during implementation
rather than shipped as written:

| Spell | Target |
|---|---|
| 236 Grave Bolt | ~45–60 per cast — his sustained pressure |
| 237 Wasting Rot | ~15 per 3s tick over 18s (~90 total) |
| 238 Gravewax Nova | ~70–90 on everyone in radius 8, plus a 40% slow for 6s |
| 239 Coffin Nail | ~35–50, on top of his auto-attack |
| 242 Dirge of the Hollow Choir | ~20–30 per cast — an annoyance, not a threat |

### Why Rite of Rising is -90% and not immunity

A hard immunity (`DamageImmunity`, aura 35) is school-scoped, so it would not stop
physical damage. `ModDamageTakenPct` at -100 would work and clamps safely at
`game_unit_s.cpp:3584`, but -90 is chosen instead: it makes the channel a strong
incentive rather than a wall, and if `RemoveAura` were ever missed the boss would still
be killable instead of soft-locking the instance. The aura's own 12s duration is a
second safety net.

## Instance variables

Keys are arbitrary `uint32` (`trigger_handler.cpp:1640`).

| Key | Meaning | Values |
|---|---|---|
| 1001 | Encounter phase | 0 = not started / reset, 1, 2, 3 |
| 1002 | Boss alive | 1 = alive, 0 = dead |

Instance variables are runtime-only and are not persisted, which is correct here — a
fresh instance starts at 0 and `OnReset` restores the same state.

## Triggers

Next free trigger id is 29 (current max 28).

| Id | Name | Event | Flags | Actions |
|---|---|---|---|---|
| 29 | Sevrin Wax - On Aggro | `OnAggro` | — | Yell intro; `SetEncounterState(1, InProgress)`; phase=1; bossAlive=1 |
| 30 | Sevrin Wax - Rite of Rising (Phase 2) | `OnHealthDroppedBelow 65` | `AbortOnOwnerDeath` | See sequence below |
| 31 | Sevrin Wax - Unhallowed (Phase 3) | `OnHealthDroppedBelow 30` | `AbortOnOwnerDeath` | See sequence below |
| 32 | Sevrin Wax - Husk Wave (Left) | `OnTimer 25000,32000` | `OnlyInCombat` | Condition phase >= 1: Say; summon 82 at left arch, attack-nearest |
| 33 | Sevrin Wax - Husk Wave (Right) | `OnTimer 25000,32000` | `OnlyInCombat` | Condition phase >= 2: summon 82 at right arch, attack-nearest. Because the phase variable is set as the Rite *begins*, the second arch opens with the Rite rather than 8s after it |
| 34 | Sevrin Wax - Coffin Call | `OnTimer 30000,40000` | `OnlyInCombat` | Condition phase >= 1: Yell; `ModifyThreat(RandomPlayer, +100000)` |
| 35 | Sevrin Wax - On Killed | `OnKilled` | — | Yell; `SetEncounterState(1, Done)`; bossAlive=0; phase=0 |
| 36 | Sevrin Wax - On Reset | `OnReset` | — | phase=0; bossAlive=1; `SetEncounterState(1, NotStarted)` |
| 37 | Crypt - Encounter Wipe | `OnAllPlayersDead` | — | Yell via `NamedCreature`; `SetEncounterState(1, Fail)`; bossAlive=0 so trigger 39 clears the adds a wipe left behind |
| 38 | Choirbound Acolyte - Raise Husk | `OnTimer 15000` | — | Condition bossAlive == 1: summon 82 at (-15, 1, 0), attack-nearest, despawn 120s |
| 39 | Boss Add - Cleanup | `OnTimer 10000` | — | Condition bossAlive == 0: `Despawn` on `OwningObject` |
| 40 | Choirbound Acolyte - On Spawn | `OnSpawn` | — | Say / Emote flavour as the singing starts |

Trigger 37 is registered in `MapEntry.instance_triggers` for map 1, not on a unit.

**Trigger 38 deliberately omits `OnlyInCombat`.** The acolytes are summoned without
auto-aggro so they stay in the aisles and sing; if the timer were combat-gated,
ignoring an acolyte would stop it summoning, inverting the mechanic and rewarding
players for not engaging it.

**Trigger 39 is shared by both add types** and is what makes summons disappear after a
kill, since summons outlive their summoner.

### Rite of Rising sequence (trigger 30)

Flags: `AbortOnOwnerDeath` only. `OnlyInCombat` is deliberately **not** set — the boss
takes 90% less damage during the channel, so combat could plausibly drop and abort the
chain, leaving the damage-reduction aura applied.

1. `BroadcastMessage` (raid warning): "Sevrin Wax begins the Rite of Rising!"
2. `SetInstanceVariable 1001 = 2` — **set at the start of the chain, not the end**
3. `Yell`: "Sing with me! SING!"
4. `CancelCast`
5. `StopAutoAttack`
6. `SetCombatMovement 0`
7. `ApplyAura 240` on `OwningObject`
8. `SummonCreature 83` at left arch — attack-nearest **0**, despawn 300s
9. `SummonCreature 83` at right arch — attack-nearest **0**, despawn 300s
10. `Delay 3000`
11. `SummonCreature 82` at apse — attack-nearest 1, despawn 120s
12. `Delay 5000`
13. `RemoveAura 240` on `OwningObject`
14. `SetCombatMovement 1`
15. `Yell`: "Now. Now you hear the choir."

Channel window ≈ 8s.

### Why the phase variable is set at the start of the chain

`OnHealthDroppedBelow` fires for every threshold a single damage event crosses. One
very large hit taking the boss from above 65% to below 30% fires triggers 30 **and**
31, and both chains then run concurrently.

If each chain set its phase variable at the *end*, the shorter phase 3 chain (6s) would
finish first and set phase 3, and then the longer phase 2 chain (8s) would overwrite it
back to 2 — leaving the boss enraged but reporting phase 2. Setting the variable at the
start makes the later-firing trigger win, which is the correct outcome.

The rest of a double-crossing is benign: the aura is applied twice and `RemoveAura`
clears all stacks, so the first removal ends it and the second is a no-op, and
`SetCombatMovement 1` is idempotent. Gating trigger 31 on `phase >= 2` was rejected as a
fix — during a double-crossing, phase 2 is not yet set when 31 fires, so phase 3 would
be skipped entirely and the boss would never enrage.

At 2670 HP this needs a single ~940-damage hit, so it is not reachable in normal play.
It is reachable in a test with an over-levelled character, which is why the E2E scenario
below deliberately does not over-level.

### Unhallowed sequence (trigger 31)

Same flags and the same open/close structure, with a ~6s channel:
raid warning; `SetInstanceVariable 1001 = 3` (again first, for the reason above); yell;
`CancelCast`; `StopAutoAttack`; `SetCombatMovement 0`; `ApplyAura 240`; summon 83 at
both arches; summon 82 at apse and at left arch; `Delay 6000`; `RemoveAura 240`;
`SetCombatMovement 1`; `ApplyAura 241` (permanent enrage).

## Encounter flow

**Pull.** Intro yell, encounter slot → InProgress, phase 1.

**Phase 1 (100–65%) — The Litany.** Grave Bolt / Wasting Rot / Coffin Nail / Gravewax
Nova rotation. One Wax-Sealed Husk shuffles in through the left arch every 25–32s.
Light pressure that teaches the room.

**Phase 2 (65%) — The Choir Answers.** The Rite: he stops attacking, takes 90% less
damage and channels for ~8s. A Choirbound Acolyte appears in each aisle and a husk
claws out of the apse behind him. Husk waves now use both arches.

The acolytes are the mechanic. Each raises a husk every 15s **for as long as it
lives**, and they are summoned without auto-aggro so they stay in the aisles. Players
must leave the boss and kill them or drown in husks. An acolyte's death *is* the state
change — nothing is counted, so the mechanic cannot desync.

The husks rise at (-15, 1, 0), mid-nave, rather than at the acolyte that raised them.
That is forced by `SummonCreature`: explicit coordinates require at least four data
values, so summoning at the caster's own position would leave no room for the despawn
timer or the auto-aggro flag, and the husks would then neither expire nor attack. Rising
mid-nave also reads better — the dead claw up where the fight is, rather than trekking in
from a side room.

**Phase 3 (30%) — Unhallowed.** Second, shorter Rite; two more acolytes and two husks;
then a permanent Unhallowed Fervor on himself.

**Throughout — Coffin Call.** Every 30–40s he yells and dumps threat onto a random
player; the tank has to win them back.

**Wipe / death / reset.** Handled by triggers 37, 35 and 36 respectively, each
maintaining the encounter slot and the instance variables.

## Loot

New `unit_loot` entry **28**, "Sevrin Wax, the Coffinwright". Next free id (current max 27).

- **Group 1 — guaranteed blue.** Items 116, 117, 118, 119, all with `dropchance = 0`.
  A group with only zero-chance definitions falls through to the equal-chance pool and
  yields exactly one entry, so every kill drops exactly one of the four.
- **Group 2 — weighted extras.** A small number of existing thematic items
  (e.g. 140 `Ritual Bone Fetish`, 135 `Gravewarden Wristplates`,
  136 `Seal of the Barrow-King`) at partial `dropchance`.
- `minmoney` / `maxmoney`: a few silver, in line with existing level-10 bosses.

No grey-quality items are used anywhere in the table.

## Map changes (map 1)

1. `EncounterEntry` id 1, name "Sevrin Wax, the Coffinwright".
2. A named `UnitSpawnEntry`: `name = "CryptBoss_SevrinWax"`, `unitentry = 81`,
   position (-24, 1, 0), `rotation = 0.0`, `movement = STATIONARY`, `maxcount = 1`,
   `respawn = true`, `respawndelay = 300000`, `isactive = true`.
   The name is required so trigger 37 can reach him via `NamedCreature`.

   `rotation = 0.0` is intended to face him east (+x), down the nave toward the
   entrance. The existing map 1 guard spawns sit at rotation ≈ ±π facing -x, which is
   what that inference rests on — confirm visually and correct if he faces the apse.
3. `instance_triggers` gains trigger 37.

The boss is 29 units from the player entry point at (5, 1, 0). Confirm during testing
that this is outside his aggro radius, so players are not pulled the instant they enter.

## Localization

Every player-facing string is localized in all four locales, per the project rule and
matching how existing units are authored (locale 1 = de, 2 = en, 3 = fr, 4 = ru):

- Unit `name_loc` and `subname_loc` for units 81, 82, 83.
- Spell `name_loc`, `description_loc`, `auratext_loc` for spells 236–242.
- Trigger `TriggerAction.texts_loc` for every `Yell`, `Say`, `Emote` and
  `BroadcastMessage`. English stays in `texts[0]`; the other three locales are
  additive overrides. Triggers are server-only, so no client mirror is needed for them.

## Testing

1. **Unit data sanity** — validate the JSON drafts with the NPC/spell skill validators
   before applying, and re-run `python tools/protocol_version_check.py` to confirm
   nothing on the wire moved.
2. **XP audit** — `python tools/xp_audit.py`, keeping the level band at or above 120%.
3. **E2E scenario** (`e2e/scenarios/`) covering the parts a human eye would miss:
   - the boss spawns at (-24, 1, 0) and aggroes;
   - crossing 65% fires trigger 30 exactly once, and the damage-reduction aura is
     applied and then removed (the soft-lock risk);
   - crossing 30% fires trigger 31 exactly once and applies Unhallowed Fervor;
   - acolytes summon husks while alive and stop when killed;
   - after the boss dies, no summons remain within ~15s (trigger 39);
   - a reset restores phase 0 and the encounter slot to NotStarted.
4. **Manual play pass** for the things data cannot assert: that he does not kite the
   tank (the Melee-behaviour assumption), that the 8s channel reads as a distinct
   moment, and that the fight length is reasonable at ~2670 HP.
5. `/gate` before any merge to `develop`.

## Deferred

- **Interactive crypt props** (douse the choir candles to break the Rite). The
  `Models/Dungeon/` kit is structural only — 119 floor pieces, walls, arches, columns,
  and no coffins, sarcophagi or braziers. This needs art before any data work.
- **A distinct add-death reaction.** `OnSummonedUnitDied` exists and fires on the boss,
  but because both add types are summons it cannot distinguish an acolyte death from a
  husk death without a counter, and `SetInstanceVariable` has no increment. If an
  increment action is added later, an acolyte-specific taunt becomes cheap.
- **Mark-and-punish mechanics**, blocked by `RandomPlayer` re-resolving per action.
  A `LastResolvedTarget` action target would unlock a whole class of boss mechanics.
