# Spell Management Tools

The MCP Content Server now includes comprehensive spell management capabilities.

## Spell Tools

### 1. **spells_list** - List All Spells
Lists spells with optional filtering capabilities.

**Parameters:**
- `minLevel` (optional): Minimum spell level
- `maxLevel` (optional): Maximum spell level
- `spellSchool` (optional): Filter by spell school
  - 0: Physical
  - 1: Holy
  - 2: Fire
  - 3: Nature
  - 4: Frost
  - 5: Shadow
  - 6: Arcane
- `powerType` (optional): Filter by power type
  - 0: Mana
  - 1: Rage
  - 2: Energy
  - 3: Health
- `limit` (optional): Maximum number of spells to return (default: 100)
- `offset` (optional): Number of spells to skip for pagination

**Returns:** Array of spell objects with basic information

### 2. **spells_get** - Get Spell Details
Retrieves detailed information about a specific spell including all effects, reagents, and attributes.

**Parameters:**
- `id` (required): The spell ID

**Returns:** Detailed spell object including:
- Basic properties (name, description, school, power type, cost, levels)
- Casting information (cast time, cooldown, speed, duration, range)
- Mechanics (spell mechanic, damage class, facing requirements)
- Effects array (all spell effects with full details)
- Reagents (required items for casting)
- Attributes (spell flags and behavior modifiers)
- Proc information (trigger flags, chance, charges)
- Class/race restrictions

### 3. **spells_create** - Create New Spell
Creates a new spell with specified properties. Automatically ensures the spell has at least 2 attribute bitmaps as required by the editor.

**Parameters:**
- `name` (required): Spell name
- `description` (optional): Spell description
- `spellSchool` (optional): Spell school (0-6)
- `powerType` (optional): Power type (0-3)
- `cost` (optional): Spell cost
- `castTime` (optional): Cast time in milliseconds
- `cooldown` (optional): Cooldown in milliseconds
- `duration` (optional): Duration in milliseconds
- `spellLevel` (optional): Spell level
- `baseLevel` (optional): Base level required
- `maxLevel` (optional): Maximum level
- `rangeType` (optional): Range type ID
- `effects` (optional): Array of spell effects (see Effect Structure below)

**Returns:** Detailed spell object

### 4. **spells_update** - Update Existing Spell
Updates an existing spell's properties.

**Parameters:**
- `id` (required): The spell ID to update
- Other parameters same as `spells_create` (all optional)

**Returns:** Updated detailed spell object

### 5. **spells_delete** - Delete Spell
Deletes a spell from the project.

**Parameters:**
- `id` (required): The spell ID to delete

**Returns:** Success status with confirmation message

### 6. **spells_search** - Search Spells
Searches for spells by name or description (case-insensitive).

**Parameters:**
- `query` (required): Search query string
- `limit` (optional): Maximum number of results (default: 50)

**Returns:** Array of matching spell objects

## Effect Structure

Spell effects are complex objects with the following properties:

```json
{
  "index": 0,
  "type": 2,
  "typeName": "SchoolDamage",
  "basePoints": 100,
  "dieSides": 20,
  "baseDice": 1,
  "targetA": 6,
  "targetB": 0,
  "radius": 0.0,
  "aura": 0,
  "amplitude": 0,
  "triggerSpell": 0,
  "miscValueA": 0,
  "miscValueB": 0
}
```

### Effect Types (Examples)
- 0: None
- 1: InstantKill
- 2: SchoolDamage
- 3: Dummy
- 6: ApplyAura
- 10: Heal
- 21: CreateItem
- 27: Energize
- 49: ApplyAreaAura

See `spell.h` for the complete list.

## Spell Enumerations

### Spell Schools
- 0: Physical/Normal
- 1: Holy
- 2: Fire
- 3: Nature
- 4: Frost
- 5: Shadow
- 6: Arcane

### Power Types
- 0: Mana (most common for casters)
- 1: Rage (warriors)
- 2: Energy (rogues)
- 3: Health

### Spell Mechanics
- 0: None
- 1: Charm
- 4: Distract
- 5: Fear
- 6: Root
- 7: Silence
- 8: Sleep
- 9: Snare
- 10: Stun
- 11: Freeze

### Damage Classes
- 0: None
- 1: Magic
- 2: Melee
- 3: Ranged

## Example Usage

### Create a Simple Damage Spell
```json
{
  "name": "Fireball",
  "description": "Hurls a fiery ball that causes Fire damage.",
  "spellSchool": 2,
  "powerType": 0,
  "cost": 100,
  "castTime": 3000,
  "cooldown": 0,
  "spellLevel": 10,
  "baseLevel": 8,
  "maxLevel": 0,
  "effects": [
    {
      "index": 0,
      "type": 2,
      "basePoints": 85,
      "dieSides": 15,
      "baseDice": 1,
      "targetA": 6
    }
  ]
}
```

### Search for Fire Spells
```json
{
  "query": "fire",
  "limit": 20
}
```

### List High-Level Mana Spells
```json
{
  "minLevel": 50,
  "powerType": 0,
  "limit": 100
}
```

## Spell Attributes

Spells have two attribute bitmaps stored in the `attributes` array. Common flags include:

- `0x00000001`: Channeled
- `0x00000002`: Ranged
- `0x00000010`: Ability
- `0x00000040`: Passive
- `0x00000400`: Can Target Dead
- `0x04000000`: Negative (harmful spell)

## Architecture

Spell tools are implemented in:
- **spell_tools.h/cpp**: Implements spell management operations
- Integrated into **mcp_server.h/cpp**: JSON-RPC tool registration

## Related Files

- `src/shared/game/spell.h` - Spell constants and enumerations
- `src/shared/proto_data/spells.proto` - Spell protobuf definitions
- `src/shared/proto_data/spell_visualizations.proto` - Spell visual effects
- `src/mmo_edit/editor_windows/spell_editor_window.cpp` - Reference implementation

## Notes

- All spell modifications are persisted to disk when the server shuts down gracefully
- Spell IDs are auto-generated and managed by the project system
- The spell system supports complex mechanics including procs, auras, and triggered effects
- Effects can chain to other spells via `triggerSpell` field
- Spells can be ranked (different power levels of the same base spell)

Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
