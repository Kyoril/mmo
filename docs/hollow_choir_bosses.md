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
| 265 | Veyr | Choral Resonance | +12 % damage for 6 s; each living chorister keeps its own copy on Veyr refreshed |

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
| Choral Resonance | — | — | AURA_IDLE: magenta harmonic rings orbiting Veyr | none (refreshed every few seconds) |

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
  detonation of Guttering Candle and Dissonance. A zone lasts while its caster is in the world,
  dead or alive (a Candlebearer's flame outlives it), and ends when the caster leaves. Clients learn of
  it through `SpellZoneStart` / `SpellZoneEnd` (game protocol 19) and play the visualization's
  `GROUND_ACTIVE` kits at the position and its `GROUND_EXPIRED` kits on expiry.
- **CannotBeInterrupted** (`spell_attributes_b`, bit 11): kicks used to stop every cast. Grave
  Strike and the three summons opt out; Lament and Dirge of the Grave stay interruptible.
- **Summoned creatures reward nobody** (`GameCreatureS::GrantsKillRewards`): no experience, no
  class experience, no quest kill credit, no loot — for spell summons and for the
  `SummonCreature` trigger action alike, so a boss's adds cannot be farmed.
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
| Cantor Veyr | 260 Dissonance, 262 Dirge of the Grave, 264 The Choir Rises, 265 Choral Resonance (target yourself or an ally; 6 s) |

Sounds were picked by measurement only and still need a listen; the sound agent flagged Lament
Cast (fades instead of building), Choir Rises Cast, the Dissonance bell pitch, and how much the
Candle Flare reads as fire. Every layer take is in `generated/hollow_choir/sfx/` for swapping.

## Feedback round 1 (2026-10-08)

The user tested every spell by hand. Changes made from it:

- Guttering Candle (and every spell but Grave Strike) no longer needs its target in front.
- Summoned adds dropped loot; summons now never reward their killers (see above).
- Dissonance flashed too much: its telegraph beats are shallower and stay in one colour, and
  the detonation flash and light are softer.
- Choral Resonance never went away: it was a permanent stacking aura waiting for triggers to
  take stacks off. It is now a 6 s aura per chorister, so it decays by itself.

## Layout (map 1, Monastery_001)

The dungeon geometry is the world model `Models/Dungeon/Monastery_001.hwmo`, placed at (8, 1, 0) and
turned 90 degrees. **The design sketch's north (towards the apse) is world -X, its east is world -Z.**
Floors: entrance hall and south yard y 1.2, Wake of the Dead and nave y 0.2, the raised west wing
(apse, cloister) y 5.2.

| Sketch | World area (x, z) | Notes |
|---|---|---|
| Eingang | x 9..17, z -2..2 | |
| 01 Vorhalle | x -21..9, z -6..5, plus the south yard x -25..11, z 5..30 | |
| 02 Totenwache | x -21..9, z -46..-7 | sarcophagus x -10..-3, z -33..-11 |
| G1 | corridor x -32..-21, z -3..3 | seal at x -27 |
| 03 Kirchenschiff | x -79..-32, z -12..12 | pillar rows z -6 and +6, pews x -70..-40 |
| Sakristei | niche x -40..-34, z -24..-17 | |
| Treppe / G2 | x -86..-79, z -6..6 | seals at x -85.5 |
| 06 Apsis | round room x -104..-85, z -12..12 | podium at the centre, passages north and south |
| 04 Kreuzgang | x -105..-76, z 18..48 | arcade pillars x -100.5 and -82 |

`tools/hollow_choir/survey_layout.py` renders the floor plan with the walkable navmesh;
`tools/hollow_choir/author_spawns.py` places the spawns, checks them against the navmesh and the
spacing rule, and renders `generated/hollow_choir/spawns.png`.

**Portal culling.** The six groups (rooms) carry containment volumes derived from their floors by
`tools/world/wmo_rooms.py`; without them the engine placed the camera by the groups' bounding
boxes, which overlap across the hall, the yard and the nave, and culled the hall from its own
entrance. The same run relinked portals 6 and 7 (hall to south yard; they were linked to the
Wake). Re-run `py -3 tools/world/wmo_rooms.py Models/Dungeon/Monastery_001.hwmo --write` after
changing rooms, portals or floors in the editor; portal 8 (towards the cloister) sits inside the
cloister floor until the passage exists, and the tool reports it rather than guessing.

**Spacing.** Creatures in combat call idle allies within 8 units, so members of two packs stay at
least 12 apart (assist radius plus drift). Pairs a wall separates are exempt. Player aggro is a
different, larger radius (20 at equal level, +1 per level the creature is above the player), and
walking through a room still pulls; that is intended.

**Groups.** A (hall), B (Wake), C, D, E, F (nave), H, I (cloister), J, K (passages beside the apse),
M (sacristy), patrols P1 (nave, round the pews) and P2 (cloister arcades). Trash does not respawn
during a run. G (passage to the cloister) and L (gallery) wait for the geometry.

**Trash.** Gravewarden (92, Warden's Cleave 266), Mourning Cantor (93, Mournful Dirge 267),
Candlebearer (94, leaves Spilled Wax 268 where it dies), Restless Novice (95).

## Encounters

`tools/hollow_choir/author_triggers.py`, triggers 53-76.

| Boss | AI (creature spells, at the victim) | Triggers |
|---|---|---|
| Oswin | Grave Strike every ~10 s | Guttering Candle under a random player every 14-16 s; Last Vigil at 50 %; death opens G1 |
| Mereth | Lament every 18 s | Silent Place under a random non-tank every 15-19 s; Mourning Voices + Mourning Chorus at 65 % and 30 %; the chorus drops with the last chorister; death opens G2 |
| Veyr | Dirge of the Grave every 25 s | Dissonance under a random player every 12 s (8 s below 30 %); The Choir Rises at 60 % and 30 %; each Hollow Chorister keeps its own Choral Resonance on him |

Timed ground spells go out through the `ApplyAura` action, which casts as a proc: a normal
trigger cast is refused while the boss is still casting or channeling, and the timer would simply
be lost. Phase changes cancel the boss's current cast before they cast. Adds despawn when their
boss resets or dies (instance variables 2001-2003). Summoned creatures never reward their killers.

Covered by the E2E scenarios `hollow_choir_oswin_encounter.lua` and
`hollow_choir_mereth_veyr_encounter.lua`.

## Open work

- **Geometry (user):** a passage from the nave to the cloister. Then group G goes into it.
- **Rooms:** after the passage is built, re-run `tools/world/wmo_rooms.py` (see Layout) and make
  sure portal 8 sits on the nave/cloister boundary.
- **Navmesh:** the Wake of the Dead floor is 1 unit below the entrance hall with no ramp, and the
  stair from the nave to the west wing has almost no navmesh. Both are separate islands: creatures
  fight inside them but cannot follow players out. Rebuild the navmesh once the geometry is fixed.
- **Cramped rooms:** the apse and the cloister are small for a 20-unit player aggro radius.
  Fighting H or I can pull Mereth, and anything in the apse pulls Veyr.
- **Gate art:** the seals are placeholder walls (FP_Wall_01). The sketch wants the singing to stop
  audibly and the altar light to change when G2 opens; neither has a hook yet.
- Gallery (L) and its stair; the down stair south-west of the cloister.
- Re-point quests 58-61 (kill objectives on 81, 84, 85) at the new bosses.
- Mourning Cantors log "validation failed" for Dirge of the Hollow Choir (242) when a pillar
  blocks their line of sight; the creature AI does not check line of sight before choosing a spell.
- Update `docs/world/bible.md` section 3.9 and the named-characters table once the user confirms
  the new roster's lore.
