# Example Unit Class Configurations
# This document shows how to configure different types of creature classes
# These examples demonstrate the flexibility of the new stat-based system

## Example 1: Warrior-type Creature (Melee Fighter)
```
id: 1
name: "Warrior Creature"
internalName: "UNIT_CLASS_WARRIOR"
powertype: RAGE
baseHealthMultiplier: 1.2  # 20% more health than standard
baseDamageMultiplier: 1.1  # 10% more damage
walkSpeed: 1.0
runSpeed: 1.0

# High stamina and strength scaling
levelbasevalues: [
  { health: 100, mana: 0, stamina: 20, strength: 18, agility: 12, intellect: 8, spirit: 8 }, # Level 1
  { health: 150, mana: 0, stamina: 25, strength: 23, agility: 15, intellect: 10, spirit: 10 }, # Level 10
  { health: 800, mana: 0, stamina: 80, strength: 75, agility: 50, intellect: 30, spirit: 30 }  # Level 60
]

healthStatSources: [
  { statId: STAMINA, factor: 10.0 }  # Each stamina = 10 health
]

attackPowerStatSources: [
  { statId: STRENGTH, factor: 2.0 },  # Each strength = 2 attack power
  { statId: AGILITY, factor: 1.0 }    # Each agility = 1 attack power
]

armorStatSources: [
  { statId: AGILITY, factor: 2.0 }    # Each agility = 2 armor
]
```

## Example 2: Caster-type Creature (Magic User)
```
id: 2
name: "Caster Creature"
internalName: "UNIT_CLASS_CASTER"
powertype: MANA
baseHealthMultiplier: 0.8   # 20% less health than standard
baseDamageMultiplier: 1.3   # 30% more spell damage
walkSpeed: 0.9
runSpeed: 1.1

# High intellect and spirit scaling
levelbasevalues: [
  { health: 80, mana: 120, stamina: 15, strength: 10, agility: 10, intellect: 22, spirit: 18 }, # Level 1
  { health: 120, mana: 180, stamina: 18, strength: 12, agility: 12, intellect: 28, spirit: 23 }, # Level 10
  { health: 600, mana: 900, stamina: 60, strength: 40, agility: 40, intellect: 90, spirit: 75 }  # Level 60
]

healthStatSources: [
  { statId: STAMINA, factor: 8.0 }    # Each stamina = 8 health (less than warrior)
]

manaStatSources: [
  { statId: INTELLECT, factor: 15.0 } # Each intellect = 15 mana
]

baseManaRegenPerTick: 5.0
spiritPerManaRegen: 4.0  # Better mana regen than warriors
```

## Example 3: Balanced Creature (Hybrid)
```
id: 3
name: "Balanced Creature"
internalName: "UNIT_CLASS_BALANCED"
powertype: MANA
baseHealthMultiplier: 1.0
baseDamageMultiplier: 1.0
walkSpeed: 1.0
runSpeed: 1.0

# Balanced stat scaling
levelbasevalues: [
  { health: 90, mana: 60, stamina: 18, strength: 15, agility: 15, intellect: 15, spirit: 12 }, # Level 1
  { health: 135, mana: 90, stamina: 22, strength: 19, agility: 19, intellect: 19, spirit: 16 }, # Level 10
  { health: 700, mana: 450, stamina: 70, strength: 60, agility: 60, intellect: 60, spirit: 50 }  # Level 60
]

healthStatSources: [
  { statId: STAMINA, factor: 9.0 }
]

manaStatSources: [
  { statId: INTELLECT, factor: 12.0 }
]

attackPowerStatSources: [
  { statId: STRENGTH, factor: 1.5 },
  { statId: AGILITY, factor: 1.0 }
]

armorStatSources: [
  { statId: AGILITY, factor: 1.5 }
]
```

## Example 4: Elite Creature (Boss-type)
```
id: 4
name: "Elite Creature"
internalName: "UNIT_CLASS_ELITE"
powertype: MANA
baseHealthMultiplier: 2.0   # Double health
baseDamageMultiplier: 1.5   # 50% more damage
walkSpeed: 1.0
runSpeed: 1.2

# Very high stat scaling for elite creatures
levelbasevalues: [
  { health: 200, mana: 100, stamina: 35, strength: 30, agility: 25, intellect: 25, spirit: 20 }, # Level 1
  { health: 300, mana: 150, stamina: 45, strength: 40, agility: 35, intellect: 35, spirit: 28 }, # Level 10
  { health: 1500, mana: 750, stamina: 140, strength: 120, agility: 100, intellect: 100, spirit: 80 }  # Level 60
]

healthStatSources: [
  { statId: STAMINA, factor: 12.0 }   # More health per stamina
]

manaStatSources: [
  { statId: INTELLECT, factor: 18.0 } # More mana per intellect
]

attackPowerStatSources: [
  { statId: STRENGTH, factor: 2.5 },
  { statId: AGILITY, factor: 1.5 }
]

armorStatSources: [
  { statId: AGILITY, factor: 3.0 }
]

healthRegenPerTick: 10.0  # Faster health regen
spiritPerHealthRegen: 5.0
```

# Migration Strategy

When implementing this system:

1. **Phase 1**: Add new fields to units.proto (✓ Done)
2. **Phase 2**: Create unit class definitions
3. **Phase 3**: Update editor to support new fields
4. **Phase 4**: Update server code to use new calculations
5. **Phase 5**: Gradually migrate existing creatures

The `useStatBasedSystem` flag allows for gradual migration - creatures can be updated one by one without breaking existing functionality.
