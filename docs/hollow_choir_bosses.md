# The Hollow Choir — boss rework

Status: **in progress** (2026-10-08, `feature/hollow-choir-bosses`).

The first roster (Sevrin Wax, Ossuar, Choirmistress Vell) is retired: their spawns are gone from
map 1 and their triggers 29-50 are deleted. The unit rows 81-85 remain because quests 58-61 still
name them as kill objectives; re-pointing those quests is open work (see the end).

Order of work, as agreed with the user:

1. Boss and add NPCs — **done**, unspawned (the new layout has no coordinates yet).
2. The bosses' spells, each with cast animation, impact, sound and particles. Each spell is
   testable on its own: `learnspell <id>` on a GM character, then cast it at a hostile NPC.
3. Encounter logic in triggers (health thresholds, timers, add bookkeeping) — **not started**.

Target players are level 10-13, in a five-person group.

## Roster

| Id | Name | Subtitle | Level | Health | Melee | Loot |
|---|---|---|---|---|---|---|
| 86 | Brother Oswin | Keeper of the Vigil | 12 | 4822 | 55 every 2.4 s | 29 |
| 87 | Sister Mereth | The Mourning Voice | 13 | 5894 | 64 every 2.8 s | 30 |
| 88 | Cantor Veyr | The Hollow Choir | 14 | 7053 | 74 every 3.0 s | 28 (holds the epic) |
| 89 | Risen Novice | Oswin's add | 11 | 504 | 13 every 2.0 s | none |
| 90 | Mourning Chorister | Mereth's add | 12 | 179 | 10 every 2.4 s | none |
| 91 | Hollow Chorister | Veyr's add | 12 | 266 | 12 every 2.4 s | none |

All are faction template 4 (hostile undead) and use the stat-based unit classes. On this system one
knob, `eliteStatMultiplier`, scales health and melee damage together, so the bosses swing slowly:
health carries the fight length and tank damage stays healable. These are first-test values.
The fight lengths in the brief (90-120 s, 120-150 s, 150-180 s) assume a group dealing about
50-60 damage per second; tune health once a real group has played them.

Authoring: `tools/hollow_choir/author_bosses.py` (idempotent).

## Spells

| Id | Boss | Name (brief) | Shape |
|---|---|---|---|
| 251 | Oswin | Grave Strike (Grabhieb) | 1.5 s cast, frontal cone, heavy physical hit |
| 252 | Oswin | Last Vigil (Letzte Wache) | 2 s cast, raises two Risen Novices |
| 253 | Oswin | Guttering Candle (Erlöschende Kerze) | marks the ground under a player; detonates after 2 s |
| 254 | Oswin | Guttering Candle — detonation | fire damage to everyone still inside |
| 255 | Mereth | Lament (Klagelied) | 3 s interruptible cast, shadow damage to the whole group |
| 256 | Mereth | Mourning Voices (Trauernde Stimmen) | 2 s cast, two Mourning Choristers on opposite sides |
| 257 | Mereth | Mourning Chorus | aura on Mereth: 40 % less damage taken while choristers live |
| 258 | Mereth | Silent Place (Schweigende Stelle) | 12 s ground zone under a player |
| 259 | Mereth | Silent Place — pulse | shadow damage and a 50 % slow while standing in it |
| 260 | Veyr | Dissonance (Dissonanz) | marks the ground under a player; sound wave after 2 s |
| 261 | Veyr | Dissonance — wave | shadow damage to everyone still inside |
| 262 | Veyr | Dirge of the Grave (Grabeshymne) | 5 s interruptible channel |
| 263 | Veyr | Dirge of the Grave — pulse | shadow damage to the whole group, once per second |
| 264 | Veyr | The Choir Rises (Der Chor erhebt sich) | 2 s cast, two Hollow Choristers |
| 265 | Veyr | Choral Resonance | stacking aura on Veyr: +12 % damage per living chorister |

The encounter rules from the brief that are *not* spells — health thresholds, cast timers,
"Dissonance comes more often below 30 %", removing Mourning Chorus when the choristers die — are
trigger work for step 3.

## Engine work the spells need

The spell runtime could not express three of the brief's mechanics as data:

- **Frontal cone.** `spell_effect_targets::ConeEnemy` existed in the enum with no resolver.
- **Summons from a spell.** `spell_effects::Summon` was an empty handler; only the trigger action
  `SummonCreature` could spawn units.
- **Ground zones.** `spell_effects::PersistentAreaAura` was an empty handler, and nothing told the
  client about an area on the ground.

Their design is described in the sections below as they land.

## Open work

- Re-point quests 58-61 (kill objectives on 81, 84, 85) at the new bosses.
- Spawn the bosses once the new layout has coordinates.
- Encounter triggers (step 3).
- Update `docs/world/bible.md` section 3.9 and the named-characters table once the user confirms
  the new roster's lore.
