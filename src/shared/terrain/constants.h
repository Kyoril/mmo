// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace terrain
	{
		namespace constants
		{
			// New terrain structure: each tile has 9x9 outer vertices + 8x8 inner vertices
			constexpr uint32 OuterVerticesPerTileSide = 9;
			constexpr uint32 InnerVerticesPerTileSide = 8;
			constexpr uint32 OuterVerticesPerTile = OuterVerticesPerTileSide * OuterVerticesPerTileSide; // 81
			constexpr uint32 InnerVerticesPerTile = InnerVerticesPerTileSide * InnerVerticesPerTileSide; // 64
			constexpr uint32 VerticesPerTile = OuterVerticesPerTile + InnerVerticesPerTile; // 145
			
			constexpr uint32 PixelsPerTile = 64;
			constexpr uint32 TilesPerPage = 16;
			
			// Page vertices: outer vertices are shared between tiles
			// For 16 tiles in a row: 16 * (9-1) + 1 = 129 outer vertices per side
			constexpr uint32 OuterVerticesPerPageSide = (OuterVerticesPerTileSide - 1) * TilesPerPage + 1;
			constexpr uint32 InnerVerticesPerPageSide = InnerVerticesPerTileSide * TilesPerPage;
			constexpr uint32 VerticesPerPage = OuterVerticesPerPageSide * OuterVerticesPerPageSide + InnerVerticesPerPageSide * InnerVerticesPerPageSide;
			
			constexpr uint32 PixelsPerPage = (PixelsPerTile - 1) * TilesPerPage + 1;
			constexpr uint32 MaxPages = 64;
			constexpr uint32 MaxPagesSquared = MaxPages * MaxPages;
			constexpr double TileSize = 33.33333f;
			constexpr double PageSize = TileSize * TilesPerPage;
		}
	}
}