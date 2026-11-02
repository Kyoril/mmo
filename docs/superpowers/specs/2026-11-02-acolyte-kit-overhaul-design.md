# Acolyte kit overhaul

Date: 2026-11-02. Branch `feature/acolyte-kit-overhaul` (data/client and data/editor on branches of
the same name). Approved in chat by the user ("Yes, build it").

## Why

Playing the acolyte today is two buttons: keep Corrupting Touch up and spam Shadowbolt. The class
trains 4 combat spells (Mage ~10, Scout ~8), has no interrupt, slow, root, self-heal, mana tool or
area damage before a capstone, and 10 of its 27 talent nodes are placeholders. Only Blight is fun,
because its talents add logic (rot that heals, kill rewards, spreading). The Plaguebearer capstone
does nothing at all: it is a `ChanceOfSuccess +100` modifier, the spread was never implemented.

The user wants three things (their words, paraphrased):

- **Summons** — a tree that empowers the Duskling and calls small temporary dusklings mid-fight.
- **Rot and control** — PvP tools: slows, weakening, longer cast times, temporary Stamina loss.
- **More rot, more area** — more than one DoT, real area damage beyond a slow single-DoT spread.

## Goals

- Every acolyte gets a real rotation from the trainer: curses, a second DoT, a drain, an AoE, a
  temporary summon, spread over levels 1-12 (current content range).
- Three talent trees with distinct identities, all 27 nodes real:
  **Summoner** (replaces Umbra), **Blight** (DoTs and area), **Dread** (curses and control).
- Every new active spell, aura and proc the player sees is presented: visualization kits
  (particles, lights, tint), layered generated sound, cast animation, and an icon that **no other
  spell in the game uses** (ranks of the same spell may share one).
- Plaguebearer does what its tooltip says.
- Embrace of Dread's "+20% shadow damage" works: its modifier (like Deepening Dark and Dark
  Insight) uses family mask `0x1`, which no acolyte spell carries, so it never applied. Talent
  masks must name the real family bits of the acolyte's damage spells.
- All acolyte German texts (new and touched) use Ihr/Euch/Euer, never du/dich/dein.

## Non-goals

- No change to Shadowbolt / Corrupting Touch / Fear / Veil basics beyond the curse rule below.
- No new classes of engine system (no stealth, no portals, no ground-targeting UI).
- No rebalance of other classes.
- No new content (quests, zones). The Duskling quest line stays the only way to bind the pet.

## Anti-goals

- Two spells sharing an icon, or a spell shipping without visuals/sound ("silent grey button").
- A talent that does nothing (the Plaguebearer failure mode). Every talent must be covered by a
  test or E2E check that observes its effect.
- Sounds on aura applied/idle/tick events (they re-fire when units come into view).
- A tooltip that lies about numbers.

## The kit

Numbers are starting values for level 1-12 content, tuned to sit beside the mage's spells of the
same level; the builder may tune them, keeping the role.

### Trainer spells (trainer 4, all acolytes)

| Lvl | Spell | Kind | Effect |
|---|---|---|---|
| 1 | Shadowbolt (existing) | | |
| 1 | Veil of Shadows (existing) | | |
| 2 | Corrupting Touch (existing) | | |
| 3 | **Curse of Frailty** | curse, instant, 30 yd | 30 s: target Stamina -10%, damage done -5%. |
| 4 | **Drain Essence** | channel 3 s | each 1 s: shadow damage, caster healed for 100% of damage dealt. |
| 5 | Fear (existing) | | |
| 6 | Shadowbolt r2 (existing) | | |
| 6 | **Curse of the Mire** | curse, instant, 8 s cd | 10 s: movement speed -40%. |
| 7 | **Wasting Plague** | DoT, instant | 18 s shadow DoT (own track beside Corrupting Touch), target attack speed -15%. |
| 8 | Shadow Lore: Murkbind (existing) | | |
| 9 | **Call Gloomlings** | instant, 90 s cd | 2 small temporary dusklings ("Gloomlings") for 15 s, attack the caster's target. |
| 10 | **Rot Field** | 1.5 s cast, 20 s cd | rot patch at the target's position, 8 s, shadow damage to every enemy inside every 2 s. |

**Curse rule:** an acolyte keeps at most **one curse per target**; casting another curse of the
same acolyte replaces it. Curses: Curse of Frailty, Curse of the Mire, Tangled Tongue (talent).
Different acolytes' curses coexist.

### Summoner tree (replaces Umbra; tab keeps its slot)

| Node | Talent | Effect |
|---|---|---|
| root | **Dark Bond** (2 ranks) | Duskling damage +15/30%, health +10/20%. |
| root | **Shadow Sympathy** *(replaced "Pack Instinct" during planning: a conditional "while the pet is out" aura has no engine support)* | the Duskling takes 20% less damage. |
| root | **Swarm Lore** | Call Gloomlings summons 3 and they last 5 s longer. |
| mid | **Gloom Tether** | Corrupting Touch ticks have a 10% chance to spawn one Gloomling for 8 s. |
| mid | **Shared Night** *(changed during build: a proc cannot see the damage that triggered it)* | each Drain Essence tick also heals the Duskling for a fixed, level-scaled amount (about half a tick). |
| mid | **Empower Duskling** (active, 1 min cd) | Duskling +30% damage and +30% attack speed for 12 s. |
| late | **Dark Tutelage** (builder's call) | Call Gloomlings cooldown -30 s. |
| late | **Soul Siphon** (builder's call) | Drain Essence channels 1 s longer. |
| capstone | **Brood Horror** (active, 3 min cd) | summon a large temporary horror for 20 s that fights the caster's target. |

### Blight tree (existing talents kept)

Kept: Rapid Decay, Endless Decay, Withering, Leeching Rot, Void Resurgence.
**Plaguebearer (capstone) fixed:** when a target carrying the acolyte's Corrupting Touch dies,
Corrupting Touch is applied to up to 2 enemies within 10 yd of the corpse.
Placeholders become:

- **Miasma** *(changed during build: no per-field memory in data)* — Rot Field applies Corrupting Touch to enemies it ticks on; every tick refreshes it rather than applying it once per enemy per field.
- **Creeping Rot** — Rot Field lasts 4 s longer and slows enemies inside by 30%.
- **Venomous Backlash** — when hit in melee, 20% chance to apply Wasting Plague to the attacker.

### Dread tree (existing talents kept)

Kept: Dread Presence, Grave Endurance, Midnight Tithe, Soul Armor, Embrace of Dread (capstone).
Placeholders become:

- **Tangled Tongue** (active curse) — target cast times +30% for 20 s.
- **Grasping Dread** — Curse of the Mire also roots the target for 2 s when applied.
- **Wail of the Lost** (active, 1 min cd) — fear up to 3 enemies within 8 yd for 3 s.
- **Lingering Dread** (was "Bound Servant") — Curse of Frailty also reduces healing received by 20%.

### Retired

Umbra's talents (Deepening Dark, Whispered Bolt, Dark Insight, Gloaming + Gloaming Trance,
Abyssal Reach, Umbral Bloom) leave the trees. Their spells are neutered the way earlier retired
ranks are (`RETIRED_RANK_SPELLS` pattern), and spent talent points are refunded. Their
visualizations (134, 136) stay in the data for now (unused).

## Engine work

Decided in chat as the builder's responsibility; each needs a unit test in
`src/tests/game_server_tests/`:

1. **Leech** — damage that heals the caster by a share of the damage dealt (Drain Essence,
   Shared Night). Either a `HealthLeech` effect handler or a proc on the tick spell.
2. **Cast-time debuff** — a debuff that raises the *target's* cast times (Tangled Tongue), on
   players and creatures. Existing spell modifiers only touch the owner's own family-flagged
   spells, so this likely needs a new aura type hooked into cast-time calculation.
3. **Pet empowerment** — owner talents/actives that change the persistent pet's damage, health
   and attack speed (Dark Bond, Empower Duskling, Shared Night), surviving resummon and relog.
4. **Spread on death** — Plaguebearer's spread from a dying target to nearby enemies.
5. **Curse exclusivity** — via aura stacking categories if they support "one per caster per
   target, newest wins", else a small extension.

Anything on the wire (new packets/fields) needs the protocol version bump per CLAUDE.md. A new
aura type is data, not wire, unless the client must react to it.

## Presentation

Follows `docs/acolyte_spell_visuals.md` (art direction: dark smoke body + saturated rim; shadow
= violet, blight = acid green, dread = crimson) and extends `tools/acolyte_visuals/
author_acolyte_visuals.py`, particle recipes `tools/particle_gen/recipes/acolyte_*.py`, sound
recipe `tools/sfx_gen/recipes/acolyte.py`. Rules kept: no `duration_ms`, `sound_ids` only, no
sound on aura events, aura kits TARGET scope, only CASTING kits loop, sounds are layered mixes
(`tools/sfx_gen/layer.py`) of separately generated layers.

- Every new active: CASTING kit (CastLoop + hand particles + loop sound) when it has a cast or
  channel, CAST_SUCCEEDED (CastRelease + sound), IMPACT / AURA_IDLE / GROUND_ACTIVE as fits.
  Curses get a distinct on-target idle so a player can tell which curse is up.
- Gloomlings and the Brood Horror need creature entries and models: Gloomling = Duskling model
  scaled down/tinted; Brood Horror = a larger shadow creature model available in the data.
- **Icons:** every new spell gets a newly generated icon (image-generation tool, normalised to
  128x128, imported with `import_spell_icon.py`). A data test enforces that no two distinct spells
  (different `baseid` chains) share an icon among player-class spells, including the new ones.
  If existing spells already collide, fix those for the acolyte; report others.
  *Scope (builder's call, settled during review):* "a spell" is a player-visible ability: a
  trainer spell, a talent node, or a pet ability. A proc/aura spell triggered by exactly one
  talent or spell (Gloaming → Gloaming Trance, Leeching Rot → its heal) belongs to that ability and
  may share its icon; ranks of one chain share one. Retired and neutered spells are ignored. The
  test enforces this for acolyte spells (classmask 4, the Duskling's and Gloomlings' spells) against
  every other spell in the catalog; collisions entirely inside other classes are printed as a
  report, not failed (out of scope).
- Localization: names, descriptions, aura texts in every locale (deDE in Ihr/Euch form).

## Verification

- Unit tests per engine item; data tests (talent script drift, visual data, icon uniqueness).
- E2E scenario: an acolyte learns the new spells, casts each on a dummy, observes auras/damage,
  the curse replacement rule, Plaguebearer spread, summon counts.
- In-client look at every new effect (cast, impact, auras) — screenshots for the user.
- Fast gate green before `/ship`.

## Left to the builder

Exact spell ids, numbers within the stated roles, the two extra Summoner nodes, node placement in
the constellation layout, sound layer choices, model choice for the Brood Horror.
