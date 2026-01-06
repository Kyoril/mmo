# Bot Party Invitation System

## Overview

The bot framework now supports handling party invitations from other players. This feature demonstrates event-driven design where game events are delegated to bot profiles for custom handling.

## Architecture

### Event Flow

1. **Server → BotRealmConnector**: Server sends `GroupInvite` packet
2. **BotRealmConnector**: Parses packet and fires `PartyInvitationReceived` signal
3. **BotSession**: Receives signal and delegates to active profile
4. **BotProfile**: `OnPartyInvitation()` event handler decides how to respond
5. **Response**: Either auto-decline (return false) or queue urgent accept action (return true)

### Key Components

#### 1. IBotProfile Interface
```cpp
virtual bool OnPartyInvitation(BotContext& context, const std::string& inviterName) = 0;
```
- Pure virtual method that all profiles must implement
- Returns `true` to indicate interest in accepting (profile should queue urgent action)
- Returns `false` to automatically decline the invitation

#### 2. Interruptible Actions
```cpp
virtual bool IsInterruptible() const { return false; }
```
- Actions can mark themselves as interruptible (e.g., `WaitAction`)
- Non-interruptible actions (e.g., `ChatMessageAction`) complete atomically
- Used by `QueueUrgentAction()` to determine if current action can be aborted

#### 3. Urgent Action Queuing
```cpp
void QueueUrgentAction(BotActionPtr action, BotContext& context);
void QueueUrgentActions(const std::vector<BotActionPtr>& actions, BotContext& context);
```
- Queues actions at the front of the queue
- **Interrupts current action** if it's interruptible (e.g., aborts a 24-hour wait)
- Perfect for event-driven responses that need immediate handling

#### 4. BotContext Facade
```cpp
void AcceptPartyInvitation();
void DeclinePartyInvitation();
```
- Clean API for profiles to interact with party system
- Encapsulates protocol details from profile logic

#### 5. AcceptPartyInvitationAction
- Bot action that accepts a pending invitation
- Can be queued with delays to simulate realistic human behavior
- Validates bot is in world before executing

## Usage Examples

### Example 1: Auto-Accept with Delay (Recommended Pattern)
```cpp
bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
{
    ILOG("Received party invitation from " << inviterName);
    
    // Create urgent actions: wait briefly then accept
    // This will interrupt the current action if it's a wait
    std::vector<BotActionPtr> urgentActions = {
        std::make_shared<WaitAction>(2000ms),  // Simulate thinking time
        std::make_shared<AcceptPartyInvitationAction>()
    };
    
    QueueUrgentActions(urgentActions, context);
    return true; // Don't auto-decline
}
```

### Example 2: Conditional Acceptance
```cpp
bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
{
    // Only accept invitations from specific players
    if (inviterName == "TrustedPlayer" || inviterName == "Admin")
    {
        QueueUrgentAction(std::make_shared<AcceptPartyInvitationAction>(), context);
        return true;
    }
    
    ILOG("Declining invitation from " << inviterName);
    return false; // Auto-decline
}
```

### Example 3: Random Delay for Realism
```cpp
bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
{
    // Random delay between 1-5 seconds
    std::uniform_int_distribution<int> dist(1000, 5000);
    auto delay = std::chrono::milliseconds(dist(RandomGenerator));
    
    std::vector<BotActionPtr> urgentActions = {
        std::make_shared<WaitAction>(delay),
        std::make_shared<AcceptPartyInvitationAction>()
    };
    
    QueueUrgentActions(urgentActions, context);
    return true;
}
```

### Example 4: Always Decline (Default Behavior)
```cpp
bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
{
    ILOG("Bot is busy, declining party invitation from " << inviterName);
    return false; // Base class behavior
}
```

## Interruptible Actions Design

### Why Interruptible?

The problem: Profiles often queue actions upfront, including long waits (e.g., 24 hours). Without interruptibility, event-triggered actions would wait hours to execute.

The solution: Mark certain actions as interruptible so urgent actions can preempt them.

### Which Actions Are Interruptible?

| Action | Interruptible | Reason |
|--------|---------------|--------|
| `WaitAction` | ✅ Yes | Waiting can be safely aborted |
| `ChatMessageAction` | ❌ No | Should complete atomically |
| `MoveToPositionAction` | ❌ No | Should reach destination or fail |
| `AcceptPartyInvitationAction` | ❌ No | Instant, non-interruptible |

### Creating Custom Interruptible Actions

```cpp
class MyInterruptibleAction : public BotAction
{
public:
    bool IsInterruptible() const override
    {
        return true; // Can be interrupted by urgent actions
    }
    
    void OnAbort(BotContext& context) override
    {
        // Clean up when interrupted
    }
    
    // ... other methods
};
```

## Queue Methods Comparison

| Method | Where Queued | Interrupts Current? | Use Case |
|--------|-------------|---------------------|----------|
| `QueueAction()` | End | No | Normal workflow actions |
| `QueueActionNext()` | Front | No | Priority but not urgent |
| `QueueUrgentAction()` | Front | Yes (if interruptible) | Event responses |
| `QueueUrgentActions()` | Front (in order) | Yes (if interruptible) | Multi-step event responses |

## Design Rationale

### Why Event-Driven?
- **Separation of Concerns**: Network protocol handling is separate from bot behavior
- **Flexibility**: Each profile can implement custom logic
- **Testability**: Easy to mock events for testing

### Why Interruptible Actions?
- **Real-world Profiles**: Profiles queue actions upfront, including long waits
- **Responsive Events**: Events need to be handled promptly, not after hours
- **Selective Interruption**: Only idle-like actions should be interrupted

### Why Return Bool Instead of Auto-Accept?
- **Flexibility**: Profile controls timing and conditions
- **Realism**: Allows simulating human behavior with delays
- **Control**: Profile might want to check state before accepting

## Protocol Details

### Incoming Packet: GroupInvite
```cpp
String inviterName;
packet >> io::read_container<uint8>(inviterName);
```

### Outgoing Packet: GroupAccept
```cpp
packet.Start(game::client_realm_packet::GroupAccept);
packet.Finish();
```

### Outgoing Packet: GroupDecline
```cpp
packet.Start(game::client_realm_packet::GroupDecline);
packet.Finish();
```

## Clean Architecture Principles Applied

1. **Dependency Inversion**: Profiles depend on IBotProfile interface, not concrete implementations
2. **Single Responsibility**: Each component has one clear purpose
3. **Interface Segregation**: BotContext provides minimal, focused API
4. **Encapsulation**: Protocol details hidden from profiles
5. **Open/Closed**: Easy to extend with new profiles without modifying framework

## Future Enhancements

Potential additional events that could follow this pattern:
- `OnTradeRequest()`
- `OnDuelChallenge()`
- `OnGuildInvitation()`
- `OnWhisperReceived()`
- `OnCombatEntered()`

Each would follow the same event-driven pattern with urgent action queuing for responsive handling.
