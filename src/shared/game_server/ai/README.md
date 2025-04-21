# Game Server AI System

## Overview
The AI system in the `game_server` library is responsible for controlling the behavior of non-player characters (NPCs), such as creatures and other autonomous entities, within the game world. It provides a framework for defining, updating, and executing AI logic on the server side.

## How the AI System Works
- **AI Controllers:** Each NPC or creature is associated with an AI controller (e.g., `CreatureAI`). The controller determines the entity's actions based on its current state and environment.
- **State Machine:** AI controllers typically use a state machine to manage different behaviors (idle, patrol, combat, fleeing, etc.). Transitions between states are triggered by events or conditions (e.g., spotting a player, taking damage).
- **Update Loop:** The world server periodically updates all AI controllers. During each update, the controller evaluates the current state, checks for triggers, and executes actions (move, attack, cast spell, etc.).
- **Scripting and Extensibility:** AI logic can be extended or customized by implementing new controller classes or integrating scripting support for more complex behaviors.

## Key Features
- **Per-Entity AI:** Each creature or NPC can have its own AI logic, allowing for diverse behaviors.
- **Event Handling:** AI controllers can respond to in-game events (e.g., aggro, death, spell cast, proximity to players).
- **Integration with Game Systems:** AI interacts with combat, movement, spell casting, and other core systems to perform actions.
- **Performance:** Designed to efficiently update many entities in large worlds by only processing relevant logic each tick.

## How to Use
- **Assigning AI:** When spawning a creature or NPC, assign an appropriate AI controller (e.g., `CreatureAI`).
- **Custom AI:** To create custom behaviors, subclass the base AI controller and override relevant methods (e.g., `OnUpdate`, `OnAggro`, `OnDeath`).
- **Configuration:** AI parameters (such as patrol paths, aggro radius, etc.) can be set via data files or code.

## Example Flow
1. A creature spawns in the world and is assigned a `CreatureAI` controller.
2. On each server tick, the AI controller's update method is called.
3. The controller checks for nearby players, evaluates its state, and decides whether to idle, patrol, chase, or attack.
4. If a player enters aggro range, the AI transitions to combat state and initiates an attack.
5. The AI can react to events (e.g., being attacked, low health) and change behavior accordingly.

## Extending the AI System
- Implement new AI controller classes for unique NPC behaviors.
- Integrate scripting (e.g., Lua) for dynamic or data-driven AI logic.
- Add new event hooks or state transitions as needed for gameplay features.

---

_Last updated: April 21, 2025_
