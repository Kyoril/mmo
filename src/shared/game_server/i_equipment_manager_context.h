// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file i_equipment_manager_context.h
 *
 * @brief Interface for equipment manager dependencies.
 *
 * Defines the minimal interface required by EquipmentManager to handle
 * equipment operations. This abstraction enables testing without full
 * game world dependencies.
 */

#pragma once

#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	// Forward declarations
	namespace proto { class ItemEntry; }
	class GameItemS;

	/**
	 * @brief Interface providing dependencies for equipment management.
	 *
	 * This interface defines the properties and operations required by
	 * EquipmentManager to validate equipment placement, apply item stats,
	 * and handle visual updates.
	 */
	class IEquipmentManagerContext
	{
	public:
		/**
		 * @brief Gets the player's current level.
		 * @return Level value for requirement validation.
		 */
		virtual uint32 GetLevel() const noexcept = 0;

		/**
		 * @brief Gets the player's weapon proficiency flags.
		 * @return Bitfield of weapon proficiencies.
		 */
		virtual uint32 GetWeaponProficiency() const noexcept = 0;

		/**
		 * @brief Gets the player's armor proficiency flags.
		 * @return Bitfield of armor proficiencies.
		 */
		virtual uint32 GetArmorProficiency() const noexcept = 0;

		/**
		 * @brief Checks if the player can dual wield weapons.
		 * @return true if dual wield is allowed, false otherwise.
		 */
		virtual bool CanDualWield() const noexcept = 0;

		/**
		 * @brief Gets an item at a specific equipment slot.
		 * @param slot Absolute slot index.
		 * @return Item instance if present, nullptr otherwise.
		 */
		virtual std::shared_ptr<GameItemS> GetItemAtSlot(uint16 slot) const noexcept = 0;

		/**
		 * @brief Applies or removes item stats from the player.
		 * @param item Item to apply/remove stats from.
		 * @param apply true to apply stats, false to remove.
		 */
		virtual void ApplyItemStats(const GameItemS& item, bool apply) noexcept = 0;

		/**
		 * @brief Updates equipment visual field for client display.
		 * @param equipSlot Equipment slot index (0-18).
		 * @param entryId Item entry ID (0 for empty slot).
		 * @param creatorGuid Item creator GUID (0 for empty slot).
		 */
		virtual void UpdateEquipmentVisual(
			uint8 equipSlot,
			uint32 entryId,
			uint64 creatorGuid) noexcept = 0;

		/**
		 * @brief Handles item set effects when equipping/unequipping.
		 * @param itemSetId Item set identifier.
		 * @param equipped true when equipping, false when unequipping.
		 */
		virtual void HandleItemSetEffect(uint32 itemSetId, bool equipped) noexcept = 0;

	protected:
		~IEquipmentManagerContext() = default;
	};
}
