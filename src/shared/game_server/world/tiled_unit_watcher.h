// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
		/// Whether Start() has run. No tile is watched before it does, so a shape update
		/// arriving earlier must not try to unwatch the tiles of the previous shape.
		bool m_started = false;

		TileArea GetTileIndexArea(const Circle& shape) const;
		void WatchTile(Tile& tile);
		void UnwatchTile(Tile& tile);
		void OnUnitMoved(GameUnitS& unit);
		void UpdateTile(Tile& tile);
		void OnShapeUpdated() override;
	};
}
