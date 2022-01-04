#pragma once

#include "tile_index.h"

namespace mmo
{
	namespace constants
	{
		static const size_t PlayerZoneSight = 2;
		static const size_t PlayerScopeWidth = 1 + 2 * PlayerZoneSight;
		static const size_t PlayerScopeAreaCount = PlayerScopeWidth * PlayerScopeWidth;
		static const size_t PlayerScopeSurroundingAreaCount = PlayerScopeAreaCount - 1;
	}

	class TileArea
	{
	public:
		TileIndex2D topLeft, bottomRight;

		TileArea() = default;

		explicit TileArea(const TileIndex2D &topLeft, const TileIndex2D &bottomRight)
			: topLeft(topLeft)
			, bottomRight(bottomRight)
		{
		}

		bool IsInside(const TileIndex2D &point) const
		{
			return
			    (point.x() >= topLeft.x()) &&
			    (point.y() >= topLeft.y()) &&
			    (point.x() <= bottomRight.x()) &&
			    (point.y() <= bottomRight.y());
		}
	};

	inline TileArea GetSightArea(const TileIndex2D &center)
	{
		static const TileIndex2D Sight(constants::PlayerZoneSight, constants::PlayerZoneSight);

		return TileArea(
		           center - Sight,
		           center + Sight);
	}
}
