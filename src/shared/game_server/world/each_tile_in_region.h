// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "visibility_grid.h"
#include "visibility_tile.h"

namespace mmo
{
	template <class F>
	void ForEachTileInArea(
	    VisibilityGrid &grid,
	    const TileIndex2DPair &area,
	    const F &function)
	{
		const auto &topLeft = area[0];
		const auto &bottomRight = area[1];

		for (TileIndex y = topLeft.y(); y < bottomRight.y(); ++y)
		{
			for (TileIndex x = topLeft.x(); x < bottomRight.x(); ++x)
			{
				auto *const tile = grid.GetTile(TileIndex2D(x, y));
				if (tile)
				{
					function(*tile);
				}
			}
		}
	}

	template <class F>
	void ForEachTileInAreaWithout(
	    VisibilityGrid &grid,
	    const TileIndex2DPair &area,
	    const TileIndex2DPair &without,
	    const F &function)
	{
		ForEachTileInArea(grid, area,
		                  [&without, &function](VisibilityTile & tile)
		{
			if (!IsInside(tile.GetPosition(), without))
			{
				function(tile);
			}
		});
	}

	template <class F>
	void ForEachTileInRange(
	    VisibilityGrid &grid,
	    const TileIndex2D &center,
	    size_t range,
	    const F &function)
	{
		const auto area = GetTileAreaAround(center, range);
		ForEachTileInArea(
		    grid,
		    area,
		    function);
	}

	template <class F>
	void ForEachTileInSight(
	    VisibilityGrid &grid,
	    const TileIndex2D &center,
	    const F &function)
	{
		ForEachTileInRange(
		    grid,
		    center,
		    constants::PlayerZoneSight,
		    function);
	}
}
