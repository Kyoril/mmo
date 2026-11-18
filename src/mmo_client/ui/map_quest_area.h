// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	/// @brief The objective area of an active quest, shown on the minimap and the world map.
	struct MapQuestArea
	{
		/// @brief Centre of the area in world coordinates (y is unused).
		Vector3 center;

		/// @brief Radius of the area in metres.
		float radius = 30.0f;

		/// @brief Quest title, shown when hovering the area.
		String title;

		uint32 questId = 0;
	};
}
