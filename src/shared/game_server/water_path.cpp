// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_path.h"

#include <algorithm>

namespace mmo
{
	bool IsSwimmableWaterAt(const Vector3& position, const WaterSurfaceQuery& waterQuery)
	{
		if (!waterQuery)
		{
			return false;
		}

		float surfaceY = 0.0f;
		if (!waterQuery(position.x, position.z, surfaceY))
		{
			return false;
		}

		return surfaceY - position.y >= swim::StartDepth;
	}

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
				if (i == 0)
				{
					// The unit already stands here, so it has to be allowed to walk out of the
					// water rather than be locked in place by it.
					continue;
				}

				// Water this deep is a wall for this unit. Its path was already routed around
				// deep water by DeepWaterQueryFilter, so this only catches what poly-granular
				// filtering let through: the unit walks up to the water and stops there.
				path.resize(i);

				result = WaterPathResult();
				result.truncated = true;
				result.blocked = path.size() < 2;
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
