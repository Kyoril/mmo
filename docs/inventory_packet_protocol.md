# Inventory Packet Protocol Specification

## Overview

This document specifies the packet protocol for inventory persistence between World Server (gameplay) and Realm Server (database). The protocol extends the existing `auth_protocol` infrastructure defined in `src/shared/auth_protocol/auth_protocol.h`.

---

## Architecture Context

### Server Responsibilities

**World Server** (Gameplay Logic, NO Database Access):
- Manages active game sessions and player inventories
- Sends inventory changes to Realm Server for persistence
- Buffers operations during transactions to minimize network calls

**Realm Server** (Persistence Layer, HAS Database Access):
- Receives inventory operations from World Servers
- Persists changes to database asynchronously
- Returns operation results to World Server

### Communication Flow

```
World Server                              Realm Server
┌─────────────────┐                      ┌─────────────────┐
│  GamePlayerS    │                      │  World          │
│  ┌───────────┐  │                      │  Connection     │
│  │ Inventory │  │  Inventory Ops       │  ┌───────────┐  │
│  │           │──┼──────────────────────┼─>│ Handler   │  │
│  └───────────┘  │  (buffered)          │  └─────┬─────┘  │
│        │        │                      │        │        │
│        v        │                      │        v        │
│  WorldServer    │                      │   IDatabase     │
│  InventoryRepo  │  BatchCommit         │   (MySQL)       │
│  (Network Proxy)│──────────────────────┼───> Async       │
│                 │                      │     Persist     │
│                 │<─────────────────────┼────             │
│                 │  OperationResult     │                 │
└─────────────────┘                      └─────────────────┘
```

---

## Critical Design Decision: Inventory Loading Timing

### Problem Statement

Equipped items affect player stats (HP, armor, damage, spell power, etc.). If inventory loads AFTER player spawn, the following issues occur:

1. **Visual Glitches**: Player spawns naked, then suddenly wears equipment
2. **Stat Inconsistency**: HP bar shows incorrect maximum, then updates
3. **Gameplay Bugs**: Player might die during initialization if HP is recalculated
4. **Network Waste**: Client receives player spawn, then immediate stat updates

### Solution: Inline Inventory with Character Data

**Decision:** Inventory data is sent inline with `PlayerCharacterJoin` packet via `CharacterData.items`.

**Rationale:**
- Ensures atomic player spawn (all data arrives together)
- Equipment stats applied BEFORE player becomes visible
- No race conditions or initialization order issues
- Matches existing architecture (`CharacterData` already includes inventory)

**Implementation:**
```cpp
// Realm Server: Load character with inventory
std::optional<CharacterData> data = database.CharacterEnterWorld(characterId, accountId);
// data.items already populated from database

// Realm Server: Send to World Server
outPacket.Start(auth::realm_world_packet::PlayerCharacterJoin);
outPacket << data;  // Includes inventory
outPacket.Finish();

// World Server: Receive and construct player
CharacterData characterData;
packet >> characterData;  // Inventory included

GamePlayerS player(project, timerQueue);
player.GetInventory().ConstructFromRealmData(out_items);  // Uses characterData.items
// Equipment stats applied here

// NOW player can spawn with correct stats
```

**Benefits:**
1. ✅ Player spawns with equipment already worn
2. ✅ Stats correct from first frame (HP, armor, damage)
3. ✅ No visual glitches (no "naked then dressed" effect)
4. ✅ No separate load request needed (fewer packets)
5. ✅ Consistent with existing architecture

### When to Use SaveInventoryItems

`SaveInventoryItems` is used for **incremental updates** during gameplay:
- Player loots items
- Player equips/unequips items
- Player buys/sells items
- Periodic auto-save

**NOT used for initial load** - that happens via `PlayerCharacterJoin`.

---

## Protocol Extension

### New Opcodes

Extend `auth_protocol.h` with the following opcodes:

#### World → Realm Packets (`world_realm_packet` namespace)

Add to existing `enum Type` after `PlayerGroupUpdate`:

```cpp
namespace world_realm_packet
{
    enum Type
    {
        // ... existing opcodes ...
        PlayerGroupUpdate,
        
        // NEW: Inventory persistence operations
        /// Batch save inventory items (transactional)
        SaveInventoryItems,
        
        /// Delete specific inventory items
        DeleteInventoryItems,
        
        // NOTE: LoadInventoryItems is NOT needed - inventory data is sent
        // inline with PlayerCharacterJoin packet via CharacterData.items
    };
}
```

#### Realm → World Packets (`realm_world_packet` namespace)

Add to existing `enum Type` after `PlayerGuildChanged`:

```cpp
namespace realm_world_packet
{
    enum Type
    {
        // ... existing opcodes ...
        PlayerGuildChanged,
        
        // NEW: Inventory operation results
        /// Response to inventory operations (save/load/delete)
        InventoryOperationResult,
    };
}
```

---

## Packet Formats

### 1. SaveInventoryItems (World → Realm)

**Opcode:** `world_realm_packet::SaveInventoryItems`

**Purpose:** Persist multiple inventory item changes in a single transaction.

**Payload:**
```cpp
struct SaveInventoryItemsPacket
{
    uint64 characterGuid;        // Character whose inventory is being saved
    uint32 operationId;          // Unique ID for tracking this operation
    uint16 itemCount;            // Number of items to save
    ItemData items[];            // Array of item data (already defined)
};
```

**Serialization:**
```cpp
outPacket.Start(auth::world_realm_packet::SaveInventoryItems);
outPacket
    << io::write<uint64>(characterGuid)
    << io::write<uint32>(operationId)
    << io::write<uint16>(itemCount);
    
for (const auto& itemData : items)
{
    outPacket << itemData;  // Uses existing ItemData serialization
}

outPacket.Finish();
```

**Notes:**
- Uses existing `ItemData` structure (matches `InventoryItemData`)
- All items saved in single database transaction
- `operationId` allows async result tracking

---

### 2. LoadInventoryItems - **NOT NEEDED** ❌

**Decision:** LoadInventoryItems opcode is **NOT IMPLEMENTED** in this protocol.

**Reason:** Inventory data is already sent inline with character spawn data.

**Current Flow:**
1. Realm Server receives player enter world request
2. Realm Server loads character from database (including inventory via `CharacterData.items`)
3. Realm Server sends `PlayerCharacterJoin` packet with complete `CharacterData`
4. World Server receives character data with inventory included
5. World Server constructs player with equipment already applied
6. Player spawns with correct stats (HP, armor, etc. affected by equipped items)

**Why This Matters:**
- Equipped items affect player stats (HP, armor, damage, etc.)
- Player must spawn with equipment already applied
- Separating inventory load would cause player to spawn naked, then suddenly get equipment
- This would cause stat recalculation after spawn (incorrect HP bar, visual glitches)

**Implementation Note:**
The existing `CharacterData` struct already contains `std::vector<ItemData> items` which is serialized:

```cpp
// From src/shared/game_server/character_data.h
struct CharacterData
{
    // ... other fields ...
    std::vector<ItemData> items;  // Inventory included here
};

// Serialization (line 189)
writer << io::write_dynamic_range<uint16>(data.items);

// Deserialization (line 112)
reader >> io::read_container<uint16>(data.items);
```

**Conclusion:** `LoadInventoryItems` opcode is unnecessary. Inventory loading happens via existing `PlayerCharacterJoin` packet.

---

### 3. DeleteInventoryItems (World → Realm)

**Opcode:** `world_realm_packet::DeleteInventoryItems`

**Purpose:** Delete specific items from persistent storage.

**Payload:**
```cpp
struct DeleteInventoryItemsPacket
{
    uint64 characterGuid;        // Character whose items to delete
    uint32 operationId;          // Unique ID for tracking this operation
    uint16 slotCount;            // Number of slots to delete
    uint16 slots[];              // Array of absolute slot indices
};
```

**Serialization:**
```cpp
outPacket.Start(auth::world_realm_packet::DeleteInventoryItems);
outPacket
    << io::write<uint64>(characterGuid)
    << io::write<uint32>(operationId)
    << io::write<uint16>(slotCount);
    
for (const auto slot : slots)
{
    outPacket << io::write<uint16>(slot);
}

outPacket.Finish();
```

**Notes:**
- Can delete multiple items in single transaction
- Uses absolute slot indices (0-based)

---

### 4. InventoryOperationResult (Realm → World)

**Opcode:** `realm_world_packet::InventoryOperationResult`

**Purpose:** Return result of inventory operation (success/failure).

**Payload:**
```cpp
enum InventoryOperationType : uint8
{
    Save = 0,
    Delete = 1
    // Load removed - not needed (see LoadInventoryItems section)
};

struct InventoryOperationResultPacket
{
    uint64 characterGuid;            // Character affected
    uint32 operationId;              // Matches request operation ID
    uint8  operationType;            // Save or Delete
    bool   success;                  // Operation succeeded?
};
```

**Serialization (Success or Failure):**
```cpp
outPacket.Start(auth::realm_world_packet::InventoryOperationResult);
outPacket
    << io::write<uint64>(characterGuid)
    << io::write<uint32>(operationId)
    << io::write<uint8>(operationType)
    << io::write<uint8>(success ? 1 : 0);  // Bool converted to uint8
outPacket.Finish();
```

**Notes:**
- Simplified format - no item data needed in response
- Save/Delete responses only contain success/failure
- Load operation removed (inventory comes with PlayerCharacterJoin)

---

## Implementation Plan

### Phase A: Extend Protocol Definition

**File:** `src/shared/auth_protocol/auth_protocol.h`

Add new opcodes to existing enums (see "New Opcodes" section above).

---

### Phase B: World Server - Packet Sending

**File:** `src/world_server/realm_connector.h`

Add new methods:

```cpp
class RealmConnector
{
public:
    // ... existing methods ...
    
    /// Send batch of inventory items to persist
    void SendSaveInventoryItems(uint64 characterGuid, uint32 operationId, 
                               const std::vector<ItemData>& items);
    
    /// Request to delete inventory items
    void SendDeleteInventoryItems(uint64 characterGuid, uint32 operationId, 
                                 const std::vector<uint16>& slots);
};
```

**File:** `src/world_server/realm_connector.cpp`

Implement using existing `sendSinglePacket` pattern (see examples at lines 364-394).

**Note:** No `SendLoadInventoryItems` method needed - inventory comes with character spawn.

---

### Phase C: World Server - Packet Handler

**File:** `src/world_server/realm_connector.h`

Add handler declaration:

```cpp
private:
    PacketParseResult OnInventoryOperationResult(auth::IncomingPacket& packet);
```

**File:** `src/world_server/realm_connector.cpp`

In `RealmConnector` constructor, register handler:

```cpp
RegisterPacketHandler(auth::realm_world_packet::InventoryOperationResult, 
                     *this, &RealmConnector::OnInventoryOperationResult);
```

Implement handler to:
1. Parse operation result
2. Notify `WorldServerInventoryRepository` (via callback or signal)
3. Log success/failure

---

### Phase D: Realm Server - Packet Handlers

**File:** `src/realm_server/world.h`

Add handler declarations:

```cpp
private:
    PacketParseResult OnSaveInventoryItems(auth::IncomingPacket& packet);
    PacketParseResult OnDeleteInventoryItems(auth::IncomingPacket& packet);
    // Note: No OnLoadInventoryItems - inventory sent with PlayerCharacterJoin
```

**File:** `src/realm_server/world.cpp`

In `World` constructor (around line 287), register handlers:

```cpp
strongThis->RegisterPacketHandler(auth::world_realm_packet::SaveInventoryItems, 
                                 *strongThis, &World::OnSaveInventoryItems);
strongThis->RegisterPacketHandler(auth::world_realm_packet::DeleteInventoryItems, 
                                 *strongThis, &World::OnDeleteInventoryItems);
```

Implement handlers following existing pattern (see `OnCharacterData` at line 640).

---

### Phase E: Realm Server - Database Operations

**File:** `src/realm_server/database/mysql_database.h` (or equivalent)

Add/extend methods:

```cpp
class IDatabase
{
public:
    // ... existing methods ...
    
    /// Save multiple inventory items in single transaction
    virtual bool SaveInventoryItems(uint64 characterGuid, 
                                   const std::vector<ItemData>& items) = 0;
    
    /// Delete specific inventory items
    virtual bool DeleteInventoryItems(uint64 characterGuid, 
                                     const std::vector<uint16>& slots) = 0;
    
    // Note: LoadInventoryItems not needed - inventory loaded with character data
    // via existing CharacterEnterWorld method which already returns CharacterData.items
};
```

Implement using existing `asyncRequest` pattern (see line 700 for reference).

---

### Phase F: Connect WorldServerInventoryRepository

**File:** `src/shared/game_server/world_server_inventory_repository.cpp`

Complete the TODO stubs:

```cpp
void WorldServerInventoryRepository::Commit()
{
    if (!m_inTransaction)
    {
        return;
    }
    
    if (m_pendingOperations.empty())
    {
        m_inTransaction = false;
        return;
    }
    
    // Convert PendingOperation to ItemData
    std::vector<ItemData> itemsToSave;
    std::vector<uint16> slotsToDelete;
    
    for (const auto& op : m_pendingOperations)
    {
        if (op.type == OperationType::Save)
        {
            ItemData data;
            data.entry = op.data.entry;
            data.slot = op.data.slot;
            data.stackCount = op.data.stackCount;
            data.creator = op.data.creator;
            data.contained = op.data.contained;
            data.durability = op.data.durability;
            // ... copy other fields ...
            itemsToSave.push_back(data);
        }
        else if (op.type == OperationType::Delete)
        {
            slotsToDelete.push_back(op.data.slot);
        }
    }
    
    // Get RealmConnector from context
    // Send batched packets
    if (!itemsToSave.empty())
    {
        realmConnector.SendSaveInventoryItems(characterId, operationId, itemsToSave);
    }
    if (!slotsToDelete.empty())
    {
        realmConnector.SendDeleteInventoryItems(characterId, operationId, slotsToDelete);
    }
    
    m_pendingOperations.clear();
    m_inTransaction = false;
}
```

---

## Transaction Semantics

### World Server Side

1. **BeginTransaction()**: Start buffering operations locally
2. **SaveItem/DeleteItem**: Add to pending operations buffer
3. **Commit()**: Send all pending operations in batched packets
4. **Rollback()**: Discard pending operations buffer

### Realm Server Side

1. Receive packet with multiple operations
2. Begin database transaction
3. Execute all SQL statements
4. Commit database transaction (or rollback on error)
5. Send single result packet back to World Server

### Guarantees

- **Atomicity**: All items saved/deleted together or none
- **Consistency**: Database constraints enforced by Realm Server
- **Network Efficiency**: Batching reduces round-trips (1 request + 1 response per transaction)
- **Async Operation**: World Server doesn't block waiting for database

---

## Error Handling

### Network Errors

- **Connection Lost**: World Server maintains pending operations until reconnection
- **Timeout**: Operation IDs allow tracking and retry logic
- **Packet Corruption**: Existing auth_protocol handles checksum validation

### Database Errors

- **Constraint Violation**: Realm Server logs error, sends failure result
- **Deadlock**: Realm Server retries transaction (database layer)
- **Disk Full**: Realm Server sends failure result, World Server can retry

### Application Errors

- **Invalid Character GUID**: Realm Server rejects, sends failure result
- **Invalid Slot Index**: Realm Server validates, rejects if invalid
- **Duplicate Save**: Realm Server uses UPSERT/REPLACE to handle

---

## Performance Considerations

### Batching Strategy

**Current Approach:**
- Buffer operations during transaction
- Send single batch on commit
- Typical batch size: 5-30 items (full inventory save)

**Benefits:**
- Reduces network overhead (1 packet vs N packets)
- Enables database transaction for consistency
- Matches existing character data save pattern

### Load on Login

**Scenario:** Character login requires loading inventory

**Flow:**
1. **Realm Server** receives player enter world request from client
2. **Realm Server** calls `IDatabase::CharacterEnterWorld(characterId, accountId)`
3. **Database** loads character data INCLUDING inventory (`CharacterData.items`)
4. **Realm Server** sends `PlayerCharacterJoin` packet with complete `CharacterData`
5. **World Server** receives packet and deserializes `CharacterData`
6. **World Server** constructs `GamePlayerS` with inventory via `ConstructFromRealmData()`
7. **World Server** applies equipment stats BEFORE player spawns
8. **Player spawns** with correct HP, armor, and visual appearance

**Critical Timing:**
- Inventory MUST be loaded before player spawn
- Equipped items affect player stats (HP, armor, damage, etc.)
- Player must spawn with equipment already applied
- No separate load request needed - all data comes atomically

**Existing Implementation:**
```cpp
// src/shared/game_server/character_data.h (line 68)
struct CharacterData
{
    // ... other fields ...
    std::vector<ItemData> items;  // Inventory included here
};

// Already serialized in PlayerCharacterJoin packet (line 189)
writer << io::write_dynamic_range<uint16>(data.items);

// Already deserialized on World Server (line 112)
reader >> io::read_container<uint16>(data.items);

// Already reconstructed in Inventory class (line 1309)
void Inventory::ConstructFromRealmData(std::vector<GameObjectS*>& out_items)
{
    // Iterates through m_realmData (populated from CharacterData.items)
    // Creates item instances, applies equipment stats, etc.
}
```

**Optimization:**
- Database index on `character_id` column (already standard)
- Single query loads all character data including inventory
- No additional network round-trip needed
- Inventory cached with character data on Realm Server

### Auto-Save Strategy

**Recommendation:** Use existing periodic save (already implemented for character data)

**File:** `src/world_server/realm_connector.cpp` line 364 (`SendCharacterData`)

**Integration:**
```cpp
void RealmConnector::SendCharacterData(const GamePlayerS& character, ...)
{
    // ... existing code ...
    
    // ALSO send inventory data using new protocol
    const auto& inventory = character.GetInventory();
    const auto items = ConvertInventoryToItemData(inventory);
    SendSaveInventoryItems(character.GetGuid(), GenerateOperationId(), items);
}
```

---

## Migration Path

### Phase 1: Protocol Implementation
- Add opcodes to `auth_protocol.h`
- Implement packet sending (World Server)
- Implement packet handlers (Realm Server)
- Implement database operations (Realm Server)

### Phase 2: Parallel Run
- Keep existing `GetItemData()` / `UpdateCharacter()` path
- ALSO send new inventory packets
- Compare results for consistency testing

### Phase 3: Repository Integration
- Refactor `Inventory` class to use `WorldServerInventoryRepository`
- Remove old serialization operators
- Remove `m_realmData` member

### Phase 4: Cleanup
- Remove old inventory persistence from `IDatabase::UpdateCharacter`
- Simplify character save packet
- Remove obsolete code paths

---

## Testing Strategy

### Unit Tests

**World Server:**
- Test packet serialization (SaveInventoryItems, LoadInventoryItems, DeleteInventoryItems)
- Test operation buffering in `WorldServerInventoryRepository`
- Test transaction rollback

**Realm Server:**
- Test packet deserialization
- Test database operations (mock database)
- Test transaction handling

### Integration Tests

**Scenario 1: Save and Load**
1. Create character with inventory
2. Send SaveInventoryItems
3. Verify database contains items
4. Send LoadInventoryItems
5. Verify received data matches

**Scenario 2: Transaction Rollback**
1. Begin transaction
2. Add items to buffer
3. Trigger rollback
4. Verify no packets sent
5. Verify database unchanged

**Scenario 3: Network Failure**
1. Begin transaction
2. Add items
3. Disconnect during commit
4. Verify buffer retained
5. Reconnect and retry

### Load Testing

- 100 concurrent players saving inventories
- Measure: Network bandwidth, database query time, packet processing time
- Target: < 100ms round-trip for inventory save

---

## Backwards Compatibility

### Existing Code Paths

The new protocol does NOT break existing functionality:

- `ItemData` structure unchanged (reused from existing code)
- Existing serialization operators remain functional
- `GetItemData()` and `ConstructFromRealmData()` continue to work
- Migration can happen incrementally

### Version Negotiation

The existing `auth_protocol.h` defines `ProtocolVersion`:

```cpp
constexpr uint32 ProtocolVersion = 0x00000002;
```

**Recommendation:** Increment to `0x00000003` when inventory packets are added.

World Servers and Realm Servers can check version during handshake to ensure compatibility.

---

## Summary

This protocol specification extends the existing `auth_protocol` infrastructure with inventory-specific operations. It follows established patterns for packet structure, handler registration, and database operations. The design prioritizes:

1. **Network Efficiency**: Batching reduces round-trips
2. **Transaction Semantics**: Atomic operations across multiple items
3. **Backwards Compatibility**: Existing code continues to work
4. **Incremental Migration**: Can roll out in phases

Next steps: Implement Phase A (protocol definition), then Phases B-F (packet handlers and database operations).
