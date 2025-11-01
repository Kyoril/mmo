# Inventory Protocol Addendum: Player Spawn Timing

**Date:** November 1, 2025  
**Issue:** LoadInventoryItems opcode removed from protocol  
**Reason:** Inventory must be loaded BEFORE player spawn

---

## Problem Identified

During protocol design review, a critical timing issue was identified:

### Original Flawed Approach

```
1. Realm Server sends PlayerCharacterJoin (WITHOUT inventory)
2. World Server spawns player (naked, incorrect stats)
3. World Server sends LoadInventoryItems request
4. Realm Server responds with inventory data
5. World Server applies equipment (AFTER spawn)
   - Player stats recalculated (HP, armor, damage)
   - Visual appearance changes (naked → equipped)
```

### Issues with Flawed Approach

1. **Visual Glitch**: Player spawns naked, then suddenly wears equipment (bad UX)
2. **Stat Inconsistency**: 
   - Player spawns with base HP (e.g., 100)
   - Equipment adds +50 HP from items
   - HP bar shows 100/100, then updates to 150/150
   - Confusing for player and other observers
3. **Gameplay Bugs**:
   - Player at 90/100 HP (90% health)
   - Equipment gives +50 max HP
   - Now at 90/150 HP (60% health)
   - Player appears more damaged than before equipping!
4. **Network Waste**: Multiple packets sent to correct initial state
5. **Race Conditions**: What if player takes damage before inventory loads?

---

## Correct Solution

### Inventory Sent Inline with Character Data

The existing architecture already handles this correctly:

```cpp
// src/shared/game_server/character_data.h
struct CharacterData
{
    // ... basic fields ...
    std::vector<ItemData> items;  // <-- Inventory included here
};
```

**Flow:**
```
1. Realm Server: CharacterEnterWorld() loads ALL data (including inventory)
2. Realm Server: Send PlayerCharacterJoin with CharacterData (includes items)
3. World Server: Receive CharacterData
4. World Server: ConstructFromRealmData() builds inventory
5. World Server: Apply equipment stats
6. World Server: Spawn player (WITH equipment, correct stats)
```

### Code Evidence

**Database Load (Realm Server):**
```cpp
// src/realm_server/mysql_database.cpp (line 531)
std::optional<CharacterData> MySQLDatabase::CharacterEnterWorld(uint64 characterId, uint64 accountId)
{
    // ... loads character data ...
    // Items loaded here from database
    result.items = loadedItemsFromDB;
    return result;
}
```

**Packet Send (Realm Server):**
```cpp
// src/realm_server/world.cpp (line 418)
void World::Join(CharacterData characterData, JoinWorldCallback callback)
{
    GetConnection().sendSinglePacket([characterData](auth::OutgoingPacket& outPacket)
    {
        outPacket.Start(auth::realm_world_packet::PlayerCharacterJoin);
        outPacket << characterData;  // Includes inventory
        outPacket.Finish();
    });
}
```

**Serialization:**
```cpp
// src/shared/game_server/character_data.h (line 189)
io::Writer& operator<<(io::Writer& writer, const CharacterData& data)
{
    writer
        // ... other fields ...
        << io::write_dynamic_range<uint16>(data.items);  // Inventory serialized
    return writer;
}
```

**Deserialization (World Server):**
```cpp
// src/shared/game_server/character_data.h (line 112)
io::Reader& operator>>(io::Reader& reader, CharacterData& data)
{
    if (!(reader
        // ... other fields ...
        >> io::read_container<uint16>(data.items)))  // Inventory deserialized
    {
        return reader;
    }
    return reader;
}
```

**Inventory Construction (World Server):**
```cpp
// src/shared/game_server/inventory.cpp (line 1309)
void Inventory::ConstructFromRealmData(std::vector<GameObjectS*>& out_items)
{
    // Iterates through m_realmData (populated from CharacterData.items)
    for (auto& data : m_realmData)
    {
        // Create item instances
        auto item = std::make_shared<GameItemS>(m_owner.GetProject(), *entry);
        
        // Apply equipment stats
        if (IsEquipmentSlot(data.slot))
        {
            m_owner.ApplyItemStats(*item, true);  // <-- Stats applied HERE
        }
        
        m_itemsBySlot[data.slot] = item;
    }
}
```

---

## Protocol Implications

### Opcodes Needed

✅ **SaveInventoryItems** - For incremental updates during gameplay  
✅ **DeleteInventoryItems** - For item destruction/removal  
❌ **LoadInventoryItems** - NOT NEEDED (comes with PlayerCharacterJoin)

### When SaveInventoryItems Is Used

**Incremental updates during gameplay:**
- Player loots items → SaveInventoryItems
- Player equips/unequips items → SaveInventoryItems
- Player buys/sells items → SaveInventoryItems
- Periodic auto-save (e.g., every 5 minutes) → SaveInventoryItems
- Player logs out → SaveInventoryItems

**NOT used for:**
- Initial character load (that's PlayerCharacterJoin)
- Character creation (that's handled separately)

### Transaction Batching

The `SaveInventoryItems` packet supports batching multiple changes:

```cpp
// World Server buffers changes during transaction
repo.BeginTransaction();
repo.SaveItem(characterId, item1);  // Added to buffer
repo.SaveItem(characterId, item2);  // Added to buffer
repo.SaveItem(characterId, item3);  // Added to buffer
repo.Commit();  // Sends 1 packet with all 3 items
```

**Benefits:**
- Reduces network overhead (1 packet instead of 3)
- Database transaction ensures atomicity
- Matches existing pattern (CharacterData, QuestData)

---

## Testing Implications

### Unit Tests

**Test: Player Spawn with Equipment**
```cpp
TEST_CASE("Player spawns with equipment applied")
{
    // Setup
    CharacterData charData;
    charData.items.push_back(CreateHelmetData(slot: 0, entry: 12345));
    charData.items.push_back(CreateArmorData(slot: 4, entry: 23456));
    
    // Act
    GamePlayerS player = CreatePlayerFromCharacterData(charData);
    
    // Assert
    REQUIRE(player.GetEquippedItem(0) != nullptr);  // Helmet equipped
    REQUIRE(player.GetEquippedItem(4) != nullptr);  // Armor equipped
    REQUIRE(player.GetMaxHP() > player.GetBaseHP()); // Stats from equipment
}
```

**Test: Equipment Affects Stats**
```cpp
TEST_CASE("Equipment stats applied before spawn")
{
    // Setup
    CharacterData charData;
    charData.hp = 100;
    charData.maxHp = 100;
    
    // Add armor that gives +50 HP
    ItemData armor;
    armor.entry = 23456;  // Armor with +50 HP
    armor.slot = 4;
    charData.items.push_back(armor);
    
    // Act
    GamePlayerS player = CreatePlayerFromCharacterData(charData);
    
    // Assert
    REQUIRE(player.GetHP() == 100);       // Current HP unchanged
    REQUIRE(player.GetMaxHP() == 150);    // Max HP increased by armor
    REQUIRE(player.GetHPPercent() == 66); // 100/150 = 66.6%
}
```

### Integration Tests

**Test: Full Player Login Flow**
```cpp
TEST_CASE("Full player login with inventory")
{
    // 1. Realm Server loads from database
    auto charData = database.CharacterEnterWorld(characterId, accountId);
    REQUIRE(charData.has_value());
    REQUIRE(!charData->items.empty());
    
    // 2. Realm Server sends to World Server
    world.Join(charData.value(), callback);
    
    // 3. Simulate network transmission
    ProcessPackets();
    
    // 4. World Server receives and constructs player
    auto& worldPlayer = worldServer.GetPlayer(characterId);
    REQUIRE(worldPlayer.GetInventory().GetItemCount() > 0);
    
    // 5. Verify equipment applied
    auto helmet = worldPlayer.GetEquippedItem(0);
    REQUIRE(helmet != nullptr);
    REQUIRE(worldPlayer.GetMaxHP() > worldPlayer.GetBaseHP());
}
```

---

## Migration Notes

### Existing Code

**Good news:** The existing code ALREADY does this correctly!

- `CharacterData` already includes `std::vector<ItemData> items`
- Serialization operators already handle inventory
- `ConstructFromRealmData()` already rebuilds inventory before spawn

### No Breaking Changes

The new `SaveInventoryItems` protocol is an **addition**, not a replacement:

**Before (still works):**
- Inventory saved via `IDatabase::UpdateCharacter()` with all character data
- Inventory loaded via `CharacterEnterWorld()` with all character data

**After (new capability):**
- Inventory saved via `SaveInventoryItems` packet (incremental updates)
- Inventory loaded via `CharacterEnterWorld()` (unchanged)

**Migration Path:**
1. Add `SaveInventoryItems` / `DeleteInventoryItems` opcodes
2. Implement packet handlers
3. Use new protocol for incremental saves (during gameplay)
4. Keep old path for full character save (on logout, if needed)
5. Gradually phase out inventory from `UpdateCharacter()` (optional)

---

## Summary

| Aspect | Decision |
|--------|----------|
| **Initial Load** | Via `PlayerCharacterJoin` (inline with `CharacterData`) |
| **Incremental Save** | Via `SaveInventoryItems` (new protocol) |
| **Delete Items** | Via `DeleteInventoryItems` (new protocol) |
| **LoadInventoryItems Opcode** | ❌ NOT IMPLEMENTED (not needed) |
| **Spawn Timing** | Inventory loaded BEFORE player spawns |
| **Equipment Stats** | Applied DURING inventory construction |
| **Visual Glitches** | ✅ PREVENTED (player spawns fully equipped) |
| **Breaking Changes** | ❌ NONE (additive protocol extension) |

---

## References

**Related Files:**
- `src/shared/game_server/character_data.h` - CharacterData structure
- `src/shared/game_server/inventory.h` - Inventory class
- `src/shared/game_server/inventory.cpp` - ConstructFromRealmData (line 1309)
- `src/realm_server/world.cpp` - Join method (line 407)
- `src/realm_server/mysql_database.cpp` - CharacterEnterWorld (line 531)
- `docs/inventory_packet_protocol.md` - Complete protocol specification

**Key Sections in Protocol Spec:**
- "Critical Design Decision: Inventory Loading Timing" (new section)
- "Load on Login" (updated to reflect inline loading)
- "LoadInventoryItems - NOT NEEDED" (section 2, explains why removed)

---

**Reviewed By:** User (November 1, 2025)  
**Status:** ✅ Approved - Protocol updated to reflect inline inventory loading
