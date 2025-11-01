// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file remove_item_command.h
 *
 * @brief Command for removing an item from the inventory.
 *
 * Encapsulates the logic for removing an item from a specific slot,
 * including validation and state updates.
 */

#pragma once

#include "inventory_command.h"
#include "base/typedefs.h"

#include <memory>
#include <optional>

namespace mmo
{
    // Forward declarations
    class GameItemS;

    /**
     * @brief Context interface for RemoveItemCommand dependencies.
     */
    class IRemoveItemCommandContext
    {
    public:
        virtual ~IRemoveItemCommandContext() = default;

        /**
         * @brief Gets the item at a specific slot.
         * @param slot Absolute slot coordinates.
         * @return Item at slot, or nullptr if empty.
         */
        [[nodiscard]] virtual std::shared_ptr<GameItemS> GetItemAtSlot(
            uint16 slot) const = 0;

        /**
         * @brief Removes item from a specific slot and updates all systems.
         * @param slot Absolute slot coordinates.
         * @param stacks Number of stacks to remove (0 = all).
         */
        virtual void RemoveItemFromSlot(uint16 slot, uint16 stacks) = 0;
    };

    /**
     * @brief Command to remove an item from the inventory.
     *
     * This command encapsulates the complete operation of removing an item:
     * 1. Validate item exists at slot
     * 2. Validate stack count
     * 3. Remove item and update all related systems
     *
     * Usage:
     * @code
     * auto command = std::make_unique<RemoveItemCommand>(context, slot);
     * auto result = command->Execute();
     * if (result.IsSuccess()) {
     *     // Item removed successfully
     * }
     * @endcode
     */
    class RemoveItemCommand final : public IInventoryCommand
    {
    public:
        /**
         * @brief Constructs command to remove all stacks from slot.
         * @param context Command execution context.
         * @param slot The slot to remove item from.
         */
        RemoveItemCommand(
            IRemoveItemCommandContext &context,
            InventorySlot slot) noexcept;

        /**
         * @brief Constructs command to remove specific stack count.
         * @param context Command execution context.
         * @param slot The slot to remove item from.
         * @param stacks Number of stacks to remove.
         */
        RemoveItemCommand(
            IRemoveItemCommandContext &context,
            InventorySlot slot,
            uint16 stacks) noexcept;

    public:
        // IInventoryCommand implementation
        [[nodiscard]] InventoryResult<void> Execute() override;
        [[nodiscard]] const char *GetDescription() const noexcept override;

        /**
         * @brief Gets the item that was removed (after Execute).
         * @return Removed item, or nullptr if not yet executed.
         */
        [[nodiscard]] std::shared_ptr<GameItemS> GetRemovedItem() const noexcept;

    private:
        /**
         * @brief Validates item can be removed from slot.
         * @return Success or validation error.
         */
        [[nodiscard]] InventoryResult<void> ValidateRemoval();

    private:
        IRemoveItemCommandContext &m_context;
        InventorySlot m_slot;
        uint16 m_stacks;
        std::shared_ptr<GameItemS> m_removedItem;
    };
}
