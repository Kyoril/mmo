// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file i_player_validator_context.h
 *
 * @brief Interface for player state access in validation context.
 *
 * Defines the minimal interface required by ItemValidator to validate
 * item operations. This abstraction enables testing without full game
 * world dependencies and follows the Dependency Inversion Principle.
 */

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/**
	 * @brief Interface providing player state for item validation.
	 *
	 * This interface defines only the properties required by ItemValidator,
	 * allowing it to remain decoupled from the full GamePlayerS implementation.
	 * This enables:
	 * - Testing with lightweight mocks
	 * - Clear contract of validation dependencies
	 * - Separation of concerns between domain logic and infrastructure
	 *
	 * Implementation note: This is a pure interface with no virtual destructor
	 * by design, as it's not meant to be deleted through a pointer to this type.
	 * Implementations should provide their own lifetime management.
	 */
	class IPlayerValidatorContext
	{
	public:
		/**
		 * @brief Gets the player's current level.
		 * @return Level value (typically 1-60 or similar range).
		 */
		virtual uint32 GetLevel() const noexcept = 0;

		/**
		 * @brief Gets the player's weapon proficiency flags.
		 * @return Bitfield of weapon_prof::Type values.
		 */
		virtual uint32 GetWeaponProficiency() const noexcept = 0;

		/**
		 * @brief Gets the player's armor proficiency flags.
		 * @return Bitfield of armor_prof::Type values.
		 */
		virtual uint32 GetArmorProficiency() const noexcept = 0;

		/**
		 * @brief Checks if the player is alive.
		 * @return true if health > 0, false otherwise.
		 */
		virtual bool IsAlive() const noexcept = 0;

		/**
		 * @brief Checks if the player is in combat.
		 * @return true if currently engaged in combat, false otherwise.
		 */
		virtual bool IsInCombat() const noexcept = 0;

		/**
		 * @brief Checks if the player can dual wield weapons.
		 * @return true if has dual wield capability, false otherwise.
		 */
		virtual bool CanDualWield() const noexcept = 0;

	protected:
		// Protected non-virtual destructor to prevent deletion through interface pointer
		// while allowing derived classes to be deleted normally
		~IPlayerValidatorContext() = default;
	};
}
