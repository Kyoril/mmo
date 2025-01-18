// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace terrain
	{
		namespace constants
		{
			constexpr uint32 PixelsPerTile = 64;
			constexpr uint32 VerticesPerTile = 18;
			constexpr uint32 TilesPerPage = 16;
			constexpr uint32 VerticesPerPage = (VerticesPerTile - 1) * TilesPerPage + 1;
			constexpr uint32 PixelsPerPage = (PixelsPerTile - 1) * TilesPerPage + 1;
			constexpr uint32 MaxPages = 64;
			constexpr uint32 MaxPagesSquared = MaxPages * MaxPages;
			constexpr double TileSize = 33.33333f;
			constexpr double PageSize = TileSize * TilesPerPage;
		}
	}
}