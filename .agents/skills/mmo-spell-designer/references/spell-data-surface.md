<overview>
The spell authoring surface is centered on `src/shared/proto_data/spells.proto`, but real spell work almost always depends on adjacent protobuf datasets. Use this reference to understand which files must be inspected together before creating or editing a spell.
</overview>

<primary_schema>
The main spell dataset is `data/editor/data/spells.data`, backed by `src/shared/proto_data/spells.proto`.

Important top-level `SpellEntry` fields:

- `id`, `name`, `description`, `icon`, `auratext`
- `attributes` as repeated bitfield words
- `effects` as repeated `SpellEffect`
- cast and cost fields: `casttime`, `powertype`, `cost`, `costpct`
- level fields: `baselevel`, `spelllevel`, `maxlevel`
- school and damage class: `spellSchool`, `dmgclass`, `mechanic`
- range and target location: `minrange`, `maxrange`, `rangetype`, `targetmap`, `targetx`, `targety`, `targetz`, `targeto`
- cooldown fields: `cooldown`, `category`, `categorycooldown`, `cooldownflags`
- proc fields: `procflags`, `procchance`, `proccharges`, `procschool`, `procfamily`, `procfamilyflags`, `procexflags`, `procpermin`, `proccooldown`
- stacking fields: `stackamount`, `stacking_rule`, `stacking_category_id`, `extend_duration`, `stack_reset_policy`
- restrictions: `racemask`, `classmask`, `reagents`
- relationships: `additionalspells`, `baseid`, `prevspell`, `nextspell`, `visualization_id`
</primary_schema>

<effect_schema>
Each `SpellEffect` entry is indexed and contains the execution payload for one effect slot.

Most important fields:

- `index`, `type`
- `basepoints`, `diesides`, `basedice`, `diceperlevel`, `pointsperlevel`, `pointspercombopoint`
- `targeta`, `targetb`, `radius`, `chaintarget`
- `aura`, `amplitude`
- `itemtype`, `miscvaluea`, `miscvalueb`
- `triggerspell`, `summonunit`
- `multiplevalue`, `affectmask`
- `powerbonustype`, `powerbonusfactor`
- `conditiontype`, `conditionvalue`, `conditiontarget` (data-driven conditional gating; see below)

Do not author effects by field presence alone. The meaning of `miscvaluea`, `itemtype`, `triggerspell`, `radius`, `amplitude`, and `aura` changes with the selected effect or aura type.
</effect_schema>

<conditional_effects>
Any `SpellEffect` can be gated by a runtime condition. When `conditiontype` is non-zero the
effect is evaluated before its targets are resolved; if the condition is false the whole
effect is skipped (no damage, no aura, no trigger). This works for every effect type.

Fields:

- `conditiontype` selects the predicate:
  - `0` = None (always apply; default)
  - `1` = TargetHasAuraFromCaster (apply only if the condition unit has an aura cast by this
    spell's caster whose source spell id equals `conditionvalue`)
  - `2` = TargetMissingAuraFromCaster (apply only if that aura is absent)
- `conditionvalue` is the parameter for the predicate. For the aura predicates it is the spell
  id of the aura to look for. It must reference an existing spell.
- `conditiontarget` selects which unit the predicate checks:
  - `0` = PrimaryTarget (the spell's "your target"; default)
  - `1` = Caster

The aura check uses caster identity: it matches auras applied by the current caster only, so
"if your target has your Wounding Strike" is expressible without affecting auras from other
casters. The enums live in `src/shared/game/spell_target_map.h`
(`spell_effect_condition`, `spell_effect_condition_target`); the runtime evaluation is in
`SingleCastState::EvaluateEffectCondition`.
</conditional_effects>

<secondary_and_multi_target>
Two targeting capabilities support "hit your target and also a nearby enemy" designs:

- `targeta = 19` (`TargetSecondaryEnemy`) resolves enemies near the spell's PRIMARY unit target,
  excluding the primary target itself. Candidates are gathered within the effect `radius`
  around the primary target and sorted nearest-first; `chaintarget` caps how many are taken
  (defaults to 1 when 0). Selection is deterministic, so two effects using this target type in
  the same spell resolve to the same secondary unit(s) — e.g. one effect deals weapon damage to
  the secondary enemy and another applies an aura to that same enemy.
- Weapon-damage effects (`WeaponDamage`, `WeaponPercentDamage`, `WeaponDamageNoSchool`,
  `NormalizedWeaponDamage`) now apply to EVERY resolved unit target, each with an independent
  roll. Combined with a multi-target `targeta` (e.g. `TargetSecondaryEnemy` or an area enemy
  type) this expresses cleave / multi-strike weapon abilities purely as data.

Worked example — "strike your target and one nearby enemy, both for weapon damage + 8, and if
your target has your Wounding Strike the second enemy is wounded":

- effect 0: `WeaponDamage`, `targeta = TargetEnemy`, `basepoints = 8`
- effect 1: `WeaponDamage`, `targeta = TargetSecondaryEnemy`, `radius = 8`, `chaintarget = 1`,
  `basepoints = 8`
- effect 2: `TriggerSpell` (the Wounded spell), `targeta = TargetSecondaryEnemy`, `radius = 8`,
  `chaintarget = 1`, `conditiontype = 1`, `conditionvalue = <Wounding Strike spell id>`,
  `conditiontarget = 0`
</secondary_and_multi_target>

<dependency_files>
Inspect these files when needed:

- `classes.data` from `classes.proto`
  Use for class IDs, power types, spell families, and class-granted spells.
- `races.data` from `races.proto`
  Use for race IDs and initial spell sources.
- `items.data` from `items.proto`
  Use for reagent references, `CreateItem`, and item-triggered `OnEquip`, `OnUse`, or `OnHitChance` spell references.
- `spell_categories.data` from `spell_categories.proto`
  Use for category grouping and shared cooldown intent.
- `aura_stacking_categories.data` from `aura_stacking_categories.proto`
  Use when stacking exclusivity is category-based.
- `spell_visualizations.data` from `spell_visualizations.proto`
  Use for visualization, projectile, and icon overrides.
- `proficiencies.data` from `proficiencies.proto`
  Use when a spell grants a proficiency effect.
- `skills.data` from `skills.proto`
  Use when a spell references profession or skill IDs.
- `ranges.data` from `ranges.proto`
  Use to confirm named range presets instead of guessing raw distances.
</dependency_files>

<mask_rules>
`racemask` and `classmask` are bitmasks, not direct IDs.

- Bit position is `(entry.id - 1)`.
- A zero mask means unrestricted.
- Class and race restrictions are checked during cast validation and also when learning spells on players.

Examples:

- Race ID `1` uses bit `1 << 0`
- Class ID `3` uses bit `1 << 2`
</mask_rules>

<item_links>
Item-triggered spells come from `ItemEntry.spells`.

Trigger meanings from `src/shared/game/item.h`:

- `0` = `OnUse`
- `1` = `OnEquip`
- `2` = `OnHitChance`

`OnEquip` spells are automatically cast when the item is equipped. If those casts apply auras, those auras are removed when the item is unequipped.
</item_links>

<authoring_defaults>
Prefer these habits:

- Clone an existing spell in the same family or gameplay role.
- Keep effect indexes explicit and stable.
- Keep ranks linked with `baseid`, `prevspell`, `nextspell` when editing a spell chain.
- Use live categories, proficiencies, and visualizations rather than inventing placeholders.
- Leave a reference field at `0` only when `0` is the established "none" value for that field.
</authoring_defaults>
