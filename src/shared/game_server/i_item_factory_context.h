// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file i_item_factory_context.h
 *
 * @brief Interface for item factory dependencies.
 *
 * Defines the minimal interface required by ItemFactory to create
 * and initialize item instances. This abstraction enables testing
 * without full game world dependencies.
 */

#pragma once

#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	// Forward declarations
	namespace proto { class Project; }
	class GameBagS;

	/**
	 * @brief Interface providing dependencies for item creation.
	 *
	 * This interface defines the properties and services required by
	 * ItemFactory to create properly initialized game items. It abstracts:
	 * - GUID generation for new items
	 * - Player/owner information
	 * - Project data access
	 * - Container (bag) lookups
	 */
	class IItemFactoryContext
	{
	public:
		/**
		 * @brief Generates a new unique ID for an item.
		 * @return Newly generated item ID.
		 */
		virtual uint64 GenerateItemId() const noexcept = 0;

		/**
		 * @brief Gets the owner's GUID.
		 * @return Player/owner GUID for item ownership.
		 */
		virtual uint64 GetOwnerGuid() const noexcept = 0;

		/**
		 * @brief Gets the proto::Project containing game data.
		 * @return Reference to project data.
		 */
		virtual const proto::Project& GetProject() const noexcept = 0;

		/**
		 * @brief Gets the bag at a specific slot for container assignment.
		 * @param slot The absolute slot index.
		 * @return Bag instance if present, nullptr otherwise.
		 */
		virtual std::shared_ptr<GameBagS> GetBagAtSlot(uint16 slot) const noexcept = 0;

	protected:
		~IItemFactoryContext() = default;
	};
}
