# Stealth Movement — Design

Date: 2026-07-05
Status: Implemented

## Goal

WoW-style stealth: a stealthed unit's visibility is decided **per observer** (level
difference + front cone), party members always see stealthed units, and clients keep
known-but-hidden units in memory — the server toggles their visibility with GUID lists
instead of spawn/despawn packets. Hostile creatures that detect a stealthed unit enter
an alert state (stop, face, 3 s window) before engaging.

**Explicit non-goal:** the existing invisibility system (`aura_type::ModVisibility` →
`unit_visibility::Off`, visible to GMs only) is untouched. Stealth is a separate,
parallel system.

## 1. Stealth state (server)

- New aura type `ModStealth` appended to `aura_type::Type` in `src/shared/game/aura.h`
  (before `Count_`; enum values are serialized in spell data and must not be reordered).
- Handler registered in the `AuraEffect` dispatch map
  (`src/shared/game_server/spells/aura_effect.cpp`), posting
  `NotifyVisibilityChanged()` to the universe, same pattern as `HandleModVisibility`.
- `GameUnitS::NotifyVisibilityChanged()` becomes a priority chain:
  1. `ModVisibility` aura present → `unit_visibility::Off` (invisibility wins, unchanged)
  2. `ModStealth` aura present → `unit_visibility::GroupStealth`
  3. otherwise → `unit_visibility::On`

## 2. Per-observer detection — `CanBeSeenBy()` GroupStealth case

File: `src/shared/game_server/objects/game_unit_s.cpp`.

Order of checks for a stealthed unit observed by `other`:

1. Self and game masters: always visible.
2. Party members: if both units are players in the same `PlayerGroup`, always visible
   (no cone, no range check).
3. Front cone: `other.IsFacingTowards(*this)` (existing 120° arc helper,
   `game_object_s.cpp:207`). Outside the cone → **never** visible.
4. Level-based detection range:
   `range = 10 + (observerLevel - stealthLevel) * 1.0` when observer is higher,
   `range = 10 - (stealthLevel - observerLevel) * 1.5` when lower,
   clamped to `[1.5, 25]` meters. Within range → visible.

Constants live in one place (`stealth constants` in `game_unit_s.h`) for tuning.

Dead observers cannot detect. Creatures and players use the same rule.

## 3. Network protocol — GUID lists, no spawn/despawn churn

- New S→C opcode `UnitVisibilityList`: `uint16 visibleCount`, packed GUIDs,
  `uint16 invisibleCount`, packed GUIDs. Sent **only for units the client already
  knows** (tracked in `Player::m_spawnedGuids`).
- Units not yet known to a client are spawned normally when first visible (existing
  `CanBeSeenBy` spawn filter).
- While a known unit is hidden for a client: movement packets and field updates for
  that unit are suppressed for that client. On reveal, the server resends movement
  state (`UnitMover::SendMovementPackets`) so the client position resyncs.
- Tile-boundary despawn paths in `world_instance.cpp` / `player.cpp` that currently
  filter by `CanBeSeenBy` switch to `IsObjectKnown`, so a hidden-but-known unit is
  properly destroyed at the client when it leaves sight range (avoids a leak).
- `Player` gains `m_hiddenGuids` (known but currently hidden). Visibility edges are
  computed against this set and batched into `UnitVisibilityList` packets.
- Re-evaluation triggers:
  - stealthed unit moves (`OnObjectMoved`),
  - an observer moves,
  - stealth aura applied/removed (`UpdateVisibilityAndView`).

## 4. Creature alert AI — `CreatureAIAlertState`

New state in `src/shared/game_server/ai/`.

- Trigger: idle-state unit watcher sees a hostile unit whose visibility is
  `GroupStealth` and `CanBeSeenBy(creature)` is true → enter alert state instead of
  instant combat.
- On enter: `StopMovement()`, `SetFacing(GetAngle(target))`, start 3 s `Countdown`,
  record spot distance. Send `StealthDetected(creatureGuid)` to the spotted player.
- Recheck every ~400 ms:
  - target dropped stealth → `EnterCombat`,
  - target came meaningfully closer than spot distance → `EnterCombat`,
  - target invalid/dead/despawned → back to `Idle`.
- On 3 s expiry: still visible → `EnterCombat`; otherwise → `Idle` (movement resumes).
- Damage taken during alert → combat (existing `OnDamage` path).

## 5. Client

- `UnitVisibilityList` handler in `WorldState`: units stay in `ObjectMgr`; visuals
  toggled via new `GameUnitC::SetStealthHidden(bool)` (entity + name plate + attached
  visuals). Movement packets for hidden units don't arrive, so no interpolation issues.
- `StealthDetected` handler: plays an alert sound. Per-creature sound comes from a new
  optional field on the unit proto data; falls back to a default sound when unset.
- Stealthed units the client *can* see (party members, own character): rendered
  translucent if the material-tint infrastructure supports alpha cheaply; otherwise
  rendered normally (follow-up).

## Authoring the stealth spell (data, no code)

In the spell editor, create a spell with:

- Effect: `ApplyAura` with aura type `ModStealth`.
- Aura interrupt flags: `Damage | HitBySpell | Attack | Cast` so stealth breaks when the
  unit takes damage, attacks or casts (all handled by the existing aura interrupt system).
- Optional second effect: `ApplyAura` with `ModDecreaseSpeed` (e.g. base points -30) for
  the classic stealth movement slow.

Per-creature alert sounds are set in the creature editor ("Stealth Alert Sound" in the
Scripting section); the client falls back to `Sound/Creature/StealthAlert.wav` when empty
(place a sound file there or configure sounds per creature).

## Testing

- `game_server_unit_tests`: `CanBeSeenBy` GroupStealth matrix — self/GM/party always;
  cone gating; level-difference range; invisibility path unchanged.
- Build verification of world_server + client on Windows.
- Manual: stealth near a creature patrol — alert stop/face/3 s behavior, GUID-list
  visibility toggling without spawn/despawn churn.
