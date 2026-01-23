// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "remove_item_command.h"
#include "game_server/objects/game_item_s.h"
#include "objects/game_bag_s.h"

namespace mmo
{
    RemoveItemCommand::RemoveItemCommand(
        IRemoveItemCommandContext &context,
        InventorySlot slot) noexcept
        : m_context(context), m_slot(slot), m_stacks(0) // 0 means remove all stacks
          ,
          m_removedItem(nullptr)
    {
    }

    RemoveItemCommand::RemoveItemCommand(
        IRemoveItemCommandContext &context,
        InventorySlot slot,
        uint16 stacks) noexcept
        : m_context(context), m_slot(slot), m_stacks(stacks), m_removedItem(nullptr)
    {
    }

    InventoryResult<void> RemoveItemCommand::Execute()
    {
        // Validate removal
        auto validationResult = ValidateRemoval();
        if (validationResult.IsFailure())
        {
            return validationResult;
        }

        // Get item before removal
        m_removedItem = m_context.GetItemAtSlot(m_slot.GetAbsolute());

        // Perform removal
        m_context.RemoveItemFromSlot(m_slot.GetAbsolute(), m_stacks);

        return InventoryResult<void>::Success();
    }

    const char *RemoveItemCommand::GetDescription() const noexcept
    {
        return "Remove item from inventory";
    }

    std::shared_ptr<GameItemS> RemoveItemCommand::GetRemovedItem() const noexcept
    {
        return m_removedItem;
    }

    InventoryResult<void> RemoveItemCommand::ValidateRemoval()
    {
        // Check if item exists at slot
        const auto item = m_context.GetItemAtSlot(m_slot.GetAbsolute());
        if (!item)
        {
            return InventoryResult<void>::Failure(
                inventory_change_failure::ItemNotFound);
        }

        // If item is a bag ensure it is empty
        if (item->IsContainer())
        {
            const auto bag = std::static_pointer_cast<GameBagS>(item);
            if (!bag->IsEmpty())
            {
                return InventoryResult<void>::Failure(inventory_change_failure::CanOnlyDoWithEmptyBags);
            }
        }

        // If specific stack count requested, validate it
        if (m_stacks > 0)
        {
            const uint32 currentStacks = item->Get<uint32>(object_fields::StackCount);
            if (m_stacks > currentStacks)
            {
                // Cap to actual stack count (remove all available)
                m_stacks = static_cast<uint16>(currentStacks);
            }
        }

        return InventoryResult<void>::Success();
    }
}
