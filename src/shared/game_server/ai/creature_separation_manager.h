// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"
#include <vector>
#include <memory>

namespace mmo
{
	// Forward declaration
	class GameUnitS;

	/**
	 * @brief Manages creature separation logic to prevent stacking in combat.
	 * 
	 * This singleton class orchestrates separation forces for multiple creatures in combat,
	 * ensuring they maintain visible spacing (~2-3m apart) rather than stacking on each other.
	 * 
	 * The core function AdjustTargetForSeparation() iterates through nearby combat targets,
	 * calculates repulsion forces from each, sums them with a 2m cap, and returns an adjusted
	 * target position that spreads creatures out naturally.
	 */
	class CreatureSeparationManager
	{
	public:
		/**
		 * @brief Gets the singleton instance of the separation manager.
		 * @return Reference to the singleton instance.
		 */
		static CreatureSeparationManager& Get();

		/**
		 * @brief Adjusts a target position to account for separation from nearby creatures.
		 * 
		 * This method:
		 * 1. Iterates through the provided threat list to find nearby creatures
		 * 2. For each nearby creature, calculates repulsion force using CalculateSeparationForce()
		 * 3. Sums all forces and clamps the total offset to a maximum of 2 meters
		 * 4. Returns the adjusted target: targetPosition + separationOffset
		 * 
		 * @param creature The creature being repositioned (reference needed for logging)
		 * @param targetPosition The original target position to adjust
		 * @param threatList List of creatures nearby in combat (includes the creature itself)
		 * @return Adjusted target position that accounts for separation spacing
		 */
		[[nodiscard]] Vector3 AdjustTargetForSeparation(
			GameUnitS& creature,
			const Vector3& targetPosition,
			const std::vector<GameUnitS*>& threatList) const;

	private:
		/**
		 * @brief Private constructor for singleton pattern.
		 */
		CreatureSeparationManager() = default;

		/**
		 * @brief Maximum separation offset applied to target position (in meters).
		 * Prevents excessive repositioning when surrounded by many creatures.
		 */
		static constexpr float MAX_SEPARATION_OFFSET = 2.0f;

		/**
		 * @brief Distance threshold for applying separation forces (in meters).
		 * Creatures within this distance generate repulsion.
		 */
		static constexpr float SEPARATION_THRESHOLD = 3.0f;

		/**
		 * @brief Maximum force magnitude applied per creature (strength of repulsion).
		 */
		static constexpr float SEPARATION_FORCE_MAGNITUDE = 1.0f;
	};
}
