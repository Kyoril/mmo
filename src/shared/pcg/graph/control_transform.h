// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "controls.h"

#include "math/vector3.h"

namespace mmo::pcg::graph
{
	/// @brief Smallest scale factor ScaleControl applies.
	inline constexpr float MinControlScaleFactor = 0.01f;

	/// @brief Largest scale factor ScaleControl applies.
	inline constexpr float MaxControlScaleFactor = 100.0f;

	/// @brief Gets the pivot a whole-control transform turns and scales about.
	/// @details Spline shapes (the instance has spline points): the mean x, y and z of the points.
	///          Transform shapes (no spline points: volume, point, rectangle): the position.
	Vector3 ControlPivot(const ControlInstance& inst);

	/// @brief Moves a whole control by a world-space offset.
	/// @details Spline shapes move every point in x/z; the height follows the rule a single dragged point
	///          follows: curved (CatmullRom) splines move y too, Linear splines keep their heights. Transform
	///          shapes move their position (height included). A non-finite offset changes nothing.
	void TranslateControl(ControlInstance& inst, const Vector3& delta);

	/// @brief Turns a whole control about its pivot around world up.
	/// @details Uses the engine's yaw convention (Quaternion(yaw, +Y), which turns +X towards -Z), so the
	///          result matches turning a volume's rotation by the same yaw. Spline shapes turn the point
	///          offsets from the pivot and the tangent handles (inX/inZ, outX/outZ); heights and handle
	///          heights stay. Transform shapes pre-multiply their rotation. A non-finite yaw changes nothing.
	/// @param yawRadians Rotation about +Y, in radians.
	void RotateControl(ControlInstance& inst, float yawRadians);

	/// @brief Scales a whole control: spline shapes along world x and z, transform shapes along their local axes.
	/// @details Spline shapes (loops and open splines) scale the point offsets from the pivot and the tangent
	///          handles by the factors along world x and z; transform shapes (volume, point, rectangle) scale
	///          their local half extents x/z. A half extent that is above 0 before scaling is clamped to at least
	///          MinRectangleHalfExtent afterwards; a zero half extent (a point's unused extents) stays 0. Each
	///          factor is clamped to [MinControlScaleFactor, MaxControlScaleFactor]; a non-finite factor counts
	///          as 1. Point ids, nextPointId, point heights and widths never change.
	void ScaleControl(ControlInstance& inst, float scaleX, float scaleZ);
}
