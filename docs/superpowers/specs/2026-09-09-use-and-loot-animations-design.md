# Use animations on Open spells, and a held Loot pose

Date: 2026-09-09
Branch: `feature/use-and-loot-animations`

## Problem

Two gaps in character animation now that the player rigs carry more clips:

1. The `OpenLock` spells (opening chests, doors, mining nodes) play no character
   animation. Two spell visualizations exist for them, 14 "Open" and 13 "Mining",
   but both name clips (`"Open"`, `"Mining"`) that exist on no rig, and three of the
   four Open spells do not reference a visualization at all.
2. Looting a corpse or an object plays no animation. The `Loot` clip kneels the
   character down and has no loop, so it needs to be held on its last frame for as
   long as the loot window stays open, and it must stop the instant the character
   moves so nobody floats across the floor in a kneel.

## What already exists

Established by inspection before designing; the plan leans on all of it.

- `unit_flags::Looting` (0x00000004) is already a flag in
  `src/shared/game/object_type_id.h`. `Player::OpenLootDialog` sets it and
  `Player::CloseLootDialog` clears it (`src/world_server/player.cpp`), on
  `object_fields::Flags`, which is already replicated to nearby players. Nothing
  client-side reads it. No new flag and no wire change are needed.
- The rigs have the clips. `HumanMale` and `HumanFemale_V2` both carry `UseStart`,
  `UseLoop`, `UseEnd` and `Loot`. `OrcMale` (8 clips) and `OrcFemale` (1 clip) carry
  none of them.
- `AnimationState::SetTimePosition` clamps a non-looping clip at its length instead
  of wrapping. A held last frame therefore needs no new hold mode.
- `SpellVisualizationService` already supports one-shot kits, locked-loop kits and
  per-kit `delay_ms`, and already releases the locked loop on `CAST_SUCCEEDED` and
  `CANCEL_CAST`. Part A is pure data.
- `MovementInfo::IsChangingPosition()` tests `movement_flags::PositionChanging`
  (forward, backward, strafe, ascending, descending, falling; not turn-in-place).
  Both the client suppression and the server cancel use this one predicate, so the
  two tiers cannot disagree about what "moving" means.

## Current data

| Spell | Name | Cast time | visualization_id | miscvaluea |
|---|---|---|---|---|
| 39 | Open | 5000 | (unset) | 0 |
| 112 | Mining | 5000 | (unset) | 0 |
| 157 | Mining | 5000 | 13 | 1 |
| 235 | Open Door | 0 | (unset) | 3 |

Clip lengths, in seconds:

| Clip | HumanMale | HumanFemale_V2 |
|---|---|---|
| `UseStart` | 0.567 | 0.500 |
| `UseLoop` | 0.700 | 0.700 |
| `UseEnd` | 1.367 | 1.333 |
| `Loot` | 0.867 | 0.833 |

## Part A: Use animations on the Open spells (data only)

No engine change.

Visualization 14 "Open", rebuilt:

| Event | Kit | delay_ms |
|---|---|---|
| `CASTING` (2) | `UseStart`, one-shot | 0 |
| `CASTING` (2) | `UseLoop`, `loop: true` | 520 |
| `CAST_SUCCEEDED` (3) | `UseEnd`, one-shot | 0 |

520 ms is where `UseStart` ends; one number has to serve both rigs (0.567 / 0.500).

Visualization 13 "Mining", rebuilt with the same three kits as a placeholder until
pickaxe clips exist. It keeps its id, so swapping the clip names later is a data-only
edit and no spell needs re-linking.

Visualization 41 "Open Door", new. The client gates `START_CAST` and `CASTING` on
`castTime > 0` (`WorldState::OnSpellStart`), so an instant spell only ever sees
`CAST_SUCCEEDED`. That event therefore carries the whole sequence: `UseStart` at
delay 0 and `UseEnd` at delay 520. The flourish outlives the instant cast by design;
spell 235 keeps `casttime = 0`.

Spell links (`visualization_id`): 39 to 14, 112 to 14, 157 to 13 (already correct,
left alone), 235 to 41.

Constraints:

- No kit sets `duration_ms`. `ApplyAnimationToActor` turns it into
  `playRate = clipLength / duration`, which time-warps the clip.
- `CANCEL_CAST` gets no kits. The terminating handler drops the locked loop and the
  character returns to running, which is the right read for an interrupt.
- Kits are written without the zeroed `tint` block the editor emits by default.

Authoring: `tools/open_spell_visuals/author_open_visuals.py`, modelled on
`tools/warrior_visuals/author_visuals.py`. Validate-only by default, `--apply` writes
both trees, idempotent by id. Unlike the warrior script it also writes `spells.data`,
in both trees, because the `visualization_id` links are missing. Datasets touched:
`data/editor/data/spell_visualizations.data`, `data/editor/data/spells.data`,
`data/client/ClientDB/spell_visualizations.data`, `data/client/ClientDB/spells.data`.

Orc rigs resolve none of the `Use*` clips, so `ApplyAnimationToActor` logs a
"not found on entity" line per kit. That WLOG is downgraded to a debug log, since a
rig legitimately lacking a clip is not a warning.

## Part B: the loot pose (client)

`ANIM_SLOT_LOOT = 17` added to `animation_profiles.proto` in both
`src/shared/proto_data/` and `src/shared/client_data/`, at the same enum value. Added
to the builtin default table in
`src/shared/game_client/animation/animation_binding.cpp`, bound to the clip `"Loot"`.
Existing `animation_profiles.data` in both trees stays valid untouched: a new enum
value requires no data change. Per-profile overrides work for free, so a future race-
or context-specific loot clip is data.

`AnimationContext::looting`, filled in `GameUnitC::Update` alongside the other
per-frame flags:

    ctx.looting = (Get<uint32>(object_fields::Flags) & unit_flags::Looting) != 0
        && !m_movementInfo.IsChangingPosition() && !ctx.swimming && !ctx.airborne;

Recomputed every frame from the replicated field, so there is no cached state to
drift and no mirror handler to register. It applies to every unit, so other players
seen looting kneel too. The local movement test means the pose drops on the frame the
player starts moving rather than a round trip later.

Controller branch in `AnimationController::EvaluateLocomotion`, immediately after the
`ctx.dead` case and before the pose/spell lock case:

    else if (ctx.looting) -> single(m_bindings.Get(ANIM_SLOT_LOOT))

Ahead of the pose lock so looting while seated kneels and returns to sitting
afterwards; ahead of swimming and airborne is moot because `ctx.looting` already
excludes both.

On the frame the pose is entered (tracked by a `bool m_lootPoseActive` member), the
controller sets `SetLoop(false)`, `SetPlayRate(1.0f)`, `SetTimePosition(0.0f)` on the
clip and calls `m_action.FastForwardCurrent()`. Once the 0.87 s clip reaches its end
`SetTimePosition` clamps it there and it holds indefinitely.

The fast-forward matters for the most common path: finishing an `Open` cast on a
chest fires the 1.37 s `UseEnd` at the same instant the loot window opens. Without it
the character stands up out of the chest and only then kneels.

Leaving the pose clears `m_lootPoseActive` and drops the clip;
`LocomotionLayer::ApplyWeights` crossfades back to idle or run over the normal fade
duration.

A rig with no `"Loot"` clip resolves the slot to `nullptr`, `clipCount` stays 0, and
the unit keeps its normal locomotion. No warning, no T-pose.

## Part C: movement cancels looting (server)

In `Player::OnMovement` (`src/world_server/player.cpp`), after the movement info is
validated and applied: if a loot dialog is open and
`movementInfo.IsChangingPosition()`, call `CloseLootDialog()`. That already sends
`LootReleaseResponse`, so the client's loot frame closes, and clears
`unit_flags::Looting`, which replicates to everyone nearby. Jump and knockback are
covered because `Falling` is in the mask. Turning in place and pure facing changes do
not cancel: the character stays planted, so nothing floats, and a mouse-look cannot
cost the player their loot.

## Protocol

No `ProtocolVersion` bump in any part. No opcode is added, removed or renumbered; no
packet payload changes; framing and the cipher are untouched. `unit_flags::Looting`
and `object_fields::Flags` already exist and already cross the wire, and spell
visualization contents never do.

## Testing

E2E (new). The harness has no loot verbs, so three are added to the headless client:
`GetUnitFlags(g)`, `LootUnit(g)`, `ReleaseLoot()`. New scenario
`e2e/scenarios/loot_animation_flag.lua`:

1. `GM.CreateMonster` plus `GM.KillTarget`: the corpse carries `unit_flags::Lootable`.
2. `LootUnit(corpse)`: `unit_flags::Looting` set on `Me()`.
3. `MoveTo` a step away: `Looting` cleared, and a `LootReleaseResponse` arrived.
4. Loot again, `ReleaseLoot()`: `Looting` cleared.

This is the part that breaks silently, and it covers Part C end to end plus the field
Part B reads.

Unit tests. `SlotBindingTable::Rebuild` needs a live `Entity`, so the loot slot
resolution is not headless-testable and gets no unit test. The E2E scenario and the
visual pass below are the coverage.

Data tests. `tools/tests/test_open_spell_visual_data.py`, following
`test_warrior_visual_data.py`: asserts every Open spell links a visualization, that
each kit names a clip present on both human rigs, that no kit sets `duration_ms`, and
that the editor and ClientDB copies agree.

Visual. Manual in the real client, driven with the RunOnce auto-login and screenshot
path: cast Open on a chest (start, loop held across the 5 s cast, end), use a door
(instant flourish), loot a corpse (kneel, hold, release on move).

## Risks and non-goals

- Not in scope: pickaxe and herbalism animations. Mining rides the Use clips as a
  placeholder; visualization 13 keeps its id so the swap is data-only.
- Not in scope: a "stand up from loot" exit clip. There is no such clip, so leaving
  the pose is a crossfade.
- Spells 112 and 157 are both named "Mining" with different lock types (0 and 1); 112
  looks vestigial. It is linked to visualization 14 rather than deleted, since
  deleting a spell is out of scope here.
- The `data/client` and `data/editor` submodules already point ahead of what the
  superproject records, from work that predates this branch. Data commits go into the
  submodules on their own branches; the superproject pointer bump is raised with the
  user rather than folded in silently.
