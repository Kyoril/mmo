// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

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
