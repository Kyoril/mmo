# Data-driven attributes

Status: implemented on `feature/data-driven-attributes` (2026-10-29).

## Goal

Attributes (today Stamina, Strength, Agility, Intellect, Spirit) are defined by game data instead of
code. Designers can add, remove and rename attributes in the editor. With the shipped data, gameplay
is unchanged.

## Model

* **`attributes.proto`** (server `proto_data` + client `client_data`, same field numbers, so
  ExportToClient keeps binary-copying the blob): `AttributeEntry { id, name, description,
  levelUpText, name_loc=200, description_loc=201, levelUpText_loc=202 }`.
  * `id` is the attribute's *slot*: `0 <= id < MaxAttributes` (= 10, `game/attribute.h`). Slot ids
    are stable; deleting an attribute leaves a gap, adding one takes a free slot.
  * Display order is ascending id.
  * `levelUpText` is the full chat line ("You have gained %d stamina."), because the localized
    level-up sentences do not follow from the attribute name (frFR "Énergie" vs "endurance").
* Shipped data: 0 Stamina, 1 Strength, 2 Agility, 3 Intellect, 4 Spirit. This is the order the
  replicated fields, aura misc values, player stat sources and DB columns already used.

## Wire (protocol bump)

* Unit fields: `Stat`, `PosStat`, `NegStat` are blocks of `MaxAttributes` fields each, indexed by
  attribute id.
* Player field `AttributePointCost` is `AttributePointCostFieldCount = ceil(MaxAttributes / 4)`
  uint32 fields, one cost byte per attribute.
* `LevelUp` packet: a count followed by `(uint32 attributeId, int32 diff)` pairs instead of five
  fixed diffs.
* Realm<->world `CharacterClassData`: the attribute spending is a map
  `attributeId -> points`, plus `attributeLayout` (see the reset section below).

## Formulas

Every formula that named an attribute now takes it from data:

| Formula | Before | After |
|---|---|---|
| Base attributes per level | `ClassBaseValues.stamina..spirit`, `UnitClassBaseValues.stamina..spirit` | `repeated int32 attributes` (index = attribute id) |
| Health/mana/AP/armor/block/crit-block stat sources (players) | `statId` (field order) | unchanged, `statId` = attribute id |
| Same for creatures | `StatConstants.StatType` (a different order) plus a `switch` | `uint32 statId` = attribute id; data remapped |
| Health / mana regen | `StatSpirit / spiritPer*Regen` | `repeated AttributeRatioSource healthRegenSources / manaRegenSources` (`value / pointsPer`) |
| Melee crit | `5 + Agility / 20` for all units | `5 + Σ value / pointsPer` over the class's / unit class's `meleeCritSources` |
| Dodge | `base + Agility / 20` for all units | `base + Σ value / pointsPer` over `dodgeSources` |
| Item stats | `item_stat::Agility..Stamina` (2..6) and a switch | `item_stat::AttributeStart + id` (64 + id); data remapped |
| ModStat / ModStatPct auras | misc value 0..4 | misc value = attribute id (unchanged values) |

`unit_mods` holds `MaxAttributes` attribute slots at `unit_mods::AttributeStart`;
`GetUnitModByStat(id)` is `AttributeStart + id`.

Division semantics (`value / pointsPer`) are kept for the regen, crit and dodge sources so that the
migrated data produces bit-identical floats.

## Attribute point spending and reset

* DB: new table `character_class_attributes (character, class, attribute, points)` and a new column
  `character_classes.attr_layout`. The migration copies `attr_0..attr_4` into the table and sets the
  layout of rows that spent points to the hash of the shipped layout. The old columns stay (additive
  migration) and are no longer written.
* `attributeLayout` is a hash of the sorted attribute ids (FNV-1a), stored per class. When a class's
  spending is applied (login, class change) and points were spent under a different layout, that
  class's spending is cleared, so all points can be spent again, and the client gets the existing
  "attribute points reset" notification. A rename or text change does not change the layout; adding
  or removing an attribute does. This works the same way as the talent validation on login.
* Every class saved by the world carries the current layout.

## Client

* `GetNumAttributes()`, `GetAttributeInfo(index)` returning `id, name, description` in display order,
  and `GetAttributeName(id)`. The names are localized from the data's `_loc` fields.
* The character window builds its attribute rows from a template at load time.
* Level-up chat prints each attribute's `levelUpText`.
* Item tooltips use `item:GetStatAttribute(i)` to name attribute stats.

## Editor

* A new Attributes editor window lists the entries and edits id, name, description and
  level-up text.
* The class, unit class, creature, item and spell editors take their attribute lists and names from
  the project instead of literal arrays.

## Data migration

`tools/attributes/migrate_attributes.py` is a one-shot, idempotent script. It reads the pre-change
fields as unknown fields through the previous schema, then:

1. writes `attributes.data` (editor and ClientDB);
2. moves class and unit class base values into `attributes`;
3. remaps creature `statId`s from StatConstants order to attribute ids;
4. converts the `spiritPer*Regen` fields into regen sources;
5. adds `{Agility, 20}` crit and dodge sources to every class and unit class;
6. remaps item stat types 2..6 to `64 + id`.
