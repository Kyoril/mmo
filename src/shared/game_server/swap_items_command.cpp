// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "swap_items_command.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_bag_s.h"
#include "game_server/inventory.h"

namespace mmo
{
    SwapItemsCommand::SwapItemsCommand(
        ISwapItemsCommandContext &context,
        InventorySlot sourceSlot,
        InventorySlot destSlot) noexcept
        : m_context(context), m_sourceSlot(sourceSlot), m_destSlot(destSlot), m_splitCount(0) // 0 means swap entire stacks
    {
    }

    SwapItemsCommand::SwapItemsCommand(
        ISwapItemsCommandContext &context,
        InventorySlot sourceSlot,
        InventorySlot destSlot,
        uint16 count) noexcept
        : m_context(context), m_sourceSlot(sourceSlot), m_destSlot(destSlot), m_splitCount(count)
    {
    }

    InventoryResult<void> SwapItemsCommand::Execute()
    {
        // Validate swap operation
        auto validationResult = ValidateSwap();
        if (validationResult.IsFailure())
        {
            return validationResult;
        }

        // Handle stack split
        if (IsStackSplit())
        {
            const bool success = m_context.SplitStack(
                m_sourceSlot.GetAbsolute(),
                m_destSlot.GetAbsolute(),
                m_splitCount);

            if (!success)
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::InternalBagError);
            }

            return InventoryResult<void>::Success();
        }

        // Handle stack merge
        if (CanMergeStacks())
        {
            const bool success = m_context.MergeStacks(
                m_sourceSlot.GetAbsolute(),
                m_destSlot.GetAbsolute());

            if (!success)
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::InternalBagError);
            }

            return InventoryResult<void>::Success();
        }

        // Simple slot swap
        m_context.SwapItemSlots(
            m_sourceSlot.GetAbsolute(),
            m_destSlot.GetAbsolute());

        return InventoryResult<void>::Success();
    }

    const char *SwapItemsCommand::GetDescription() const noexcept
    {
        if (IsStackSplit())
        {
            return "Split item stack";
        }

        if (CanMergeStacks())
        {
            return "Merge item stacks";
        }

        return "Swap inventory items";
    }

    InventoryResult<void> SwapItemsCommand::ValidateSwap()
    {
        // Check if source has an item
        const std::shared_ptr<GameItemS> sourceItem = m_context.GetItemAtSlot(m_sourceSlot.GetAbsolute());
        if (!sourceItem)
        {
            return InventoryResult<void>::Failure(
                inventory_change_failure::ItemNotFound);
        }

        // Check if owner is alive
        if (!m_context.IsOwnerAlive())
        {
            return InventoryResult<void>::Failure(
                inventory_change_failure::YouAreDead);
        }

        // Check destination item (maybe null)
        const std::shared_ptr<GameItemS> destItem = m_context.GetItemAtSlot(m_destSlot.GetAbsolute());

        // Validate stack split
        if (IsStackSplit())
        {
            // Destination must be empty for split
            if (destItem)
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::InventoryFull);
            }

            // Check source has enough stacks
            const uint32 sourceStacks = sourceItem->Get<uint32>(object_fields::StackCount);
            if (m_splitCount >= sourceStacks)
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::TriedToSplitMoreThanCount);
            }

            return InventoryResult<void>::Success();
        }

        // If source slot is a bag and bag is not empty
        if (sourceItem->IsContainer())
        {
            auto sourceBag = std::static_pointer_cast<GameBagS>(sourceItem);
            if (!sourceBag->IsEmpty())
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::CanOnlyDoWithEmptyBags);
            }
        }

        // Destination bag must be empty as well
        if (destItem && destItem->IsContainer())
        {
            auto destBag = std::static_pointer_cast<GameBagS>(destItem);
            if (!destBag->IsEmpty())
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::CanOnlyDoWithEmptyBags);
            }
        }

        if (m_sourceSlot.IsBagBar() && !m_destSlot.IsBagBar())
        {
            // Check that the new slot is not inside the bag itself
            auto bag = m_context.GetBagAtSlot(m_destSlot.GetAbsolute());
            if (bag && bag == sourceItem)
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::BagsCantBeWrapped);
            }
        }

        // Can't change equipment while in combat (except weapons)
        if (m_context.IsOwnerInCombat() && m_sourceSlot.IsEquipment())
        {
            const uint8 equipSlot = m_sourceSlot.GetSlot();
            if (equipSlot != player_equipment_slots::Mainhand &&
                equipSlot != player_equipment_slots::Offhand &&
                equipSlot != player_equipment_slots::Ranged)
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::NotInCombat);
            }
        }

        // Verify destination slot for source item
        auto result = m_context.IsValidSlot(m_destSlot.GetAbsolute(), sourceItem->GetEntry());
        if (result != inventory_change_failure::Okay)
        {
            return InventoryResult<void>::Failure(result);
        }

        // If there is an item in the destination slot, also verify the source slot
        if (destItem)
        {
            result = m_context.IsValidSlot(m_sourceSlot.GetAbsolute(), destItem->GetEntry());
            if (result != inventory_change_failure::Okay)
            {
                return InventoryResult<void>::Failure(result);
            }
        }

        // Validate stack merge
        if (destItem && CanMergeStacks())
        {
            const uint32 maxStack = sourceItem->GetEntry().maxstack();
            const uint32 destStacks = destItem->Get<uint32>(object_fields::StackCount);

            // Check if destination has room
            if (destStacks >= maxStack)
            {
                return InventoryResult<void>::Failure(
                    inventory_change_failure::ItemCantStack);
            }
        }

        // For simple swap, source and dest must be different
        if (m_sourceSlot == m_destSlot)
        {
            return InventoryResult<void>::Failure(
                inventory_change_failure::ItemNotFound);
        }

        return InventoryResult<void>::Success();
    }

    bool SwapItemsCommand::CanMergeStacks() const
    {
	    const std::shared_ptr<GameItemS> sourceItem = m_context.GetItemAtSlot(m_sourceSlot.GetAbsolute());
	    const std::shared_ptr<GameItemS> destItem = m_context.GetItemAtSlot(m_destSlot.GetAbsolute());

        // Both slots must have items
        if (!sourceItem || !destItem)
        {
            return false;
        }

        // Items must be same entry
        const uint32 sourceEntry = sourceItem->Get<uint32>(object_fields::Entry);
        const uint32 destEntry = destItem->Get<uint32>(object_fields::Entry);

        return sourceEntry == destEntry;
    }

    bool SwapItemsCommand::IsStackSplit() const noexcept
    {
        return m_splitCount > 0;
    }
}