// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "controls.h"

#include <array>

namespace mmo::pcg::graph
{
	/// @brief Smallest half extent (x or z) a rectangle control may have, in world units.
	inline constexpr float MinRectangleHalfExtent = 0.5f;

	/// @brief Corners of a rectangle control (yaw only; pitch and roll of the rotation are ignored).
	///        Order in local space: (-x,-z), (+x,-z), (+x,+z), (-x,+z), the same as a volume's footprint.
	///        Every corner has y = inst.position.y.
	std::array<SplinePoint, 4> RectangleCorners(const ControlInstance& inst);
}
