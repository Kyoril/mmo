# Distributed Architecture Consideration for Repository Pattern

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Client                                │
└───────────────────────────┬─────────────────────────────────┘
                            │ Game Protocol
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                     Realm Server                             │
│  - Authentication                                            │
│  - Character selection                                       │
│  - Database access (MySQL/PostgreSQL)                       │
│  - Data persistence                                          │
│  - Manages multiple World Servers                           │
└───────────────────────────┬─────────────────────────────────┘
                            │ Internal Protocol
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                     World Server                             │
│  - Live gameplay                                             │
│  - In-memory inventory (current state)                      │
│  - Business logic                                            │
│  - Commands & validation                                     │
│  - NO direct database access                                │
└─────────────────────────────────────────────────────────────┘
```

## Repository Implementation Strategy

### World Server Side
The repository implementation on the **World Server** will be a **network proxy** that:
1. Serializes repository operations into packets
2. Sends packets to the Realm Server
3. Waits for response (async/sync)
4. Deserializes response
5. Returns result to caller

### Realm Server Side
The repository implementation on the **Realm Server** will be:
1. Direct database access (MySQL/PostgreSQL)
2. Receives packets from World Server
3. Executes database operations
4. Sends response back to World Server

## Network Repository Implementation

### WorldServerInventoryRepository
```cpp
class WorldServerInventoryRepository : public IInventoryRepository
{
public:
    WorldServerInventoryRepository(
        RealmConnector& realmConnector,
        uint64 characterId);

    // IInventoryRepository implementation
    std::vector<InventoryItemData> LoadItems(uint64 characterId) override;
    bool SaveItem(uint64 characterId, const InventoryItemData& item) override;
    bool SaveAllItems(uint64 characterId, const std::vector<InventoryItemData>& items) override;
    bool DeleteItem(uint64 characterId, uint16 slot) override;
    bool DeleteAllItems(uint64 characterId) override;
    
    // Transactions are problematic in distributed systems
    bool BeginTransaction() override;  // May need to be deferred
    bool Commit() override;            // Sends commit packet to realm
    bool Rollback() override;          // Sends rollback packet to realm

private:
    RealmConnector& m_realmConnector;
    uint64 m_characterId;
    bool m_inTransaction;
};
```

### RealmServerInventoryRepository
```cpp
class RealmServerInventoryRepository : public IInventoryRepository
{
public:
    RealmServerInventoryRepository(
        DatabaseConnection& database,
        uint64 characterId);

    // Direct database access
    std::vector<InventoryItemData> LoadItems(uint64 characterId) override;
    bool SaveItem(uint64 characterId, const InventoryItemData& item) override;
    // ... etc.

private:
    DatabaseConnection& m_database;
    uint64 m_characterId;
};
```

## Transaction Challenges in Distributed Systems

### Problem
Transactions across network boundaries are complex:
- Network latency
- Connection failures
- Partial failures
- Two-phase commit complexity

### Solutions

#### Option 1: Deferred Transactions (Recommended)
Buffer operations on World Server and send batch to Realm Server:

```cpp
class WorldServerInventoryRepository : public IInventoryRepository
{
public:
    bool BeginTransaction() override
    {
        m_inTransaction = true;
        m_pendingOperations.clear();
        return true;
    }

    bool SaveItem(uint64 characterId, const InventoryItemData& item) override
    {
        if (m_inTransaction)
        {
            // Buffer operation
            m_pendingOperations.push_back({OperationType::Save, item});
            return true;
        }
        else
        {
            // Send immediately
            return SendSaveItemPacket(characterId, item);
        }
    }

    bool Commit() override
    {
        if (!m_inTransaction)
        {
            return false;
        }

        // Send all buffered operations in one packet
        bool success = SendBatchOperationsPacket(m_characterId, m_pendingOperations);
        
        m_inTransaction = false;
        m_pendingOperations.clear();
        
        return success;
    }

private:
    bool m_inTransaction;
    std::vector<PendingOperation> m_pendingOperations;
};
```

#### Option 2: Eventual Consistency
Accept that operations may be asynchronous:

```cpp
class WorldServerInventoryRepository : public IInventoryRepository
{
public:
    bool SaveItem(uint64 characterId, const InventoryItemData& item) override
    {
        // Send packet asynchronously
        m_realmConnector.SendAsync(SaveItemPacket(characterId, item));
        
        // Assume success (eventual consistency)
        return true;
    }

    // Register callback for confirmation
    void OnSaveConfirmation(uint64 characterId, uint16 slot, bool success)
    {
        if (!success)
        {
            // Handle failure (retry, notify, etc.)
        }
    }
};
```

#### Option 3: Write-Back Cache
Keep authoritative state on World Server, periodically sync to Realm:

```cpp
class CachedInventoryRepository : public IInventoryRepository
{
public:
    bool SaveItem(uint64 characterId, const InventoryItemData& item) override
    {
        // Save to local cache immediately
        m_cache.SaveItem(characterId, item);
        
        // Mark dirty for sync
        m_dirtyItems.insert(item.slot);
        
        return true;
    }

    void PeriodicSync()
    {
        if (!m_dirtyItems.empty())
        {
            // Send dirty items to realm server
            std::vector<InventoryItemData> dirtyData;
            for (uint16 slot : m_dirtyItems)
            {
                auto item = m_cache.GetItem(characterId, slot);
                dirtyData.push_back(item);
            }
            
            SendBatchSavePacket(characterId, dirtyData);
            m_dirtyItems.clear();
        }
    }
};
```

## Packet Protocol Design

### Save Item Packet (World -> Realm)
```cpp
struct SaveInventoryItemPacket
{
    uint64 characterId;
    InventoryItemData itemData;
};
```

### Save Response Packet (Realm -> World)
```cpp
struct SaveInventoryItemResponse
{
    uint64 characterId;
    uint16 slot;
    bool success;
    uint8 errorCode;  // If failed
};
```

### Batch Save Packet (World -> Realm)
```cpp
struct BatchSaveInventoryPacket
{
    uint64 characterId;
    uint16 itemCount;
    std::vector<InventoryItemData> items;
};
```

### Batch Response (Realm -> World)
```cpp
struct BatchSaveInventoryResponse
{
    uint64 characterId;
    bool allSuccess;
    std::vector<std::pair<uint16, bool>> results;  // slot -> success
};
```

## Phase 5 Approach

Given this architecture, Phase 5 should focus on:

1. **Abstract the Network Layer**
   - Create `IRealmConnector` interface for network operations
   - World Server sends repository operations as packets
   - Realm Server handles packet routing to database

2. **Implement WorldServerInventoryRepository**
   - Uses `RealmConnector` to send packets
   - Handles async responses
   - Buffers operations for transactions

3. **Update Existing Inventory Class**
   - Accept `IInventoryRepository&` in constructor
   - Use repository for all persistence
   - Remove direct serialization (`m_realmData`)

4. **Deferred Persistence Strategy**
   - World Server maintains authoritative in-memory state
   - Periodic/on-demand sync to Realm Server
   - Realm Server persists to database
   - Handle disconnections gracefully

## Sync Points

When should World Server sync inventory to Realm Server?

1. **On Character Logout** - Full save
2. **On Item Changes** - Incremental updates
3. **Periodic Autosave** - Every N minutes
4. **On Critical Operations** - Trade, mail, etc.
5. **On Server Shutdown** - Graceful shutdown

## Error Handling

### Network Failures
- Retry logic with exponential backoff
- Queue operations for later
- Notify player of "saving" status

### Realm Server Unavailable
- Cache operations locally
- Attempt reconnection
- Prevent character logout until saved

### Partial Failures
- Track which operations succeeded
- Retry failed operations
- Maintain consistency

## Implementation Recommendation

For **Phase 5**, implement:

1. **WorldServerInventoryRepository** with deferred transactions
2. **Mock/Fake RealmConnector** for testing
3. **Integration into existing Inventory class**
4. **Periodic sync mechanism**

This allows:
- Testing without realm server
- Gradual migration
- Maintains separation of concerns
- Handles distributed complexity properly

---

**Next Step:** Implement `WorldServerInventoryRepository` with buffered operations and integrate into `Inventory` class.