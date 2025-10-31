# Inventory System Refactoring Plan

## Executive Summary

The current `Inventory` class is a 1200+ line "God Class" that violates Single Responsibility Principle and suffers from primitive obsession. This document outlines a phased refactoring approach following Clean Architecture, DDD principles, and C++ Core Guidelines.

## Current Problems

### 1. God Class Anti-pattern
- Single class handles: validation, creation, slot management, equipment, bags, serialization
- 40+ methods with complex interdependencies
- Difficult to test, maintain, and extend

### 2. Primitive Obsession
- `uint16` used for slots everywhere (ambiguous absolute vs relative)
- No type safety for item counts, stacks, or errors
- Magic numbers scattered throughout

### 3. Hidden Dependencies
- Anonymous namespace functions (proficiency checks)
- Tight coupling to `GamePlayerS`
- No clear interfaces or abstractions

### 4. Poor Separation of Concerns
- Business logic mixed with data access
- Notification logic embedded in operations
- Serialization concerns mixed with domain logic

### 5. Mutable State Everywhere
- Multiple caches (`m_itemCounter`, `m_freeSlots`) that can desync
- No clear ownership semantics
- Difficult to reason about state changes

## Refactoring Phases

### ✅ Phase 1: Strong Types (COMPLETED)

**Goal**: Replace primitive obsession with value objects

**Delivered**:
- `InventorySlot` - Type-safe slot addressing
- `ItemStack` - Stack count with validation
- `ItemCount` - Item counting with safe arithmetic
- `SlotAvailability` - Space tracking
- `InventoryResult<T>` - Type-safe error handling

**Benefits**:
- Type safety prevents coordinate confusion
- Self-documenting code
- Foundation for further refactoring
- Independently testable

**Files**:
- `inventory_types.h` - Header with Doxygen documentation
- `inventory_types.cpp` - Implementation
- `inventory_types_test.cpp` - Comprehensive unit tests
- `inventory_strong_types_usage.md` - Usage guide

---

### ✅ Phase 2: Extract Domain Services (IN PROGRESS)

**Goal**: Break down God Class into focused, single-responsibility services

#### 2.1 ItemValidator Service ✅ COMPLETE

**Responsibility**: All item validation logic

**Status**: Implemented and ready for integration

```cpp
class ItemValidator
{
public:
    explicit ItemValidator(const GamePlayerS& player);
    
    /// Validates if player can use this item
    InventoryResult<void> ValidateItemRequirements(
        const proto::ItemEntry& entry) const;
    
    /// Validates if item can go in this slot
    InventoryResult<void> ValidateSlot(
        InventorySlot slot, 
        const proto::ItemEntry& entry) const;
    
    /// Validates proficiency (weapon/armor)
    InventoryResult<void> ValidateProficiency(
        const proto::ItemEntry& entry) const;
    
    /// Validates level requirements
    InventoryResult<void> ValidateLevel(
        const proto::ItemEntry& entry) const;
    
private:
    const GamePlayerS& m_player;
    
    bool HasWeaponProficiency(weapon_prof::Type prof) const;
    bool HasArmorProficiency(armor_prof::Type prof) const;
};
```

**Extracted Logic**:
- Proficiency checks (currently in anonymous namespace)
- Level/skill requirements
- Slot compatibility
- Equipment restrictions (2H weapons, dual wield, etc.)

---

#### 2.2 SlotManager Service

**Responsibility**: Slot allocation and queries

```cpp
class SlotManager
{
public:
    /// Finds first empty slot in inventory
    std::optional<InventorySlot> FindEmptySlot() const;
    
    /// Finds slots suitable for item placement
    SlotAvailability FindAvailableSlots(
        const proto::ItemEntry& entry,
        uint16 amount) const;
    
    /// Gets all slots matching predicate
    std::vector<InventorySlot> FindSlots(
        std::function<bool(InventorySlot)> predicate) const;
    
    /// Iterates through all bag slots
    void ForEachBagSlot(
        std::function<void(InventorySlot)> callback) const;
    
private:
    const std::map<InventorySlot, std::shared_ptr<GameItemS>>& m_items;
    uint16 m_freeSlots;
    
    SlotAvailability CalculateAvailability(
        const proto::ItemEntry& entry,
        uint16 amount) const;
};
```

**Extracted Logic**:
- Empty slot finding
- Slot iteration (ForEachBag)
- Free slot counting
- Availability calculations

---

#### 2.3 ItemFactory Service

**Responsibility**: Item instance creation and configuration

```cpp
class ItemFactory
{
public:
    ItemFactory(
        const proto::Project& project,
        GamePlayerS& owner,
        WorldInstance& world);
    
    /// Creates a new item instance
    std::shared_ptr<GameItemS> CreateItem(
        const proto::ItemEntry& entry,
        InventorySlot targetSlot);
    
    /// Creates a bag instance
    std::shared_ptr<GameBagS> CreateBag(
        const proto::ItemEntry& entry,
        InventorySlot targetSlot);
    
private:
    const proto::Project& m_project;
    GamePlayerS& m_owner;
    WorldInstance& m_world;
    
    void ConfigureNewItem(
        GameItemS& item,
        const proto::ItemEntry& entry,
        InventorySlot slot);
    
    void ApplyBinding(
        GameItemS& item,
        const proto::ItemEntry& entry);
};
```

**Extracted Logic**:
- Item/bag instantiation
- GUID generation
- Initial property setup
- Binding application

---

#### 2.4 EquipmentManager Service

**Responsibility**: Equipment effects, visuals, and stats

```cpp
class EquipmentManager
{
public:
    explicit EquipmentManager(GamePlayerS& owner);
    
    /// Equips item in slot
    void EquipItem(
        std::shared_ptr<GameItemS> item,
        InventorySlot slot);
    
    /// Unequips item from slot
    void UnequipItem(
        std::shared_ptr<GameItemS> item,
        InventorySlot slot);
    
    /// Updates equipment visuals
    void UpdateVisuals(
        std::shared_ptr<GameItemS> item,
        InventorySlot slot);
    
    /// Applies/removes item stats
    void ApplyItemStats(
        const GameItemS& item,
        bool apply);
    
private:
    GamePlayerS& m_owner;
    
    void HandleItemSetEffects(
        const GameItemS& item,
        bool equipped);
    
    void UpdateEquipmentFields(
        const GameItemS& item,
        InventorySlot slot);
};
```

**Extracted Logic**:
- Equipment visual updates
- Stat application/removal
- Item set bonus tracking
- Equipment field updates

---

#### 2.5 BagManager Service

**Responsibility**: Bag-specific operations

```cpp
class BagManager
{
public:
    BagManager(
        GamePlayerS& owner,
        std::map<InventorySlot, std::shared_ptr<GameItemS>>& items);
    
    /// Gets bag at slot
    std::shared_ptr<GameBagS> GetBag(InventorySlot slot) const;
    
    /// Equips bag (updates free slot count)
    void EquipBag(
        std::shared_ptr<GameBagS> bag,
        InventorySlot slot);
    
    /// Unequips bag (validates empty)
    InventoryResult<void> UnequipBag(InventorySlot slot);
    
    /// Updates item reference in bag
    void UpdateBagSlot(
        std::shared_ptr<GameBagS> bag,
        InventorySlot itemSlot,
        std::shared_ptr<GameItemS> item);
    
private:
    GamePlayerS& m_owner;
    std::map<InventorySlot, std::shared_ptr<GameItemS>>& m_items;
    
    void UpdateFreeSlotCount(
        std::shared_ptr<GameBagS> bag,
        bool equipping);
};
```

**Extracted Logic**:
- Bag equipping/unequipping
- Free slot calculation
- Bag content management
- Bag field updates

---

### 🔄 Phase 3: Command Pattern

**Goal**: Encapsulate operations as commands for better testability and potential undo

```cpp
// Base command interface
class InventoryCommand
{
public:
    virtual ~InventoryCommand() = default;
    virtual InventoryResult<void> Execute() = 0;
};

// Example: Add item command
class AddItemCommand : public InventoryCommand
{
public:
    AddItemCommand(
        Inventory& inventory,
        std::shared_ptr<GameItemS> item,
        InventorySlot targetSlot);
    
    InventoryResult<void> Execute() override;
    
private:
    Inventory& m_inventory;
    std::shared_ptr<GameItemS> m_item;
    InventorySlot m_targetSlot;
};
```

**Benefits**:
- Clear operation boundaries
- Easy to test in isolation
- Can add logging, validation layers
- Future: Undo/redo capability

---

### 🔄 Phase 4: Repository Pattern

**Goal**: Separate persistence concerns

```cpp
class InventoryRepository
{
public:
    virtual ~InventoryRepository() = default;
    
    /// Saves inventory to database
    virtual void Save(const Inventory& inventory) = 0;
    
    /// Loads inventory from database
    virtual std::unique_ptr<Inventory> Load(uint64 ownerId) = 0;
    
    /// Serializes for network transfer
    virtual std::vector<ItemData> Serialize(
        const Inventory& inventory) const = 0;
    
    /// Deserializes from network data
    virtual void Deserialize(
        Inventory& inventory,
        const std::vector<ItemData>& data) = 0;
};
```

**Benefits**:
- Clear separation of concerns
- Testable without database
- Can swap implementations (DB, cache, mock)

---

### 🔄 Phase 5: Refactored Inventory (Aggregate Root)

**Goal**: Lean class focused on invariants and coordination

```cpp
class Inventory
{
public:
    Inventory(
        uint64 ownerId,
        std::unique_ptr<ItemValidator> validator,
        std::unique_ptr<SlotManager> slotManager,
        std::unique_ptr<ItemFactory> itemFactory,
        std::unique_ptr<EquipmentManager> equipmentManager,
        std::unique_ptr<BagManager> bagManager);
    
    // Core operations return strong types
    InventoryResult<void> AddItem(
        std::shared_ptr<GameItemS> item);
    
    InventoryResult<void> RemoveItem(
        InventorySlot slot,
        ItemStack stacks);
    
    InventoryResult<void> SwapItems(
        InventorySlot from,
        InventorySlot to);
    
    // Queries
    std::optional<std::shared_ptr<GameItemS>> GetItemAt(
        InventorySlot slot) const;
    
    ItemCount GetItemCount(uint32 itemId) const;
    
    bool HasFreeSlot() const;
    
    // Domain events
    signal<void(std::shared_ptr<GameItemS>, InventorySlot)> itemAdded;
    signal<void(std::shared_ptr<GameItemS>, InventorySlot)> itemRemoved;
    signal<void(std::shared_ptr<GameItemS>, InventorySlot)> itemUpdated;
    
private:
    uint64 m_ownerId;
    std::map<InventorySlot, std::shared_ptr<GameItemS>> m_items;
    std::map<uint32, ItemCount> m_itemCounts;
    
    // Injected services
    std::unique_ptr<ItemValidator> m_validator;
    std::unique_ptr<SlotManager> m_slotManager;
    std::unique_ptr<ItemFactory> m_itemFactory;
    std::unique_ptr<EquipmentManager> m_equipmentManager;
    std::unique_ptr<BagManager> m_bagManager;
    
    // Maintains invariants
    void UpdateItemCount(uint32 itemId, int16 delta);
    void ValidateInvariants() const;
};
```

**Key Changes**:
- Uses strong types everywhere
- Delegates to focused services
- Maintains only essential state
- Clear invariants
- Domain events for notifications

---

## Migration Strategy

### Approach: Strangler Fig Pattern

1. ✅ **Add strong types** (no breaking changes)
2. **Add new services alongside existing code**
3. **Gradually migrate methods to use services**
4. **Add adapter layer for backward compatibility**
5. **Migrate callers to new API**
6. **Remove old implementation**

### Example Migration

**Before**:
```cpp
InventoryChangeFailure Inventory::SwapItems(uint16 slotA, uint16 slotB)
{
    // 100+ lines of mixed concerns
}
```

**During (Adapter Pattern)**:
```cpp
// Old API delegates to new implementation
InventoryChangeFailure Inventory::SwapItems(uint16 slotA, uint16 slotB)
{
    return SwapItems(
        InventorySlot::FromAbsolute(slotA),
        InventorySlot::FromAbsolute(slotB)
    ).GetError();
}

// New API using strong types
InventoryResult<void> Inventory::SwapItems(
    InventorySlot from,
    InventorySlot to)
{
    // Clean implementation using services
}
```

**After**:
```cpp
// Only new API remains
InventoryResult<void> Inventory::SwapItems(
    InventorySlot from,
    InventorySlot to)
{
    auto validation = m_validator->ValidateSwap(from, to);
    if (validation.IsFailure())
    {
        return validation;
    }
    
    return m_swapCommand->Execute(from, to);
}
```

---

## Testing Strategy

### Unit Tests
- Each service tested independently
- Mock dependencies
- Test edge cases thoroughly

### Integration Tests
- Test service interactions
- Test with real item data
- Test state consistency

### Characterization Tests
- Capture existing behavior before changes
- Ensure no regressions
- Use as safety net

---

## Success Metrics

### Code Quality
- ✅ Reduce `Inventory` class to < 300 lines
- ✅ Each service < 200 lines
- ✅ Cyclomatic complexity < 10 per method
- ✅ 100% unit test coverage for new code

### Maintainability
- ✅ Single Responsibility: each class has one reason to change
- ✅ Type safety: no primitive obsession
- ✅ Testability: all logic unit-testable
- ✅ Expressiveness: code reads like domain language

### Performance
- ⚠️ No performance regressions
- ⚠️ Profile before/after
- ⚠️ Optimize hot paths if needed

---

## Timeline Estimate

- **Phase 1**: ✅ Complete (Strong Types)
- **Phase 2**: 2-3 weeks (Extract Services)
- **Phase 3**: 1 week (Commands)
- **Phase 4**: 1 week (Repository)
- **Phase 5**: 1 week (Final Refactoring)

**Total**: 5-6 weeks for complete refactoring

---

## Next Steps

1. ✅ Review and approve Phase 1 (Strong Types)
2. Create `ItemValidator` service
3. Create `SlotManager` service
4. Migrate validation logic
5. Add comprehensive tests
6. Continue with remaining services

---

## References

- **Clean Architecture** by Robert C. Martin
- **Domain-Driven Design** by Eric Evans
- **Working Effectively with Legacy Code** by Michael Feathers
- **C++ Core Guidelines**: https://isocpp.github.io/CppCoreGuidelines/
- **Refactoring** by Martin Fowler
