# Inventory Strong Types - Usage Guide

## Overview

This document describes the strong types introduced to replace primitive obsession in the inventory system. These types provide type safety, better expressiveness, and encapsulate domain logic.

## Background

**Problem**: The original inventory code suffered from primitive obsession, using `uint16` for slots everywhere, leading to:
- Type confusion (absolute vs relative slots)
- Error-prone slot calculations
- Poor self-documentation
- Difficult testing

**Solution**: Introduce strong types that encapsulate domain concepts and provide type safety.

## Types Reference

### 1. `InventorySlot`

Encapsulates slot addressing logic with automatic conversion between absolute and relative coordinates.

#### Construction

```cpp
// From absolute slot value (bag << 8 | slot)
auto slot = InventorySlot::FromAbsolute(0xFF10);

// From relative coordinates (bag, slot)
auto slot = InventorySlot::FromRelative(255, 16);

// Common slots
auto headSlot = InventorySlot::FromRelative(
    player_inventory_slots::Bag_0, 
    player_equipment_slots::Head
);
```

#### Accessors

```cpp
uint16 absolute = slot.GetAbsolute();
uint8 bag = slot.GetBag();
uint8 slotIndex = slot.GetSlot();
```

#### Type Queries

```cpp
if (slot.IsEquipment())
{
    // Handle equipment slot (slots 0-18 of bag 0)
}

if (slot.IsInventory())
{
    // Handle backpack inventory (slots 23-38 of bag 0)
}

if (slot.IsBagPack())
{
    // Handle bag container slots (slots 19-22 of bag 0)
}

if (slot.IsBag())
{
    // Handle slots inside equipped bags (bags 19-22)
}

if (slot.IsBagBar())
{
    // Handle bag bar slots
}

if (slot.IsBuyBack())
{
    // Handle buyback slots (74-85)
}
```

#### Usage in Containers

```cpp
// Works with std::map (has operator<)
std::map<InventorySlot, ItemInstance> items;

// Works with std::unordered_map (has std::hash specialization)
std::unordered_map<InventorySlot, ItemInstance> itemsHash;

// Works with std::set
std::set<InventorySlot> occupiedSlots;
```

### 2. `ItemStack`

Represents a stack of items with validation and safe operations.

#### Construction and Basic Usage

```cpp
ItemStack stack(10);  // Stack of 10 items

uint16 count = stack.GetCount();
bool canAdd = stack.CanAddStacks(20);  // Check if can add to max of 20
uint16 space = stack.GetAvailableSpace(20);  // Get remaining capacity
```

#### Mutations

```cpp
ItemStack stack(15);

// Add items (returns number actually added)
uint16 added = stack.Add(7, 20);  // Try to add 7, max stack 20
// added = 5 (because 15 + 5 = 20)
// stack now contains 20

// Remove items (returns number actually removed)
uint16 removed = stack.Remove(5);
// removed = 5
// stack now contains 15
```

#### State Checks

```cpp
if (stack.IsEmpty())
{
    // Stack has 0 items
}

if (stack.IsFull(20))
{
    // Stack is at maximum (20 items)
}
```

### 3. `ItemCount`

Simple wrapper for tracking item counts with safe arithmetic.

```cpp
ItemCount count(10);

count.Add(5);      // Now 15
count.Subtract(3); // Now 12

if (count.IsZero())
{
    // No items
}

// Implicit conversion to uint16
uint16 value = count;  // value = 12
```

### 4. `SlotAvailability`

Tracks available space for item placement operations.

```cpp
SlotAvailability availability;
availability.emptySlots = 5;
availability.partialStacks = 2;
availability.availableStackSpace = 45;

if (availability.HasSpace())
{
    // Some space available
}

if (availability.CanAccommodate(40))
{
    // Can place 40 items
}
```

### 5. `InventoryResult<T>`

Type-safe result type for operations that may fail.

#### Void Results

```cpp
InventoryResult<void> AddItem(const ItemEntry& entry)
{
    if (IsFull())
    {
        return InventoryResult<void>::Failure(inventory_change_failure::InventoryFull);
    }
    
    // ... perform operation ...
    
    return InventoryResult<void>::Success();
}

// Usage
auto result = AddItem(entry);
if (result.IsSuccess())
{
    // Success
}
else
{
    InventoryChangeFailure error = result.GetError();
    // Handle error
}
```

#### Results with Values

```cpp
InventoryResult<InventorySlot> FindEmptySlot()
{
    auto slot = /* ... find slot ... */;
    
    if (slot.has_value())
    {
        return InventoryResult<InventorySlot>::Success(*slot);
    }
    
    return InventoryResult<InventorySlot>::Failure(inventory_change_failure::InventoryFull);
}

// Usage
auto result = FindEmptySlot();
if (result.IsSuccess())
{
    InventorySlot slot = *result.GetValue();
    // Use slot
}
```

#### Callback Chains

```cpp
FindEmptySlot()
    .OnSuccess([](InventorySlot slot)
    {
        // Handle success
        PlaceItem(slot);
    })
    .OnFailure([](InventoryChangeFailure error)
    {
        // Handle failure
        NotifyPlayer(error);
    });
```

## Migration Guide

### Before (Primitive Obsession)

```cpp
// Hard to understand what this means
uint16 slot = 0xFF10;

// Easy to mix up parameters
void MoveItem(uint16 from, uint16 to);

// No type safety
if (slot >> 8 == 255 && (slot & 0xFF) < 19)
{
    // What does this mean?
}

// Error-prone
InventoryChangeFailure result = AddItem(entry);
if (result == inventory_change_failure::Okay)
{
    // Forgot to handle all error cases
}
```

### After (Strong Types)

```cpp
// Clear and self-documenting
auto slot = InventorySlot::FromRelative(
    player_inventory_slots::Bag_0, 
    player_equipment_slots::Head
);

// Type-safe parameters
void MoveItem(InventorySlot from, InventorySlot to);

// Expressive and readable
if (slot.IsEquipment())
{
    // Clear intent
}

// Better error handling
InventoryResult<void> result = AddItem(entry);
result
    .OnSuccess([]() { /* Success path */ })
    .OnFailure([](auto error) { /* Handle error */ });
```

## Benefits

1. **Type Safety**: Compiler prevents mixing absolute and relative coordinates
2. **Self-Documentation**: Code reads like domain language
3. **Encapsulation**: Slot logic centralized in one place
4. **Testability**: Each type can be unit tested independently
5. **Maintainability**: Changes to slot layout only affect one place
6. **Error Handling**: Explicit success/failure with InventoryResult

## Next Steps

Phase 2 of the refactoring will extract domain services:
- `ItemValidator` - Validates item constraints
- `SlotManager` - Manages slot allocation
- `ItemFactory` - Creates item instances
- `EquipmentManager` - Handles equipment effects
- `BagManager` - Manages bag-specific logic

## Testing

All strong types have comprehensive unit tests in `inventory_types_test.cpp`. Run tests with:

```bash
ctest -R inventory_types
```

## References

- C++ Core Guidelines: [C.4 Make a function a member only if it needs direct access to internals](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c4-make-a-function-a-member-only-if-it-needs-direct-access-to-the-representation-of-a-class)
- C++ Core Guidelines: [C.120 Use class hierarchies to represent concepts with inherent hierarchical structure only](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only)
- Domain-Driven Design: Value Objects
- Effective C++: Item 18 - Make interfaces easy to use correctly and hard to use incorrectly
