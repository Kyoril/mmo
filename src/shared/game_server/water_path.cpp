// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_path.h"

#include <algorithm>

namespace mmo
{
	WaterPathResult ApplyWaterToPath(std::vector<Vector3>& path, const MovementAbilityFlags movementAbilities,
		const WaterSurfaceQuery& waterQuery)
	{
		WaterPathResult result;

		if (!waterQuery || path.empty())
		{
			return result;
		}

		const bool canSwim = (movementAbilities & movement_ability::CanSwim) != 0;

		for (size_t i = 0; i < path.size(); ++i)
		{
			Vector3& point = path[i];

			float surfaceY = 0.0f;
			if (!waterQuery(point.x, point.z, surfaceY))
			{
				continue;
			}

			if (surfaceY - point.y < swim::StartDepth)
			{
				// Shallow enough to stand in: the unit wades through it on the ground.
				continue;
			}

			if (!canSwim)
			{
				// Water this deep is a wall for this unit. The path is left untouched, since
				// the caller has to refuse the movement entirely.
				result = WaterPathResult();
				result.blocked = true;
				return result;
			}

			result.swimming = true;
			if (i + 1 == path.size())
			{
				result.destinationSwimming = true;
			}

			// Float at the surface instead of walking along the bottom of the water body.
			point.y = std::max(point.y, surfaceY - swim::SurfaceMargin);
		}

		return result;
	}
}
