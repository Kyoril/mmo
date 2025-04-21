#pragma once

#include "tiled_unit_finder.h"
#include "unit_watcher.h"

#include "base/signal.h"

namespace mmo
{
	class TiledUnitFinder::TiledUnitWatcher final : public UnitWatcher
	{
	public:

		explicit TiledUnitWatcher(const Circle& shape, TiledUnitFinder& finder, UnitWatcher::VisibilityChange visibilityChanged);
		~TiledUnitWatcher();

		void Start() override;

	private:

		typedef TiledUnitFinder::Tile Tile;
		typedef std::unordered_map<Tile*, connection> ConnectionsByTile;

		TiledUnitFinder& m_finder;
		Circle m_previousShape;
		ConnectionsByTile m_connections;

		TileArea GetTileIndexArea(const Circle& shape) const;
		bool WatchTile(Tile& tile);
		bool UnwatchTile(Tile& tile);
		void OnUnitMoved(GameUnitS& unit);
		bool UpdateTile(Tile& tile);
		void OnShapeUpdated() override;
	};
}
