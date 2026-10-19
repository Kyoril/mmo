// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain/terrain_region_snapshot.h"

namespace mmo
{
	/// @brief A copied terrain region together with the world it was copied from.
	/// @details Owned by the WorldEditor rather than by a single world's terrain edit mode, so a
	///          region copied in one open world can be pasted into another. The snapshot holds
	///          only world independent data (heights, colours, holes, splat coverage, area IDs);
	///          the layer textures come from the destination tiles' materials.
	struct TerrainClipboard
	{
		/// @brief The captured region.
		terrain::TerrainRegionSnapshot snapshot;

		/// @brief Asset path of the world the region was copied from.
		String sourceWorld;
	};
}
