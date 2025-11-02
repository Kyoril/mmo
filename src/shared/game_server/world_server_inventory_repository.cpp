// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_server_inventory_repository.h"
#include "world_server/realm_connector.h"
#include "inventory.h"  // For ItemData

namespace mmo
{
    // Helper functions to convert between InventoryItemData (domain) and ItemData (DTO)
    static ItemData ToItemData(const InventoryItemData& item)
    {
        ItemData data;
        data.entry = item.entry;
        data.slot = item.slot;
        data.stackCount = static_cast<uint8>(item.stackCount);  // Narrowing conversion
        data.creator = item.creator;
        data.contained = item.contained;
        data.durability = static_cast<uint16>(item.durability);  // Narrowing conversion
        data.randomPropertyIndex = static_cast<uint16>(item.randomPropertyIndex);
        data.randomSuffixIndex = static_cast<uint16>(item.randomSuffixIndex);
        return data;
    }

    static InventoryItemData ToInventoryItemData(const ItemData& data)
    {
        InventoryItemData item;
        item.entry = data.entry;
        item.slot = data.slot;
        item.stackCount = data.stackCount;
        item.creator = data.creator;
        item.contained = data.contained;
        item.durability = data.durability;
        item.randomPropertyIndex = data.randomPropertyIndex;
        item.randomSuffixIndex = data.randomSuffixIndex;
        return item;
    }

    WorldServerInventoryRepository::WorldServerInventoryRepository(
        RealmConnector &realmConnector,
        uint64 characterId) noexcept
        : m_realmConnector(realmConnector), m_characterId(characterId), m_inTransaction(false)
    {
    }

    std::vector<InventoryItemData> WorldServerInventoryRepository::LoadItems(uint64 characterId)
    {
        // Note: Inventory loading happens via CharacterData.items during PlayerCharacterJoin
        // This method would only be called if we need to reload inventory mid-session
        // For now, return empty vector as loading is handled by the existing architecture
        return {};
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
    DLOG("WorldServerInventoryRepository::SaveAllItems called with " << items.size() << " items, inTransaction=" << m_inTransaction);
    
    if (m_inTransaction)
    {
        // Buffer all items
        for (const auto &item : items)
        {
            m_pendingOperations.emplace_back(OperationType::Save, item);
        }
        DLOG("Buffered " << items.size() << " items for transaction");
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
        DLOG("Sending " << ops.size() << " operations immediately");
        return SendBatchOperationsPacket(characterId, ops);
    }
}    bool WorldServerInventoryRepository::DeleteItem(uint64 characterId, uint16 slot)
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
        // For now, not implemented as it's not used during normal gameplay
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
        WLOG("Commit called but not in transaction");
        return false;
    }

    DLOG("Committing transaction with " << m_pendingOperations.size() << " pending operations");

    // Send all buffered operations to realm server
    const bool success = SendBatchOperationsPacket(m_characterId, m_pendingOperations);

    m_inTransaction = false;
    m_pendingOperations.clear();

    return success;
}    bool WorldServerInventoryRepository::Rollback()
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
        std::vector<ItemData> items;
        items.push_back(ToItemData(item));

        // Generate operation ID (for now, use 0 - will be enhanced with proper tracking)
        const uint32 operationId = 0;

        // Send to realm server via RealmConnector
        m_realmConnector.SendSaveInventoryItems(characterId, operationId, items);
        return true;
    }

    bool WorldServerInventoryRepository::SendDeleteItemPacket(uint64 characterId, uint16 slot)
    {
        // Generate operation ID (for now, use 0 - will be enhanced with proper tracking)
        const uint32 operationId = 0;

        // Send delete packet to realm server
        std::vector<uint16> slots;
        slots.push_back(slot);

        m_realmConnector.SendDeleteInventoryItems(characterId, operationId, slots);
        return true;
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
        std::vector<ItemData> itemsToSave;
        std::vector<uint16> slotsToDelete;

        for (const auto &op : operations)
        {
            if (op.type == OperationType::Save)
            {
                itemsToSave.push_back(ToItemData(op.itemData));
            }
            else if (op.type == OperationType::Delete)
            {
                slotsToDelete.push_back(op.slot);
            }
        }

        // Generate operation ID (for now, use 0 - will be enhanced with proper tracking)
        const uint32 operationId = 0;

        // Send batch save packet if any saves
        if (!itemsToSave.empty())
        {
            m_realmConnector.SendSaveInventoryItems(characterId, operationId, itemsToSave);
        }

        // Send batch delete packet if any deletes
        if (!slotsToDelete.empty())
        {
            m_realmConnector.SendDeleteInventoryItems(characterId, operationId, slotsToDelete);
        }

        return true;
    }
}