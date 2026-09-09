# Sitting Rest Buff — Design

Date: 2026-09-09
Branch: `claude/sitting-player-buff-trigger-9163fa`

## Goal

Sitting down should feel like resting, and like a risk. A player character that enters the
`Sit` stand state gains a buff that regenerates health and mana faster and makes every
attack against them a critical hit. Standing up, moving, taking damage or attacking ends it.

The same "you must stay seated" rule is extended to the existing Eat and Drink spells, which
today claim it in their data and their tooltip but never enforce it.

## What already exists

Most of the machinery is in place and is not rebuilt here:

- **Player triggers.** `trigger_flags::PlayerTrigger` plus `Project::GetPlayerTriggers()`
  bucket flagged triggers by event, so a raise is one array index. `OnPlayerLevelUp` is the
  existing user of this and is the pattern the new event follows.
- **Server-authoritative casts.** The trigger `CastSpell` action already passes
  `ignoreKnownSpellCheck = true` (`src/world_server/trigger_handler.cpp`), so a trigger can
  cast a spell the player has never learned. No change needed.
- **Aura interrupts.** `Move`, `Damage`, `DirectDamage`, `HitBySpell`, `Attack` and
  `NotSeated` are all implemented server-side, driven from `AuraContainer::NotifyOwnerMoved`,
  `AuraContainer::OnOwnerDamaged`, `GameUnitS::RemoveAurasByInterrupt` at the attack site,
  and `GameUnitS::SetStandState`. The break conditions in this design need no new machinery,
  only the right flags on the spell.
- **The cached-scalar pattern.** `ModDodgeChance` accumulates into `m_dodgeChanceBonus` on
  apply/misapply and `DodgeChance()` reads that one float. The three new auras copy it.

## What is missing

1. No trigger event fires on a stand state change.
2. No aura type modifies regeneration by a percentage, and none modifies the chance of
   *being* critically hit.
3. Nothing ever puts a player into `Sit` as a result of a spell, and
   `src/world_server/player.cpp` force-stands the caster on **every** client-initiated cast,
   ignoring the `CastableWhileSitting` attribute that Drink already carries. Food and Drink
   auras are therefore always applied while standing, and `NotSeated` — which only fires on a
   transition *into* `Stand` — never fires for them. The seated requirement is dead data.

## Design

### 1. Trigger event: `OnPlayerStandStateChanged`

Appended to `trigger_event::Type` in `src/shared/proto_data/trigger_helper.h`, immediately
before `Invalid`. The enum is append-only: values are serialized as raw integers in
`triggers.data`.

```
/// Executed on a player character right after its stand state changed. Requires the
/// PlayerTrigger flag, since players carry no trigger list of their own.
/// Data: [<STAND-STATE>]; When given, only fires on entering exactly that state.
OnPlayerStandStateChanged,
```

Stand state values come from `unit_stand_state::Type`: 0 Stand, 1 Sit, 2 Sleep, 3 Dead,
4 Kneel.

**Raise site.** `GameUnitS::SetStandState` is currently an inline function in
`game_unit_s.h`. It moves into `game_unit_s.cpp` and, after writing the field and running the
existing `NotSeated` interrupt, raises the event when all of the following hold:

- the state actually changed (`previous != standState`),
- the unit is a player (`IsPlayer()`) — the base `GameUnitS::RaiseTrigger` logs a warning for
  unimplemented owners, so raising unconditionally would spam the log for every creature
  spawned with a stand state,
- the unit is in a world instance.

The login trap that bit `OnPlayerLevelUp` (`SetLevel` fires during character load) does not
apply: the only construction-time call is `SetStandState(Stand)` while the field is already
0, so the `previous != standState` guard rejects it. The world-instance guard is belt and
braces for any future construction-time path.

**Editor.** `src/mmo_edit/editor_windows/trigger_editor_window.cpp` gets the event in its
name list and in the two `switch` sites that currently special-case `OnPlayerLevelUp`
(the event summary text and the event-data editing UI), so the stand state is picked from a
combo box rather than typed as a raw integer.

`Project::RebuildPlayerTriggerIndex` and `TriggerEventIndex` need no change — the index is
sized by `trigger_event::Count_`, which grows automatically.

### 2. Three aura types with cached scalars

Appended to `aura_type::Type` in `src/shared/game/aura.h`. As the header's own warning
states, entries are never reordered or removed; these go before `Count_`.

| Value | Type | Base points | Misc value |
|---|---|---|---|
| 38 | `ModHealthRegenPercent` | percent (50 = +50%) | — |
| 39 | `ModPowerRegenPercent` | percent | `miscvaluea` = `power_type` |
| 40 | `ModCritChanceTaken` | flat percentage points | — |

`GameUnitS` gains three members next to `m_dodgeChanceBonus`:

```cpp
/// Accumulated health regeneration bonus in percent from auras (ModHealthRegenPercent).
float m_healthRegenPctBonus = 0.0f;
/// Accumulated power regeneration bonus in percent per power type (ModPowerRegenPercent).
std::array<float, power_type::Count_> m_powerRegenPctBonus{};
/// Accumulated bonus in percentage points to any attacker's chance to crit this unit.
float m_critChanceTakenBonus = 0.0f;
```

each with a `Modify...(amount, apply)` mutator of the same shape as `ModifyDodgeChanceBonus`
(`+= apply ? amount : -amount`) and a const getter. The mutators are called from new
`AuraEffect::HandleModHealthRegenPercent` / `HandleModPowerRegenPercent` /
`HandleModCritChanceTaken`, registered in the dispatch table in `aura_effect.cpp` and invoked
on both apply and misapply. Nothing iterates auras at read time.

Read sites, one arithmetic operation each:

- `GameUnitS::RegenerateHealth()` — scales `m_healthRegenPerTick` by
  `1.0f + m_healthRegenPctBonus / 100.0f`.
- `GameUnitS::RegeneratePower()` — scales the computed amount for that power type by
  `1.0f + m_powerRegenPctBonus[powerType] / 100.0f`, **only when the amount is positive**.
  Rage is a decay (`amount -= 3`); scaling it would make a "+50% rage regen" buff drain rage
  faster.
- `GameUnitS::CriticalHitChance(victim, attackType)` — adds `victim`'s crit-taken bonus
  before the existing clamp. This one site covers the entire melee attack table.
- The weapon-damage spell crit roll in `spell_effects.cpp` — adds the target's crit-taken
  bonus to the local `critChance`.

Plain school-damage spell effects do not roll crit at all today (three sites carry
`TODO: Do real calculation including crit chance, miss chance, resists`). They are left
alone; when crit is implemented there it will need the same one-line add, and the design note
here is the reminder.

Healing crit is deliberately **not** affected: "critically hit" reads as damage.

Because the melee attack table is a cumulative roll (miss, dodge, parry, glancing, crit,
crushing), a +100 crit-taken bonus means *every attack that lands* is a critical hit. A
sitting player can still be missed, dodge or parry. `victimCanReact` — which currently
excludes only stunned and sleeping units from dodge/parry — is not changed; making sitting
units unable to react is a separate combat-feel decision and is out of scope.

`s_auraTypeNames` in `spell_editor_window.cpp` gets three entries; its `static_assert`
against `aura_type::Count_` enforces this.

### 3. Sit on cast

New attribute `spell_attributes_b::SitsCaster = 1 << 10` in `src/shared/game/spell.h`.

The unconditional force-stand in the spell cast handler in `src/world_server/player.cpp`
becomes:

- if the spell has `SitsCaster` and the caster is alive and not already seated, set the stand
  state to `Sit`;
- otherwise, stand the caster up **unless** the spell has `CastableWhileSitting`.

The second half fixes an existing bug on its own: Drink already carries
`CastableWhileSitting` and was being stood up anyway.

Data changes: Food (spell 143) and Drink (spells 59 and 60) gain `SitsCaster`; Food
additionally gains the `CastableWhileSitting` it lacks. Their `aurainterruptflags` already
include `NotSeated`, so no flag edits are needed there.

### 4. The Resting spell and its trigger (data)

A new spell, "Resting", at the next free spell id (allocated when the data is authored; the
placeholder below is written as `<resting spell id>` and is the only value this design does
not fix):

- instant, no cost, no cooldown, no class or race restriction;
- **not** flagged `HiddenClientSide`. That attribute hides the spell from the spell book, but
  `AuraContainer::IsVisible()` reads the same bit, so setting it would also hide the buff
  icon. It is unnecessary regardless: the player never learns the spell, so it can never
  appear in the spell book;
- `duration = 0`, which `AuraContainer::DoesExpire()` treats as "never expires" — the aura
  ends only by interrupt;
- `aurainterruptflags = HitBySpell | Damage | Move | Attack | NotSeated | ChangeMap |
  DirectDamage`;
- `attributes(1)` includes `NotBreakCastInterruptAuras`;
- three `ApplyAura` effects: `ModHealthRegenPercent` 50, `ModPowerRegenPercent` 50 with
  `miscvaluea` = mana, `ModCritChanceTaken` 100;
- name, description and aura text localized in all four locales, plus an icon.

`NotBreakCastInterruptAuras` is not optional. The trigger makes the *player* the caster, and
`GameUnitS::CastSpell` runs `RemoveAurasByInterrupt(spell_aura_interrupt_flags::Cast)` on its
caster. Without the attribute, the resting buff would strip the food or drink aura that
seated the player a moment earlier — both carry the `Cast` interrupt flag.

The trigger entry in `triggers.data`:

- `flags = trigger_flags::PlayerTrigger`
- event `OnPlayerStandStateChanged`, `data = [1]` (Sit)
- action `CastSpell`, target `TriggeringUnit` (the player is the caster), `data =
  [<resting spell id>, trigger_spell_cast_target::Caster]` (the player is also the target)

The Resting spell is not added to any trainer, starting-spell set or talent — the trigger is
its only source.

`data/editor` is a git submodule and is not checked out in this worktree. It is initialised
here and committed separately from the engine changes.

### 5. Interaction: eating while resting

Because eating and drinking now seat the player, the sit trigger fires and the player also
receives the resting buff, including its crit vulnerability. This is intended: one rule
("sitting means resting") with no special-casing, and it makes eating in a dangerous place a
real decision.

### 6. Protocol

No opcode is added, removed or renumbered, no packet payload changes, and framing and the
cipher are untouched. The new aura types and the new spell attribute are values inside
existing protobuf fields, and `client_data/spells.proto` already mirrors `attributes` and
`aurainterruptflags` as `uint32`. No `ProtocolVersion` bump, and
`tools/protocol_version_check.py` should stay green.

## Testing

**Unit (Catch2, `src/tests/game_server_tests/`).**

- Extend `trigger_event_filter_test.cpp` for `OnPlayerStandStateChanged`: an event with no
  data matches any stand state, an event with `[1]` matches only `Sit`.
- New coverage for the three cached scalars: applying then misapplying returns each to zero,
  two stacked auras accumulate, the regen multiplier scales health and mana but leaves rage
  decay alone, and a crit-taken bonus raises the value returned by `CriticalHitChance`.

**End to end (`e2e/scenarios/sitting_rest_buff.lua`).**

The harness cannot sit today, so the scenario needs two additions to the e2e client: a
`Sit()` / `Stand()` action (sending the pose emote the real client sends) and a
`GetStandState(g)` query. Both are documented in `e2e/README.md`.

The scenario asserts, in order:

1. sit → the resting aura is present;
2. move → the aura is gone and the character is standing;
3. sit again → the aura is back;
4. stand → the aura is gone;
5. use food → the character is seated and has both the food aura and the resting aura;
6. move → both auras are gone.

## Out of scope

- Making sitting units unable to dodge or parry.
- Crit rolls for plain school-damage spell effects (three existing `TODO`s).
- Healing crit against sitting targets.
- Any client-side prediction of the seated state; the server remains authoritative.
