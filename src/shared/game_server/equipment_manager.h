// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file equipment_manager.h
 *
 * @brief Domain service for equipment slot validation and effects.
 *
 * Encapsulates equipment-related business logic including slot compatibility
 * validation, stat application, visual updates, and special weapon rules.
 */

#pragma once

#include "i_equipment_manager_context.h"
#include "inventory_types.h"
#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	// Forward declarations
	namespace proto { class ItemEntry; }
	class GameItemS;

	/**
	 * @brief Domain service responsible for equipment management.
	 *
	 * EquipmentManager handles equipment-specific logic that was previously
	 * embedded in the Inventory class. Responsibilities include:
	 * - Validating item compatibility with equipment slots
	 * - Handling weapon/armor proficiency requirements
	 * - Managing dual wield and two-handed weapon constraints
	 * - Applying/removing item stats and set bonuses
	 * - Updating equipment visuals for client display
	 * - Applying Bind-on-Equip binding
	 *
	 * Design: Stateless service following Clean Architecture principles.
	 */
	class EquipmentManager final
	{
	public:
		/**
		 * @brief Constructs an EquipmentManager with required context.
		 * @param context Interface providing equipment dependencies.
		 */
		explicit EquipmentManager(IEquipmentManagerContext& context) noexcept;

	public:
		/**
		 * @brief Validates if an item can be equipped in a specific slot.
		 *
		 * Checks level requirements, proficiency, inventory type compatibility,
		 * and special weapon rules (dual wield, two-handed constraints).
		 *
		 * @param entry Item proto data.
		 * @param slot Equipment slot to validate.
		 * @return Success or specific failure reason.
		 */
		[[nodiscard]] InventoryResult<void> ValidateEquipment(
			const proto::ItemEntry& entry,
			InventorySlot slot) const noexcept;

		/**
		 * @brief Applies equipment effects when equipping an item.
		 *
		 * Handles stat application, visual updates, item set effects,
		 * and Bind-on-Equip binding. If replacing an existing item,
		 * removes effects from the old item first.
		 *
		 * @param newItem Item being equipped (must not be null).
		 * @param oldItem Item being replaced (nullptr if slot was empty).
		 * @param slot Equipment slot.
		 */
		void ApplyEquipmentEffects(
			std::shared_ptr<GameItemS> newItem,
			std::shared_ptr<GameItemS> oldItem,
			InventorySlot slot) noexcept;

		/**
		 * @brief Removes equipment effects when unequipping an item.
		 *
		 * Removes item stats, visual updates, and item set effects.
		 *
		 * @param item Item being unequipped (must not be null).
		 * @param slot Equipment slot.
		 */
		void RemoveEquipmentEffects(
			std::shared_ptr<GameItemS> item,
			InventorySlot slot) noexcept;

	private:
		/**
		 * @brief Validates item type matches the equipment slot.
		 * @param inventoryType Item's inventory type.
		 * @param slot Equipment slot.
		 * @return Success if compatible, failure otherwise.
		 */
		[[nodiscard]] InventoryResult<void> ValidateSlotCompatibility(
			inventory_type::Type inventoryType,
			InventorySlot slot) const noexcept;

		/**
		 * @brief Validates weapon/armor proficiency requirements.
		 * @param entry Item proto data.
		 * @return Success if proficient, failure otherwise.
		 */
		[[nodiscard]] InventoryResult<void> ValidateProficiency(
			const proto::ItemEntry& entry) const noexcept;

		/**
		 * @brief Validates two-handed weapon constraints.
		 * @param inventoryType Item's inventory type.
		 * @param slot Target equipment slot.
		 * @return Success if valid, failure otherwise.
		 */
		[[nodiscard]] InventoryResult<void> ValidateTwoHandedWeapon(
			inventory_type::Type inventoryType,
			InventorySlot slot) const noexcept;

		/**
		 * @brief Validates dual wield and offhand constraints.
		 * @param entry Item proto data.
		 * @param slot Target equipment slot.
		 * @return Success if valid, failure otherwise.
		 */
		[[nodiscard]] InventoryResult<void> ValidateOffhandWeapon(
			const proto::ItemEntry& entry,
			InventorySlot slot) const noexcept;

	private:
		IEquipmentManagerContext& m_context;
	};
}
