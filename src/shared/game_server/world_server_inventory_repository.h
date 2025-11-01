// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "inventory_repository.h"

#include <vector>
#include <functional>

namespace mmo
{
    // Forward declarations
    class RealmConnector;

    /**
     * @brief Repository implementation for World Server.
     *
     * Acts as a network proxy that forwards inventory operations to the
     * Realm Server for actual database persistence. This repository:
     * - Serializes operations into network packets
     * - Sends packets to Realm Server via RealmConnector
     * - Buffers operations during transactions
     * - Handles async responses (future enhancement)
     *
     * Architecture:
     * World Server (this) -> Network -> Realm Server -> Database
     *
     * Transaction Strategy:
     * Operations are buffered during a transaction and sent as a batch
     * on Commit(). This minimizes network roundtrips and ensures atomicity
     * at the database level.
     */
    class WorldServerInventoryRepository final : public IInventoryRepository
    {
    public:
        /**
         * @brief Constructs a world server repository.
         * @param realmConnector The connection to the realm server.
         * @param characterId The character ID for this repository.
         */
        WorldServerInventoryRepository(
            RealmConnector &realmConnector,
            uint64 characterId) noexcept;

    public:
        // IInventoryRepository implementation

        /**
         * @brief Loads items from realm server.
         *
         * Note: This is typically called once when character logs in.
         * The World Server maintains the authoritative in-memory state
         * after loading.
         */
        std::vector<InventoryItemData> LoadItems(uint64 characterId) override;

        /**
         * @brief Saves a single item (buffered if in transaction).
         */
        bool SaveItem(uint64 characterId, const InventoryItemData &item) override;

        /**
         * @brief Saves all items as a batch operation.
         */
        bool SaveAllItems(uint64 characterId, const std::vector<InventoryItemData> &items) override;

        /**
         * @brief Deletes an item (buffered if in transaction).
         */
        bool DeleteItem(uint64 characterId, uint16 slot) override;

        /**
         * @brief Deletes all items for character.
         */
        bool DeleteAllItems(uint64 characterId) override;

        /**
         * @brief Begins a transaction (buffers operations).
         */
        bool BeginTransaction() override;

        /**
         * @brief Commits buffered operations to realm server.
         */
        bool Commit() override;

        /**
         * @brief Discards buffered operations.
         */
        bool Rollback() override;

    public:
        /**
         * @brief Checks if there are pending operations.
         * @return True if operations are buffered.
         */
        bool HasPendingOperations() const noexcept;

        /**
         * @brief Gets count of pending operations.
         * @return Number of buffered operations.
         */
        size_t GetPendingOperationCount() const noexcept;

    private:
        /**
         * @brief Type of pending operation.
         */
        enum class OperationType : uint8
        {
            Save,
            Delete
        };

        /**
         * @brief Represents a pending operation to be sent on commit.
         */
        struct PendingOperation
        {
            OperationType type;
            InventoryItemData itemData; // Used for Save
            uint16 slot;                // Used for Delete

            PendingOperation(OperationType t, const InventoryItemData &data)
                : type(t), itemData(data), slot(data.slot)
            {
            }

            PendingOperation(OperationType t, uint16 s)
                : type(t), itemData(), slot(s)
            {
            }
        };

        /**
         * @brief Sends save item packet to realm server.
         */
        bool SendSaveItemPacket(uint64 characterId, const InventoryItemData &item);

        /**
         * @brief Sends delete item packet to realm server.
         */
        bool SendDeleteItemPacket(uint64 characterId, uint16 slot);

        /**
         * @brief Sends batch of operations to realm server.
         */
        bool SendBatchOperationsPacket(uint64 characterId, const std::vector<PendingOperation> &operations);

    private:
        RealmConnector &m_realmConnector;
        uint64 m_characterId;
        bool m_inTransaction;
        std::vector<PendingOperation> m_pendingOperations;
    };
}