// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "swap_items_command.h"
#include "game_server/objects/game_item_s.h"

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
        auto sourceItem = m_context.GetItemAtSlot(m_sourceSlot.GetAbsolute());
        if (!sourceItem)
        {
            return InventoryResult<void>::Failure(
                inventory_change_failure::ItemNotFound);
        }

        // Check destination item (may be null)
        auto destItem = m_context.GetItemAtSlot(m_destSlot.GetAbsolute());

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
			}            return InventoryResult<void>::Success();
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
        if (m_sourceSlot.GetAbsolute() == m_destSlot.GetAbsolute())
        {
            return InventoryResult<void>::Failure(
                inventory_change_failure::ItemNotFound);
        }

        return InventoryResult<void>::Success();
    }

    bool SwapItemsCommand::CanMergeStacks() const
    {
        auto sourceItem = m_context.GetItemAtSlot(m_sourceSlot.GetAbsolute());
        auto destItem = m_context.GetItemAtSlot(m_destSlot.GetAbsolute());

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