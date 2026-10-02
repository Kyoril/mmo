// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/aabb.h"
#include "terrain/constants.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mmo
{
	/// @brief The page grid of a world, indexed [x][z]: true for every page that gets a navigation page.
	using NavPageGrid = bool[terrain::constants::MaxPages][terrain::constants::MaxPages];

	/// @brief Gets the page that contains a world-space x or z coordinate, clamped to the page grid. Page 32
	///	starts at the world origin, the same layout TerrainPage uses for its bounds.
	/// @param coordinate The world-space x or z coordinate.
	/// @returns The page index along that axis.
	inline int32 PageIndexForCoordinate(const double coordinate)
	{
		constexpr int32 originPage = static_cast<int32>(terrain::constants::MaxPages / 2);
		constexpr int32 lastPage = static_cast<int32>(terrain::constants::MaxPages) - 1;

		const int32 page = static_cast<int32>(std::floor(coordinate / terrain::constants::PageSize)) + originPage;
		return std::clamp(page, 0, lastPage);
	}

	/// @brief Marks every page whose XZ footprint overlaps one of the given bounds. A world without terrain has no
	///	.tile files to say where its pages are, so nav_builder builds the pages its placed geometry covers instead.
	/// @param bounds World-space bounds of the placed geometry.
	/// @param pages Receives true for every covered page. Entries no bounds cover are left untouched.
	/// @returns The number of pages that were not marked before.
	inline uint32 MarkPagesCoveredByBounds(const std::vector<AABB>& bounds, NavPageGrid& pages)
	{
		uint32 newlyMarked = 0;

		for (const AABB& box : bounds)
		{
			// Skips inverted boxes, and NaN or infinite ones (the comparisons are false for NaN, and casting a
			// non-finite value to an integer is undefined).
			const bool finite = std::isfinite(box.min.x) && std::isfinite(box.max.x) && std::isfinite(box.min.z) && std::isfinite(box.max.z);
			if (!finite || box.min.x > box.max.x || box.min.z > box.max.z)
			{
				continue;
			}

			const int32 minX = PageIndexForCoordinate(box.min.x);
			const int32 maxX = PageIndexForCoordinate(box.max.x);
			const int32 minZ = PageIndexForCoordinate(box.min.z);
			const int32 maxZ = PageIndexForCoordinate(box.max.z);

			for (int32 x = minX; x <= maxX; ++x)
			{
				for (int32 z = minZ; z <= maxZ; ++z)
				{
					if (!pages[x][z])
					{
						pages[x][z] = true;
						++newlyMarked;
					}
				}
			}
		}

		return newlyMarked;
	}
}
