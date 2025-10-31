// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file slot_manager.h
 *
 * @brief Domain service for inventory slot management and queries.
 *
 * Extracts slot finding, allocation, and iteration logic from the Inventory
 * class. This service provides focused slot management operations following
 * the Single Responsibility Principle.
 */

#pragma once

#include "base/typedefs.h"
#include "base/linear_set.h"
#include "inventory_types.h"
#include "game/item.h"

#include <functional>
#include <map>
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
	 * @brief Context interface for slot manager to access inventory state.
	 *
	 * This interface decouples SlotManager from the full Inventory implementation,
	 * enabling independent testing and following the Dependency Inversion Principle.
	 */
	class ISlotManagerContext
	{
	public:
		/**
		 * @brief Gets an item at the specified absolute slot.
		 * @param slot The absolute slot index.
		 * @return Shared pointer to the item, or nullptr if slot is empty.
		 */
		virtual std::shared_ptr<GameItemS> GetItemAtSlot(uint16 slot) const noexcept = 0;

		/**
		 * @brief Gets a bag at the specified absolute slot.
		 * @param slot The absolute slot index (must be a bag pack slot).
		 * @return Shared pointer to the bag, or nullptr if no bag equipped.
		 */
		virtual std::shared_ptr<GameBagS> GetBagAtSlot(uint16 slot) const noexcept = 0;

		/**
		 * @brief Gets the current count of a specific item type.
		 * @param itemId The item entry ID.
		 * @return Total count of this item across all inventory slots.
		 */
		virtual uint16 GetItemCount(uint32 itemId) const noexcept = 0;

	protected:
		~ISlotManagerContext() = default;
	};

	/**
	 * @brief Result structure for slot allocation queries.
	 *
	 * Contains information about available slots and stacks for item placement.
	 * Used by FindAvailableSlots to communicate complex allocation state.
	 */
	struct SlotAllocationResult
	{
		/// Empty slots that can accept new item instances
		LinearSet<uint16> emptySlots;
		
		/// Slots with existing items that have remaining stack capacity
		LinearSet<uint16> usedCapableSlots;
		
		/// Total available stack capacity across all slots
		uint16 availableStacks = 0;
	};

	/**
	 * @brief Domain service for inventory slot management.
	 *
	 * Responsibilities:
	 * - Find empty slots for item placement
	 * - Find available slots for stackable items
	 * - Iterate through bags and slots
	 * - Calculate slot availability and capacity
	 *
	 * This service is stateless and operates on the inventory context provided.
	 * All methods are const, ensuring no side effects on inventory state.
	 */
	class SlotManager final
	{
	public:
		/// Callback function for bag iteration
		/// Parameters: bagId, startSlot, endSlot
		/// Return: true to continue iteration, false to stop
		using BagCallback = std::function<bool(uint8 bagId, uint8 startSlot, uint8 endSlot)>;

		/**
		 * @brief Constructs a slot manager for a specific inventory context.
		 * @param context The inventory context to query.
		 */
		explicit SlotManager(const ISlotManagerContext& context) noexcept;

		/**
		 * @brief Finds the first empty slot in the inventory.
		 *
		 * Searches through all bags in order (main inventory, then bag slots)
		 * to find the first available empty slot.
		 *
		 * @return Absolute slot index of first empty slot, or 0 if no slots available.
		 */
		uint16 FindFirstEmptySlot() const noexcept;

		/**
		 * @brief Finds available slots for placing a specific amount of items.
		 *
		 * This method performs intelligent slot allocation for stackable items:
		 * - Identifies existing stacks with remaining capacity
		 * - Identifies empty slots for new item instances
		 * - Calculates total available stack capacity
		 *
		 * @param entry The item entry to place.
		 * @param amount The number of items to place.
		 * @param result Output structure containing allocation information.
		 * @return Success if enough space available, InventoryFull otherwise.
		 */
		InventoryResult<void> FindAvailableSlots(
			const proto::ItemEntry& entry,
			uint16 amount,
			SlotAllocationResult& result) const noexcept;

		/**
		 * @brief Iterates through all equipped bags.
		 *
		 * Executes the provided callback for each bag (including main inventory).
		 * The callback receives the bag ID and slot range, and can stop iteration
		 * by returning false.
		 *
		 * Iteration order: Main inventory (Bag_0), then bag slots 0-3.
		 *
		 * @param callback Function to execute for each bag.
		 */
		void ForEachBag(const BagCallback& callback) const noexcept;

		/**
		 * @brief Counts total free slots across all bags.
		 *
		 * @return Number of empty slots available for items.
		 */
		uint16 CountFreeSlots() const noexcept;

		/**
		 * @brief Checks if a specific slot is empty.
		 *
		 * @param slot The absolute slot index to check.
		 * @return true if slot is empty, false if occupied.
		 */
		bool IsSlotEmpty(uint16 slot) const noexcept;

		/**
		 * @brief Gets the slot range for a specific bag.
		 *
		 * @param bagId The bag identifier (Bag_0 or bag pack slot).
		 * @param outStartSlot Output parameter for first slot in bag.
		 * @param outEndSlot Output parameter for one-past-last slot in bag.
		 * @return true if bag exists and has slots, false otherwise.
		 */
		bool GetBagSlotRange(uint8 bagId, uint8& outStartSlot, uint8& outEndSlot) const noexcept;

	private:
		const ISlotManagerContext& m_context;
	};
}
