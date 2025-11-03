// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "inventory_command.h"
#include "inventory_types.h"

#include <memory>

namespace mmo
{
    class GameItemS;
    class GameBagS;

    namespace proto
    {
        class ItemEntry;
    }

    /**
     * @brief Context interface for swap items command execution.
     *
     * Provides access to inventory query and modification operations needed
     * by the SwapItemsCommand. This interface allows dependency injection
     * for testing and decouples the command from concrete inventory implementations.
     */
    class ISwapItemsCommandContext
    {
    public:
        virtual ~ISwapItemsCommandContext() = default;

    public:
        /**
         * @brief Gets the item at a specific slot.
         * @param slot The absolute slot index to query.
         * @return The item at the slot, or nullptr if slot is empty.
         */
        virtual std::shared_ptr<GameItemS> GetItemAtSlot(uint16 slot) const = 0;

        /**
         * @brief Gets the bag at a specific slot.
         * @param slot The absolute slot index to query.
         * @return The bag at the slot, or nullptr if slot is empty or not a bag.
         */
        virtual std::shared_ptr<GameBagS> GetBagAtSlot(uint16 slot) const = 0;

        /**
         * @brief Validates if an item can be placed in a specific slot.
         * @param slot The absolute slot index to validate.
         * @param entry The item entry to validate.
         * @return Okay if valid, error code otherwise.
         */
        virtual InventoryChangeFailure IsValidSlot(uint16 slot, const proto::ItemEntry& entry) const = 0;

        /**
         * @brief Checks if the owner is alive.
         * @return True if owner is alive, false otherwise.
         */
        virtual bool IsOwnerAlive() const = 0;

        /**
         * @brief Checks if the owner is in combat.
         * @return True if owner is in combat, false otherwise.
         */
        virtual bool IsOwnerInCombat() const = 0;

        /**
         * @brief Swaps items between two slots.
         * @param slot1 First slot absolute index.
         * @param slot2 Second slot absolute index.
         */
        virtual void SwapItemSlots(uint16 slot1, uint16 slot2) = 0;

        /**
         * @brief Splits a stack of items from source to destination.
         * @param sourceSlot The slot containing the stack to split.
         * @param destSlot The destination slot (must be empty).
         * @param count Number of items to move to destination.
         * @return True if split succeeded, false otherwise.
         */
        virtual bool SplitStack(uint16 sourceSlot, uint16 destSlot, uint16 count) = 0;

        /**
         * @brief Merges stacks from source into destination.
         * @param sourceSlot The slot to merge from.
         * @param destSlot The slot to merge into.
         * @return True if merge succeeded, false otherwise.
         */
        virtual bool MergeStacks(uint16 sourceSlot, uint16 destSlot) = 0;
    };

    /**
     * @brief Command to swap items between two inventory slots.
     *
     * Encapsulates the logic for swapping items between inventory slots with
     * proper validation. Handles various swap scenarios:
     * - Simple slot swap (both slots occupied)
     * - Moving to empty slot
     * - Stack splitting (if count specified)
     * - Stack merging (same item type)
     *
     * The command validates equipment rules, bag placement, and stack constraints
     * before performing the swap operation.
     */
    class SwapItemsCommand final : public IInventoryCommand
    {
    public:
        /**
         * @brief Constructs a swap command for swapping entire stacks.
         * @param context The inventory context providing access to operations.
         * @param sourceSlot The source inventory slot.
         * @param destSlot The destination inventory slot.
         */
        SwapItemsCommand(
            ISwapItemsCommandContext &context,
            InventorySlot sourceSlot,
            InventorySlot destSlot) noexcept;

        /**
         * @brief Constructs a swap command for splitting stacks.
         * @param context The inventory context providing access to operations.
         * @param sourceSlot The source inventory slot containing the stack.
         * @param destSlot The destination inventory slot (must be empty).
         * @param count Number of items to split from source to destination.
         */
        SwapItemsCommand(
            ISwapItemsCommandContext &context,
            InventorySlot sourceSlot,
            InventorySlot destSlot,
            uint16 count) noexcept;

    public:
        // IInventoryCommand implementation
        InventoryResult<void> Execute() override;
        const char *GetDescription() const noexcept override;

    private:
        /**
         * @brief Validates the swap operation.
         * @return Success if swap is valid, failure with error code otherwise.
         */
        InventoryResult<void> ValidateSwap();

        /**
         * @brief Checks if source and destination can merge stacks.
         * @return True if items can be merged, false otherwise.
         */
        bool CanMergeStacks() const;

        /**
         * @brief Checks if the operation is a stack split.
         * @return True if splitting a stack, false otherwise.
         */
        bool IsStackSplit() const noexcept;

    private:
        ISwapItemsCommandContext &m_context;
        InventorySlot m_sourceSlot;
        InventorySlot m_destSlot;
        uint16 m_splitCount; // 0 means swap entire stacks, >0 means split
    };
}