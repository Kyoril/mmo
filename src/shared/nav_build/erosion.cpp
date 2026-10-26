// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "erosion.h"

#include "Recast.h"

#include <algorithm>
#include <vector>

namespace mmo
{
	bool ErodeWalkableArea(rcContext& ctx, rcCompactHeightfield& chf, const int radius, const int narrowRadius, const uint8 narrowAreas)
	{
		if (narrowRadius >= radius || narrowAreas == 0)
		{
			return rcErodeWalkableArea(&ctx, radius, chf);
		}

		// Recast erodes every span by one radius, so erode twice and keep, for the narrow areas,
		// what the smaller radius left walkable.
		const std::vector<unsigned char> original(chf.areas, chf.areas + chf.spanCount);
		if (!rcErodeWalkableArea(&ctx, narrowRadius, chf))
		{
			return false;
		}

		const std::vector<unsigned char> narrow(chf.areas, chf.areas + chf.spanCount);
		std::copy(original.begin(), original.end(), chf.areas);
		if (!rcErodeWalkableArea(&ctx, radius, chf))
		{
			return false;
		}

		for (int i = 0; i < chf.spanCount; ++i)
		{
			if (chf.areas[i] == RC_NULL_AREA && (original[i] & narrowAreas) != 0)
			{
				chf.areas[i] = narrow[i];
			}
		}

		return true;
	}
}
