// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace terrain
	{
		/// @brief Liquid type stored per terrain tile.
		enum class WaterType : uint8
		{
			None   = 0,
			Water  = 1,
			Ocean  = 2,
			Lava   = 3,
			Slime  = 4,
			Count_
		};

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

			/// @brief Default ocean surface height in world units (see Terrain::GetSeaLevel).
			constexpr float DefaultSeaLevel = 0.0f;

			/// @brief How far below the sea level the editor places the flat seabed of an ocean
			///        placeholder page when a brush stroke first turns it into real terrain.
			/// @remark Deep enough that the shore foam (water_mesh::ShoreDepthEncodeRange, 8 units) and
			///         the ocean renderer's depth tint both read it as open water, shallow enough that
			///         raising an island out of it stays a short sculpt.
			constexpr float OceanSeabedDepth = 30.0f;
		}
	}
}