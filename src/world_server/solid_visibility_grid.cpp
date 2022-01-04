
#include "solid_visibility_grid.h"

#include "visibility_tile.h"
#include "game/constants.h"

namespace mmo
{
	
	namespace constants
	{
		static const size_t MapZonesInParallel = 16;
	}

	namespace
	{
		size_t GetVisibilityGridLength(size_t worldWidth, float tileWidth)
		{
			return std::max<size_t>(1,
			                        static_cast<size_t>(static_cast<float>(worldWidth) * constants::MapWidth / tileWidth));
		}
	}

	SolidVisibilityGrid::SolidVisibilityGrid(const TileIndex2D &worldSize)
		: VisibilityGrid()
		, m_tiles(GetVisibilityGridLength(worldSize[0], constants::MapWidth / 16.0f), GetVisibilityGridLength(worldSize[1], constants::MapWidth / 16.0f))
	{
	}

	SolidVisibilityGrid::~SolidVisibilityGrid() = default;

	VisibilityTile *SolidVisibilityGrid::GetTile(const TileIndex2D &position)
	{
		if ((position[0] >= 0) &&
		        (position[0] < static_cast<TileIndex>(m_tiles.width()) &&
		         (position[1] >= 0) &&
		         (position[1] < static_cast<TileIndex>(m_tiles.height()))))
		{
			auto &tile = m_tiles(position[0], position[1]);
			if (!tile)
			{
				tile = std::make_unique<VisibilityTile>();
				tile->SetPosition(position);
			}

			return tile.get();
		}

		return nullptr;
	}

	VisibilityTile &SolidVisibilityGrid::RequireTile(const TileIndex2D &position)
	{
		ASSERT(m_tiles.width());
		ASSERT(m_tiles.height());

		auto &tile = m_tiles(position[0], position[1]);
		if (!tile)
		{
			tile = std::make_unique<VisibilityTile>();
			tile->SetPosition(position);
		}

		return *tile;
	}
}
