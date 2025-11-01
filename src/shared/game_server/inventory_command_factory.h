// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "inventory_command.h"
#include "inventory_types.h"

#include <memory>
#include <functional>

namespace mmo
{
	class GameItemS;
	class IAddItemCommandContext;
	class IRemoveItemCommandContext;
	class ISwapItemsCommandContext;

	/**
	 * @brief Factory for creating inventory commands.
     *
     * Centralizes command creation and provides a single point of control
     * for instantiating inventory operation commands. This factory:
     * - Simplifies command creation with builder-like interface
     * - Encapsulates context dependencies
     * - Enables command interceptors/decorators
     * - Facilitates testing with mock commands
     */
    class InventoryCommandFactory
    {
    public:
        /**
         * @brief Constructs a factory with required context dependencies.
         * @param addContext Context for add item operations.
         * @param removeContext Context for remove item operations.
         * @param swapContext Context for swap item operations.
         */
        InventoryCommandFactory(
            IAddItemCommandContext &addContext,
            IRemoveItemCommandContext &removeContext,
            ISwapItemsCommandContext &swapContext) noexcept;

    public:
        /**
         * @brief Creates command to add item with auto-slot selection.
         * @param item The item to add to inventory.
         * @return Unique pointer to the add item command.
         */
        std::unique_ptr<IInventoryCommand> CreateAddItem(
            std::shared_ptr<GameItemS> item) const;

        /**
         * @brief Creates command to add item to specific slot.
         * @param item The item to add to inventory.
         * @param targetSlot The target slot for the item.
         * @return Unique pointer to the add item command.
         */
        std::unique_ptr<IInventoryCommand> CreateAddItem(
            std::shared_ptr<GameItemS> item,
            InventorySlot targetSlot) const;

        /**
         * @brief Creates command to remove all items from slot.
         * @param slot The slot to remove items from.
         * @return Unique pointer to the remove item command.
         */
        std::unique_ptr<IInventoryCommand> CreateRemoveItem(
            InventorySlot slot) const;

        /**
         * @brief Creates command to remove specific stack count from slot.
         * @param slot The slot to remove items from.
         * @param stacks Number of stacks to remove.
         * @return Unique pointer to the remove item command.
         */
        std::unique_ptr<IInventoryCommand> CreateRemoveItem(
            InventorySlot slot,
            uint16 stacks) const;

        /**
         * @brief Creates command to swap items between slots.
         * @param sourceSlot The source inventory slot.
         * @param destSlot The destination inventory slot.
         * @return Unique pointer to the swap items command.
         */
        std::unique_ptr<IInventoryCommand> CreateSwapItems(
            InventorySlot sourceSlot,
            InventorySlot destSlot) const;

        /**
         * @brief Creates command to split item stack.
         * @param sourceSlot The source slot containing the stack.
         * @param destSlot The destination slot (must be empty).
         * @param count Number of items to split off.
         * @return Unique pointer to the swap items command.
         */
        std::unique_ptr<IInventoryCommand> CreateSplitStack(
            InventorySlot sourceSlot,
            InventorySlot destSlot,
            uint16 count) const;

    private:
        IAddItemCommandContext &m_addContext;
        IRemoveItemCommandContext &m_removeContext;
        ISwapItemsCommandContext &m_swapContext;
    };
}