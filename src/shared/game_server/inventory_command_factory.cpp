// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "inventory_command_factory.h"
#include "add_item_command.h"
#include "remove_item_command.h"
#include "swap_items_command.h"

namespace mmo
{
    InventoryCommandFactory::InventoryCommandFactory(
        IAddItemCommandContext &addContext,
        IRemoveItemCommandContext &removeContext,
        ISwapItemsCommandContext &swapContext) noexcept
        : m_addContext(addContext), m_removeContext(removeContext), m_swapContext(swapContext)
    {
    }

    std::unique_ptr<IInventoryCommand> InventoryCommandFactory::CreateAddItem(
        std::shared_ptr<GameItemS> item) const
    {
        return std::make_unique<AddItemCommand>(m_addContext, item);
    }

    std::unique_ptr<IInventoryCommand> InventoryCommandFactory::CreateAddItem(
        std::shared_ptr<GameItemS> item,
        InventorySlot targetSlot) const
    {
        return std::make_unique<AddItemCommand>(m_addContext, item, targetSlot);
    }

    std::unique_ptr<IInventoryCommand> InventoryCommandFactory::CreateRemoveItem(
        InventorySlot slot) const
    {
        return std::make_unique<RemoveItemCommand>(m_removeContext, slot);
    }

    std::unique_ptr<IInventoryCommand> InventoryCommandFactory::CreateRemoveItem(
        InventorySlot slot,
        uint16 stacks) const
    {
        return std::make_unique<RemoveItemCommand>(m_removeContext, slot, stacks);
    }

    std::unique_ptr<IInventoryCommand> InventoryCommandFactory::CreateSwapItems(
        InventorySlot sourceSlot,
        InventorySlot destSlot) const
    {
        return std::make_unique<SwapItemsCommand>(m_swapContext, sourceSlot, destSlot);
    }

    std::unique_ptr<IInventoryCommand> InventoryCommandFactory::CreateSplitStack(
        InventorySlot sourceSlot,
        InventorySlot destSlot,
        uint16 count) const
    {
        return std::make_unique<SwapItemsCommand>(m_swapContext, sourceSlot, destSlot, count);
    }
}