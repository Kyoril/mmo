// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"

namespace mmo
{
	/// Calculates the separation repulsion force between two creatures.
	/// 
	/// @param creaturePos The position of the creature being pushed away
	/// @param targetPos The position of the target creature (source of repulsion)
	/// @param distanceThreshold The maximum distance at which repulsion is applied (in meters)
	/// @param forceMagnitude The maximum force magnitude when distance = 0 (strength multiplier)
	/// 
	/// @return A 3D repulsion vector pointing away from targetPos. Returns zero vector if:
	///         - Distance >= distanceThreshold
	///         - CreaturePos and targetPos are identical (zero direction)
	///
	/// Force calculation:
	///   - If distance < threshold:
	///     force = (creaturePos - targetPos).Normalized() * forceMagnitude * (1.0 - distance/threshold)
	///   - If distance >= threshold:
	///     force = zero vector
	[[nodiscard]] Vector3 CalculateSeparationForce(
		const Vector3& creaturePos,
		const Vector3& targetPos,
		float distanceThreshold,
		float forceMagnitude);
}
