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

	/// @brief The editing handles of a rectangle control: one in the middle of each side and one per corner.
	///        Names refer to the rectangle's local axes (x, z), signs to the side of the centre.
	enum class RectangleHandle : uint8
	{
		SideNegX,
		SidePosX,
		SideNegZ,
		SidePosZ,
		CornerNegNeg,
		CornerPosNeg,
		CornerPosPos,
		CornerNegPos
	};

	/// @brief World position of a rectangle handle: a side handle sits at the side's midpoint, a corner handle on
	///        the corner. Yaw only, like RectangleCorners; y = inst.position.y.
	Vector3 RectangleHandlePosition(const ControlInstance& inst, RectangleHandle handle);

	/// @brief Drags a rectangle handle to the cursor at (worldX, worldZ). The handle's side (or both sides of a corner)
	///        moves to the cursor along the rectangle's own axes while the opposite side (or corner) stays where it is,
	///        so position and half extents change together. Each half extent is clamped to MinRectangleHalfExtent.
	///        Rotation, position.y and halfExtents.y are kept. A non-finite cursor leaves inst unchanged.
	void DragRectangleHandle(ControlInstance& inst, RectangleHandle handle, float worldX, float worldZ);
}
