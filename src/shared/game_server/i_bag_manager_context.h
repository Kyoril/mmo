// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file i_bag_manager_context.h
 *
 * @brief Interface defining dependencies for bag management operations.
 *
 * Provides bag-related data access and notifications required by the
 * BagManager service. This interface follows the Dependency Inversion
 * Principle, allowing BagManager to remain independent of the Inventory
 * aggregate root.
 */

#pragma once

#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	// Forward declarations
	class GameItemS;
	class GameBagS;
	class InventorySlot;

	/**
	 * @brief Context interface for BagManager operations.
	 *
	 * This interface provides the minimal set of operations required
	 * for bag management without exposing the entire Inventory state.
	 * Implementations should be provided by the Inventory aggregate.
	 */
	class IBagManagerContext
	{
	public:
		virtual ~IBagManagerContext() = default;

		/**
		 * @brief Gets the item at a specific slot.
		 * @param slot The slot to check (absolute coordinates).
		 * @return Item at slot, or nullptr if slot is empty.
		 */
		[[nodiscard]] virtual std::shared_ptr<GameItemS> GetItemAtSlot(
			uint16 slot) const noexcept = 0;

		/**
		 * @brief Notifies that an item instance was updated.
		 * @param item The item that was updated.
		 * @param slot The slot where the item is located.
		 */
		virtual void NotifyItemUpdated(
			std::shared_ptr<GameItemS> item,
			uint16 slot) noexcept = 0;

		/**
		 * @brief Gets the owner's GUID for container field setup.
		 * @return The 64-bit GUID of the inventory owner.
		 */
		[[nodiscard]] virtual uint64 GetOwnerGuid() const noexcept = 0;
	};
}
