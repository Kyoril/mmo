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
- `MoveToPositionAction`: Move to a target position (sends MoveStartForward, periodic MoveHeartBeat packets, and MoveStop)

**Example:**
```cpp
auto action = std::make_shared<ChatMessageAction>("Hello!", ChatType::Say);
ActionResult result = action->Execute(context);
```

### Movement Protocol

Movement in the game follows a specific packet protocol:

**Initial Spawn:**
- Characters spawn with `Falling` flag set
- Before any other movement, send `MoveFallLand` to clear the FALLING flag
- Attempting to start movement with FALLING flag will cause server errors
- `MoveToPositionAction` automatically handles this by detecting and clearing the flag

**Starting Movement:**
1. Send `MoveStartForward` (or other start packet)
2. Add appropriate movement flag (e.g., `Forward`)

**During Movement:**
3. Every ~500ms, send `MoveHeartBeat` packet with updated position
4. **IMPORTANT**: Do NOT modify movement flags during heartbeat packets
5. Server validates position and broadcasts to nearby players

**Stopping Movement:**
6. Send `MoveStop` (or other stop packet)
7. Remove movement flag

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
- `CombatProfile`: Demonstrates target selection plus direct pursuit melee behavior
- `PartyFollowProfile`: Runs the live companion runtime: follow the party leader out of combat, switch to role-aware combat anchors in combat, regroup or hold conservatively when anchor data becomes invalid, and execute class-specific companion combat decisions inside the same long-lived action

**Profile Lifecycle:**
```cpp
profile->OnActivate(context);    // Initialize when bot enters world
bool continue = profile->Update(context);  // Execute queued actions
profile->OnDeactivate(context);  // Clean up on exit
```

### 4. **Configuration and Startup Selection** (`bot_main.cpp`)
Startup now resolves exactly one profile before the bot session begins networking.

**Precedence:**
1. `--profile <key>` CLI override
2. `behavior.profile` in the JSON config
3. interactive console prompt when multiple profiles are available
4. clear non-zero exit when prompting is required but stdin is not interactive

**CLI options:**
- `--config <path>`: load a specific bot JSON file
- `--profile <key>`: override the configured behavior profile
- `--character <name>`: override the configured character selector

**Example config:**
```json
{
  "login": {
    "host": "mmo-dev.net",
    "port": 3724,
    "username": "your-account",
    "password": "your-password"
  },
  "realm": {
    "name": "Development",
    "index": 0
  },
  "character": {
    "name": "Bot",
    "create_if_missing": true,
    "race": 1,
    "class": 1,
    "gender": 0
  },
  "behavior": {
    "greeting": "Hi",
    "random_move": false,
    "heartbeat_ms": 5000,
    "profile": "combat"
  }
}
```

If `behavior.profile` is omitted or empty and more than one profile is registered, `mmo_bot` prompts for a numbered selection only when stdin is interactive. Non-interactive runs fail fast instead of silently falling back to `simple_greeter`.

Character startup follows the same "pick exactly one target" contract:
1. `--character <name>` CLI override
2. `character.name` from the JSON config
3. auto-resolve when exactly one enumerated character is available
4. interactive numbered prompt when the character list is ambiguous or malformed (duplicate case-insensitive names / empty names)
5. fail fast on non-interactive startup when a prompt would be required

`character.create_if_missing` only creates a character for an explicit named selector that was not found. Successful creation keeps the existing re-enumerate flow before entering the world, and startup logs now distinguish `profile_resolution`, `character_resolution`, and `character_create` phases without echoing credentials.

Supported profiles: `simple_greeter`, `chatter`, `sequence`, `unit_awareness`, `combat`, `party_follow`

### Party Follow Companion Runtime

`party_follow` is the canonical live startup profile for companion behavior. It queues one long-lived companion action that:

- follows the current party leader while the party is out of combat
- switches to a role-aware combat anchor once the party enters combat
- keeps the S02/S03 movement seam as the only live movement path
- resolves warrior capabilities from committed class/spell data plus the live spellbook when the configured character is a warrior
- resolves cleric capabilities from committed class/spell/range data plus the live spellbook when the configured character is a healer
- resolves mage combat capabilities from committed class/spell/range data plus the live spellbook when the configured character is a mage
- translates warrior controller intents into live auto-attack or cast packets inside the same runtime tick without starting a second action queue
- translates cleric controller intents into live heal, support-aura, or filler casts inside the same runtime tick without starting a second action queue
- translates mage controller intents into live Frostbolt-style ranged casts inside the same runtime tick without starting a second action queue, while holding explicitly when authored instant-fallback or spacing metadata is missing for the current project data
- remembers last observed ally health for conservative healer triage when an injured party member drops out of awareness
- regroups on the leader or holds position conservatively when the leader GUID, awareness, nav state, combat anchor data, or class runtime state becomes invalid

**Expected diagnostics:**
- one-shot `companion mode=` transitions when the runtime switches between travel, combat anchor, regroup, and hold states
- one-shot `anchor decision=` logs that include the current anchor reason and follow decision
- one-shot `warrior action=` logs for real auto-attack or cast sends, including reason codes and target/spell details
- one-shot `warrior failure=` logs for conservative skips, blocked casts, cast failures, incomplete spellbook/cooldown state, invalid targets, and follow/runtime gating
- one-shot `cleric action=` logs for real heal, support-aura, or filler cast sends, including spell ids, target guids, health/mana context, and reason codes
- one-shot `cleric failure=` logs for conservative holds, oom/cooldown gating, out-of-awareness allies, blocked casts, cast failures, incomplete spellbook/cooldown state, and follow/runtime gating
- one-shot `mage action=` logs for real ranged cast sends, including spell ids, spell names, target guids, mana context, and reason codes
- one-shot `mage failure=` logs for conservative holds, missing authored fallback categories, oom/cooldown gating, invalid targets, cast failures, and cast-failure backoff
- explicit reason codes for conservative fallbacks such as leader loss, stale combat anchors, unresolved maps, nav unavailability, or class runtime state gaps

These diagnostics may include GUIDs, spell ids, spell names, health percentages, distances, reason codes, and anchor coordinates, but they must not include credentials or session secrets.

### Integrated recovery proof

The cold-start proof entrypoint for S07 is:

```powershell
powershell -NoProfile -File .\tools\verify_m002_s07_recovery.ps1
```

Run it from the repository root before a live session. The script is the canonical automated check for the assembled `party_follow` runtime and intentionally stops on the first failing step so the failing suite or inventory drift is obvious. It performs four checks in order:

1. builds the Debug `unit_tests` and `mmo_bot` targets
2. runs the focused warrior, cleric, mage, companion, and follow runtime suites with the same Windows-friendly executable invocation the slice used during development
3. runs aggregate `ctest --test-dir build -C Debug --output-on-failure -R '^unit_tests$'` so the proof also passes through the registered test surface
4. verifies the operator-facing diagnostics inventory in this README and the live runtime log prefixes in the companion runtime source

Treat the proof as failed if any focused suite fails, if `ctest` fails after the focused suites pass, or if the documentation/runtime inventory no longer contains the expected recovery markers.

### Acceptable conservative degradation vs regression

The hardened S07 contract allows conservative holds when the runtime says *why* it held. These still count as acceptable behavior for M002 as long as they are explicit, one-shot, and the bot recovers once the underlying fault clears:

- `follow_runtime_blocked` only when the bot's own runtime preconditions are invalid, such as `self_unavailable` or an invalid self position
- `cast_failure_backoff` after a real server-reported cast failure so retry suppression is visible instead of silently spamming the same cast every tick
- hold, regroup, or abort reasons tied to leader loss, stale combat anchors, `map_unresolved`, `nav_unavailable`, or missing authored class data
- mage conservative holds when close-range recovery metadata is genuinely unavailable instead of speculatively casting the wrong spell

Treat the session as regressed when the bot goes quiet during combat, keeps repeating the same failure without the deduped one-shot diagnostics, stops class execution on a merely recoverable follow abort, or loses `companion mode=` / `anchor decision=` correlation that lets a future agent explain the state transition.

#### live session checklist

Use one real party session after the automated proof passes. The goal is not perfect uptime; the goal is explicit, diagnosable behavior that either keeps contributing or holds conservatively for a named reason.

1. **Startup**: launch `mmo_bot` with the `party_follow` profile and confirm the bot enters the world without profile-selection ambiguity or character-resolution surprises.
2. **Out-of-combat following**: verify the bot trails the leader and emits `companion mode=` with travel/leader-follow context plus matching `anchor decision=` follow output.
3. **Combat anchor switch**: pull combat and confirm the runtime switches to combat-anchor behavior instead of continuing pure travel follow.
4. **Class contribution**: for the class under test, confirm at least one matching class-specific signal appears during combat:
   - warrior: `warrior action=` or an explicit `warrior failure=`
   - cleric: `cleric action=` or an explicit `cleric failure=`
   - mage: `mage action=` or an explicit `mage failure=`
5. **Recoverable follow fault**: trigger or observe a recoverable nav/map-style fault and confirm the follow reason is explicit (`map_unresolved`, nav loss, stale anchor, leader awareness loss, and similar) without the class runtime going brain-dead on that same tick.
6. **Cast failure visibility**: if the server rejects a cast, confirm the runtime first surfaces the cast failure and then emits a one-shot `cast_failure_backoff` hold instead of retrying invisibly.
7. **Conservative hold audit**: when the bot cannot act safely, confirm the hold/regroup/failure reason is specific enough to explain whether the issue was self state, leader state, spellbook/cooldown readiness, mana, or authored capability gaps.
8. **Success bar**: count the live pass as good when the bot starts, follows, switches anchors, contributes by class when inputs are valid, and exposes explicit recovery/backoff diagnostics whenever it must hold. Count it as failed when it silently degrades, spams repeated undecorated failures, or stops contributing without a reason code.

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
- Proper physics simulation (gravity, acceleration, deceleration)
- Handle all movement types (backward, strafe, turn, jump, fall)
- Respect terrain height and nav mesh

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
- Check that MoveStartForward is sent before movement begins
- Verify MovementInfo position is being calculated correctly based on speed and time
- Ensure heartbeat packets are sent every ~500ms during movement
- Check that movement flags are only modified by appropriate packets (not during heartbeat)
- Enable verbose logging to see movement packets
- Verify MoveStop is sent when destination is reached

**"Client tried to remove FALLING flag in non-jump packet" error:**
- This occurs when the bot tries to move without first landing
- Characters spawn with the FALLING flag set
- The bot must send MoveFallLand packet before any other movement
- MoveToPositionAction now automatically detects and clears the FALLING flag
- The server only allows FALLING flag removal via MoveFallLand or MoveJump packets

**Movement validation errors:**
- Server validates movement strictly - check `OnMovement()` in world_server/player.cpp
- Ensure movement flags match the packet type (e.g., Forward flag with MoveStartForward)
- Don't modify flags during heartbeat packets
- Position must be reasonable based on movement speed and elapsed time
- Character must not move while rooted or dead

**Actions not executing:**
- Check `CanExecute()` validation
- Verify bot is in world (`context.IsWorldReady()`)
- Check profile's `Update()` is being called regularly

**Profile selection not working:**
- Check that `behavior.profile` or `--profile` matches one of the registered startup profile keys
- Omit the selector only when you expect an interactive prompt on a TTY
- Non-interactive runs now fail instead of silently falling back to another profile
- Regenerate CMake cache after adding new files
- Rebuild the project completely

## Contributing

When adding new actions or profiles:

1. Follow the existing naming conventions
2. Document the purpose and usage
3. Add examples to this README
4. Consider testability
5. Update bot_config.json sample if needed
6. Follow C++ Core Guidelines and project conventions
