// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "visibility_grid.h"
#include "game/constants.h"

namespace mmo
{
	VisibilityGrid::VisibilityGrid() = default;

	VisibilityGrid::~VisibilityGrid() = default;

	bool VisibilityGrid::GetTilePosition(const Vector3& position, int32& outX, int32& outY) const
	{
		// Calculate grid x coordinates
		outX  = static_cast<int32>(floor((static_cast<double>(constants::MapWidth) - (static_cast<double>(position.x) / 33.3333))));
		if (outX < 0 || outX >= 1024) {
			return false;
		}

		// Calculate grid y coordinates
		outY = static_cast<int32>(floor((static_cast<double>(constants::MapWidth) - (static_cast<double>(position.z) / 33.3333))));
		if (outY < 0 || outY >= 1024) {
			return false;
		}

		return true;
	}
}
