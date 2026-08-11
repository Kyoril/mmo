// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <algorithm>
#include <vector>

namespace mmo
{
	/// Computes the absolute arrival timestamp for every point of a movement path.
	///
	/// Absolute timestamps come from GetAsyncTimeMs(), which is the host's monotonic
	/// clock and therefore grows with machine uptime. A float carries only 24 significant
	/// bits, so once a timestamp exceeds ~2^24 ms (4.7 hours of uptime) it can no longer
	/// represent single milliseconds: adding a segment duration to a float-converted
	/// timestamp quantizes the result, and on a host that has been up for weeks the whole
	/// duration can be swallowed or even round backwards. Segment durations are therefore
	/// computed in double precision and accumulated as integer milliseconds - absolute
	/// timestamps must never take part in floating point arithmetic.
	///
	/// @param startTime Absolute timestamp at which the movement starts, in milliseconds.
	/// @param startPosition Position the movement starts from.
	/// @param path Path points to travel through; the last one is the destination.
	/// @param speed Movement speed in world units per second. Must be positive - a unit
	///        that cannot move must be refused by the caller, because a path whose
	///        arrival time equals its start time reads as "already arrived" downstream
	///        and teleports the unit onto its destination.
	/// @returns The absolute arrival timestamp per path point, one entry per path point;
	///          empty for an empty path, so callers taking back() must reject that first.
	inline std::vector<GameTime> BuildPathTimestamps(
		const GameTime startTime,
		const Vector3& startPosition,
		const std::vector<Vector3>& path,
		const float speed)
	{
		std::vector<GameTime> timestamps;
		timestamps.reserve(path.size());

		GameTime moveTime = startTime;
		for (size_t i = 0; i < path.size(); ++i)
		{
			const float distance = (i == 0)
				? (path[i] - startPosition).GetLength()
				: (path[i] - path[i - 1]).GetLength();

			if (speed > 0.0f && distance > 0.0f)
			{
				const double durationMs =
					(static_cast<double>(distance) / static_cast<double>(speed)) * 1000.0;

				// A vanishingly small speed produces a duration that does not fit into a
				// GameTime, and converting it would be undefined behaviour. Capping it keeps
				// the result a well defined (if useless) timestamp instead.
				constexpr double maxDurationMs = 30.0 * 24.0 * 60.0 * 60.0 * 1000.0;
				moveTime += static_cast<GameTime>(std::min(durationMs, maxDurationMs) + 0.5);
			}

			timestamps.push_back(moveTime);
		}

		return timestamps;
	}
}
