// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "tile_area.h"
#include "math/vector3.h"


namespace mmo
{
	class VisibilityTile;

	class VisibilityGrid
	{
	public:
		explicit VisibilityGrid();
		virtual ~VisibilityGrid();

		/// Resolves the tile index of a world position.
		/// @param position The world position.
		/// @param outX Receives the tile column. Always a valid index: clamped to the grid edge if the position is outside.
		/// @param outY Receives the tile row. Always a valid index: clamped to the grid edge if the position is outside.
		/// @return false if the position lies outside the grid (or is not finite).
		bool GetTilePosition(const Vector3 &position, int32 &outX, int32 &outY) const;

		/// @return The tile at the given index, or nullptr if the index is outside the grid.
		virtual VisibilityTile* GetTile(const TileIndex2D &position) = 0;

		/// @return The tile at the given index. An index outside the grid is clamped to the nearest edge tile.
		virtual VisibilityTile& RequireTile(const TileIndex2D &position) = 0;
	};

	
	template <class Handler>
	void ForEachTileInArea(
	    VisibilityGrid &grid,
	    const TileArea &area,
	    const Handler &handler)
	{
		for (TileIndex z = area.topLeft[1]; z <= area.bottomRight[1]; ++z)
		{
			for (TileIndex x = area.topLeft[0]; x <= area.bottomRight[0]; ++x)
			{
				auto *const tile = grid.GetTile(TileIndex2D(x, z));
				if (tile)
				{
					handler(*tile);
				}
			}
		}
	}

	template <class OutputIterator>
	void CopyTilePtrsInArea(
	    VisibilityGrid &grid,
	    const TileArea &area,
	    OutputIterator &dest)
	{
		ForEachTileInArea(grid, area,
		                  [&dest](VisibilityTile & tile)
		{
			*dest = &tile;
			++dest;
		});
	}
}
