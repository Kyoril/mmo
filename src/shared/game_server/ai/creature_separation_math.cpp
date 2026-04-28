// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_separation_math.h"

namespace mmo
{
	Vector3 CalculateSeparationForce(
		const Vector3& creaturePos,
		const Vector3& targetPos,
		float distanceThreshold,
		float forceMagnitude)
	{
		// Calculate the direction vector from target to creature
		const Vector3 direction = creaturePos - targetPos;
		const float distance = direction.GetLength();

		// If distance is zero or beyond threshold, no repulsion force
		if (distance >= distanceThreshold || distance < 1e-6f)
		{
			return Vector3::Zero;
		}

		// Normalize the direction to get the repulsion direction
		const Vector3 normalizedDirection = direction / distance;

		// Calculate force magnitude with linear falloff: (1.0 - distance/threshold)
		// This creates maximum force at distance 0 and zero force at threshold
		const float forceFactor = 1.0f - (distance / distanceThreshold);
		const float force = forceMagnitude * forceFactor;

		// Return the force vector pointing away from the target
		return normalizedDirection * force;
	}
}
