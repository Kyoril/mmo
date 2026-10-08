# The Hollow Choir — boss rework

Status: **spells ready for manual testing** (2026-10-08, `feature/hollow-choir-bosses`).

The first roster (Sevrin Wax, Ossuar, Choirmistress Vell) is retired: their spawns are gone from
map 1 and their triggers 29-50 are deleted. The unit rows 81-85 remain because quests 58-61 still
name them as kill objectives; re-pointing those quests is open work (see the end).

Order of work, as agreed with the user:

1. Boss and add NPCs — **done**, unspawned (the new layout has no coordinates yet).
2. **Done, awaiting the user's feedback:** the bosses' spells, each with cast animation, impact, sound and particles. Each spell is
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

## Audio-visual design

Every spell gets a cast animation, an impact, a sound and particles. Each one has to read clearly
between pillars and under the players' feet in a dark crypt. Particles live in
`data/client/Particles/HollowChoir/`, their recipes in `tools/particle_gen/recipes/hollow_choir_*.py`,
sounds in `data/client/Sound/Spells/HollowChoir/`.

Each boss has its own palette, so a player can tell from a glance whose mechanic is on the floor:

| Boss | Palette | Motifs |
|---|---|---|
| Brother Oswin | warm candle amber and gold, grey incense smoke, bone dust | censer, candle flames, wax, the vigil |
| Sister Mereth | spectral pale teal and cyan, mourning silver | wails, tears, sound rings, ghostly veils, hush |
| Cantor Veyr | discordant magenta and violet, with a sickly gold accent | jittering sound rings, notes, choir columns |

Ground telegraphs are built from a bright rim (the ring sprite) plus a low, dense fill, and they
build up over their warning time. They have to tell the player where the edge is, not just
where the danger is roughly.

| Spell | Cast (caster) | Release / success | Ground / impact | Sound |
|---|---|---|---|---|
| Grave Strike | 1.5 s wind-up: CastLoop, incense smoke at the censer hand, amber embers fanning forward over the ground in a 100° cone of 8 units | Attack_1H_02, forward dust and ash shockwave, bone shards | — | censer chain swing + rising groan; crushing slam |
| Last Vigil | ChannelUp loop, pale soul wisps rising around Oswin | EmoteRoar, ghost burst | — | stone lids grinding + low monk chant |
| Guttering Candle | instant: CastRelease | — | GROUND_ACTIVE 2 s: ring of guttering candle flames on the 3.5 rim, amber floor glow growing; GROUND_EXPIRED: flame pillar and embers | flame ignite + small bell (the warning); fire flare |
| Lament | 3 s CastLoop, teal wail rings pulsing from the chest, rising tears | CastRelease, teal sound ring sweeping outward | small teal shiver on each player hit | wordless ghostly female wail; spectral shockwave |
| Mourning Voices | ChannelUp, choir wisps | EmoteCry | — | eerie choir swell |
| Mourning Chorus | — | — | AURA_IDLE: veil of ghostly ribbons around Mereth while the choristers live | — |
| Silent Place | instant: CastRelease | — | GROUND_ACTIVE 12 s loop: dark indigo pool, 4.5 rim, motes sinking into it, ripples running inward | muffled sub thump + whisper; low hush loop |
| Dissonance | instant: CastRelease | — | GROUND_ACTIVE 2 s: concentric magenta rings contracting and jittering on the 4.0 rim; GROUND_EXPIRED: sonic ring burst and column | discordant bell cluster rising; discordant choir blast |
| Dirge of the Grave | CHANNELING 5 s: ChannelUp (conducting), violet voices spiralling up, a ring pulse every second | — | small violet hit on each player per pulse | dark male choir drone loop |
| The Choir Rises | ChannelUp, magenta motes | EmoteShout, burst | — | choir crescendo |
| Choral Resonance | — | — | AURA_IDLE: magenta harmonic rings orbiting Veyr | soft harmonic hum on gain |

## Engine work the spells need

The spell runtime could not express three of the brief's mechanics as data:

- **Frontal cone.** `spell_effect_targets::ConeEnemy` existed in the enum with no resolver.
- **Summons from a spell.** `spell_effects::Summon` was an empty handler; only the trigger action
  `SummonCreature` could spawn units.
- **Ground zones.** `spell_effects::PersistentAreaAura` was an empty handler, and nothing told the
  client about an area on the ground.

All three now exist, plus two fixes the boss spells needed:

- **ConeEnemy** resolves enemies within `radius` and within `miscvalueb` degrees (default 90) of
  the caster's facing (`spell_target_resolver.cpp`, `IsInPlanarCone`).
- **Summon** spawns `summonunit` x `basepoints` creatures evenly on a circle of `radius` around
  the caster, starting at its right, so two summons stand on opposite sides. They despawn after
  the spell's `duration` (0 = never), attack the caster's victim, and their death raises
  `OnSummonedUnitDied` on the caster — the hook the encounter triggers will use.
- **PersistentAreaAura** creates a ground zone (`world/spell_zone_set.h`): `radius`, lifetime =
  spell `duration`, and every `amplitude` ms it casts `triggerspell` on each enemy inside. An
  amplitude equal to the duration means exactly one tick as the zone runs out — the delayed
  detonation of Guttering Candle and Dissonance. A zone dies with its caster. Clients learn of
  it through `SpellZoneStart` / `SpellZoneEnd` (game protocol 19) and play the visualization's
  `GROUND_ACTIVE` kits at the position and its `GROUND_EXPIRED` kits on expiry.
- **CannotBeInterrupted** (`spell_attributes_b`, bit 11): kicks used to stop every cast. Grave
  Strike and the three summons opt out; Lament and Dirge of the Grave stay interruptible.
- An **interrupted channel** now removes the aura it put on its caster. Before, a kicked channel
  kept ticking to its natural end (Fire Barrage did too).

Covered by `game_server_tests` (`[cone]`, `[summon]`, `[spell_zone]`) and the E2E scenario
`hollow_choir_boss_spells.lua`.

## Testing the spells by hand

On a GM character: `learnspell <id>`, select a hostile NPC (a Training Dummy works well), cast.
The ground spells mark the floor under the selected NPC. Last Vigil, Mourning Voices and The
Choir Rises summon hostile adds next to the caster — godmode helps. Spells 254, 259, 261 and
263 are the internal hits of the zones and of the Dirge; they are not meant to be cast directly.

| Boss | Spells to learn |
|---|---|
| Brother Oswin | 251 Grave Strike, 252 Last Vigil, 253 Guttering Candle |
| Sister Mereth | 255 Lament, 256 Mourning Voices, 257 Mourning Chorus (self buff), 258 Silent Place |
| Cantor Veyr | 260 Dissonance, 262 Dirge of the Grave, 264 The Choir Rises, 265 Choral Resonance (self buff, stacks) |

Sounds were picked by measurement only and still need a listen; the sound agent flagged Lament
Cast (fades instead of building), Choir Rises Cast, the Dissonance bell pitch, and how much the
Candle Flare reads as fire. Every layer take is in `generated/hollow_choir/sfx/` for swapping.

## Open work

- Re-point quests 58-61 (kill objectives on 81, 84, 85) at the new bosses.
- Spawn the bosses once the new layout has coordinates.
- Encounter triggers (step 3).
- Update `docs/world/bible.md` section 3.9 and the named-characters table once the user confirms
  the new roster's lore.
