// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_movement_math.h"

#include "math/math_utils.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		Vector3 MoveTowards(const Vector3& current, const Vector3& target, const float maxDistance)
		{
			Vector3 delta = target - current;
			const float distance = delta.GetLength();
			if (distance <= 1e-5f || distance <= maxDistance)
			{
				return target;
			}

			return current + (delta / distance) * maxDistance;
		}

		Vector3 ComputePlanarStepTarget(const Vector3& current, const Vector3& target, const float stepDistance)
		{
			const float planarDistance = PlanarDistance(current, target);
			if (planarDistance <= 1e-5f || planarDistance <= stepDistance)
			{
				return target;
			}

			const float t = stepDistance / planarDistance;
			return Vector3(
				current.x + (target.x - current.x) * t,
				current.y + (target.y - current.y) * t,
				current.z + (target.z - current.z) * t);
		}
	}

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

	Radian ComputeFacingTo(const Vector3& from, const Vector3& to, const Radian& fallback)
	{
		const Vector3 direction = FlattenToGround(to - from);
		if (direction.GetSquaredLength() <= 1e-6f)
		{
			return NormalizeFacingSigned(fallback);
		}

		return NormalizeFacingSigned(DirectionToFacing(direction));
	}

	float SmallestAngleDelta(const Radian& a, const Radian& b)
	{
		const float delta = NormalizeFacingSigned(a - b).GetValueRadians();
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

		const Radian normalizedFacing = NormalizeFacingSigned(anchorFacing);
		const Vector3 fallbackBehind = -FacingToDirection(normalizedFacing);
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

	BotPathFollowState AdvanceBotPathFollowing(
		const std::vector<Vector3>& points,
		const std::size_t waypointIndex,
		const Vector3& position,
		const float acceptanceRadius,
		const float turnThresholdRadians,
		const float maxTrimDistance)
	{
		BotPathFollowState state;
		state.waypointIndex = waypointIndex;

		while (state.waypointIndex < points.size())
		{
			const Vector3 steeringTarget = ComputeSmoothedPathTarget(
				points, state.waypointIndex, turnThresholdRadians, maxTrimDistance);
			if (PlanarDistance(position, steeringTarget) > acceptanceRadius)
			{
				state.steeringTarget = steeringTarget;
				return state;
			}

			++state.waypointIndex;
		}

		state.exhausted = true;
		state.steeringTarget = points.empty() ? position : points.back();
		return state;
	}

	BotLowLevelMovementOutput AdvanceBotLowLevelMovement(const BotLowLevelMovementInput& input)
	{
		BotLowLevelMovementOutput output;
		output.movement = input.movement;
		output.runtime = input.runtime;

		output.distanceToSteeringTarget = PlanarDistance(input.movement.position, input.steeringTarget);
		if (output.distanceToSteeringTarget <= input.acceptanceRadius)
		{
			output.runtime.velocity = Vector3::Zero;
			output.reachedSteeringTarget = true;
			return output;
		}

		const GameTime elapsedMs = input.now >= input.runtime.lastSimulationTime
			? input.now - input.runtime.lastSimulationTime
			: 0;
		if (elapsedMs == 0)
		{
			return output;
		}

		const float deltaSeconds = static_cast<float>(elapsedMs) / 1000.0f;
		const Vector3 direction = SafeNormalizePlanar(input.steeringTarget - input.movement.position);
		const Vector3 desiredVelocity = direction * std::max(0.0f, input.maxSpeed);
		const float maxVelocityDelta = std::max(0.0f, input.maxAcceleration) * deltaSeconds;
		output.runtime.velocity = MoveTowards(output.runtime.velocity, desiredVelocity, maxVelocityDelta);

		const float speed = FlattenToGround(output.runtime.velocity).GetLength();
		if (speed <= 1e-5f)
		{
			output.runtime.lastSimulationTime = input.now;
			return output;
		}

		const float maxStepDistance = speed * deltaSeconds;
		const Vector3 newPosition = ComputePlanarStepTarget(input.movement.position, input.steeringTarget, maxStepDistance);
		output.moved = PlanarDistanceSquared(input.movement.position, newPosition) > 1e-6f;
		output.movement.position = newPosition;
		output.movement.facing = ComputeFacingTo(input.movement.position, input.steeringTarget, input.movement.facing);
		output.movement.timestamp = input.now;
		output.runtime.lastSimulationTime = input.now;

		if (output.moved)
		{
			output.runtime.lastProgressPosition = output.movement.position;
			output.runtime.lastProgressTime = input.now;
			output.runtime.hasLastProgressPosition = true;
		}

		output.distanceToSteeringTarget = PlanarDistance(output.movement.position, input.steeringTarget);
		output.reachedSteeringTarget = output.distanceToSteeringTarget <= input.acceptanceRadius;
		return output;
	}
}
