// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "inventory_repository.h"

#include <algorithm>

namespace mmo
{
    InMemoryInventoryRepository::InMemoryInventoryRepository() noexcept
        : m_inTransaction(false)
    {
    }

    std::vector<InventoryItemData> InMemoryInventoryRepository::LoadItems(uint64 characterId)
    {
        auto it = m_storage.find(characterId);
        if (it == m_storage.end())
        {
            return std::vector<InventoryItemData>();
        }

        return it->second;
    }

    bool InMemoryInventoryRepository::SaveItem(uint64 characterId, const InventoryItemData &item)
    {
        auto &items = m_storage[characterId];

        // Find existing item at slot
        auto it = std::find_if(items.begin(), items.end(),
                               [&item](const InventoryItemData &existing)
                               {
                                   return existing.slot == item.slot;
                               });

        if (it != items.end())
        {
            // Update existing item
            *it = item;
        }
        else
        {
            // Add new item
            items.push_back(item);
        }

        return true;
    }

    bool InMemoryInventoryRepository::SaveAllItems(uint64 characterId, const std::vector<InventoryItemData> &items)
    {
        m_storage[characterId] = items;
        return true;
    }

    bool InMemoryInventoryRepository::DeleteItem(uint64 characterId, uint16 slot)
    {
        auto it = m_storage.find(characterId);
        if (it == m_storage.end())
        {
            return false;
        }

        auto &items = it->second;
        auto itemIt = std::find_if(items.begin(), items.end(),
                                   [slot](const InventoryItemData &item)
                                   {
                                       return item.slot == slot;
                                   });

        if (itemIt == items.end())
        {
            return false;
        }

        items.erase(itemIt);
        return true;
    }

    bool InMemoryInventoryRepository::DeleteAllItems(uint64 characterId)
    {
        m_storage.erase(characterId);
        return true;
    }

    bool InMemoryInventoryRepository::BeginTransaction()
    {
        if (m_inTransaction)
        {
            return false;
        }

        m_transactionBackup = m_storage;
        m_inTransaction = true;
        return true;
    }

    bool InMemoryInventoryRepository::Commit()
    {
        if (!m_inTransaction)
        {
            return false;
        }

        m_transactionBackup.clear();
        m_inTransaction = false;
        return true;
    }

    bool InMemoryInventoryRepository::Rollback()
    {
        if (!m_inTransaction)
        {
            return false;
        }

        m_storage = m_transactionBackup;
        m_transactionBackup.clear();
        m_inTransaction = false;
        return true;
    }

    void InMemoryInventoryRepository::Clear() noexcept
    {
        m_storage.clear();
        m_transactionBackup.clear();
        m_inTransaction = false;
    }

    size_t InMemoryInventoryRepository::GetItemCount(uint64 characterId) const noexcept
    {
        auto it = m_storage.find(characterId);
        if (it == m_storage.end())
        {
            return 0;
        }

        return it->second.size();
    }
}