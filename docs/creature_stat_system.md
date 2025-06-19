# Creature Stat-Based System

## Overview

The stat-based creature system enables creatures to use a flexible, class-based stat calculation system similar to players. This system is fully backward compatible with the existing legacy system.

## Key Features

- **Unit Classes**: Define stat calculations, power types, and scaling formulas
- **Flexible Stat Sources**: Health, mana, armor, and attack power can be derived from any combination of base stats
- **Level Scaling**: Automatic stat progression with level
- **Elite Modifiers**: Simple multipliers for elite creatures
- **Backward Compatibility**: Legacy creatures continue to work without changes

## System Components

### 1. Unit Classes (`unit_classes.proto`)

Unit classes define:
- Base stat values per level
- Power type (mana, rage, energy)
- How health/mana/armor/attack power are calculated from base stats
- Regeneration rates
- Movement speeds and attack timings

### 2. Creature Stat Fields (`units.proto`)

Each creature entry can now include:
- `unitClassId`: Reference to a unit class template
- `baseStats`: Base values for stamina, strength, agility, intellect, spirit
- `statScaling`: How much each stat increases per level
- `baseHealth/baseMana`: Base values before stat modifiers
- `eliteStatMultiplier`: Multiplier for elite creatures
- `useStatBasedSystem`: Enable/disable the new system

### 3. Stat Constants (`stat_constants.proto`)

Defines consistent stat IDs and power types used across the system.

## How It Works

### Runtime Calculation (GameCreatureS::CalculateStatBasedStats)

1. **Base Stats**: Calculate final stats using base values + level scaling + elite multiplier
2. **Health**: Base health + health from stat sources (e.g., stamina × factor)
3. **Mana**: Base mana + mana from stat sources (e.g., intellect × factor)
4. **Armor**: Base armor + armor from stat sources (e.g., agility × factor)
5. **Attack Power**: Base attack power + attack power from stat sources (e.g., strength × factor)
6. **Damage**: Calculate min/max damage with variance and attack power scaling

### Editor Integration

The editor provides:
- **Unit Class Editor**: Create and edit unit class templates
- **Creature Editor**: Enhanced with stat-based system controls
- **Stat Preview**: Real-time calculation preview at different levels
- **Class Selection**: Choose from available unit classes

## Example Usage

### 1. Create a Unit Class (Warrior)

```
id: 1
name: "Warrior"
internalName: "warrior"
powertype: RAGE

// Health from stamina (10 HP per stamina point)
healthStatSources: {
  statId: STAMINA
  factor: 10.0
}

// Attack power from strength (2 AP per strength point)
attackPowerStatSources: {
  statId: STRENGTH
  factor: 2.0
}

// Armor from agility (1 armor per agility point)
armorStatSources: {
  statId: AGILITY
  factor: 1.0
}
```

### 2. Configure a Creature

```
id: 12345
name: "Orc Warrior"
useStatBasedSystem: true
unitClassId: 1

// Base stats at level 1
baseStamina: 20
baseStrength: 15
baseAgility: 10
baseIntellect: 8
baseSpirit: 8

// Stat scaling per level
staminaPerLevel: 2.0
strengthPerLevel: 1.5
agilityPerLevel: 1.0
intellectPerLevel: 0.5
spiritPerLevel: 0.5

// Base health/mana before stat modifiers
baseHealth: 50
baseMana: 0

// Elite multiplier (1.0 = normal, 1.5 = elite, 3.0 = boss)
eliteStatMultiplier: 1.0
```

### 3. Result at Level 10

- **Final Stamina**: (20 + 2.0 × 9) × 1.0 = 38
- **Final Strength**: (15 + 1.5 × 9) × 1.0 = 28.5 ≈ 29
- **Final Health**: 50 + (38 × 10) = 430 HP
- **Final Attack Power**: (29 × 2) = 58 AP

## Migration Strategy

1. **Phase 1**: Create unit classes for common creature types
2. **Phase 2**: Convert existing creatures to use the new system gradually
3. **Phase 3**: Add new creatures using the stat-based system
4. **Phase 4**: Eventually deprecate legacy fields (optional)

## Benefits

- **Consistency**: Creatures use the same stat calculation rules as players
- **Flexibility**: Easy to create different creature archetypes
- **Maintainability**: Changes to stat formulas apply to all creatures using a class
- **Scalability**: New stat sources and calculations can be added easily
- **Balance**: Elite modifiers provide simple but effective creature scaling

## Technical Details

### Object Fields

The system uses existing object fields:
- `StatStamina`, `StatStrength`, `StatAgility`, `StatIntellect`, `StatSpirit`
- `MaxHealth`, `MaxMana`, `Armor`, `AttackPower`
- `MinDamage`, `MaxDamage`, `PowerType`

### Backward Compatibility

- Legacy creatures (useStatBasedSystem = false) continue to work unchanged
- All new fields have sensible defaults
- No breaking changes to existing data or protocols

### Performance

- Stat calculations are done in RefreshStats() (called infrequently)
- No runtime performance impact on legacy creatures
- Minimal memory overhead for new fields
