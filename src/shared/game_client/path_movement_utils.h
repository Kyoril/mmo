// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// Samples the position along a movement path at the given traveled distance.
	/// The path is the polyline pathStart -> waypoints[0] -> ... -> waypoints[N-1];
	/// segmentLengths[i] is the length of the segment ending at waypoints[i].
	/// Movement is sampled at constant speed to mirror the server's linear path timing.
	/// @param pathStart Position the path traversal started from.
	/// @param waypoints Path waypoints (the last one is the destination).
	/// @param segmentLengths Precomputed lengths per segment; same count as waypoints.
	/// @param distance Traveled distance along the path (clamped to [0, total length]).
	/// @return The interpolated position.
	inline Vector3 SamplePathPosition(
		const Vector3& pathStart,
		const std::vector<Vector3>& waypoints,
		const std::vector<float>& segmentLengths,
		const float distance)
	{
		if (waypoints.empty() || segmentLengths.empty())
		{
			return pathStart;
		}

		// Plain linear walk over the polyline: the server times paths at constant speed,
		// so any client-side speed modulation (like the former per-turn easing, which
		// remapped progress with zero-slope endpoints) desyncs from the server and shows
		// up as stop-and-go stutter at every waypoint.
		float remainingDistance = distance;
		Vector3 currentPos = pathStart;

		for (size_t i = 0; i < waypoints.size() && i < segmentLengths.size(); ++i)
		{
			const float segmentLength = segmentLengths[i];
			const Vector3 segmentEnd = waypoints[i];

			if (remainingDistance <= segmentLength)
			{
				if (segmentLength <= 0.0f)
				{
					return segmentEnd;
				}

				const float t = remainingDistance / segmentLength;
				return currentPos + (segmentEnd - currentPos) * t;
			}

			remainingDistance -= segmentLength;
			currentPos = segmentEnd;
		}

		return waypoints.back();
	}
	/// Lower bound on the speed a movement path may imply, as a factor of the unit's own
	/// run speed. A patrol route walked at walk speed sits around 0.35 of run speed, so
	/// this only rejects speeds that are effectively zero.
	constexpr float MinPathSpeedFactor = 0.01f;

	/// Upper bound on the speed a movement path may imply, as a factor of the unit's own
	/// run speed. The charge spell, the fastest server driven movement, sits around 5.
	constexpr float MaxPathSpeedFactor = 20.0f;

	/// Derives the speed at which a server-driven movement path has to be followed from
	/// the duration the server announced for it.
	///
	/// The announced duration is only trusted while it implies a plausible speed. A
	/// degenerate duration derives a speed that is either absurdly high, which makes the
	/// unit visually teleport to the destination, or effectively zero - and a near-zero
	/// speed is unrecoverable: the travelled distance never grows, so neither the arrival
	/// check nor the overshoot failsafe in the path update can ever complete the path, and
	/// a locally controlled player stays frozen under server movement control until relog.
	/// The bound is expressed relative to the unit's own run speed rather than as an
	/// absolute duration, because a single path may legitimately cover anything from a
	/// charge of a few hundred milliseconds to a patrol route of several minutes.
	///
	/// @param pathTotalLength Total length of the movement path in world units.
	/// @param moveTime Duration the server announced for the whole path, in milliseconds.
	/// @param runSpeed The unit's own run speed, used as the reference and as the fallback.
	/// @param outDurationRejected Set to true when the announced duration was discarded.
	/// @return The speed to follow the path with; runSpeed if the duration is not usable.
	inline float DerivePathMoveSpeed(
		const float pathTotalLength,
		const GameTime moveTime,
		const float runSpeed,
		bool* outDurationRejected = nullptr)
	{
		if (outDurationRejected)
		{
			*outDurationRejected = false;
		}

		// Nothing was announced (or there is no path to speak of): not a rejection, there
		// is simply nothing to derive a speed from.
		if (moveTime == 0 || pathTotalLength <= 0.0f)
		{
			return runSpeed;
		}

		const float impliedSpeed = pathTotalLength / (static_cast<float>(moveTime) / 1000.0f);
		if (!(impliedSpeed > 0.0f))
		{
			if (outDurationRejected)
			{
				*outDurationRejected = true;
			}

			return runSpeed;
		}

		// Without a usable reference speed there is nothing to validate against, so the
		// announced duration is taken at face value - it still beats falling back to a
		// run speed that is itself zero, which would freeze the unit outright.
		if (runSpeed <= 0.0f)
		{
			return impliedSpeed;
		}

		if (impliedSpeed < runSpeed * MinPathSpeedFactor ||
			impliedSpeed > runSpeed * MaxPathSpeedFactor)
		{
			if (outDurationRejected)
			{
				*outDurationRejected = true;
			}

			return runSpeed;
		}

		return impliedSpeed;
	}

	/// Decides whether a unit that follows a server-driven movement path has arrived at
	/// the final destination and may therefore complete the path.
	/// @param traveledPathDistance Distance the unit should have covered so far (movement speed * elapsed time).
	/// @param pathTotalLength Total length of the whole movement path.
	/// @param finalSegmentLength Length of the last path segment (0 if the path has a single segment covering everything).
	/// @param horizontalDistanceToDestination Horizontal (XZ) distance from the unit to the final destination.
	/// @param verticalDistanceToDestination Absolute height difference between the unit and the final destination.
	/// @param arrivalThreshold Maximum horizontal distance to count as arrived.
	/// @param arrivalHeightTolerance Maximum height difference to count as arrived (nav mesh height error).
	/// @return true if the unit counts as having reached the path destination.
	inline bool HasReachedPathDestination(
		const float traveledPathDistance,
		const float pathTotalLength,
		const float finalSegmentLength,
		const float horizontalDistanceToDestination,
		const float verticalDistanceToDestination,
		const float arrivalThreshold,
		const float arrivalHeightTolerance)
	{
		// Proximity to the destination only counts once travel progress has entered the
		// final path segment: a patrol chain that forms a closed loop starts exactly on
		// its own destination, and raw proximity would complete the whole path on the
		// first frame (NPC frozen on the client while the server walks the loop). The
		// same applies to routes whose middle passes close to their endpoint.
		if (traveledPathDistance < pathTotalLength - finalSegmentLength)
		{
			return false;
		}

		return horizontalDistanceToDestination <= arrivalThreshold &&
			verticalDistanceToDestination <= arrivalHeightTolerance;
	}
}
