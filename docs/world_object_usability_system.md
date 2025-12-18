# World Object Usability System

## Overview

The world object usability system provides **data-driven**, flexible control over when and how world objects (chests, doors, interactive items, etc.) can be used by players. The system supports three main restriction types:

1. **Type-based restrictions** - Some object types may never be usable
2. **Quest-based restrictions** - Objects only usable when specific quests are active (configured in editor)
3. **Script-based restrictions** - Objects can be dynamically enabled/disabled by server scripts

## Data-Driven Configuration

### Object Editor

Objects can be configured to require a quest directly in the object editor:

1. Open the object in the editor
2. Navigate to the **Quests** section
3. Under **Usability**, select a **Required Quest** from the dropdown
4. The object will only be usable by players who have this quest active (status: Incomplete)

This is the **recommended approach** for most use cases.

### Proto Definition

The quest requirement is stored in the `ObjectEntry` proto:

```proto
message ObjectEntry {
    // ... other fields ...
    optional uint32 requiredquest = 19 [default = 0];
}
```

- `requiredquest = 0`: Object is usable by everyone (default)
- `requiredquest = <quest_id>`: Object requires the specified quest to be active

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

These flags are automatically set based on the proto configuration.

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

- `IsUsable(player)` - Server-side usability validation (enforced before looting)
- `SetEnabled(bool)` - Enable/disable via scripts
- `SetRequiredQuest(questId)` - Dynamically set quest requirement (overrides proto)
- `GetRequiredQuestId()` - Get current quest requirement

#### Enforcement Points

The system enforces usability at two key points:

1. **OnLoot handler** ([player_inventory_handlers.cpp](c:\\Kyoril\\mmo\\src\\world_server\\player_inventory_handlers.cpp#L352)) - Validates before allowing loot
2. **Use method** ([game_world_object_s.cpp](c:\\Kyoril\\mmo\\src\\shared\\game_server\\objects\\game_world_object_s.cpp#L62)) - Validates before creating loot instance

## Usage Examples

### 1. Data-Driven Quest Requirement (Recommended)

**In the Object Editor:**
1. Select object \"Silverleaf Sprig\" (ID: 1)
2. Go to Quests → Usability → Required Quest
3. Select \"Gather Silverleaf\" quest
4. Save

The object will now only be usable when players have the \"Gather Silverleaf\" quest active.

**No code required!**

### 2. Type-Based Restriction (Never Usable)

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

### 3. Script-Based Dynamic Control

Change usability at runtime via server scripts:

```cpp
// Disable an object (e.g., during a scripted event)
gameObject->SetEnabled(false);

// Later, re-enable it
gameObject->SetEnabled(true);
```

### 4. Dynamic Quest Requirement Override

Override the proto-configured quest requirement at runtime:

```cpp
// Change quest requirement dynamically
gameObject->SetRequiredQuest(9999);

// Remove quest requirement
gameObject->SetRequiredQuest(0);
```

**Note:** This overrides the proto configuration until the object despawns.

### 5. Custom Quest Requirements (Advanced)

For objects with hardcoded quest requirements, override `GetRequiredQuestId()`:

**Server-side:**
```cpp
class MySpecialWorldObject : public GameWorldObjectS
{
public:
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

## Implementation Details

### Server-Side Initialization

When an object is spawned, the system automatically applies the proto configuration:

```cpp
void GameWorldObjectS::Initialize()
{
    // ... other initialization ...
    
    // Apply quest requirement from proto data
    if (m_entry.has_requiredquest() && m_entry.requiredquest() != 0)
    {
        SetRequiredQuest(m_entry.requiredquest());
    }
}
```

### Server-Side Validation Flow

The `GameWorldObjectS::IsUsable()` method performs:

1. **Disabled flag check**: `ObjectFlags & Disabled`
2. **Quest requirement check**: `ObjectFlags & RequiresQuest`
   - Uses `GamePlayerS::GetQuestStatus()`
   - Validates status is `Incomplete`
3. **Success**: Returns true if all checks pass

### Enforcement Flow

When a player attempts to loot an object:

1. **Client sends loot request** with object GUID
2. **Server receives in OnLoot handler**
3. **Server checks `IsUsable(player)`**
4. **If failed**: Send `loot_error::Locked` response
5. **If passed**: Proceed with loot dialog

### Client-Side Validation Flow

The `GameWorldObjectC::IsUsable()` method performs checks in this order:

1. **Type-based check**: `m_typeData->CanUse()`
2. **Disabled flag check**: `ObjectFlags & Disabled`
3. **Quest requirement check**: `ObjectFlags & RequiresQuest`
   - Queries player's quest log (max 20 slots)
   - Validates quest is active with `Incomplete` status
4. **Runtime type check**: `m_typeData->CanUseNow(player)`
5. **Success**: Returns true if all checks pass

## Quest Integration

The system integrates seamlessly with the existing quest system:

- Quest requirement read from `ObjectEntry.requiredquest`
- Automatically sets `RequiresQuest` flag when quest ID is non-zero
- Uses `object_fields::QuestLogSlot_1` through `QuestLogSlot_20`
- Checks `QuestField` structure with `questId` and `status`
- Only accepts `quest_status::Incomplete` as valid active state
- Automatically becomes unusable when quest is completed/failed/abandoned

## Best Practices

1. **Use Data-Driven Configuration**: Configure quest requirements in the object editor whenever possible

2. **Quest Status**: Objects with `RequiresQuest` flag only work when quest status is `Incomplete`
   - Quest must be in player's quest log
   - Quest must not be completed or failed

3. **Consistency**: Ensure client and server use the same quest IDs

4. **Error Messages**: Players receive `loot_error::Locked` when attempting to use restricted objects

5. **Performance**: Client-side checks iterate quest log (max 20 slots), acceptable for interaction checks

6. **Loot Conditions vs Object Usability**:
   - **Object Usability**: Controls whether object can be interacted with at all
   - **Loot Conditions**: Controls what items drop from usable objects
   - Use object usability when you want to hide/disable the interaction entirely
   - Use loot conditions when object should be usable but rewards are conditional

## Example: Quest-Gated Resource Node

**Scenario**: A magical herb (Silverleaf Sprig) that only herbalists with the \"Learn Herbalism\" quest can gather.

**Setup**:
1. Create quest \"Learn Herbalism\" (ID: 100)
2. Create object \"Silverleaf Sprig\" (ID: 1, Type: Chest)
3. In object editor, set Required Quest = \"Learn Herbalism\"
4. Create loot entry with Silverleaf item
5. Optionally add condition to loot for additional filtering

**Result**:
- Players without quest: Cannot interact with object at all
- Players with quest active: Can interact and receive loot
- Players who completed quest: Cannot interact (quest not active)

**Alternative with loot conditions only**:
- All players can interact
- Only players with quest get loot
- Creates empty loot window for non-eligible players (poor UX)

## Debugging

**Server-side logging:**
```cpp
DLOG(\"Object usable: \" << object->IsUsable(player));
DLOG(\"Object flags: 0x\" << std::hex << object->Get<uint32>(object_fields::ObjectFlags));
DLOG(\"Required quest: \" << object->GetRequiredQuestId());
DLOG(\"Player quest status: \" << player.GetQuestStatus(object->GetRequiredQuestId()));
```

**Check proto configuration:**
- Open object in editor
- Verify \"Required Quest\" field is set correctly
- Check quest ID matches expected quest

**Common issues:**
1. Object always usable → Check `requiredquest` field in proto
2. Never usable → Check if object type's `CanUse()` returns false
3. Quest completed but object still locked → Working as intended (requires active quest, not completed)

## Migration Notes

- Existing objects default to usable (`requiredquest = 0`)
- Proto field added with default value 0 (backward compatible)
- No database migration required
- Existing loot conditions continue to work
- Can combine object usability with loot conditions for layered control

