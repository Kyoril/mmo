// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file bag_manager.h
 *
 * @brief Domain service for bag-specific inventory operations.
 *
 * Encapsulates all bag-related logic including slot management,
 * free slot calculations, and bag content operations.
 */

#pragma once

#include "i_bag_manager_context.h"
#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	// Forward declarations
	class GameBagS;
	class GameItemS;
	class InventorySlot;

	/**
	 * @brief Domain service responsible for bag management.
	 *
	 * BagManager handles all bag-specific operations that were previously
	 * scattered throughout the Inventory class. It manages:
	 * - Retrieving bag instances from slots
	 * - Updating bag slot references (item GUIDs in bag fields)
	 * - Calculating and updating free slot counts when bags are equipped/unequipped
	 * - Validating bag operations
	 *
	 * Design: Stateless service following Clean Architecture principles.
	 * All operations delegate to the context for data access and rely on
	 * the GameBagS object for bag-specific state.
	 */
	class BagManager final
	{
	public:
		/**
		 * @brief Constructs a BagManager with required context.
		 * @param context Interface providing bag management dependencies.
		 */
		explicit BagManager(IBagManagerContext& context) noexcept;

	public:
		/**
		 * @brief Gets the bag at a specific slot.
		 *
		 * Handles both bag pack slots (0xFFXX format) and bag slots that
		 * need conversion. Returns nullptr if slot doesn't contain a bag.
		 *
		 * @param slot Absolute slot coordinates.
		 * @return Bag instance, or nullptr if no bag at slot.
		 */
		[[nodiscard]] std::shared_ptr<GameBagS> GetBag(
			InventorySlot slot) const noexcept;

		/**
		 * @brief Updates an item reference within a bag's slot fields.
		 *
		 * Sets the appropriate object field on the bag to reference the
		 * item's GUID, maintaining the bag's internal slot tracking.
		 *
		 * @param item The item to reference in the bag.
		 * @param bagSlot The bag's location (0-4 for Bag_1 through Bag_5).
		 * @param itemSlot The slot within the bag (0-based relative).
		 */
		void UpdateBagSlot(
			std::shared_ptr<GameItemS> item,
			uint8 bagSlot,
			uint8 itemSlot) const noexcept;

		/**
		 * @brief Calculates free slot change when equipping a bag.
		 *
		 * When a bag is equipped, the number of free slots increases by
		 * the bag's slot count. This method calculates that change.
		 *
		 * @param bag The bag being equipped.
		 * @return The number of slots to add to the free slot count.
		 */
		[[nodiscard]] int32 CalculateEquipBagSlotChange(
			const GameBagS& bag) const noexcept;

		/**
		 * @brief Calculates free slot change when unequipping a bag.
		 *
		 * When a bag is unequipped, the number of free slots decreases by
		 * the bag's slot count. This method calculates that change.
		 *
		 * @param bag The bag being unequipped.
		 * @return The number of slots to remove from the free slot count (negative).
		 */
		[[nodiscard]] int32 CalculateUnequipBagSlotChange(
			const GameBagS& bag) const noexcept;

		/**
		 * @brief Calculates net slot change when swapping bags.
		 *
		 * When swapping two bags, calculates the net change in free slots
		 * based on the difference in their slot counts.
		 *
		 * @param oldBag The bag being removed (can be nullptr).
		 * @param newBag The bag being equipped (can be nullptr).
		 * @return Net change in free slot count.
		 */
		[[nodiscard]] int32 CalculateSwapBagSlotChange(
			const GameBagS* oldBag,
			const GameBagS* newBag) const noexcept;

	private:
		/**
		 * @brief Converts bag slot to bag pack slot if necessary.
		 *
		 * Bag pack slots use format 0xFFXX where XX is the bag slot ID.
		 * This method ensures we always work with bag pack slot format.
		 *
		 * @param slot Absolute slot that might be a bag slot.
		 * @return Bag pack slot in 0xFFXX format.
		 */
		[[nodiscard]] uint16 ConvertToBagPackSlot(uint16 slot) const noexcept;

		/**
		 * @brief Checks if a slot is in bag pack slot format.
		 * @param slot Absolute slot coordinates.
		 * @return True if slot is bag pack slot (0xFFXX format).
		 */
		[[nodiscard]] static bool IsBagPackSlot(uint16 slot) noexcept;

	private:
		IBagManagerContext& m_context;
	};
}
