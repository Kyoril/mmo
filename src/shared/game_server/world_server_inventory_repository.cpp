// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_server_inventory_repository.h"

// TODO: Include actual RealmConnector when integrating
// #include "realm_connector.h"

namespace mmo
{
    // Forward declare to avoid include for now
    class RealmConnector
    {
    public:
        // Stub methods - will be replaced with actual implementation
        bool SendInventorySavePacket(uint64 characterId, const std::vector<InventoryItemData> &items) { return true; }
        bool SendInventoryDeletePacket(uint64 characterId, uint16 slot) { return true; }
        std::vector<InventoryItemData> RequestInventoryLoad(uint64 characterId) { return {}; }
    };

    WorldServerInventoryRepository::WorldServerInventoryRepository(
        RealmConnector &realmConnector,
        uint64 characterId) noexcept
        : m_realmConnector(realmConnector), m_characterId(characterId), m_inTransaction(false)
    {
    }

    std::vector<InventoryItemData> WorldServerInventoryRepository::LoadItems(uint64 characterId)
    {
        // Send load request to realm server
        // This is typically done once when character logs in
        return m_realmConnector.RequestInventoryLoad(characterId);
    }

    bool WorldServerInventoryRepository::SaveItem(uint64 characterId, const InventoryItemData &item)
    {
        if (m_inTransaction)
        {
            // Buffer the operation
            m_pendingOperations.emplace_back(OperationType::Save, item);
            return true;
        }
        else
        {
            // Send immediately
            return SendSaveItemPacket(characterId, item);
        }
    }

    bool WorldServerInventoryRepository::SaveAllItems(uint64 characterId, const std::vector<InventoryItemData> &items)
    {
        if (m_inTransaction)
        {
            // Buffer all items
            for (const auto &item : items)
            {
                m_pendingOperations.emplace_back(OperationType::Save, item);
            }
            return true;
        }
        else
        {
            // Send batch immediately
            std::vector<PendingOperation> ops;
            for (const auto &item : items)
            {
                ops.emplace_back(OperationType::Save, item);
            }
            return SendBatchOperationsPacket(characterId, ops);
        }
    }

    bool WorldServerInventoryRepository::DeleteItem(uint64 characterId, uint16 slot)
    {
        if (m_inTransaction)
        {
            // Buffer the deletion
            m_pendingOperations.emplace_back(OperationType::Delete, slot);
            return true;
        }
        else
        {
            // Send immediately
            return SendDeleteItemPacket(characterId, slot);
        }
    }

    bool WorldServerInventoryRepository::DeleteAllItems(uint64 characterId)
    {
        // This is a rare operation (character deletion)
        // Send directly to realm server
        // TODO: Implement actual packet sending
        return true;
    }

    bool WorldServerInventoryRepository::BeginTransaction()
    {
        if (m_inTransaction)
        {
            return false;
        }

        m_inTransaction = true;
        m_pendingOperations.clear();
        return true;
    }

    bool WorldServerInventoryRepository::Commit()
    {
        if (!m_inTransaction)
        {
            return false;
        }

        // Send all buffered operations to realm server
        const bool success = SendBatchOperationsPacket(m_characterId, m_pendingOperations);

        m_inTransaction = false;
        m_pendingOperations.clear();

        return success;
    }

    bool WorldServerInventoryRepository::Rollback()
    {
        if (!m_inTransaction)
        {
            return false;
        }

        // Simply discard buffered operations
        m_inTransaction = false;
        m_pendingOperations.clear();
        return true;
    }

    bool WorldServerInventoryRepository::HasPendingOperations() const noexcept
    {
        return !m_pendingOperations.empty();
    }

    size_t WorldServerInventoryRepository::GetPendingOperationCount() const noexcept
    {
        return m_pendingOperations.size();
    }

    bool WorldServerInventoryRepository::SendSaveItemPacket(uint64 characterId, const InventoryItemData &item)
    {
        // Create packet with single item
        std::vector<InventoryItemData> items;
        items.push_back(item);

        // Send to realm server
        return m_realmConnector.SendInventorySavePacket(characterId, items);
    }

    bool WorldServerInventoryRepository::SendDeleteItemPacket(uint64 characterId, uint16 slot)
    {
        // Send delete packet to realm server
        return m_realmConnector.SendInventoryDeletePacket(characterId, slot);
    }

    bool WorldServerInventoryRepository::SendBatchOperationsPacket(
        uint64 characterId,
        const std::vector<PendingOperation> &operations)
    {
        if (operations.empty())
        {
            return true;
        }

        // Separate saves and deletes
        std::vector<InventoryItemData> itemsToSave;
        std::vector<uint16> slotsToDelete;

        for (const auto &op : operations)
        {
            if (op.type == OperationType::Save)
            {
                itemsToSave.push_back(op.itemData);
            }
            else if (op.type == OperationType::Delete)
            {
                slotsToDelete.push_back(op.slot);
            }
        }

        // Send batch save packet if any saves
        if (!itemsToSave.empty())
        {
            if (!m_realmConnector.SendInventorySavePacket(characterId, itemsToSave))
            {
                return false;
            }
        }

        // Send delete packets if any deletes
        for (uint16 slot : slotsToDelete)
        {
            if (!m_realmConnector.SendInventoryDeletePacket(characterId, slot))
            {
                return false;
            }
        }

        return true;
    }
}