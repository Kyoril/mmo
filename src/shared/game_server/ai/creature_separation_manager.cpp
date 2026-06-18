// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_separation_manager.h"
#include "creature_separation_math.h"
#include "objects/game_unit_s.h"
#include "log/log.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		/// Produces a deterministic, anti-symmetric repulsion direction for two creatures that
		/// occupy (almost) the same spot. CalculateSeparationForce() returns a zero vector when
		/// the distance is ~0 — which is exactly the perfectly-stacked case we most need to break
		/// up. Deriving the direction from the unordered GUID pair (and flipping its sign by GUID
		/// order) guarantees both creatures pick the same axis but opposite ends of it, so they
		/// always slide apart instead of staying welded together.
		Vector3 DeterministicOverlapPush(uint64 creatureGuid, uint64 neighborGuid, float magnitude)
		{
			const uint64 lo = std::min(creatureGuid, neighborGuid);
			const uint64 hi = std::max(creatureGuid, neighborGuid);
			const uint64 hash = lo * 0x9E3779B97F4A7C15ull + hi;

			// Map the hash onto [0, 2*pi) to get a stable per-pair axis (2*pi ~= 6.28318).
			const float angle = static_cast<float>(hash % 628318u) / 100000.0f;
			const float sign = (creatureGuid < neighborGuid) ? 1.0f : -1.0f;

			return Vector3(std::cos(angle), 0.0f, std::sin(angle)) * (sign * magnitude);
		}
	}

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

			// Work on a flat (XZ) basis: combat separation is a ground-plane problem, and mixing
			// in height differences (e.g. a slope) would tilt the offset off the navmesh.
			const Vector3 threatPos = threatener->GetPosition();
			Vector3 flatDelta = creaturePos - threatPos;
			flatDelta.y = 0.0f;
			const float distance = flatDelta.GetLength();

			// Too far apart to matter.
			if (distance >= SEPARATION_THRESHOLD)
			{
				continue;
			}

			Vector3 repulsionForce;
			if (distance < OVERLAP_EPSILON)
			{
				// (Near-)perfect overlap: the analytic force is undefined here (zero direction),
				// so fall back to a deterministic push that guarantees the pair splits apart.
				repulsionForce = DeterministicOverlapPush(creatureGuid, threatener->GetGuid(), SEPARATION_FORCE_MAGNITUDE);
			}
			else
			{
				// Standard distance-based falloff. Recomputed on the flat delta so the offset
				// stays in the ground plane.
				const float forceFactor = 1.0f - (distance / SEPARATION_THRESHOLD);
				repulsionForce = (flatDelta / distance) * (SEPARATION_FORCE_MAGNITUDE * forceFactor);
			}

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

		// Return adjusted target position
		return targetPosition + adjustedOffset;
	}
}
