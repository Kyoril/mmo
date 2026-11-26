// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "traversal.h"

#include "math/math_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo
{
	namespace
	{
		float GetGlideDrain(const TraversalSettings& settings, const VigorActivity activity)
		{
			switch (activity)
			{
			case VigorActivity::Gliding:
				return settings.glideDrainPerSecond;
			case VigorActivity::GlidingInUpdraft:
				return settings.glideDrainInUpdraftPerSecond;
			default:
				return 0.0f;
			}
		}

		bool IsPointInSphere(const Vector3& center, const float radius, const Vector3& point)
		{
			return (point - center).GetSquaredLength() <= radius * radius;
		}

		// Same math as AreaTriggerManager::IsPointInBox.
		bool IsPointInBox(const Vector3& center, const Vector3& halfExtents, const float orientation, const Vector3& point)
		{
			Vector3 localPoint = point - center;

			if (std::abs(orientation) > 1e-6f)
			{
				const Vector3 axisX = FacingToDirection(Radian(orientation));
				const Vector3 axisZ(-axisX.z, 0.0f, axisX.x);

				const float rotatedX = localPoint.x * axisX.x + localPoint.z * axisX.z;
				const float rotatedZ = localPoint.x * axisZ.x + localPoint.z * axisZ.z;

				localPoint.x = rotatedX;
				localPoint.z = rotatedZ;
			}

			return std::abs(localPoint.x) <= halfExtents.x &&
				std::abs(localPoint.y) <= halfExtents.y &&
				std::abs(localPoint.z) <= halfExtents.z;
		}

		float MoveToward(const float current, const float target, const float maxDelta)
		{
			if (std::abs(target - current) <= maxDelta)
			{
				return target;
			}

			return current + (target > current ? maxDelta : -maxDelta);
		}
	}

	void AdvanceVigor(VigorState& state, const TraversalSettings& settings, const VigorActivity activity, const float extraDrainPerSecond, const bool inCombat, const bool grounded, const float seconds)
	{
		if (!(seconds > 0.0f))
		{
			return;
		}

		const float drain = GetGlideDrain(settings, activity) + extraDrainPerSecond;
		if (drain > 0.0f)
		{
			state.current -= drain * seconds;
			state.secondsSinceDrain = 0.0f;
		}
		else
		{
			if (grounded && !inCombat)
			{
				// Only the part of the step after the regen delay elapsed regenerates.
				const float delayLeft = std::max(0.0f, settings.regenDelaySeconds - state.secondsSinceDrain);
				const float regenSeconds = std::max(0.0f, seconds - delayLeft);
				state.current += settings.regenPerSecond * regenSeconds;
			}

			state.secondsSinceDrain += seconds;
		}

		state.current = state.max > 0.0f ? std::clamp(state.current, 0.0f, state.max) : 0.0f;
	}

	float SecondsUntilEmpty(const VigorState& state, const TraversalSettings& settings, const VigorActivity activity, const float extraDrainPerSecond)
	{
		const float drain = GetGlideDrain(settings, activity) + extraDrainPerSecond;
		if (drain <= 0.0f)
		{
			return std::numeric_limits<float>::infinity();
		}

		return std::max(0.0f, state.current) / drain;
	}

	float GetUpdraftStrength(const std::vector<UpdraftVolume>& volumes, const Vector3& position)
	{
		float strongest = 0.0f;
		for (const UpdraftVolume& volume : volumes)
		{
			const bool inside = volume.radius > 0.0f
				? IsPointInSphere(volume.center, volume.radius, position)
				: IsPointInBox(volume.center, volume.halfExtents, volume.orientation, position);
			if (inside)
			{
				strongest = std::max(strongest, volume.strength);
			}
		}

		return strongest;
	}

	Vector3 StepGlideVelocity(const Vector3& velocity, const Vector3& desiredHorizontalDir, const float updraftStrength, const TraversalSettings& settings, const float seconds)
	{
		Vector3 result = velocity;

		// Horizontal: full forward speed, turning toward the input at the turn rate, or decay to a
		// stop without input. From (nearly) standing still the glider takes the input direction.
		const float inputLength = std::sqrt(desiredHorizontalDir.x * desiredHorizontalDir.x + desiredHorizontalDir.z * desiredHorizontalDir.z);
		if (inputLength > 1e-6f)
		{
			const float inputAngle = std::atan2(desiredHorizontalDir.z, desiredHorizontalDir.x);
			float angle = inputAngle;

			const float currentSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
			if (currentSpeed >= 1.0f)
			{
				constexpr float pi = 3.14159265358979f;
				const float currentAngle = std::atan2(velocity.z, velocity.x);
				float delta = std::remainder(inputAngle - currentAngle, 2.0f * pi);

				// A (near) reversal always turns the same way, so the owner and the remote copies,
				// which derive the input with different rounding, do not turn in opposite directions.
				constexpr float reversalTolerance = 0.01f;
				if (std::abs(delta) > pi - reversalTolerance)
				{
					delta = std::abs(delta);
				}
				const float maxTurn = settings.glideTurnRate * seconds;
				if (std::abs(delta) > maxTurn)
				{
					angle = currentAngle + (delta > 0.0f ? maxTurn : -maxTurn);
				}
			}

			if (angle == inputAngle)
			{
				result.x = desiredHorizontalDir.x / inputLength * settings.glideForwardSpeed;
				result.z = desiredHorizontalDir.z / inputLength * settings.glideForwardSpeed;
			}
			else
			{
				result.x = std::cos(angle) * settings.glideForwardSpeed;
				result.z = std::sin(angle) * settings.glideForwardSpeed;
			}
		}
		else
		{
			const float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
			const float newSpeed = std::max(0.0f, speed - settings.glideVerticalAccel * seconds);
			const float scale = speed > 1e-6f ? newSpeed / speed : 0.0f;
			result.x = velocity.x * scale;
			result.z = velocity.z * scale;
		}

		// Vertical: approach the sink speed, or the updraft strength inside an updraft.
		const float targetY = updraftStrength > 0.0f ? updraftStrength : -settings.glideSinkSpeed;
		result.y = MoveToward(velocity.y, targetY, settings.glideVerticalAccel * seconds);

		return result;
	}
}
