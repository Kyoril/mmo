// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_movement_math.h"

#include "math/math_utils.h"

#include <algorithm>
#include <cmath>

namespace
{
	const float kTwoPi = mmo::Pi * 2.0f;
}

namespace mmo
{
	bool IsFiniteVector(const Vector3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	Vector3 FlattenToGround(const Vector3& value)
	{
		return Vector3(value.x, 0.0f, value.z);
	}

	float PlanarDistanceSquared(const Vector3& a, const Vector3& b)
	{
		const float dx = a.x - b.x;
		const float dz = a.z - b.z;
		return dx * dx + dz * dz;
	}

	float PlanarDistance(const Vector3& a, const Vector3& b)
	{
		return std::sqrt(PlanarDistanceSquared(a, b));
	}

	Vector3 SafeNormalizePlanar(const Vector3& value, const Vector3& fallback)
	{
		Vector3 planar = FlattenToGround(value);
		const float lengthSquared = planar.GetSquaredLength();
		if (lengthSquared <= 1e-6f)
		{
			Vector3 fallbackPlanar = FlattenToGround(fallback);
			if (fallbackPlanar.GetSquaredLength() <= 1e-6f)
			{
				return Vector3::UnitZ;
			}

			fallbackPlanar.Normalize();
			return fallbackPlanar;
		}

		planar /= std::sqrt(lengthSquared);
		return planar;
	}

	Radian NormalizeFacing(const Radian& facing)
	{
		float value = std::fmod(facing.GetValueRadians(), kTwoPi);
		if (value <= -Pi)
		{
			value += kTwoPi;
		}
		else if (value > Pi)
		{
			value -= kTwoPi;
		}

		return Radian(value);
	}

	Radian ComputeFacingTo(const Vector3& from, const Vector3& to, const Radian& fallback)
	{
		const Vector3 direction = FlattenToGround(to - from);
		if (direction.GetSquaredLength() <= 1e-6f)
		{
			return NormalizeFacing(fallback);
		}

		return NormalizeFacing(Radian(std::atan2(-direction.z, direction.x)));
	}

	float SmallestAngleDelta(const Radian& a, const Radian& b)
	{
		const float delta = NormalizeFacing(a - b).GetValueRadians();
		return std::fabs(delta);
	}

	bool IsDegenerateSegment(const Vector3& start, const Vector3& end, const float epsilon)
	{
		return PlanarDistanceSquared(start, end) <= epsilon * epsilon;
	}

	Vector3 TrimSegmentEnd(const Vector3& start, const Vector3& end, float trimDistance)
	{
		if (trimDistance <= 0.0f)
		{
			return end;
		}

		const Vector3 segment = FlattenToGround(end - start);
		const float length = segment.GetLength();
		if (length <= 1e-6f)
		{
			return end;
		}

		const float clampedTrim = std::min(trimDistance, length);
		const Vector3 direction = segment / length;
		return Vector3(end.x - direction.x * clampedTrim, end.y, end.z - direction.z * clampedTrim);
	}

	Vector3 ComputeFollowStandOffTarget(
		const Vector3& selfPosition,
		const Vector3& anchorPosition,
		const Radian& anchorFacing,
		const bool anchorHasFacing,
		const float desiredDistance)
	{
		if (desiredDistance <= 0.0f)
		{
			return anchorPosition;
		}

		const Radian normalizedFacing = NormalizeFacing(anchorFacing);
		const Vector3 fallbackBehind(
			-std::cos(normalizedFacing.GetValueRadians()),
			0.0f,
			std::sin(normalizedFacing.GetValueRadians()));
		const Vector3 offsetDirection = SafeNormalizePlanar(
			selfPosition - anchorPosition,
			anchorHasFacing ? fallbackBehind : Vector3::UnitZ);

		return Vector3(
			anchorPosition.x + offsetDirection.x * desiredDistance,
			anchorPosition.y,
			anchorPosition.z + offsetDirection.z * desiredDistance);
	}

	Vector3 ComputeSmoothedPathTarget(
		const std::vector<Vector3>& points,
		const std::size_t currentIndex,
		const float turnThresholdRadians,
		const float maxTrimDistance)
	{
		if (points.empty())
		{
			return Vector3::Zero;
		}

		if (currentIndex >= points.size())
		{
			return points.back();
		}

		const Vector3& current = points[currentIndex];
		if (maxTrimDistance <= 0.0f || currentIndex == 0 || currentIndex + 1 >= points.size())
		{
			return current;
		}

		const Vector3& previous = points[currentIndex - 1];
		const Vector3& next = points[currentIndex + 1];
		if (IsDegenerateSegment(previous, current) || IsDegenerateSegment(current, next))
		{
			return current;
		}

		const float turnAngle = ComputeSegmentAngle(previous, current, next);
		const float turnDeviation = std::max(0.0f, Pi - turnAngle);
		if (turnDeviation <= turnThresholdRadians)
		{
			return current;
		}

		const float normalizedTurn = std::min(1.0f, turnDeviation / Pi);
		const float easedTurn = EaseInOutCubic(normalizedTurn);
		const float trimDistance = maxTrimDistance * easedTurn;
		return TrimSegmentEnd(previous, current, trimDistance);
	}
}
