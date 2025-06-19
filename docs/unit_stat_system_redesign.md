# Unit Stat System Redesign

## Overview
This redesign transforms the MMO's creature system from using hard-coded health/mana/damage values to a flexible, stat-based system similar to how players work. This enables consistent spell effects across all unit types and provides much more flexibility in creature design.

## Files Created/Modified

### New Proto Files
1. **`unit_classes.proto`** - Defines creature class templates with stat formulas
2. **`stat_constants.proto`** - Centralized constants for stat types, power types, etc.
3. **`stat_calculations.proto`** - Formula definitions and calculation helpers
4. **`default_unit_classes.proto`** - Container for default class configurations

### Modified Files
1. **`units.proto`** - Added new stat-based fields while maintaining backward compatibility

## Key Concepts

### Unit Classes
Creatures now reference a `UnitClass` (similar to player classes) that defines:
- **Power Type**: Mana, Rage, Energy, Focus, or Runic Power
- **Stat Scaling**: How stats grow per level
- **Formulas**: How stats convert to health, mana, damage, armor, etc.
- **Base Values**: Starting values for different levels

### Stat-Based Calculations
Instead of fixed values, creatures now calculate final stats using:
```
FinalValue = BaseValue + (Level * PerLevelIncrease) + StatModifiers
```

Where `StatModifiers` come from formulas like:
- Health = Stamina × 10
- Mana = Intellect × 15  
- Attack Power = Strength × 2 + Agility × 1
- Armor = Agility × 2

### Backward Compatibility
The system includes a `useStatBasedSystem` flag in `UnitEntry`. When `false`, the creature uses the old hard-coded values. When `true`, it uses the new stat-based calculations.

## Benefits

### 1. Consistent Spell Effects
Spells that modify stamina will now affect creature health just like player health:
- A spell that reduces stamina by 10 will reduce health by 100 (if stamina factor is 10)
- Temporary stat buffs/debuffs work on all units consistently

### 2. Flexible Creature Design
Designers can now:
- Create creature "archetypes" (warrior, caster, hybrid, elite)
- Easily scale creatures by changing their class or level
- Apply consistent stat progressions across creature families

### 3. Simplified Balancing
- Balance one class template, apply to many creatures
- Level scaling becomes automatic and consistent
- Easy to create elite/rare variants with stat multipliers

### 4. Rich Customization
Each creature can still have individual:
- Base stats different from class defaults
- Stat growth rates per level
- Elite multipliers for special creatures
- Custom resistance arrays

## Example Usage

### Creating a Warrior-type Creature
```protobuf
# In units.proto
UnitEntry {
  id: 12345
  name: "Orc Warrior"
  minlevel: 10
  maxlevel: 15
  
  # New stat-based fields
  useStatBasedSystem: true
  unitClassId: 1  # References warrior class
  
  # Base stats (before class modifiers)
  baseStamina: 25
  baseStrength: 30
  baseAgility: 15
  baseIntellect: 8
  baseSpirit: 10
  
  # Elite modifier for tougher variant
  eliteStatMultiplier: 1.2  # 20% stat boost
}
```

### The Warrior Class Template
```protobuf
# In unit_classes.proto  
UnitClassEntry {
  id: 1
  name: "Warrior"
  powertype: RAGE
  
  # Health calculation: stamina × 10
  healthStatSources: [
    { statId: STAMINA, factor: 10.0 }
  ]
  
  # Attack power: strength × 2 + agility × 1
  attackPowerStatSources: [
    { statId: STRENGTH, factor: 2.0 },
    { statId: AGILITY, factor: 1.0 }
  ]
  
  # Base values per level
  levelbasevalues: [
    { stamina: 20, strength: 18, agility: 12, ... },  # Level 1
    { stamina: 25, strength: 23, agility: 15, ... },  # Level 10
    # ... more levels
  ]
}
```

## Implementation Phases

### Phase 1: Data Structure (✅ Complete)
- [x] Create new proto files
- [x] Add backward-compatible fields to units.proto
- [x] Define calculation formulas
- [x] Create example configurations

### Phase 2: Editor Support (Next)
- [ ] Update unit editor to show new stat fields
- [ ] Create unit class editor interface
- [ ] Add preview of calculated final stats
- [ ] Migration tools for existing creatures

### Phase 3: Server Implementation (Later)
- [ ] Implement stat calculation engine
- [ ] Update creature spawning code
- [ ] Modify spell effect system to use stats
- [ ] Add runtime stat caching for performance

### Phase 4: Migration & Testing (Final)
- [ ] Migrate existing creatures gradually
- [ ] Balance testing with new system
- [ ] Performance optimization
- [ ] Documentation updates

## Migration Strategy

1. **Gradual Migration**: Use `useStatBasedSystem` flag to migrate creatures individually
2. **Hybrid Period**: Both systems work simultaneously during transition
3. **Class Templates**: Create templates for common creature types first
4. **Testing**: Verify spell effects work consistently across unit types
5. **Performance**: Monitor for any performance impacts from stat calculations

## Technical Notes

- All calculations should be cached at spawn time for performance
- Stat changes should trigger recalculation of dependent values
- Level scaling is automatic once class templates are defined
- Elite/rare multipliers are applied after base calculations
- Resistance arrays remain separate from main stat system for flexibility

This system provides the foundation for much more sophisticated creature AI and spell interactions while maintaining full backward compatibility with existing content.
