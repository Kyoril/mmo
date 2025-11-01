// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "inventory_types.h"
#include "game/item.h"

#include <memory>
#include <vector>
#include <map>
#include <cstdint>

namespace mmo
{
    class GameItemS;
    class GamePlayerS;

    /**
     * @brief Data structure representing persisted item state.
     *
     * Used by repository implementations to store/load item data
     * independent of the specific storage backend.
     */
    struct InventoryItemData
    {
        uint32 entry;               ///< Item template ID
        uint16 slot;                ///< Absolute slot index
        uint16 stackCount;          ///< Number of items in stack
        uint64 creator;             ///< GUID of item creator
        uint64 contained;           ///< GUID of container
        uint32 durability;          ///< Current durability
        uint32 randomPropertyIndex; ///< Random property index
        uint32 randomSuffixIndex;   ///< Random suffix index

        InventoryItemData() noexcept
            : entry(0), slot(0), stackCount(1), creator(0), contained(0), durability(0), randomPropertyIndex(0), randomSuffixIndex(0)
        {
        }
    };

    /**
     * @brief Repository interface for inventory persistence.
     *
     * Abstracts the storage and retrieval of inventory data, decoupling
     * the domain logic from specific persistence mechanisms. This enables:
     * - Different storage backends (database, file, memory)
     * - Testability with mock repositories
     * - Transaction support
     * - Caching strategies
     *
     * Following the Repository pattern from Domain-Driven Design (DDD),
     * this interface provides collection-like access to inventory data.
     */
    class IInventoryRepository
    {
    public:
        virtual ~IInventoryRepository() = default;

    public:
        /**
         * @brief Loads all items for a character.
         * @param characterId The character's database ID.
         * @return Vector of item data loaded from storage.
         */
        virtual std::vector<InventoryItemData> LoadItems(uint64 characterId) = 0;

        /**
         * @brief Saves a single item.
         * @param characterId The character's database ID.
         * @param item The item to save.
         * @return True if save succeeded, false otherwise.
         */
        virtual bool SaveItem(uint64 characterId, const InventoryItemData &item) = 0;

        /**
         * @brief Saves all items for a character.
         * @param characterId The character's database ID.
         * @param items All items to save.
         * @return True if save succeeded, false otherwise.
         */
        virtual bool SaveAllItems(uint64 characterId, const std::vector<InventoryItemData> &items) = 0;

        /**
         * @brief Deletes a single item.
         * @param characterId The character's database ID.
         * @param slot The absolute slot of the item to delete.
         * @return True if delete succeeded, false otherwise.
         */
        virtual bool DeleteItem(uint64 characterId, uint16 slot) = 0;

        /**
         * @brief Deletes all items for a character.
         * @param characterId The character's database ID.
         * @return True if delete succeeded, false otherwise.
         */
        virtual bool DeleteAllItems(uint64 characterId) = 0;

        /**
         * @brief Begins a transaction.
         *
         * All subsequent operations will be part of this transaction
         * until Commit() or Rollback() is called.
         *
         * @return True if transaction started, false otherwise.
         */
        virtual bool BeginTransaction() = 0;

        /**
         * @brief Commits the current transaction.
         * @return True if commit succeeded, false otherwise.
         */
        virtual bool Commit() = 0;

        /**
         * @brief Rolls back the current transaction.
         * @return True if rollback succeeded, false otherwise.
         */
        virtual bool Rollback() = 0;
    };

    /**
     * @brief In-memory repository implementation for testing.
     *
     * Stores inventory data in memory only. Useful for:
     * - Unit testing
     * - Integration testing
     * - Temporary storage scenarios
     * - Development/debugging
     */
    class InMemoryInventoryRepository final : public IInventoryRepository
    {
    public:
        InMemoryInventoryRepository() noexcept;

    public:
        // IInventoryRepository implementation
        std::vector<InventoryItemData> LoadItems(uint64 characterId) override;
        bool SaveItem(uint64 characterId, const InventoryItemData &item) override;
        bool SaveAllItems(uint64 characterId, const std::vector<InventoryItemData> &items) override;
        bool DeleteItem(uint64 characterId, uint16 slot) override;
        bool DeleteAllItems(uint64 characterId) override;
        bool BeginTransaction() override;
        bool Commit() override;
        bool Rollback() override;

    public:
        /**
         * @brief Clears all stored data.
         */
        void Clear() noexcept;

        /**
         * @brief Gets item count for a character (for testing).
         * @param characterId The character ID.
         * @return Number of items stored.
         */
        size_t GetItemCount(uint64 characterId) const noexcept;

    private:
        // Character ID -> Vector of items
        std::map<uint64, std::vector<InventoryItemData>> m_storage;

        // Transaction support
        bool m_inTransaction;
        std::map<uint64, std::vector<InventoryItemData>> m_transactionBackup;
    };
}