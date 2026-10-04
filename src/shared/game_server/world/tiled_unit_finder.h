// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "tile_index.h"
#include "unit_finder.h"
#include "base/grid.h"
#include "base/signal.h"

namespace mmo
{
	class TiledUnitFinder final : public UnitFinder
	{
	public:
		explicit TiledUnitFinder(float tileWidth);

	public:
		void AddUnit(GameUnitS& findable) override;
		void RemoveUnit(GameUnitS& findable) override;
		void UpdatePosition(GameUnitS& updated, const Vector3& previousPos) override;
		void FindUnits(const Circle& shape, const std::function<bool(GameUnitS&)>& resultHandler) override;
		std::unique_ptr<UnitWatcher> WatchUnits(const Circle& shape, std::function<bool(GameUnitS&, bool)> visibilityChanged) override;
		bool IsInBounds(const Vector3& position) const override;

	private:

		class TiledUnitWatcher;
		class Tile;

		struct UnitRecord
		{
			scoped_connection moved;
			Tile* lastTile;
		};

		typedef std::unordered_map<GameUnitS*, std::unique_ptr<UnitRecord>> UnitRecordsByIdentity;
		typedef Grid<std::unique_ptr<Tile>> TileGrid;

		TileGrid m_grid;
		UnitRecordsByIdentity m_units;
		const float m_tileWidth;

		Tile& GetTile(const TileIndex2D& position);

		//const Tile &getTile(const TileIndex2D &position) const;

		/// Maps one planar coordinate onto a tile index, clamped to [0, gridLength).
		/// @return false if the coordinate lies outside the grid or is not finite.
		bool ToTileIndex(float coordinate, size_t gridLength, TileIndex& outIndex) const;

		/// @return The tile index of a planar point, clamped to the grid.
		TileIndex2D GetTilePosition(const Vector<float, 2>& point) const;

		Tile& GetUnitsTile(const GameUnitS& findable);

		void OnUnitMoved(GameUnitS& findable);

		UnitRecord& RequireRecord(GameUnitS& findable);
	};
}
