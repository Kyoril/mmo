# Bot Party Invitation System

## Overview

The bot framework now supports handling party invitations from other players. This feature demonstrates event-driven design where game events are delegated to bot profiles for custom handling.

## Architecture

### Event Flow

1. **Server → BotRealmConnector**: Server sends `GroupInvite` packet
2. **BotRealmConnector**: Parses packet and fires `PartyInvitationReceived` signal
3. **BotSession**: Receives signal and delegates to active profile
4. **BotProfile**: `OnPartyInvitation()` event handler decides how to respond
5. **Response**: Either auto-decline (return false) or queue accept action (return true)

### Key Components

#### 1. IBotProfile Interface
```cpp
virtual bool OnPartyInvitation(BotContext& context, const std::string& inviterName) = 0;
```
- Pure virtual method that all profiles must implement
- Returns `true` to indicate interest in accepting (profile should queue action)
- Returns `false` to automatically decline the invitation

#### 2. BotContext Facade
```cpp
void AcceptPartyInvitation();
void DeclinePartyInvitation();
```
- Clean API for profiles to interact with party system
- Encapsulates protocol details from profile logic

#### 3. AcceptPartyInvitationAction
- Bot action that accepts a pending invitation
- Can be queued with delays to simulate realistic human behavior
- Validates bot is in world before executing

#### 4. BotRealmConnector
- Handles `GroupInvite` packet from server
- Sends `GroupAccept` or `GroupDecline` packets to server
- Fires `PartyInvitationReceived` signal for profiles

## Usage Examples

### Example 1: Auto-Accept with Delay (Realistic Behavior)
```cpp
bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
{
    ILOG("Received party invitation from " << inviterName);
    
    // Simulate human "thinking time" (1-3 seconds)
    std::uniform_int_distribution<int> dist(1000, 3000);
    auto delay = std::chrono::milliseconds(dist(RandomGenerator));
    
    QueueActionNext(std::make_shared<WaitAction>(delay));
    QueueActionNext(std::make_shared<AcceptPartyInvitationAction>());
    
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
        QueueActionNext(std::make_shared<WaitAction>(2000ms));
        QueueActionNext(std::make_shared<AcceptPartyInvitationAction>());
        return true;
    }
    
    ILOG("Declining invitation from " << inviterName);
    return false; // Auto-decline
}
```

### Example 3: Always Decline (Default Behavior)
```cpp
bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
{
    ILOG("Bot is busy, declining party invitation from " << inviterName);
    return false; // Base class behavior
}
```

### Example 4: Immediate Acceptance (Not Recommended)
```cpp
bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
{
    // WARNING: This is unrealistic - no human accepts instantly
    context.AcceptPartyInvitation();
    return true;
}
```

## Design Rationale

### Why Event-Driven?
- **Separation of Concerns**: Network protocol handling is separate from bot behavior
- **Flexibility**: Each profile can implement custom logic
- **Testability**: Easy to mock events for testing

### Why Return Bool Instead of Auto-Accept?
- **Flexibility**: Profile controls timing and conditions
- **Realism**: Allows simulating human behavior with delays
- **Control**: Profile might want to check state before accepting

### Why Not Auto-Accept on True Return?
- **Human Simulation**: Real humans don't instantly accept invitations
- **Action Queue**: Leverages existing action system for timing control
- **Flexibility**: Profile can add chat messages, delays, or other actions

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

Each would follow the same event-driven pattern established by the party invitation system.
