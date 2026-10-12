# Briarwatch balance and talent-gated spell ranks

Date: 2026-10-12. Branch: `feature/briarwatch-balance`.

## Goal

Characters fresh from Oakenshire (level 5-6) should be able to quest in Briarwatch March. Today a
level-6 fire mage loses almost every fight against the zone's humanoids (simulated win rates of
0-2% against level-7 Cutthroats, Marauders, Hexers and Poachers). Two causes are addressed:

1. Briarwatch creature data is far above the Oakenshire curve (HP x2.4, damage x3-5).
2. The fire mage has no damage progression at level 6: Fireball is a talent with a single rank
   (13-22 damage, max level 5), while frost mages get Frostbolt rank 2 (38-44) from the trainer.

Fixing 2 needs a general rule for spell ranks that continue a talent spell, because Fireball rank 1
is a talent and rank 2 is bought from a trainer.

## Requirements (from the user)

- A follow-up rank of a talent spell can only be learned while the talent is learned.
- Resetting talents (possible at the class trainer at any time) also removes all follow-up ranks of
  that talent from the player.
- Learning the talent again restores every follow-up rank the player had bought, without paying
  again.
- The trainer shows such a rank while the talent is missing, but locked, with the hint
  "Requires talent: Fireball".
- No new talent tree nodes for spell ranks; Fireball stays a talent.

## Non-goals

- Fire Barrage rank 2 (the mechanism supports it; only data would be needed later).
- The `pointsperlevel` truncation bug in `spell_effects.cpp` (handled in a separate task). Fireball
  rank 2 values below are chosen at level 6 and stay sensible whether or not the fix lands.
- Any change to the creature stat formulas or unit class level tables.

## Part 1: Data

All data lives in the `data/editor` submodule (`data/editor/data/*.data`).

### Creature retune (Briarwatch March)

`elite` is `eliteStatMultiplier`, `dmg` is `minmeleedmg`, `dpl` is `damagePerLevel`.

| Unit | Name | elite | dmg | dpl | Other |
|---|---|---|---|---|---|
| 67 | Duskfang Stalker | 1.0 (unchanged) | 7.5 → 3.5 | 1.0 → 0.75 | |
| 48 | Kingsroad Cutthroat | 1.15 → 1.0 | 7.5 → 4.0 | 0.9 → 0.75 | |
| 49 | Kingsroad Hexer | 1.10 → 1.0 | unchanged | unchanged | Shadowbolt (180) creature cooldown 5000-7000 ms |
| 66 | Roadside Marauder | 1.20 → 1.0 | 8.0 → 3.0 | 1.0 → 0.7 | |
| 50 | Marshland Ambusher | 1.20 → 1.0 | 8.5 → 4.0 | 1.0 → 0.75 | |
| 51 | Marshland Hexer | 1.15 → 1.0 | unchanged | unchanged | Shadowbolt (180) creature cooldown 5000-7000 ms |
| 55 | Bog Rat | 1.0 (unchanged) | 8.5 → 4.0 | 1.0 → 0.75 | |
| 56 | Mire Prowler | 1.0 (unchanged) | 9.5 → 4.0 | 1.0 → 0.75 | |
| 57 | Mirewater Poacher | 1.20 → 1.0 | 10.0 → 3.0 | 1.0 → 0.7 | |

The creature cooldown is the `mincooldown`/`maxcooldown` of the unit's `creaturespells` entry for
spell 180.

### Fireball ranks

- Spell 4 (Fireball, talent): set `rank = 1`, `baseid = 4`. Values unchanged.
- New spell 1427, "Fireball" rank 2, copied from spell 4, with:
  - `rank = 2`, `baseid = 4`; same school, `familyflags` (8), icon, visualization, range, speed,
    attributes, so the fire talents (Burning Soul, Kindled Fury, Smoldering Wounds) apply.
  - Direct damage effect: `basepoints = 29`, `diesides = 9` (29-38).
  - Periodic effect: `basepoints = 4` per tick, 2 ticks over 4 s (amplitude 2000, duration 4000).
  - `casttime = 1800`, `cost = 45`, `spelllevel = 6`, `baselevel = 6`, `maxlevel = 10`.
  - `pointsperlevel` 0.6 on the direct effect (same as rank 1).
  - Localized name copied from spell 4.
- Trainer 1 (mage class trainer): add spell 1427, `reqlevel = 6`, `spellcost = 450` (same as
  Frostbolt rank 2).

### Expected result (simulation, level-6 fire mage, all attribute points in Intellect)

| Opponent | Today | Retune, Fireball rank 1 | Retune, Fireball rank 2 |
|---|---|---|---|
| Kingsroad Cutthroat L7 | 2% win | 99% / 33% HP left | 100% / 53% |
| Roadside Marauder L7 | 0% | 58% / 18% | ~97% / ~30% |
| Mire Prowler L8 | 5% | 87% / 22% | 100% / ~45% |
| Mirewater Poacher L8 | 0% | 25% / 12% | ~85% / ~19% |

Fire with rank 2 lands on par with a frost mage using Frostbolt rank 2 without talents.

## Part 2: Server mechanism

### Definition

A spell is a **talent-gated follow-up rank** when:

- its `baseid` is non-zero and differs from its own id, and
- the spell `baseid` points to is a rank spell of some talent (`TalentEntry.ranks`).

Its **gating talent** is that talent. Rank chains are linked through `baseid` today (Frostbolt rank 2
has `baseid = 3`); `prevspell`/`nextspell` are unused by game logic and stay that way.

A helper `GetGatingTalent(const proto::SpellEntry&, const proto::Project&)` (name may follow local
style) returns the gating talent entry or `nullptr`. It sits next to the existing `IsTalentSpell`
helper in `src/shared/game_server/objects/game_player_s.cpp`, moved into a small header/source pair
if needed so it can be unit tested. A follow-up rank is not itself a talent spell, so it continues to
be stored in `m_knownSpellIds`.

### Active vs. known

`GamePlayerS` already keeps two sets: `m_spells` (active: castable, in the spellbook, sent to the
client) and `m_knownSpellIds` (persisted, all classes). A spell can already be known but inactive
when the active class cannot use it (`IsSpellActiveForCurrentClass`).

Change: `IsSpellActiveForCurrentClass` additionally returns false for a talent-gated follow-up rank
whose gating talent is not in `m_talents`. Being inactive never removes the id from
`m_knownSpellIds`.

### Re-evaluation

New `GamePlayerS::RefreshTalentGatedSpells()`: for every id in `m_knownSpellIds` that is a
talent-gated follow-up rank, call `AddSpell` when it is now active and not in `m_spells`, and
`RemoveSpell` when it is now inactive and in `m_spells`. Called at the end of:

- `LearnTalent` (learning the talent activates bought ranks),
- `ResetTalents` (all follow-up ranks become inactive),
- `InitializeTalents` (login and class switch; runs after `SetKnownSpells` /
  `ActivateKnownSpellsForCurrentClass`, which leave gated ranks inactive because `m_talents` is
  still empty or being rebuilt).

Learn/unlearn packets go out through the existing `OnSpellLearned`/`OnSpellUnlearned` paths, which
already stay silent before spawn and during a class switch; `SendInitialSpells` after spawn and
after `classChanged` sends the final active set. No new packets, no DB migration, no protocol
version bump.

### Trainer purchase

`Player::OnTrainerBuySpell` (`src/world_server/player_npc_handlers.cpp`): after the spell lookup and
before `ConsumeMoney`, reject a talent-gated follow-up rank whose gating talent is not learned. The
rejection is silent on the wire (with a `WLOG`), like the existing "already known" early return; the
client never sends this request because it pre-checks the same rule. No new `TrainerBuyError`
result value, so the protocol stays unchanged.

A dormant rank (known, inactive) cannot be bought twice: while the talent is missing the gate
rejects; once the talent is learned the rank is active and the `HasSpell` early return applies.

## Part 3: Client

- `TrainerClient` (`src/mmo_client/systems/trainer_client.cpp`) evaluates the same rule with the
  client's spell and talent data: the entry's gating talent and whether the player currently has
  any rank of it (via the talent rank spells, as `TalentClient` does).
- `TrainerClient::BuySpell` refuses locally when the gating talent is missing.
- New Lua export `GetTrainerSpellRequiredTalent(index)` returning the gating talent's display name
  (its first rank spell's name) and whether it is learned, or nothing when the entry is not gated.
- `TrainerFrame.lua` (`data/client/Interface/GameUI/`): new state `TALENT` in `TrainerSpellState`,
  checked after LEARNED and before LOCKED; drawn red like LOCKED and hidden by the "hide
  unavailable" filter like LOCKED; shows the hint `TRAINER_REQUIRES_TALENT` ("Requires talent: %s").
  The list already refreshes on `SPELL_LEARNED`; it additionally refreshes on
  `PLAYER_TALENT_UPDATE`.
- Localization: add `TRAINER_REQUIRES_TALENT` to every `data/client/Locales/*/Localization.txt`
  (German: "Benötigt Talent: %s").
- Spellbook and action bar need no change: an unlearned rank disappears from the spellbook and its
  action button becomes unusable; on relearn the button works again.

## Testing

Catch2 tests in `src/tests/game_server_tests/` with a small fixture project (talent whose rank spell
is A; spell B with `baseid = A`; unrelated rank chain C/D):

1. `GetGatingTalent`: B is gated by the talent; A, C and D are not; a spell whose `baseid` is its
   own id is not.
2. Without the talent, B in the known set is not active.
3. Learning the talent activates a known B.
4. `ResetTalents` removes B from the active set and keeps it in `m_knownSpellIds`.
5. Learning the talent again re-activates B.
6. Serialize/deserialize (`operator<<`/`>>` and `SetKnownSpells` + `InitializeTalents`) keeps B
   known and active when the talent is saved.
7. Class switch away and back keeps B and re-activates it.
8. Trainer purchase gate: covered by testing the predicate the handler uses; the handler itself is
   exercised by the E2E scenario if the harness supports talents and trainers.

Then the fast gate (`/gate`). If the E2E harness can learn and reset talents and buy from trainers,
add one scenario: buy rejected without the talent, learn talent, buy, reset (rank gone), learn
talent again (rank back without paying).

## Delivery

- Data changes are committed in the `data/editor` and `data/client` submodules; the main repository
  updates the submodule pointers on `feature/briarwatch-balance`.
- Pushing the submodule `master` branches and the merge to `develop` happen only with the user's
  explicit go-ahead (`/ship`).
