# Item Balance Guidelines

Use these as fallback heuristics, not engine-enforced formulas. Compare against nearby entries from the live item catalog first.

## Item Level

- Set item level roughly `5` to `7` above required level when no stronger local convention is visible.
- Poor and common equipment normally has no bonus stats.
- Uncommon or better equipment normally requires at least level `3`, except deliberate early quest rewards.

## Primary Stats

Choose stats that support the intended role:

| Role | Typical stats |
|---|---|
| Strength melee/tank | Strength, Stamina |
| Agility melee/ranged | Agility, Stamina |
| Caster damage | Intellect, Spirit, Stamina |
| Healer | Intellect, Spirit, Stamina |

Use two or three stat types for most non-epic items. Avoid unfocused stat mixtures.

Suggested value range per chosen stat:

| Required level | Uncommon | Rare | Epic |
|---|---:|---:|---:|
| 1-10 | 1-3 | 2-5 | 4-8 |
| 11-20 | 2-5 | 3-8 | 6-14 |
| 21-40 | 5-12 | 8-18 | 14-28 |
| 41-60 | 10-20 | 15-30 | 25-45 |

Treat these as broad ceilings and compare against existing project items at the nearest level.

## Weapons

`delay` is attack time in milliseconds.

- Fast dagger: about `1300` to `1700`.
- General one-handed weapon: about `1600` to `2200`.
- Two-handed weapon or staff: about `2800` to `4000`.

Fallback DPS bands:

| Item level | One-hand DPS | Two-hand DPS |
|---:|---:|---:|
| 10 | 4-7 | 6-10 |
| 20 | 8-14 | 12-20 |
| 30 | 14-22 | 20-34 |
| 40 | 22-34 | 32-52 |
| 50 | 30-46 | 48-70 |
| 60 | 46-66 | 70-100 |

Calculate average damage as `DPS * delay / 1000`. Build a damage range around that average, usually about `+/- 30%`, while preserving the same average. Use physical damage type `0` unless a confirmed project mechanic requires another type.

## Armor And Shields

The engine's physical mitigation formula is:

```text
armor / (armor + 400 + 85 * attackerLevel)
```

Maximum reduction is `85%`.

Directional full-set mitigation targets:

| Armor type | Approximate full-set target |
|---|---:|
| Cloth | 15% |
| Leather | 28% |
| Mail | 40% |
| Plate | 60% |

Do not assign the full-set armor value to one item. Compare same-slot project items and preserve the ordering `cloth < leather < mail < plate`. A shield should add a meaningful amount of armor and a block value; use nearby shields as the primary reference.

## Binding

- Poor and common world items: usually no binding.
- Tradable random world drops: usually Bind on Equip (`2`).
- Dungeon, boss, and ownership-sensitive rewards: usually Bind on Pickup (`1`).
- Consumables that should bind only after activation: Bind on Use (`3`) when appropriate.

## Flavor Text

Treat `description` as tooltip flavor text, not a mechanical explanation.

Include it for legendary items, named story artifacts, explicit boss drops, or items tied to an NPC or lore event. Omit it for routine drops, ordinary crafted equipment, generic materials, and standard consumables unless the user asks for flavor text.

## Tooltip Preview

After writing an equippable item, show a compact preview containing only applicable lines:

```text
Item Name
Item Level 12
Binds when equipped
Main Hand                         One-Handed Sword
6-12 Damage                      Speed 1.5
+3 Agility
+1 Stamina
Requires Level 7
```

Use the designed values exactly. Do not invent color names or unsupported effects.
