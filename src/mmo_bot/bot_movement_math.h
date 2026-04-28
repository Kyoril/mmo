// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "math/radian.h"
#include "math/vector3.h"

#include <cstddef>
#include <vector>

namespace mmo
{
	[[nodiscard]] bool IsFiniteVector(const Vector3& value);
	[[nodiscard]] Vector3 FlattenToGround(const Vector3& value);
	[[nodiscard]] float PlanarDistanceSquared(const Vector3& a, const Vector3& b);
	[[nodiscard]] float PlanarDistance(const Vector3& a, const Vector3& b);
	[[nodiscard]] Vector3 SafeNormalizePlanar(const Vector3& value, const Vector3& fallback = Vector3::UnitZ);
	[[nodiscard]] Radian NormalizeFacing(const Radian& facing);
	[[nodiscard]] Radian ComputeFacingTo(const Vector3& from, const Vector3& to, const Radian& fallback = Radian(0.0f));
	[[nodiscard]] float SmallestAngleDelta(const Radian& a, const Radian& b);
	[[nodiscard]] bool IsDegenerateSegment(const Vector3& start, const Vector3& end, float epsilon = 1e-3f);
	[[nodiscard]] Vector3 TrimSegmentEnd(const Vector3& start, const Vector3& end, float trimDistance);
	[[nodiscard]] Vector3 ComputeSmoothedPathTarget(
		const std::vector<Vector3>& points,
		std::size_t currentIndex,
		float turnThresholdRadians,
		float maxTrimDistance);
}
