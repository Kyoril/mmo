// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "visibility_grid.h"
#include "base/grid.h"

#include <memory>

namespace mmo
{
	class SolidVisibilityGrid final : public VisibilityGrid
	{
	public:
		explicit SolidVisibilityGrid(const TileIndex2D &worldSize);
		~SolidVisibilityGrid() override;

		VisibilityTile *GetTile(const TileIndex2D &position) override;
		VisibilityTile &RequireTile(const TileIndex2D &position) override;

		void ForEachExistingTile(const std::function<void(VisibilityTile&)>& handler) override;

	private:

		typedef Grid<std::unique_ptr<VisibilityTile>> Tiles;

		Tiles m_tiles;
	};
}
