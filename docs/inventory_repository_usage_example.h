// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

/**
 * @file inventory_repository_usage_example.h
 * @brief Example usage of inventory repository and unit of work patterns.
 *
 * Demonstrates how to use the repository pattern for inventory persistence
 * and the unit of work pattern for transaction coordination.
 */

#include "inventory_repository.h"
#include "inventory_unit_of_work.h"

namespace mmo
{
    /**
     * @brief Example: Basic repository usage for loading items.
     */
    inline void ExampleLoadItems(IInventoryRepository &repository, uint64 characterId)
    {
        // Load all items for a character
        auto items = repository.LoadItems(characterId);

        // Process loaded items
        for (const auto &item : items)
        {
            // Access item data:
            // - item.entry (item template ID)
            // - item.slot (absolute slot)
            // - item.stackCount
            // - item.durability
            // etc.
        }
    }

    /**
     * @brief Example: Saving a single item.
     */
    inline void ExampleSaveItem(IInventoryRepository &repository, uint64 characterId)
    {
        InventoryItemData itemData;
        itemData.entry = 12345; // Item template ID
        itemData.slot = 0x1706; // Bag 0, Slot 6
        itemData.stackCount = 20;
        itemData.durability = 100;

        // Save the item
        bool success = repository.SaveItem(characterId, itemData);
        if (!success)
        {
            // Handle save failure
        }
    }

    /**
     * @brief Example: Using transactions with RAII.
     */
    inline void ExampleTransactionRAII(IInventoryRepository &repository, uint64 characterId)
    {
        // Transaction begins automatically
        InventoryTransaction transaction(repository);

        // Perform multiple operations
        InventoryItemData item1;
        item1.entry = 100;
        item1.slot = 0x1700;
        item1.stackCount = 1;

        InventoryItemData item2;
        item2.entry = 200;
        item2.slot = 0x1701;
        item2.stackCount = 5;

        repository.SaveItem(characterId, item1);
        repository.SaveItem(characterId, item2);

        // Commit all changes
        if (!transaction.Commit())
        {
            // Commit failed - transaction will auto-rollback
            return;
        }

        // Transaction completed successfully
    }

    /**
     * @brief Example: Manual transaction control.
     */
    inline void ExampleManualTransaction(IInventoryRepository &repository, uint64 characterId)
    {
        // Begin transaction manually
        if (!repository.BeginTransaction())
        {
            // Failed to start transaction
            return;
        }

        // Perform operations
        InventoryItemData item;
        item.entry = 300;
        item.slot = 0x1702;

        if (!repository.SaveItem(characterId, item))
        {
            // Operation failed - rollback
            repository.Rollback();
            return;
        }

        // Commit transaction
        if (!repository.Commit())
        {
            // Commit failed
            repository.Rollback();
        }
    }

    /**
     * @brief Example: Using Unit of Work for batch operations.
     */
    inline void ExampleUnitOfWork(IInventoryRepository &repository, uint64 characterId)
    {
        InventoryUnitOfWork uow(repository);

        // Register new items
        InventoryItemData newItem;
        newItem.entry = 400;
        newItem.slot = 0x1703;

        uow.RegisterNew([&]()
                        { repository.SaveItem(characterId, newItem); });

        // Register updates
        InventoryItemData updatedItem;
        updatedItem.entry = 500;
        updatedItem.slot = 0x1704;
        updatedItem.stackCount = 10; // Updated count

        uow.RegisterDirty([&]()
                          { repository.SaveItem(characterId, updatedItem); });

        // Register deletions
        uow.RegisterDeleted([&]()
                            { repository.DeleteItem(characterId, 0x1705); });

        // Commit all changes at once
        if (!uow.Commit())
        {
            // All changes rolled back
        }
    }

    /**
     * @brief Example: Exception-safe transaction.
     */
    inline bool ExampleExceptionSafeTransaction(
        IInventoryRepository &repository,
        uint64 characterId)
    {
        try
        {
            InventoryTransaction transaction(repository);

            // Risky operations that might throw
            InventoryItemData item;
            item.entry = 600;
            item.slot = 0x1706;

            repository.SaveItem(characterId, item);

            // Some operation that might throw
            // throw std::runtime_error("Something went wrong");

            // Commit if we get here
            return transaction.Commit();
        }
        catch (const std::exception &e)
        {
            // Transaction automatically rolled back by RAII
            // Log error: e.what()
            return false;
        }
    }

    /**
     * @brief Example: Batch save all items.
     */
    inline void ExampleBatchSave(
        IInventoryRepository &repository,
        uint64 characterId,
        const std::vector<InventoryItemData> &items)
    {
        InventoryTransaction transaction(repository);

        // Save all items in one transaction
        if (!repository.SaveAllItems(characterId, items))
        {
            // Save failed - will auto-rollback
            return;
        }

        // Commit
        transaction.Commit();
    }

    /**
     * @brief Example: Deleting items.
     */
    inline void ExampleDeleteItems(IInventoryRepository &repository, uint64 characterId)
    {
        InventoryTransaction transaction(repository);

        // Delete specific item by slot
        repository.DeleteItem(characterId, 0x1707);

        // Delete another item
        repository.DeleteItem(characterId, 0x1708);

        // Commit deletions
        transaction.Commit();
    }

    /**
     * @brief Example: In-memory repository for testing.
     */
    inline void ExampleInMemoryRepository()
    {
        // Create in-memory repository (no database needed)
        InMemoryInventoryRepository repository;

        const uint64 testCharId = 12345;

        // Add test data
        InventoryItemData item;
        item.entry = 700;
        item.slot = 0x1700;
        item.stackCount = 5;

        repository.SaveItem(testCharId, item);

        // Verify data
        auto items = repository.LoadItems(testCharId);
        // items.size() should be 1

        // Check item count
        size_t count = repository.GetItemCount(testCharId);
        // count should be 1

        // Clear for next test
        repository.Clear();
    }

    /**
     * @brief Example: Complex multi-operation transaction.
     */
    inline bool ExampleComplexTransaction(
        IInventoryRepository &repository,
        uint64 characterId,
        uint16 sourceSlot,
        uint16 destSlot)
    {
        InventoryUnitOfWork uow(repository);

        // Simulate moving item from one slot to another

        // Load source item
        auto items = repository.LoadItems(characterId);
        auto sourceIt = std::find_if(items.begin(), items.end(),
                                     [sourceSlot](const InventoryItemData &item)
                                     {
                                         return item.slot == sourceSlot;
                                     });

        if (sourceIt == items.end())
        {
            return false; // Source item not found
        }

        InventoryItemData sourceItem = *sourceIt;

        // Delete from source
        uow.RegisterDeleted([&]()
                            { repository.DeleteItem(characterId, sourceSlot); });

        // Add to destination
        sourceItem.slot = destSlot;
        uow.RegisterNew([&]()
                        { repository.SaveItem(characterId, sourceItem); });

        // Commit the move operation
        return uow.Commit();
    }

    /**
     * @brief Example: Repository factory pattern.
     */
    inline std::unique_ptr<IInventoryRepository> CreateRepositoryForEnvironment(
        bool isProduction)
    {
        if (isProduction)
        {
            // In production, would return database repository
            // return std::make_unique<DatabaseInventoryRepository>(dbConnection);

            // For now, return in-memory
            return std::make_unique<InMemoryInventoryRepository>();
        }
        else
        {
            // In testing/development, use in-memory
            return std::make_unique<InMemoryInventoryRepository>();
        }
    }
}