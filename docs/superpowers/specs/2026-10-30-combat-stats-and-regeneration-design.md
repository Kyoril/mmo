# Combat stats, ratings and real-time regeneration

Status: design, 2026-10-30. Follows docs/superpowers/specs/2026-10-29-data-driven-attributes-design.md.

## Goals

1. Every combat chance (crit, dodge, parry, block, hit, haste, ...) is a data-driven **combat stat** with a
   base value, level scaling, cap and attribute sources, instead of literals in code.
2. **Ratings** (Crit Rating, Hit Rating, Defense Rating, ...) exist as game data, can be put on items and
   auras, and convert to combat stats with a per-level curve.
3. Mechanics that ratings need but the game lacks are added: spell hit/miss, attack and cast haste,
   expertise, defense, resistances with partial resists.
4. The remaining hard-coded formula constants move into data.
5. Regeneration becomes time based: exact on the server at any moment, cheap to run for thousands of
   units, predicted smoothly on the own client, and synced every regeneration tick.

Non-goals: XP formulas (`experience.h`, quest XP step-down) stay as they are for now.

## Part 1 - Combat stats

### Model

Mechanics need code (something has to roll dodge), values do not. So the *set* of combat stats is a code
enum and everything about them is data:

```
combat_stat::Type   (src/shared/game/combat_stat.h)
  MeleeCrit, SpellCrit, Dodge, Parry, Block, CritBlock, MeleeHit, SpellHit,
  MeleeHaste, CastHaste, Expertise, Defense, SpellPenetration, Count_
```

`combat_stats.proto` (server + client, same field numbers, exported to ClientDB):

```
CombatStatEntry {
  id                      = combat_stat::Type
  name, description       (+ _loc 200/201)
  base        float       value at level 1, in percent (SpellPenetration: resistance points)
  perLevel    float       added per level above 1
  minValue / maxValue     clamp of the final value (0 = no max)
  showOnCharacterSheet    bool, sheetOrder
}
```

Shipped values reproduce today: MeleeCrit 5, Parry 5, Block 5, CritBlock = `base_critical_block_chance`,
Dodge = `base_dodge_chance`, SpellCrit = `spell_default_crit_chance`; MeleeHit, SpellHit, haste, expertise,
defense, penetration 0.

### Value of a combat stat for a unit

```
value = base + perLevel * (level - 1)
      + sum over the unit's class sources   (attribute / pointsPer)        -- per class / unit class
      + sum over ratings feeding the stat   (rating / ratingPerPercent(level))
      + flat aura bonus                     (ModCombatStat, misc = stat)
clamped to [minValue, maxValue]
```

* Class sources generalise today's `meleeCritSources` / `dodgeSources`:
  `ClassEntry.combatStatSources` / `UnitClassEntry.combatStatSources`, each `{ stat, attribute, pointsPer }`.
  The data migration converts the existing Agility/20 crit and dodge sources.
* Victim-dependent terms (level difference, defense, expertise) are applied in the roll, not in `value`.
* Each unit replicates its combat stat values: unit field block `CombatStat` (one float per stat). This
  replaces `MeleeCritChance` / `SpellCritChance` and gives the character sheet dodge, parry, block, hit,
  haste... for free.

### Ratings

`combat_ratings.proto` replaces the unused gt tables (`gtvalues.proto` and its five empty `.data` files are
deleted):

```
CombatRatingEntry {
  id                       < MaxCombatRatings (32)
  name, description        (+ _loc)
  repeated stats           combat stats this rating raises (e.g. Hit Rating -> MeleeHit + SpellHit)
  repeated float ratingPerPercent   index = level - 1; past the end the last value is used
}
```

* Item stats: `item_stat::RatingStart (32) + rating id`, next to `AttributeStart (64) + attribute id`.
* Aura `ModCombatRating` (misc = rating id, base points = rating).
* Players replicate their rating totals (player field block `CombatRating`) for character sheet tooltips
  ("Crit Rating 46 (+2.3% at level 10)").
* The editor can generate the per-level curve from a formula (start value, growth per level).

Shipped ratings: Crit (MeleeCrit + SpellCrit), Hit (MeleeHit + SpellHit), Haste (MeleeHaste + CastHaste),
Dodge, Parry, Block, Expertise, Defense, Spell Penetration. No item carries any yet.

### Mechanics

Melee attack table (`RollMeleeOutcomeAgainst`), order unchanged:

| Outcome | Chance |
|---|---|
| Miss | `melee_base_miss` + level/skill difference term (data) + dual-wield penalty (data) - attacker MeleeHit + victim Defense * `defense_miss_factor` |
| Dodge | victim Dodge - attacker Expertise |
| Parry | victim Parry - attacker Expertise (only if the victim can parry) |
| Glancing | unchanged formula, condition fixed to "victim level > attacker level" behind a setting (today equal levels also glance with 0 reduction) |
| Crit | attacker MeleeCrit - level penalty (`crit_penalty_per_level`) - victim Defense * `defense_crit_factor` + talents + crit taken |
| Crushing | `crushing_level_threshold` used consistently (the hard-coded `-3` goes) |
| Block (separate roll) | victim Block; crit block uses CritBlock |

Spells (new):

* **Spell hit**: harmful spells roll miss once per target: `spell_miss_by_level_diff[diff]` (data table,
  default 4% at equal level, rising per level) - caster SpellHit, floored at `spell_min_miss`. A miss sends
  the existing miss/resist combat log so the client shows "Miss". Helpful spells never miss.
* **Spell crit**: caster SpellCrit (now with class attribute sources, e.g. Intellect) + spell mods + crit
  taken. Healing uses SpellCrit too; `heal_crit_multiplier` replaces the hard-coded 5% / x2.
* **Weapon-damage spells** roll the melee table (miss/dodge/parry) and use MeleeCrit instead of
  `spell_weapon_default_crit_chance`.
* **Resistances**: per school resistance already exists on units (items, ModResistance auras). Damage of a
  non-physical school is reduced by `resist% = R / (R + resist_constant + resist_level_factor * casterLevel)`,
  `R = max(0, resistance - caster SpellPenetration)`, capped at `max_resist_pct`. Partial resists are binned
  (0/25/50/75/100%) around that average, so individual hits vary like classic MMOs; the resisted amount goes
  into the existing `resisted` field of the damage logs. Periodic damage resists per tick.
* **Haste**: swing time = base / (1 + MeleeHaste/100) (the `ModAttackSpeed` aura feeds MeleeHaste instead
  of the unused `unit_mods::AttackSpeed`); cast time = base / (1 + CastHaste/100) after spell mods; the GCD
  is reduced too, floored at `global_cooldown_min_ms`.
* **Expertise / Defense** as in the table above.

### Hard-coded constants that move to data

| Today | Becomes |
|---|---|
| Base melee crit 5, parry 5, block 5 | combat stat bases |
| Miss 5%, 0.1 / 0.4 per skill point, threshold 10, dual wield +19% | combat settings |
| Weapon skill = 5 x level | combat setting `skill_per_level` |
| Crit -0.2% per victim level | combat setting |
| Crushing `-3` | uses `crushing_level_threshold` |
| Heal crit 5% / x2 | SpellCrit + `heal_crit_multiplier` |
| Spell crit = one global value | SpellCrit stat + class sources |
| Health/mana from attributes: first 20 points 1:1 (players) / worth nothing (creatures) | class / unit class `attributeBaseThreshold` + `belowThresholdFactor`; shipped values reproduce both behaviours |
| Attack power: 14 AP = 1 DPS | combat setting `attack_power_per_dps` |
| Attack power aura (`ModAttackPower`) has no effect | applied (bug fix; no shipped spell uses it? checked during implementation) |
| Attribute point cost curve `1.5*sqrt(n+2) + 0.02*n^1.5` | combat settings `attribute_cost_*` |
| Rage-from-damage-taken constants (`2.5`, own quadratic) | the existing `rage_*` settings + `rage_taken_factor` |
| Client armor formula 400 / 85 / 75% | client reads the same combat settings (ClientDB export of `combat_settings`) |
| Client health/mana-from-stat tooltip math (differs from server) | shared formula code used by both |

## Part 2 - Real-time regeneration

### Server: exact at any time, settled lazily

Per unit and resource (health, mana, rage, energy) the server keeps a **settle anchor**: the time the
resource field was last brought up to date, plus a fractional remainder. The regeneration rate is piecewise
constant between events:

* health: class/unit class rate (+ aura %), 0 in combat (or `in_combat_health_regen_pct`),
* mana: normal rate, or the casting rate within `five_second_rule_ms` after spending mana,
* energy: `energy_regen_per_second`, rage: `-rage_decay_per_second` out of combat.

`SettleRegeneration(now)` integrates every resource from its anchor to `now` exactly - including the
five-second-rule boundary inside the interval - adds it to the field (keeping the fraction) and moves the
anchor. It runs:

1. **before every read that decides something**: spell cost checks (`ConsumePower`), damage/heal/energize,
2. **before every rate change**: entering/leaving combat, spending mana, aura or stat changes,
3. **on the sync tick** (`regen_sync_interval_ms`, 2000), which is what makes other clients see changes.

So the 12/15 mana case works: when the client's prediction says 15, the server's cost check settles first
and also has 15.

Cost for thousands of units:

* A unit only has a sync timer while some resource is moving (below max with a positive rate, or rage
  above 0). Full units schedule nothing; damage, cost or a rate change re-arms it. Creatures out of combat
  keep resetting to full as today.
* Settling is a few multiplications; field writes only happen when the integer value changes, so the
  network traffic is the same as today's tick at most.

### Client: predicted own player

The own player's client extrapolates its health and powers between server syncs:

* Rates come from player fields the server updates whenever a rate changes: `HealthRegen`, `ManaRegen`,
  `ManaRegenCasting`, `EnergyRegen`, `RageRegen` (per five seconds, like the existing mana fields).
* Prediction anchor = the value and local time of the last field update. The client starts its own
  five-second window when it sees its mana drop from a cast.
* Every frame `WorldState` computes the predicted integer values; when one changes it raises
  `PLAYER_POWER_CHANGED` / `PLAYER_HEALTH_CHANGED`, so bars, action-button usability and the cast
  pre-check follow without UI changes. `unit:GetPower()` / `GetHealth()` return the predicted value for the
  own player.
* The prediction is conservative by construction: the client learns about every value and state change
  one latency later than the server, so it never shows more than the server has (except clock drift,
  corrected on the next sync).

Other units are not predicted; their bars update with the sync tick.

## Rollout

Separate branches, each gated and merged on its own:

1. **Regeneration** (self-contained).
2. **Combat stat framework**: data model, replication, character sheet, editor, migration of existing
   crit/dodge/parry/block/crit block/miss and the listed constants. No behaviour change with shipped data
   except the documented bug fixes.
3. **Ratings**: data, item stats, auras, sheet, editor.
4. **New mechanics**: spell hit, haste, expertise, defense, resistances.

Protocol bumps per branch as needed; game data migrations via tools/ scripts like the attribute migration.
