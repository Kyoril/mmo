// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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

		bool GetTilePosition(const Vector3 &position, int32 &outX, int32 &outY) const;
		virtual VisibilityTile* GetTile(const TileIndex2D &position) = 0;
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
