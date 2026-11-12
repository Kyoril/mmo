// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	/// @brief A party or guild member shown on the minimap and the world map.
	struct MapMemberDot
	{
		/// @brief World position: the spawned unit's, or the last one the realm reported.
		Vector3 position;

		String name;

		/// @brief Guild mate (green) rather than party member (blue).
		bool guild = false;
	};
}
