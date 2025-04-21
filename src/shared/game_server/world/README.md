# Game Server World System

## Overview
The `world` module in the `game_server` library is responsible for simulating the game world on the server. It manages world instances, spatial partitioning, visibility, object placement, and the orchestration of all gameplay systems within each world or map. This system is central to how the MMO server handles large, dynamic, and persistent environments.

## Key Responsibilities
- **World Instances:** Supports multiple concurrent world instances (maps, dungeons, battlegrounds), each with its own set of objects and state.
- **Spatial Partitioning:** Uses grids and tiles to efficiently manage and query objects, enabling fast lookups for visibility, proximity, and area-of-effect logic.
- **Object Management:** Handles spawning, despawning, and updating of all objects within a world instance.
- **Visibility & Subscription:** Determines which objects are visible to each player or entity, and manages subscriptions for efficient update delivery.
- **Event Processing:** Orchestrates regular updates (ticks), event scheduling, and time-based logic for all world systems.
- **Pathfinding & Navigation:** Integrates with navigation meshes and pathfinding algorithms for AI and movement.
- **Instance-Specific Logic:** Supports instanced content with separate state, such as dungeons or private areas.

## How the World System Works
- **WorldInstance:** Represents a single running world or map. Manages all objects, updates, and events within its boundaries.
- **WorldInstanceManager:** Oversees creation, destruction, and management of all world instances.
- **VisibilityGrid:** Divides the world into tiles for efficient spatial queries and update propagation.
- **Tile Subscribers:** Entities subscribe to tiles to receive updates about nearby objects and events.
- **Regular Updates:** The world system runs a main update loop, processing movement, AI, combat, and other systems each tick.

## Object Placement and Movement
- Objects are placed in the world using precise coordinates and are tracked within the grid system.
- Movement and relocation are handled with collision checks, pathfinding, and event notifications to nearby entities.

## Visibility and Area Queries
- The grid system allows for fast queries of which objects are within a given area, line of sight, or range.
- Used for gameplay features like aggro, spell targeting, and area-of-effect abilities.

## Instance Management
- Each world instance is isolated, allowing for private dungeons, battlegrounds, or phased content.
- Instances can be created and destroyed dynamically as players enter or leave.

## Extending the World System
- Add new world logic by extending `WorldInstance` or adding new systems to the update loop.
- Integrate new spatial queries or event types as needed for gameplay features.
- Support new types of instanced or shared content by leveraging the instance management framework.

## Where to Look
- **world_instance.h/cpp:** Core world instance logic.
- **world_instance_manager.h/cpp:** Management of all world instances.
- **visibility_grid.h/cpp:** Spatial partitioning and visibility logic.
- **tile_subscriber.h/cpp:** Subscription and update delivery for tiles.
- **each_tile_in_sight.h/cpp:** Efficient area queries for gameplay logic.

---

_Last updated: April 21, 2025_
