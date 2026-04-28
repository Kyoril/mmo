// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_separation_manager.h"
#include "creature_separation_math.h"
#include "objects/game_unit_s.h"
#include "log/log.h"

namespace mmo
{
	CreatureSeparationManager& CreatureSeparationManager::Get()
	{
		static CreatureSeparationManager instance;
		return instance;
	}

	Vector3 CreatureSeparationManager::AdjustTargetForSeparation(
		GameUnitS& creature,
		const Vector3& targetPosition,
		const std::vector<GameUnitS*>& threatList) const
	{
		// Start with zero separation offset
		Vector3 totalSeparationOffset = Vector3::Zero;

		// Get creature position for calculations
		const Vector3 creaturePos = creature.GetPosition();
		const uint64 creatureGuid = creature.GetGuid();

		// Iterate through threat targets to find nearby creatures and calculate repulsion
		int nearbyCreatureCount = 0;

		for (const GameUnitS* threatener : threatList)
		{
			// Skip the creature itself
			if (!threatener || threatener->GetGuid() == creatureGuid)
			{
				continue;
			}

			// Skip if threatener is not alive
			if (!threatener->IsAlive())
			{
				continue;
			}

			// Calculate repulsion force from this threat
			const Vector3 threatPos = threatener->GetPosition();
			const Vector3 repulsionForce = CalculateSeparationForce(
				creaturePos,
				threatPos,
				SEPARATION_THRESHOLD,
				SEPARATION_FORCE_MAGNITUDE);

			// If there's a non-zero repulsion force, we have a nearby creature generating separation
			if (repulsionForce.GetLength() > 1e-6f)
			{
				totalSeparationOffset += repulsionForce;
				nearbyCreatureCount++;
			}
		}

		// Clamp total offset to maximum separation distance to prevent excessive repositioning
		const float offsetMagnitude = totalSeparationOffset.GetLength();
		Vector3 adjustedOffset = totalSeparationOffset;

		if (offsetMagnitude > MAX_SEPARATION_OFFSET)
		{
			adjustedOffset = (totalSeparationOffset / offsetMagnitude) * MAX_SEPARATION_OFFSET;
		}

		// Log debug information about separation application
		if (offsetMagnitude > 1e-6f)
		{
			DLOG("Creature " << creatureGuid 
				<< ": separation offset=" << offsetMagnitude << "m (clamped to " << adjustedOffset.GetLength() << "m) "
				<< "from " << nearbyCreatureCount << " nearby creatures, "
				<< "original_target=(" << targetPosition.x << ", " << targetPosition.y << ", " << targetPosition.z << "), "
				<< "adjusted_target=(" << (targetPosition + adjustedOffset).x << ", " 
				<< (targetPosition + adjustedOffset).y << ", " 
				<< (targetPosition + adjustedOffset).z << ")");
		}

		// Return adjusted target position
		return targetPosition + adjustedOffset;
	}
}
