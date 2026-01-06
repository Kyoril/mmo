# Bot Framework Architecture

## Overview

The bot framework has been redesigned using **Clean Architecture** and **Domain-Driven Design (DDD)** principles to be maintainable, extensible, and testable. The architecture separates concerns into distinct layers with clear boundaries.

## Architecture Components

### 1. **Bot Actions** (`bot_action.h`, `bot_actions.h`)
Actions follow the **Command Pattern**, encapsulating individual bot behaviors that can be queued, executed, and potentially undone.

**Key Concepts:**
- `IBotAction`: Interface for all bot actions
- `ActionResult`: Success, InProgress, or Failed
- Actions can validate preconditions before execution
- Actions can clean up on abort

**Built-in Actions:**
- `ChatMessageAction`: Send chat messages
- `WaitAction`: Wait for a specified duration  
- `MoveToPositionAction`: Move to a target position
- `HeartbeatAction`: Send heartbeat to keep connection alive

**Example:**
```cpp
auto action = std::make_shared<ChatMessageAction>("Hello!", ChatType::Say);
ActionResult result = action->Execute(context);
```

### 2. **Bot Context** (`bot_context.h`, `bot_context.cpp`)
The context acts as a **Facade**, providing actions with safe access to bot capabilities while hiding implementation details.

**Responsibilities:**
- Provides access to realm connector operations (chat, movement)
- Manages bot state (world ready, movement info)
- Offers custom state storage for profiles
- Enforces boundaries between domain logic and infrastructure

**Example:**
```cpp
context.SendChatMessage("Hi!", ChatType::Say);
context.SendMovementUpdate(opCode, movementInfo);
context.SetState("greeting_sent", "true");
```

### 3. **Bot Profiles** (`bot_profile.h`, `bot_profiles.h`)
Profiles implement the **Strategy Pattern**, defining different bot behaviors that can be swapped at runtime.

**Base Classes:**
- `IBotProfile`: Interface for all profiles
- `BotProfile`: Base implementation with action queue management

**Built-in Profiles:**
- `SimpleGreeterProfile`: Sends greeting and maintains connection
- `ChatterProfile`: Sends a sequence of messages with delays
- `PatrolProfile`: Moves through waypoints
- `SequenceProfile`: Demonstrates combining multiple actions

**Profile Lifecycle:**
```cpp
profile->OnActivate(context);    // Initialize when bot enters world
bool continue = profile->Update(context);  // Execute queued actions
profile->OnDeactivate(context);  // Clean up on exit
```

### 4. **Configuration** (`bot_main.cpp`)
Extended JSON configuration with profile selection:

```json
{
  "behavior": {
    "greeting": "Hi",
    "profile": "simple_greeter"
  }
}
```

Supported profiles: `simple_greeter`, `chatter`, `sequence`

## Design Principles Applied

### Clean Architecture
- **Separation of Concerns**: Domain logic (actions/profiles) separated from infrastructure (connectors)
- **Dependency Inversion**: High-level policies don't depend on low-level details
- **Testability**: Actions and profiles can be tested independently

### Domain-Driven Design
- **Ubiquitous Language**: Clear terminology (Action, Profile, Context)
- **Bounded Contexts**: Bot behavior is a separate context from networking
- **Entities & Value Objects**: Actions are value objects, Context manages entity state

### SOLID Principles
- **Single Responsibility**: Each class has one reason to change
- **Open/Closed**: Extend behavior by adding new actions/profiles, not modifying existing code
- **Liskov Substitution**: Any IBotAction can be used interchangeably
- **Interface Segregation**: IBotAction has minimal required methods
- **Dependency Inversion**: Depends on abstractions, not concrete implementations

## Extending the Framework

### Creating a New Action

```cpp
class CastSpellAction final : public BotAction
{
public:
    explicit CastSpellAction(uint32 spellId, uint64 targetGuid)
        : m_spellId(spellId), m_targetGuid(targetGuid) {}

    std::string GetDescription() const override
    {
        return "Cast spell " + std::to_string(m_spellId);
    }

    ActionResult Execute(BotContext& context) override
    {
        // Send spell cast packet
        auto connector = context.GetRealmConnector();
        connector->SendCastSpell(m_spellId, m_targetGuid);
        return ActionResult::Success;
    }

    bool CanExecute(const BotContext& context, std::string& outReason) const override
    {
        if (!context.IsWorldReady())
        {
            outReason = "Not in world";
            return false;
        }
        return true;
    }

private:
    uint32 m_spellId;
    uint64 m_targetGuid;
};
```

### Creating a New Profile

```cpp
class CombatTestProfile final : public BotProfile
{
public:
    std::string GetName() const override { return "CombatTest"; }

protected:
    void OnActivateImpl(BotContext& context) override
    {
        // Queue sequence of actions to test combat
        QueueAction(std::make_shared<ChatMessageAction>("Starting combat test"));
        QueueAction(std::make_shared<WaitAction>(1000ms));
        QueueAction(std::make_shared<CastSpellAction>(133, targetGuid)); // Fireball
        QueueAction(std::make_shared<WaitAction>(2000ms));
        QueueAction(std::make_shared<ChatMessageAction>("Combat test complete"));
    }

    bool OnQueueEmpty(BotContext& context) override
    {
        // Test complete, just maintain connection
        QueueAction(std::make_shared<WaitAction>(5000ms));
        QueueAction(std::make_shared<HeartbeatAction>());
        return true;
    }
};
```

## Future Enhancements

### Integration Testing Framework
- Add assertion actions to verify expected behavior
- Record packet sequences for replay testing
- Compare actual vs. expected packets

### Load Testing
- Profile that spawns multiple bot instances
- Configurable spawn rate and bot count
- Metrics collection (latency, packet rate, errors)

### Advanced Movement
- Pathfinding integration
- Collision avoidance
- Following waypoints from file

### Advanced Combat
- Target selection logic
- Spell rotation management
- Conditional actions based on health/mana

### Scripting Support
- Lua script profiles
- Runtime profile reloading
- Visual scripting editor integration

## Best Practices

1. **Keep Actions Small**: Each action should do one thing well
2. **Use Action Queues**: Chain actions for complex behaviors
3. **Validate Preconditions**: Check `CanExecute()` before queueing
4. **Handle Failures**: Override `OnActionFailed()` in profiles
5. **Document Behavior**: Provide clear descriptions for logging
6. **Avoid Blocking**: Use InProgress for long-running operations
7. **Clean Up**: Implement `OnAbort()` for resources
8. **Test Independently**: Unit test actions without network

## Example Usage Scenarios

### Simple Chat Bot
```json
{
  "behavior": {
    "profile": "chatter",
    "greeting": "Hello world"
  }
}
```

### Patrol Bot
```cpp
std::vector<Vector3> waypoints = {
    {100, 0, 100},
    {200, 0, 100},
    {200, 0, 200},
    {100, 0, 200}
};
auto profile = std::make_shared<PatrolProfile>(waypoints, true);
```

### Integration Test Bot
```cpp
// Queue actions with assertions
profile->QueueAction(std::make_shared<ChatMessageAction>("Test start"));
profile->QueueAction(std::make_shared<CastSpellAction>(spellId, target));
profile->QueueAction(std::make_shared<AssertPacketReceivedAction>(expectedPacket));
profile->QueueAction(std::make_shared<ChatMessageAction>("Test passed"));
```

## Troubleshooting

**Bot doesn't move:**
- Check that movement packets are implemented correctly in BotRealmConnector
- Verify MovementInfo is being updated properly
- Enable verbose logging to see movement packets

**Actions not executing:**
- Check `CanExecute()` validation
- Verify bot is in world (`context.IsWorldReady()`)
- Check profile's `Update()` is being called regularly

**Profile changes not working:**
- Regenerate CMake cache after adding new files
- Rebuild the project completely
- Check profile name in bot_config.json matches CreateProfile() cases

## Contributing

When adding new actions or profiles:

1. Follow the existing naming conventions
2. Document the purpose and usage
3. Add examples to this README
4. Consider testability
5. Update bot_config.json sample if needed
6. Follow C++ Core Guidelines and project conventions
