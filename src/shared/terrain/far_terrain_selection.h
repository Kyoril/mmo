// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain/constants.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		/// @brief Which pages the distant terrain covers. Pure, so it compiles into the headless terrain_tests.
		namespace far_selection
		{
			struct PageCoord
			{
				uint32 x;
				uint32 z;

				bool operator==(const PageCoord& other) const { return x == other.x && z == other.z; }
			};

			/// @brief The page grid coordinate containing a world position, clamped to the map.
			inline PageCoord PageAt(const float worldX, const float worldZ)
			{
				const auto toPage = [](const float v)
				{
					const int32 page = static_cast<int32>(std::floor(v / static_cast<float>(constants::PageSize))) + static_cast<int32>(constants::MaxPages / 2);
					return static_cast<uint32>(std::clamp(page, 0, static_cast<int32>(constants::MaxPages) - 1));
				};

				return { toPage(worldX), toPage(worldZ) };
			}

			/// @brief Distance in the XZ plane from a world position to the nearest point of a page.
			inline float DistanceToPage(const float worldX, const float worldZ, const PageCoord page)
			{
				const float pageSize = static_cast<float>(constants::PageSize);
				const float minX = (static_cast<float>(page.x) - static_cast<float>(constants::MaxPages / 2)) * pageSize;
				const float minZ = (static_cast<float>(page.z) - static_cast<float>(constants::MaxPages / 2)) * pageSize;

				const float dx = std::max({ minX - worldX, 0.0f, worldX - (minX + pageSize) });
				const float dz = std::max({ minZ - worldZ, 0.0f, worldZ - (minZ + pageSize) });
				return std::sqrt(dx * dx + dz * dz);
			}

			/// @brief The view distance a far radius covers: pages starting within it are drawn.
			inline float CoveredDistance(const uint32 radius)
			{
				return static_cast<float>(radius) * static_cast<float>(constants::PageSize);
			}

			/// @brief Collects the pages the distant terrain should cover around a viewer.
			/// @details A page is selected when it lies within `radius` pages of the viewer's page on both
			///	         axes and its nearest point is within CoveredDistance(radius), so the covered area is
			///	         round rather than a square with far-reaching corners. Whether a page is replaced by its
			///	         full-resolution version is the caller's decision.
			inline void SelectPages(const float worldX, const float worldZ, const uint32 radius, std::vector<PageCoord>& out)
			{
				out.clear();
				if (radius == 0)
				{
					return;
				}

				const PageCoord center = PageAt(worldX, worldZ);
				const float maxDistance = CoveredDistance(radius);

				const int32 r = static_cast<int32>(radius);
				for (int32 dz = -r; dz <= r; ++dz)
				{
					for (int32 dx = -r; dx <= r; ++dx)
					{
						const int32 x = static_cast<int32>(center.x) + dx;
						const int32 z = static_cast<int32>(center.z) + dz;
						if (x < 0 || z < 0 || x >= static_cast<int32>(constants::MaxPages) || z >= static_cast<int32>(constants::MaxPages))
						{
							continue;
						}

						const PageCoord page{ static_cast<uint32>(x), static_cast<uint32>(z) };
						if (DistanceToPage(worldX, worldZ, page) <= maxDistance)
						{
							out.push_back(page);
						}
					}
				}
			}
		}
	}
}
