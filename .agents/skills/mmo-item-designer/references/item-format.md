# Item JSON Format

## Document Shape

```json
{
  "format": "mmo-item",
  "version": 1,
  "item": {
    "name": "Example Item",
    "itemclass": 0,
    "subclass": 0,
    "displayid": 0,
    "quality": 1,
    "flags": 0,
    "buycount": 1,
    "buyprice": 0,
    "sellprice": 0,
    "inventorytype": 0,
    "allowedclasses": -1,
    "allowedraces": -1,
    "itemlevel": 1,
    "requiredlevel": 1,
    "maxcount": 0,
    "maxstack": 1,
    "stats": [],
    "spells": [],
    "sockets": [],
    "name_loc": [],
    "description_loc": []
  }
}
```

The importer accepts optional fields, but explicit core defaults make generated files easier to review. Import ignores source IDs and retains the destination item ID.

Prices use copper: `100` copper is `1` silver and `10000` copper is `1` gold.

## Numeric Enums

Quality:

| Value | Name |
|---:|---|
| 0 | Poor |
| 1 | Common |
| 2 | Uncommon |
| 3 | Rare |
| 4 | Epic |
| 5 | Legendary |

Inventory type:

| Value | Name | Value | Name |
|---:|---|---:|---|
| 0 | NonEquip | 15 | Ranged |
| 1 | Head | 16 | Cloak |
| 2 | Neck | 17 | TwoHandedWeapon |
| 3 | Shoulders | 18 | Bag |
| 4 | Body | 19 | Tabard |
| 5 | Chest | 20 | Robe |
| 6 | Waist | 21 | MainHandWeapon |
| 7 | Legs | 22 | OffHandWeapon |
| 8 | Feet | 23 | Holdable |
| 9 | Wrists | 24 | Ammo |
| 10 | Hands | 25 | Thrown |
| 11 | Finger | 26 | RangedRight |
| 12 | Trinket | 27 | Quiver |
| 13 | Weapon | 28 | Relic |
| 14 | Shield |  |  |

Binding: `0` none, `1` pickup, `2` equip, `3` use.

Spell trigger: `0` on use, `1` on equip, `2` chance on hit.

Damage type: inspect `src/shared/game/damage_school.h`; physical is normally `0`.

Stat type:

| Value | Name | Value | Name |
|---:|---|---:|---|
| 0 | Mana | 16 | CritSpellRating |
| 1 | Health | 17 | HitTakenMeleeRating |
| 2 | Agility | 18 | HitTakenRangedRating |
| 3 | Strength | 19 | HitTakenSpellRating |
| 4 | Intellect | 20 | CritTakenMeleeRating |
| 5 | Spirit | 21 | CritTakenRangedRating |
| 6 | Stamina | 22 | CritTakenSpellRating |
| 7 | DefenseSkillRating | 23 | HasteMeleeRating |
| 8 | DodgeRating | 24 | HasteRangedRating |
| 9 | ParryRating | 25 | HasteSpellRating |
| 10 | BlockRating | 26 | HitRating |
| 11 | HitMeleeRating | 27 | CritRating |
| 12 | HitRangedRating | 28 | HitTakenRating |
| 13 | HitSpellRating | 29 | CritTakenRating |
| 14 | CritMeleeRating | 30 | HasteRating |
| 15 | CritRangedRating | 31 | ExpertiseRating |

Item flags are a bitmask: bound `1`, conjured `2`, openable `4`, wrapped `8`, wrapper `512`, party loot `2048`, unique equipped `4096`.

## Nested Fields

Damage:

```json
"damage": {"mindmg": 4.0, "maxdmg": 7.0, "type": 0}
```

Stat:

```json
{"type": 3, "value": 2}
```

Spell:

```json
{
  "spell": 123,
  "trigger": 0,
  "charges": 0,
  "procrate": 0.0,
  "cooldown": -1,
  "category": 0,
  "categorycooldown": -1
}
```

Socket:

```json
{"color": 0, "content": 0}
```

## Supported Scalar Fields

`description`, `itemclass`, `subclass`, `displayid`, `quality`, `flags`, `buycount`, `buyprice`, `sellprice`, `inventorytype`, `allowedclasses`, `allowedraces`, `itemlevel`, `requiredlevel`, `requiredskill`, `requiredskillrank`, `requiredspell`, `requiredhonorrank`, `requiredcityrank`, `requiredrep`, `requiredreprank`, `maxcount`, `maxstack`, `containerslots`, `armor`, `holyres`, `fireres`, `natureres`, `frostres`, `shadowres`, `arcaneres`, `delay`, `ammotype`, `bonding`, `lockid`, `sheath`, `randomproperty`, `randomsuffix`, `block`, `itemset`, `durability`, `area`, `map`, `bagfamily`, `material`, `totemcategory`, `socketbonus`, `gemproperties`, `disenchantskillval`, `disenchantid`, `foodtype`, `minlootgold`, `maxlootgold`, `duration`, `extraflags`, `lootentry`, `questentry`, `rangedrange`, `skill`, `requiredproficiency`.

Class and subclass IDs are project data, not hardcoded enums. Always inspect them.
