// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file item_validator.h
 *
 * @brief Domain service for validating item operations.
 *
 * Extracts all validation logic from the Inventory class into a focused,
 * single-responsibility service following Clean Architecture principles.
 * This service encapsulates business rules for item usage, equipment,
 * and placement validation.
 */

#pragma once

#include "base/typedefs.h"
#include "game/item.h"
#include "inventory_types.h"
#include "i_player_validator_context.h"

namespace mmo
{
	class GameItemS;
	
	namespace proto
	{
		class ItemEntry;
	}

	/**
	 * @brief Domain service for validating item-related operations.
	 *
	 * This service centralizes all item validation logic, ensuring consistent
	 * business rules across the inventory system. It is stateless and depends
	 * only on the player's current state for validation.
	 *
	 * Responsibilities:
	 * - Validate item requirements (level, proficiency, etc.)
	 * - Validate slot compatibility
	 * - Validate equipment restrictions
	 * - Validate item count limits
	 */
	class ItemValidator final
	{
	public:
		/**
		 * @brief Constructs an item validator for a specific player.
		 * @param player The player whose constraints will be validated against.
		 */
		explicit ItemValidator(const IPlayerValidatorContext& player);

		/**
		 * @brief Validates if player can use/equip this item.
		 *
		 * Checks all item requirements:
		 * - Level requirements
		 * - Skill requirements
		 * - Proficiency (weapon/armor)
		 * - Class/race restrictions
		 *
		 * @param entry The item to validate.
		 * @return Success if all requirements met, failure with specific error otherwise.
		 */
		InventoryResult<void> ValidateItemRequirements(
			const proto::ItemEntry& entry) const;

		/**
		 * @brief Validates if an item can be placed in a specific slot.
		 *
		 * Performs comprehensive slot validation:
		 * - Equipment slot compatibility (head items in head slot, etc.)
		 * - Bag type restrictions (ammo in quiver, etc.)
		 * - Two-handed weapon restrictions
		 * - Dual wield capability
		 * - Bag placement rules
		 *
		 * @param slot The target slot.
		 * @param entry The item to place.
		 * @return Success if item can go in slot, failure with specific error otherwise.
		 */
		InventoryResult<void> ValidateSlotPlacement(
			InventorySlot slot,
			const proto::ItemEntry& entry) const;

		/**
		 * @brief Validates item count limits.
		 *
		 * Checks if adding the specified amount would exceed:
		 * - Per-item maximum count
		 * - Unique equipped restrictions
		 * - Free slot requirements
		 *
		 * @param entry The item type.
		 * @param amount Number to add.
		 * @param currentCount Current count player has.
		 * @param freeSlots Number of free inventory slots.
		 * @return Success if can add, failure with specific error otherwise.
		 */
		InventoryResult<void> ValidateItemLimits(
			const proto::ItemEntry& entry,
			uint16 amount,
			uint16 currentCount,
			uint16 freeSlots) const;

		/**
		 * @brief Validates if player can perform inventory operations in current state.
		 *
		 * Checks state conditions:
		 * - Not dead
		 * - Not stunned
		 * - Combat restrictions for equipment changes
		 *
		 * @param isEquipmentChange True if changing equipment.
		 * @return Success if can operate, failure with specific error otherwise.
		 */
		InventoryResult<void> ValidatePlayerState(
			bool isEquipmentChange = false) const;

		/**
		 * @brief Validates swap operation between two slots.
		 *
		 * Ensures both items can occupy each other's slots and validates
		 * special cases like bag swapping, equipment restrictions, etc.
		 *
		 * @param slotA First slot.
		 * @param slotB Second slot.
		 * @param itemA Item in first slot (can be null).
		 * @param itemB Item in second slot (can be null).
		 * @return Success if swap valid, failure with specific error otherwise.
		 */
		InventoryResult<void> ValidateSwap(
			InventorySlot slotA,
			InventorySlot slotB,
			const GameItemS* itemA,
			const GameItemS* itemB) const;

	private:
		const IPlayerValidatorContext& m_player;

		/**
		 * @brief Checks if player has required weapon proficiency.
		 */
		bool HasWeaponProficiency(const proto::ItemEntry& entry) const;

		/**
		 * @brief Checks if player has required armor proficiency.
		 */
		bool HasArmorProficiency(const proto::ItemEntry& entry) const;

		/**
		 * @brief Validates equipment slot compatibility.
		 */
		InventoryResult<void> ValidateEquipmentSlot(
			InventorySlot slot,
			const proto::ItemEntry& entry) const;

		/**
		 * @brief Validates bag slot compatibility.
		 */
		InventoryResult<void> ValidateBagSlot(
			InventorySlot slot,
			const proto::ItemEntry& entry) const;

		/**
		 * @brief Validates bag pack slot (where bags are equipped).
		 */
		InventoryResult<void> ValidateBagPackSlot(
			InventorySlot slot,
			const proto::ItemEntry& entry) const;

		/**
		 * @brief Checks two-handed weapon restrictions.
		 */
		InventoryResult<void> ValidateTwoHandedWeapon(
			InventorySlot slot,
			const proto::ItemEntry& entry) const;

		/**
		 * @brief Checks offhand weapon restrictions (dual wield, shields).
		 */
		InventoryResult<void> ValidateOffhandWeapon(
			InventorySlot slot,
			const proto::ItemEntry& entry) const;

		/**
		 * @brief Converts item subclass to weapon proficiency type.
		 */
		static weapon_prof::Type GetWeaponProficiency(uint32 subclass);

		/**
		 * @brief Converts item subclass to armor proficiency type.
		 */
		static armor_prof::Type GetArmorProficiency(uint32 subclass);
	};
}
