# Game Server Spells System

## Overview
The `spells` module in the `game_server` library manages all aspects of spell casting, spell effects, and aura (buff/debuff) handling for entities in the game world. It provides the logic for how spells are cast, how their effects are applied, and how ongoing effects (auras) are managed on the server.

## How the Spells System Works
- **Spell Casting:** Entities (players, creatures) can cast spells using the spell system. The system checks requirements (range, cooldown, resources), initiates the cast, and processes the result.
- **Spell Effects:** Each spell can have one or more effects (damage, healing, buffs, debuffs, etc.). Effects are applied to targets as defined by the spell's data.
- **Auras:** Persistent effects (such as buffs or debuffs) are managed as auras. The system tracks their duration, periodic effects, and expiration.
- **Event Hooks:** The spell system integrates with combat, movement, and other systems to trigger events (e.g., on hit, on cast, on expire).

## Key Features
- **Flexible Spell Data:** Spell definitions are data-driven, typically loaded from protobuf files, allowing easy addition and balancing of spells.
- **Targeting:** Supports various targeting types (single target, area, self, etc.) and validates targets before casting.
- **Cooldowns and Costs:** Manages cooldown timers and resource (mana, energy, etc.) costs for each spell.
- **Aura Management:** Handles stacking, overwriting, and removal of auras, as well as periodic effects (e.g., damage over time).
- **Network Synchronization:** Notifies clients of spell casts, effects, and aura changes for visual feedback and UI updates.

## How to Use
- **Casting a Spell:** Call the appropriate method on a unit or player to initiate a spell cast. The system will handle validation, execution, and effect application.
- **Defining Spells:** Add or modify spell definitions in the data files (protobuf), specifying effects, costs, cooldowns, and other parameters.
- **Custom Effects:** Implement new effect types or aura behaviors by extending the spell or aura logic classes.

## Example Flow
1. A player selects a target and casts a spell.
2. The spell system checks if the spell can be cast (range, cooldown, resources).
3. If valid, the cast is started; after any cast time, the spell's effects are applied to the target(s).
4. If the spell applies an aura, it is added to the target and managed for its duration.
5. The system notifies clients of the cast, effects, and any aura changes.

## Extending the Spells System
- Add new spell or aura effect types by subclassing the relevant base classes.
- Integrate new triggers or event hooks for advanced spell interactions.
- Adjust or add spell data in the protobuf files for new abilities or balance changes.

## Where to Look
- **spell_cast.h/cpp:** Core spell casting logic.
- **aura_container.h/cpp:** Aura (buff/debuff) management.
- **spell_target_map.h/cpp:** Targeting logic for spells.
- **spells.proto:** Data definitions for spells and effects.

---

_Last updated: April 21, 2025_
