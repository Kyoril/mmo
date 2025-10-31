// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file item_factory.h
 *
 * @brief Domain service for creating and initializing game items.
 *
 * Encapsulates item creation logic including instance creation,
 * GUID assignment, ownership setup, and binding rules.
 */

#pragma once

#include "i_item_factory_context.h"
#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	// Forward declarations
	namespace proto { class ItemEntry; }
	class GameItemS;
	class GameBagS;
	class InventorySlot;

	/**
	 * @brief Domain service responsible for item instance creation.
	 *
	 * ItemFactory handles the creation and initialization of game items,
	 * separating this concern from the main Inventory class. It manages:
	 * - Type-appropriate instance creation (GameItemS vs GameBagS)
	 * - GUID generation and assignment
	 * - Owner and container relationship setup
	 * - Item binding rule application
	 * - Stack count initialization
	 *
	 * Design: Stateless service following Clean Architecture principles.
	 * All operations are const and side-effect-free, only creating and
	 * returning configured item instances.
	 */
	class ItemFactory final
	{
	public:
		/**
		 * @brief Constructs an ItemFactory with required context.
		 * @param context Interface providing item creation dependencies.
		 */
		explicit ItemFactory(const IItemFactoryContext& context) noexcept;

	public:
		/**
		 * @brief Creates a new item instance with full initialization.
		 *
		 * Creates either GameItemS or GameBagS based on item class,
		 * initializes field map, assigns GUID, sets owner and container
		 * relationships, and applies binding rules.
		 *
		 * @param entry Item proto data defining item properties.
		 * @param slot Destination slot for container assignment.
		 * @param stackCount Initial stack count (default 1).
		 * @return Fully initialized item instance, never null.
		 */
		[[nodiscard]] std::shared_ptr<GameItemS> CreateItem(
			const proto::ItemEntry& entry,
			InventorySlot slot,
			uint16 stackCount = 1) const noexcept;

	private:
		/**
		 * @brief Creates the appropriate item type instance.
		 * @param entry Item proto data.
		 * @return Uninitialized item instance (GameItemS or GameBagS).
		 */
		[[nodiscard]] std::shared_ptr<GameItemS> CreateItemInstance(
			const proto::ItemEntry& entry) const noexcept;

		/**
		 * @brief Initializes item with GUID, owner, and container.
		 * @param item Item instance to initialize.
		 * @param entry Item proto data.
		 * @param slot Destination slot.
		 */
		void InitializeItemFields(
			std::shared_ptr<GameItemS>& item,
			const proto::ItemEntry& entry,
			InventorySlot slot) const noexcept;

		/**
		 * @brief Applies item binding rules based on entry properties.
		 * @param item Item instance to apply binding to.
		 * @param entry Item proto data containing binding rules.
		 */
		void ApplyBindingRules(
			std::shared_ptr<GameItemS>& item,
			const proto::ItemEntry& entry) const noexcept;

		/**
		 * @brief Sets up initial stack count for the item.
		 * @param item Item instance.
		 * @param stackCount Desired stack count (must be >= 1).
		 */
		void SetStackCount(
			std::shared_ptr<GameItemS>& item,
			uint16 stackCount) const noexcept;

	private:
		const IItemFactoryContext& m_context;
	};
}
