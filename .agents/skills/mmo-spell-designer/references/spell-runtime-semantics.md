<overview>
This repository's spell system is data-driven, but not schema-driven alone. Runtime behavior is determined by the server implementation in `src/shared/game_server/spells/` and related unit or item flows. Use this reference to map data choices to actual behavior.
</overview>

<passive_spells>
Passive spells are identified by the `spell_attributes::Passive` flag in the first attributes word.

Runtime behavior:

- When a unit learns a passive spell through `GameUnitS::AddSpell`, it is immediately cast on self when `activatePassiveImmediately` is true.
- `GameUnitS::ActivatePassiveSpells()` re-casts all passive spells when a unit is recreated, including login and map or teleport reconstruction flows.
- Passive spells are not intended for manual activation.

Implication:

- Mark a spell passive only when its effects should be continuously established by the server whenever the unit exists.
</passive_spells>

<item_driven_spells>
Item-driven spells are separate from passive spells.

Runtime behavior:

- In `GamePlayerS`, item spells with trigger `OnEquip` are cast automatically on equip.
- The cast carries the item GUID.
- `GameUnitS::RemoveAllAurasDueToItem()` removes auras tied to that item when the item is unequipped.

Implication:

- An automatic equipment effect does not need the passive attribute if the item is responsible for casting it.
- If the item spell should persist only while equipped, ensure the applied behavior is represented by aura state that can be removed with the item GUID.
</item_driven_spells>

<effect_execution>
Effect execution is implemented in `src/shared/game_server/spells/spell_effects.cpp`.

Important supported effects in this repository include:

- direct damage and healing
- weapon-based damage
- `ApplyAura`
- `CreateItem`
- `Energize`
- `OpenLock`
- `Charge`
- `LearnSpell`
- `TriggerSpell`
- `Proficiency`
- `ResetAttributePoints`
- `ResetTalents`
- `Parry`, `Block`, `CriticalBlock`, `Dodge`

If a requested design depends on an effect type with no meaningful handler, the spell cannot be completed purely as data yet.
</effect_execution>

<auras>
Auras are created by aura-applying spell effects and managed by `AuraContainer` plus `AuraEffect`.

Important behavior:

- A single spell can create multiple aura effects.
- Aura effects are grouped into a container by spell and target.
- Periodic auras use `amplitude` as the tick interval.
- `StartPeriodicAtApply` causes the first tick immediately on aura application.
- Aura application, removal, and periodic ticks can each produce meaningful gameplay.

Aura handlers currently support patterns such as:

- periodic damage
- periodic heal
- periodic energize
- periodic trigger spell
- flat or percent stat modifiers
- attack power, healing, spell damage, speed, resistance, dodge, root, stun, fear, sleep, disorient, visibility
- proc-triggered spell auras
- damage immunity
</auras>

<procs>
Proc behavior is split across spell-level proc fields and aura effect logic.

Spell-level proc fields matter when:

- a spell's aura should react to later combat events
- proc chance, proc charges, proc cooldown, proc family, proc school, or extended proc flags narrow the trigger conditions

Runtime anchors:

- `AuraContainer::HandleProc`
- `AuraEffect::HandleProcEffect`
- `GameUnitS::TriggerProcEvent`

For `aura_type::ProcTriggerSpell`:

- the aura effect's `triggerspell` is the spell to cast on proc
- the owning spell's proc fields decide when the proc can happen

For `spell_effects::TriggerSpell`:

- the spell effect immediately or conditionally triggers another spell during effect execution
- the proc chance is taken from the casting spell's `procchance`

Implication:

- Do not set proc fields in isolation. The aura type, trigger spell reference, and proc event sources must align.
</procs>

<restrictions_and_validation>
`SingleCastState::ValidatePlayerRequirements()` and `ValidateCasterRequirements()` enforce key gates.

Relevant restrictions include:

- `racemask`
- `classmask`
- reagent availability
- level requirements
- alive or dead casting rules
- stunned, feared, or sleeping usability flags
- line-of-sight handling
- talent-tier requirements in `GamePlayerS::LearnTalent()`: row `0` has no prerequisite, row `1`
  requires 5 points in the same tab, row `2` requires 10, and so on

Implication:

- If a spell "looks correct" in data but never casts, inspect validation semantics before changing effect data.
</restrictions_and_validation>

<stacking>
Aura stacking behavior is controlled by `AuraContainer` and spell stacking fields.

Relevant data:

- `stackamount`
- `stacking_rule`
- `stacking_category_id`
- `extend_duration`
- `stack_reset_policy`
- `OnlyOneStackTotal` attribute flag

Implication:

- Stacking behavior is not only about aura count. Reapplication, extension, exclusivity, and same-base-spell handling are all part of the result.
</stacking>

<targeting>
Target resolution and cast validation live in the cast state and target resolver
(`spell_target_resolver.cpp`).

Practical guidance:

- Confirm `targeta` and `targetb` against the chosen effect type.
- Confirm `rangetype` or raw ranges when the spell depends on distance validation.
- Confirm teleport destination fields only for teleport-style effects.
- Confirm projectile or visualization expectations separately from gameplay targeting.

Secondary / multi-target support:

- `TargetSecondaryEnemy` (targeta value 19) gathers enemies near the spell's PRIMARY unit
  target, excluding the primary target itself, within the effect `radius`, nearest-first, and
  capped by `chaintarget` (default 1). Resolution is deterministic, so multiple effects using
  this target type in one cast hit the same secondary unit(s). Use it for "and one nearby
  enemy" riders.
- Weapon-damage effects now apply their roll to every resolved unit target, so pairing a
  weapon-damage effect with a multi-target `targeta` produces a true cleave / multi-strike
  without code changes.
</targeting>

<conditional_effects>
Each `SpellEffect` can be gated by `conditiontype` / `conditionvalue` / `conditiontarget`,
evaluated by `SingleCastState::EvaluateEffectCondition` before the effect's targets are
resolved. A false condition skips the effect entirely (no targets, no handler).

- `conditiontype` `1` = TargetHasAuraFromCaster, `2` = TargetMissingAuraFromCaster (the aura
  match uses `GameUnitS::HasAuraSpellFromCaster`, so it only matches auras applied by THIS
  caster).
- `conditionvalue` is the aura's source spell id (must reference an existing spell).
- `conditiontarget` `0` = primary target, `1` = caster.

This lets riders like "if your target already has your DoT, also do X" be authored as data.
Unknown condition types fail closed (the effect is skipped) and log a warning.
</conditional_effects>

<unsupported_requests>
Escalate to code changes when the request needs:

- a brand-new spell effect type
- a brand-new aura type
- new proc event plumbing
- a targeting pattern with no resolver support
- effect semantics that conflict with the current handlers

In those cases, the correct outcome is "data plus runtime extension", not pretending the data surface already supports it.
</unsupported_requests>
