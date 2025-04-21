# MMO Game Server Library

## Overview
The `game_server` library is a core part of the MMO project's server-side infrastructure. It provides the foundational logic and data structures for simulating the game world, managing entities, and handling gameplay systems on the server. This library is shared between different server components (such as the world server and realm server) to ensure consistent game logic and data handling.

## Why This Library Exists
- **Separation of Concerns:** By isolating game simulation and entity logic from networking and database code, the project achieves better maintainability and testability.
- **Code Reuse:** Both the world server and other server-side components can use the same logic for handling game objects, units, spells, inventory, and more.
- **Consistency:** Ensures that all server components interpret and process game rules in the same way, reducing bugs and desyncs.

## What the Library Covers
The `game_server` library covers the following major areas:

### 1. Game Object Model
- **Base Classes:** Defines `GameObjectS` (server-side game object), `GameUnitS` (living entities like players and creatures), and `GamePlayerS` (player-specific logic).
- **Field System:** Uses a field map to efficiently store and update object state, supporting network synchronization and efficient change tracking.
- **Movement & Positioning:** Handles movement, facing, relocation, and world instance management for all objects.

### 2. Gameplay Systems
- **Combat:** Implements combat capabilities, stat modifications, health/power management, and combat event handling.
- **Spells & Auras:** Manages spell casting, aura application, and spell effect processing.
- **Inventory:** Provides inventory and item management for player characters.
- **Quests:** Supports quest state tracking and quest-related interactions.
- **AI:** (For creatures) Integrates with AI logic for non-player entities.

### 3. World Simulation
- **World Instances:** Supports multiple world/map instances, including instanced dungeons and regions.
- **Visibility & Grid:** Manages spatial partitioning, visibility checks, and efficient area queries.
- **Event System:** Signals and events for object spawning, despawning, movement, and other world changes.

### 4. Networking Support
- **Serialization:** Provides methods to serialize/deserialize object state for network transmission.
- **Update Blocks:** Efficiently packages state changes for clients and other servers.

## How It Works
- **Component-Based:** Entities are composed from base classes and extended with additional systems (e.g., spells, inventory).
- **Signal/Event Driven:** Uses signals for key events (spawn, despawn, movement, etc.), allowing decoupled system interactions.
- **Efficient State Tracking:** Only changed fields are sent over the network, minimizing bandwidth.
- **Shared Protobuf Data:** Integrates with shared protobuf definitions for static game data (spells, items, etc.).

## Where to Look
- **objects/**: Core entity classes (`game_object_s.h`, `game_unit_s.h`, `game_player_s.h`, etc.)
- **spells/**: Spell and aura logic.
- **world/**: World instance, grid, and visibility management.
- **inventory.h**: Inventory and item handling.

## Typical Usage
- The world server instantiates and manages game objects using these classes.
- Game logic (combat, movement, quests) is processed using the systems in this library.
- State changes are serialized and sent to clients or other servers as needed.

## Extending the Library
- Add new gameplay systems by extending base classes or adding new modules.
- Integrate new types of world objects or mechanics by following the established patterns.

---

_Last updated: April 21, 2025_
