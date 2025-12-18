# World Object Usability System

## Overview

The world object usability system provides flexible control over when and how world objects (chests, doors, interactive items, etc.) can be used by players. The system supports three main restriction types:

1. **Type-based restrictions** - Some object types may never be usable
2. **Quest-based restrictions** - Objects only usable when specific quests are active
3. **Script-based restrictions** - Objects can be dynamically enabled/disabled by server scripts

## Architecture

### Object Flags

World objects use the `ObjectFlags` field with the following bit flags:

```cpp
namespace world_object_flags
{
    enum Type : uint32
    {
        None = 0x00,           // No restrictions
        RequiresQuest = 0x01,  // Object requires an active quest
        Disabled = 0x02,       // Object is currently disabled
    };
}
```

### Client-Side Components

#### GameWorldObjectC_Type_Base

Base class for type-specific behavior with virtual methods:

- `CanUse()` - Static check if the type supports usage at all
- `CanUseNow(player)` - Runtime check for current usability

#### GameWorldObjectC

Main client-side world object class with:

- `IsUsable(player)` - Comprehensive usability check combining all restrictions
- `GetRequiredQuestId()` - Virtual method to specify quest requirement

### Server-Side Components

#### GameWorldObjectS

Main server-side world object class with:

- `IsUsable(player)` - Server-side usability validation
- `SetEnabled(bool)` - Enable/disable via scripts
- `SetRequiredQuest(questId)` - Set quest requirement
- `GetRequiredQuestId()` - Get current quest requirement

## Usage Examples

### 1. Type-Based Restriction (Never Usable)

For object types that should never be interactable:

```cpp
class GameWorldObjectC_Type_Door : public GameWorldObjectC_Type_Base
{
public:
    bool CanUse() const override
    {
        return false;  // Doors are not usable
    }
};
```

### 2. Quest-Based Restriction

Objects that require an active quest:

**Server-side setup:**
```cpp
// In your object spawn/initialization code
auto magicalChest = worldInstance->SpawnWorldObject(entry, position);

// Set quest requirement (e.g., quest ID 1234)
magicalChest->SetRequiredQuest(1234);
```

The system will automatically:
- Set the `RequiresQuest` flag
- Check player's quest log for active quest with status `Incomplete`
- Prevent usage if quest is not active

**Client-side:**
The `IsUsable()` method automatically checks:
1. If object has `RequiresQuest` flag
2. Queries player's quest log for the required quest
3. Validates quest status is `Incomplete`

### 3. Script-Based Dynamic Control

Enable/disable objects at runtime:

```cpp
// Disable an object (e.g., during a scripted event)
gameObject->SetEnabled(false);

// Later, re-enable it
gameObject->SetEnabled(true);
```

### 4. Custom Quest Requirements (Advanced)

For objects with specific quest requirements, override `GetRequiredQuestId()`:

**Server-side:**
```cpp
class MySpecialWorldObject : public GameWorldObjectS
{
protected:
    uint32 GetRequiredQuestId() const override
    {
        return 5678;  // Hardcoded quest requirement
    }
};
```

**Client-side:**
```cpp
class MySpecialWorldObjectC : public GameWorldObjectC
{
protected:
    uint32 GetRequiredQuestId() const override
    {
        return 5678;  // Must match server-side
    }
};
```

### 5. Complex Type-Specific Logic

For runtime checks based on player state:

```cpp
class GameWorldObjectC_Type_PuzzleChest : public GameWorldObjectC_Type_Base
{
public:
    bool CanUseNow(const GamePlayerC& player) const override
    {
        // Example: Only usable by high-level players
        return player.GetLevel() >= 50;
    }
};
```

## Implementation Details

### Client-Side Validation Flow

The `GameWorldObjectC::IsUsable()` method performs checks in this order:

1. **Type-based check**: `m_typeData->CanUse()`
   - Returns false if type doesn't support usage

2. **Disabled flag check**: `ObjectFlags & Disabled`
   - Returns false if server disabled the object

3. **Quest requirement check**: `ObjectFlags & RequiresQuest`
   - Queries player's quest log
   - Validates quest is active with `Incomplete` status
   - Returns false if quest not found or wrong status

4. **Runtime type check**: `m_typeData->CanUseNow(player)`
   - Allows type-specific dynamic validation

5. **Success**: Returns true if all checks pass

### Server-Side Validation Flow

The `GameWorldObjectS::IsUsable()` method performs:

1. **Disabled flag check**: `ObjectFlags & Disabled`
2. **Quest requirement check**: `ObjectFlags & RequiresQuest`
   - Uses `GamePlayerS::GetQuestStatus()`
   - Validates status is `Incomplete`
3. **Success**: Returns true if all checks pass

## Best Practices

1. **Consistency**: Ensure client and server use the same quest IDs via `GetRequiredQuestId()`

2. **Quest Status**: Objects with `RequiresQuest` flag only work when quest status is `Incomplete`
   - Quest must be in player's quest log
   - Quest must not be completed or failed

3. **Flag Management**: Use provided methods (`SetEnabled()`, `SetRequiredQuest()`) rather than manually manipulating flags

4. **Type Design**: 
   - Use `CanUse()` for permanent restrictions
   - Use `CanUseNow()` for dynamic checks based on player state

5. **Performance**: Client-side checks iterate quest log (max 20 slots), keep this in mind for high-frequency checks

## Quest Integration

The system integrates with the existing quest system:

- Uses `object_fields::QuestLogSlot_1` through `QuestLogSlot_20`
- Checks `QuestField` structure with `questId` and `status`
- Only accepts `quest_status::Incomplete` as valid active state
- Automatically becomes unusable when quest is completed/failed/abandoned

## Server Script Examples

### Timed Event

```cpp
// Make object available during a timed event
void OnEventStart(GameWorldObjectS* object)
{
    object->SetEnabled(true);
    
    // Schedule disable after 10 minutes
    scheduleTimer(600, [object]()
    {
        object->SetEnabled(false);
    });
}
```

### Quest Chain

```cpp
// Update required quest as player progresses
void OnQuestComplete(GamePlayerS* player, uint32 completedQuestId)
{
    if (completedQuestId == 1000)  // First quest
    {
        // Enable object for next quest in chain
        auto nextObject = findObject(123);
        nextObject->SetRequiredQuest(1001);
    }
}
```

## Debugging

To verify usability:

**Server-side:**
```cpp
DLOG("Object usable by player: " << object->IsUsable(player));
DLOG("Object flags: " << object->Get<uint32>(object_fields::ObjectFlags));
DLOG("Required quest: " << object->GetRequiredQuestId());
```

**Client-side:**
```cpp
const auto* activePlayer = ObjectMgr::GetActivePlayer();
bool usable = worldObject->IsUsable(*activePlayer);
DLOG("Object usable: " << usable);
```

## Migration Notes

- Existing objects default to usable (no flags set)
- `ObjectFlags` field was already present in schema
- No database migration required
- Backward compatible with existing code

