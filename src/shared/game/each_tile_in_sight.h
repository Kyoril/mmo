#pragma once

#include "visibility_grid.h"
#include "visibility_tile.h"
#include "tile_subscriber.h"

#include <vector>

namespace mmo
{
	template <class T, class OnTile, class OnSubscriber>
	static void ForEachTileAndEachSubscriber(
	    T tilesBegin,
	    T tilesEnd,
	    const OnTile &onTile,
	    const OnSubscriber &onSubscriber)
	{
		for (T t = tilesBegin; t != tilesEnd; ++t)
		{
			auto &tile = *t;
			onTile(tile);

			for (auto *const subscriber : tile.GetWatchers())
			{
				onSubscriber(*subscriber);
			}
		}
	}
	
	static inline std::vector<VisibilityTile *> GetTilesInSight(
	    VisibilityGrid &grid,
	    const TileIndex2D &center)
	{
		std::vector<VisibilityTile *> tiles;

		for (TileIndex y = center.y() - constants::PlayerZoneSight;
		        y <= static_cast<TileIndex>(center.y() + constants::PlayerZoneSight); ++y)
		{
			for (TileIndex x = center.x() - constants::PlayerZoneSight;
			        x <= static_cast<TileIndex>(center.x() + constants::PlayerZoneSight); ++x)
			{
				auto *const tile = grid.GetTile(TileIndex2D(x, y));

				if (tile)
				{
					tiles.push_back(tile);
				}
			}
		}

		return tiles;
	}
	
	template <class T, class OnSubscriber>
	static void ForEachSubscriber(
	    T tilesBegin,
	    T tilesEnd,
	    const OnSubscriber &onSubscriber)
	{
		ForEachTileAndEachSubscriber(
		    tilesBegin,
		    tilesEnd,
		[](VisibilityTile &) {},
		onSubscriber);
	}
	
	template <class OnSubscriber>
	void ForEachSubscriberInSight(
	    VisibilityGrid &grid,
	    const TileIndex2D &center,
	    const OnSubscriber &onSubscriber)
	{
		const auto tiles = GetTilesInSight(grid, center);
		ForEachSubscriber(
		    tiles.begin(),
			tiles.end(),
		    onSubscriber);
	}
	
	static bool IsInSight(
	    const TileIndex2D &first,
	    const TileIndex2D &second)
	{
		const auto diff = abs(second - first);
		return
		    static_cast<size_t>(diff.x()) <= constants::PlayerZoneSight &&
			static_cast<size_t>(diff.y()) <= constants::PlayerZoneSight;
	}
	
	template <class OnTile>
	void ForEachTileInSightWithout(
	    VisibilityGrid &grid,
	    const TileIndex2D &center,
	    const TileIndex2D &excluded,
	    const OnTile &onTile
	)
	{
		for (TileIndex y = center.y() - constants::PlayerZoneSight;
		        y <= static_cast<TileIndex>(center.y() + constants::PlayerZoneSight); ++y)
		{
			for (TileIndex x = center.x() - constants::PlayerZoneSight;
			        x <= static_cast<TileIndex>(center.x() + constants::PlayerZoneSight); ++x)
			{
				auto *const tile = grid.GetTile(TileIndex2D(x, y));

				if (tile && !IsInSight(excluded, tile->GetPosition()))
				{
					onTile(*tile);
				}
			}
		}
	}
}
