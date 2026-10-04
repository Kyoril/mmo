// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "visibility_grid.h"
#include "game/constants.h"

namespace mmo
{
	VisibilityGrid::VisibilityGrid() = default;

	VisibilityGrid::~VisibilityGrid() = default;

	namespace
	{
		constexpr int32 GridLength = 1024;

		/// Maps one world coordinate onto a grid index. A coordinate the grid cannot hold - out of
		/// range, infinite or NaN - is clamped to the nearest edge (0 for NaN) and reported as
		/// invalid, so a caller that ignores the result still gets an index it can safely use.
		bool ToGridIndex(const float coordinate, int32& outIndex)
		{
			const double index = floor(static_cast<double>(constants::MapWidth) - (static_cast<double>(coordinate) / 33.3333));

			// Written so that NaN fails this test too.
			if (!(index >= 0.0))
			{
				outIndex = 0;
				return false;
			}

			if (index >= static_cast<double>(GridLength))
			{
				outIndex = GridLength - 1;
				return false;
			}

			outIndex = static_cast<int32>(index);
			return true;
		}
	}

	bool VisibilityGrid::GetTilePosition(const Vector3& position, int32& outX, int32& outY) const
	{
		// Both axes are always written, so the output is a usable index even when this returns false.
		const bool validX = ToGridIndex(position.x, outX);
		const bool validY = ToGridIndex(position.z, outY);
		return validX && validY;
	}
}
