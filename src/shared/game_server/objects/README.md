# Game Server Objects System

## Overview
The `objects` module in the `game_server` library defines the core data structures and logic for all server-side game objects. This includes players, creatures, items, and other entities that exist in the game world. The system provides a unified way to manage, update, and synchronize these objects across the server and with connected clients.

## Object Types
- **GameObjectS:** The base class for all server-side objects. Provides common functionality such as position, facing, unique ID, and field management.
- **GameUnitS:** Inherits from `GameObjectS`. Represents living entities (players, creatures, NPCs) with health, power, stats, movement, and combat capabilities.
- **GamePlayerS:** Inherits from `GameUnitS`. Adds player-specific logic, inventory, spells, quest state, and more.
- **GameCreatureS:** Inherits from `GameUnitS`. Used for non-player creatures and NPCs, often with AI controllers.
- **GameWorldObjectS:** Used for static or interactive world objects (e.g., chests, doors, triggers).
- **Other Types:** Additional classes exist for items, containers, corpses, and more, each extending the base object model as needed.

## Object Fields and Field System
- **Field Map:** Each object maintains a map of fields (attributes) such as health, position, name, etc. Fields are indexed for efficient access and network serialization.
- **Field Types:** Fields can be integers, floats, strings, or other types, depending on the object and its needs.
- **Change Tracking:** The field system tracks which fields have changed since the last update, allowing for efficient network synchronization.

## Object Management
- **Creation and Destruction:** Objects are created and destroyed by the world server as players enter/leave, creatures spawn/despawn, or items are generated/removed.
- **Spawning and Despawning:** Signals are emitted when objects spawn or despawn, allowing other systems to react (e.g., AI, quest logic).
- **World Instance Association:** Each object is associated with a world instance, which manages its spatial location and visibility.

## Field Synchronization
- **Update Blocks:** When an object's fields change, only the changed fields are packaged into an update block and sent to clients or other servers.
- **Serialization:** The system provides methods to serialize and deserialize object state for network transmission.
- **Efficient Bandwidth Usage:** By sending only changed fields, the system minimizes network traffic and ensures clients stay in sync with the server state.

## Extending the Object System
- **New Object Types:** To add a new type of object, inherit from the appropriate base class and define any additional fields or logic.
- **Custom Fields:** Extend the field map and update logic to support new attributes as needed.
- **Event Hooks:** Use signals and virtual methods to hook into object lifecycle events (spawn, despawn, update, etc.).

## Where to Look
- **game_object_s.h/cpp:** Base object logic and field management.
- **game_unit_s.h/cpp:** Living entity logic (movement, combat, stats).
- **game_player_s.h/cpp:** Player-specific logic and state.
- **game_creature_s.h/cpp:** Creature/NPC logic and AI integration.
- **game_world_object_s.h/cpp:** Static or interactive world objects.

---

_Last updated: April 21, 2025_
