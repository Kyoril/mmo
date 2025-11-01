# Phase 5: Integration Plan - Distributed Architecture

## Overview
Integrate the Repository Pattern into the existing `Inventory` class while accounting for the distributed World Server / Realm Server architecture.

## Architecture Summary

```
┌──────────────┐
│    Client    │
└──────┬───────┘
       │ Game Protocol
       ▼
┌──────────────────────────────────────────┐
│           Realm Server                   │
│  ┌────────────────────────────────────┐  │
│  │  RealmServerInventoryRepository    │  │
│  │  (Direct DB Access)                │  │
│  └──────────┬─────────────────────────┘  │
│             │                             │
│             ▼                             │
│      ┌─────────────┐                     │
│      │  Database   │                     │
│      └─────────────┘                     │
└────────┬──────────────────────────────────┘
         │ Internal Protocol (Packets)
         ▼
┌──────────────────────────────────────────┐
│           World Server                   │
│  ┌────────────────────────────────────┐  │
│  │  WorldServerInventoryRepository    │  │
│  │  (Network Proxy)                   │  │
│  └──────────┬─────────────────────────┘  │
│             │                             │
│             ▼                             │
│      ┌─────────────┐                     │
│      │  Inventory  │                     │
│      │  (Domain)   │                     │
│      └─────────────┘                     │
└──────────────────────────────────────────┘
```

## Current State Analysis

### Existing Inventory Class
**Location:** `f:\mmo\src\shared\game_server\inventory.h/cpp`

**Current Persistence:**
```cpp
class Inventory
{
private:
    // In-memory state (World Server)
    std::map<uint16, std::shared_ptr<GameItemS>> m_itemsBySlot;
    
    // Serialization for Realm Server (mixed concern)
    std::vector<ItemData> m_realmData;
    
    // Owner reference
    GamePlayerS& m_owner;
};

// Serialization operators (used for network transfer)
io::Writer& operator<<(io::Writer& w, Inventory const& object);
io::Reader& operator>>(io::Reader& r, Inventory& object);
```

**Key Methods:**
- `CreateItems()` - Add new items
- `RemoveItem()` - Delete items
- `SwapItems()` - Move items between slots
- `ConstructFromRealmData()` - Hydrate from serialized data
- `GetItemData()` - Get serialized data for realm server

## Phase 5 Implementation Steps

### Step 1: Add Repository Dependency ✅ (Already Designed)
**Files Created:**
- `world_server_inventory_repository.h` ✅
- `world_server_inventory_repository.cpp` ✅

**Benefits:**
- Inventory no longer handles serialization
- Clear separation: Domain (Inventory) vs Persistence (Repository)
- Repository handles network communication with Realm Server

### Step 2: Refactor Inventory Constructor
**Current:**
```cpp
Inventory::Inventory(GamePlayerS& owner)
    : m_owner(owner)
    , m_freeSlots(/*...*/)
{
}
```

**Refactored:**
```cpp
Inventory::Inventory(
    GamePlayerS& owner,
    IInventoryRepository& repository,
    uint64 characterId)
    : m_owner(owner)
    , m_repository(repository)
    , m_characterId(characterId)
    , m_freeSlots(/*...*/)
{
}
```

### Step 3: Remove Serialization from Inventory
**Remove:**
- `std::vector<ItemData> m_realmData` member
- `operator<<` and `operator>>` friends
- `GetItemData()` method (if only used for serialization)
- `ConstructFromRealmData()` method

**Replace With:**
- `LoadFromRepository()` - Load items on character login
- `SaveToRepository()` - Save items (called periodically or on events)

### Step 4: Implement Load/Save Methods

```cpp
class Inventory
{
public:
    /**
     * @brief Loads inventory from repository.
     * Called when character logs in.
     */
    void LoadFromRepository();

    /**
     * @brief Saves current inventory state to repository.
     * Called on logout, periodically, or after critical operations.
     */
    bool SaveToRepository();

private:
    IInventoryRepository& m_repository;
    uint64 m_characterId;
};
```

**Implementation:**
```cpp
void Inventory::LoadFromRepository()
{
    // Load from realm server (via repository)
    auto items = m_repository.LoadItems(m_characterId);
    
    // Convert InventoryItemData -> GameItemS instances
    for (const auto& itemData : items)
    {
        auto item = CreateItemFromData(itemData);
        m_itemsBySlot[itemData.slot] = item;
        
        // Update counters, apply stats, etc.
        // ... existing logic from ConstructFromRealmData
    }
}

bool Inventory::SaveToRepository()
{
    InventoryTransaction tx(m_repository);
    
    // Convert GameItemS instances -> InventoryItemData
    std::vector<InventoryItemData> items;
    for (const auto& [slot, item] : m_itemsBySlot)
    {
        InventoryItemData data;
        data.entry = item->GetEntry().id();
        data.slot = slot;
        data.stackCount = item->GetStackCount();
        data.creator = item->Get<uint64>(object_fields::Creator);
        data.contained = item->Get<uint64>(object_fields::Contained);
        data.durability = item->Get<uint32>(object_fields::Durability);
        // ... populate other fields
        
        items.push_back(data);
    }
    
    // Save all items
    m_repository.SaveAllItems(m_characterId, items);
    
    // Commit transaction
    return tx.Commit();
}
```

### Step 5: Update Item Operations
Use commands for operations, trigger saves as needed:

```cpp
InventoryChangeFailure Inventory::CreateItems(
    const proto::ItemEntry& entry,
    uint16 amount,
    std::map<uint16, uint16>* out_addedBySlot)
{
    // Existing domain logic (validation, slot finding, etc.)
    // ...
    
    // After successful creation, consider saving
    // Options:
    // 1. Immediate save (high latency)
    // 2. Deferred save (on timer/logout)
    // 3. Dirty tracking (mark for next sync)
    
    // Mark inventory as dirty for periodic sync
    m_isDirty = true;
    
    return inventory_change_failure::Okay;
}
```

### Step 6: Periodic Save Strategy
Implement auto-save timer:

```cpp
class GamePlayerS
{
public:
    void InitializePeriodicInventorySave()
    {
        // Auto-save every 5 minutes
        m_inventorySaveTimer = m_timers.Add(std::chrono::minutes(5),
            [this]()
            {
                if (m_inventory.IsDirty())
                {
                    m_inventory.SaveToRepository();
                }
            });
    }

private:
    scoped_connection m_inventorySaveTimer;
};
```

## Sync Strategy

### When to Save to Realm Server

1. **Character Login**
   - Load from repository (one-time)
   - World Server becomes authoritative

2. **Character Logout**
   - Full save to repository
   - Ensure all changes persisted

3. **Periodic Auto-Save**
   - Every N minutes (configurable)
   - Only if dirty

4. **Critical Operations**
   - Before trade completion
   - Before mail send
   - Before auction house listing
   - Before character transfer

5. **Server Shutdown**
   - Graceful save all players
   - Prevent data loss

### Dirty Tracking
```cpp
class Inventory
{
public:
    void MarkDirty() noexcept { m_isDirty = true; }
    bool IsDirty() const noexcept { return m_isDirty; }
    void ClearDirty() noexcept { m_isDirty = false; }

private:
    bool m_isDirty = false;
};
```

## Realm Server Side (Future Work)

### Realm Server Packet Handlers
**To Implement:**

```cpp
// Realm Server: Handle save request from World Server
void RealmServer::OnWorldInventorySavePacket(Packet& packet)
{
    uint64 characterId;
    uint16 itemCount;
    packet >> characterId >> itemCount;
    
    std::vector<InventoryItemData> items;
    for (uint16 i = 0; i < itemCount; ++i)
    {
        InventoryItemData item;
        packet >> item;  // Deserialize
        items.push_back(item);
    }
    
    // Use database repository
    RealmServerInventoryRepository repo(m_database, characterId);
    
    InventoryTransaction tx(repo);
    bool success = repo.SaveAllItems(characterId, items);
    
    if (success)
    {
        tx.Commit();
    }
    
    // Send response to World Server
    SendInventorySaveResponse(characterId, success);
}

// Realm Server: Handle load request from World Server
void RealmServer::OnWorldInventoryLoadPacket(Packet& packet)
{
    uint64 characterId;
    packet >> characterId;
    
    // Load from database
    RealmServerInventoryRepository repo(m_database, characterId);
    auto items = repo.LoadItems(characterId);
    
    // Send items to World Server
    SendInventoryLoadResponse(characterId, items);
}
```

## Migration Path

### Phase 5.1: Add Repository Without Breaking Current Code
- Add `IInventoryRepository*` as optional parameter (default nullptr)
- If repository provided, use it; otherwise use old serialization
- Allows gradual migration

```cpp
Inventory::Inventory(
    GamePlayerS& owner,
    IInventoryRepository* repository = nullptr,  // Optional
    uint64 characterId = 0)
    : m_owner(owner)
    , m_repository(repository)
    , m_characterId(characterId)
{
}
```

### Phase 5.2: Implement Load/Save with Repository
- `LoadFromRepository()` replaces `ConstructFromRealmData()`
- `SaveToRepository()` replaces serialization operators
- Keep old code paths for compatibility

### Phase 5.3: Update Character Creation/Login
- Pass repository to Inventory constructor
- Call `LoadFromRepository()` on login
- Call `SaveToRepository()` on logout

### Phase 5.4: Remove Old Serialization
- Once all code uses repository
- Remove `m_realmData`
- Remove serialization operators
- Remove `ConstructFromRealmData()`

## Testing Strategy

### Unit Tests
```cpp
TEST_CASE("Inventory loads from repository")
{
    InMemoryInventoryRepository repo;
    
    // Populate repository with test data
    InventoryItemData item{};
    item.entry = 100;
    item.slot = 0x1700;
    item.stackCount = 5;
    repo.SaveItem(123, item);
    
    // Create inventory
    GamePlayerS player(/*...*/);
    Inventory inv(player, repo, 123);
    
    // Load from repository
    inv.LoadFromRepository();
    
    // Verify item was loaded
    auto loadedItem = inv.GetItemAtSlot(0x1700);
    REQUIRE(loadedItem != nullptr);
    REQUIRE(loadedItem->GetStackCount() == 5);
}

TEST_CASE("Inventory saves to repository")
{
    InMemoryInventoryRepository repo;
    GamePlayerS player(/*...*/);
    Inventory inv(player, repo, 123);
    
    // Add item to inventory
    inv.CreateItems(itemEntry, 10);
    
    // Save to repository
    REQUIRE(inv.SaveToRepository());
    
    // Verify repository has items
    auto items = repo.LoadItems(123);
    REQUIRE(!items.empty());
}
```

### Integration Tests
```cpp
TEST_CASE("World Server Repository buffers during transaction")
{
    MockRealmConnector connector;
    WorldServerInventoryRepository repo(connector, 123);
    
    repo.BeginTransaction();
    
    InventoryItemData item{};
    item.entry = 100;
    item.slot = 0x1700;
    
    repo.SaveItem(123, item);
    
    // Should be buffered, not sent yet
    REQUIRE(repo.HasPendingOperations());
    REQUIRE(connector.GetSentPacketCount() == 0);
    
    // Commit sends packets
    repo.Commit();
    REQUIRE(connector.GetSentPacketCount() == 1);
}
```

## Performance Considerations

### Network Overhead
- **Batch Operations:** Buffer multiple changes, send once
- **Compression:** Consider compressing large inventory packets
- **Delta Sync:** Only send changed items (dirty tracking)

### Latency
- **Async Operations:** Don't block gameplay waiting for save confirmation
- **Fire and Forget:** Send save packets, assume success
- **Eventual Consistency:** Accept small window of inconsistency

### Reliability
- **Retry Logic:** Retry failed packets with exponential backoff
- **Persistence Queue:** Queue operations if realm server unavailable
- **Graceful Degradation:** Continue gameplay even if saves temporarily fail

## Next Implementation Steps

1. ✅ Create `WorldServerInventoryRepository` (DONE)
2. ⏭️ Add repository parameter to `Inventory` constructor (optional)
3. ⏭️ Implement `LoadFromRepository()` method
4. ⏭️ Implement `SaveToRepository()` method
5. ⏭️ Add dirty tracking to inventory
6. ⏭️ Implement periodic auto-save
7. ⏭️ Update character login/logout to use repository
8. ⏭️ Implement realm server packet handlers (separate PR)
9. ⏭️ Remove old serialization code
10. ⏭️ Integration testing

---

**Status:** Ready for Step 2 - Refactor Inventory constructor
**Next:** Add repository as optional parameter to maintain compatibility