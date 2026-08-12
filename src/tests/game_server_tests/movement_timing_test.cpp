// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/movement_timing.h"

#include <algorithm>

using namespace mmo;

namespace
{
	/// One day expressed in milliseconds.
	constexpr GameTime OneDayMs = 24ull * 60ull * 60ull * 1000ull;

	/// A charge-like path: 20 world units split into the corners a nav mesh typically
	/// produces, travelled at the charge speed of 35 units per second.
	std::vector<Vector3> MakeChargePath()
	{
		return {
			Vector3(4.0f, 0.0f, 0.0f),
			Vector3(7.0f, 0.0f, 0.0f),
			Vector3(12.0f, 0.0f, 0.0f),
			Vector3(18.0f, 0.0f, 0.0f),
			Vector3(20.0f, 0.0f, 0.0f)
		};
	}

	constexpr float ChargeSpeed = 35.0f;
}

TEST_CASE("BuildPathTimestamps - durations do not depend on the absolute clock base", "[movement_timing]")
{
	// GetAsyncTimeMs() is the monotonic system clock, so its magnitude grows with the
	// uptime of the host. A production Linux host easily runs for weeks, while a
	// development machine is rebooted daily - which is exactly why this only ever
	// showed up in production.
	const std::vector<GameTime> clockBases = {
		0ull,
		OneDayMs,
		7ull * OneDayMs,
		14ull * OneDayMs,
		30ull * OneDayMs,
		60ull * OneDayMs,
		365ull * OneDayMs
	};

	const std::vector<Vector3> path = MakeChargePath();
	const Vector3 start(0.0f, 0.0f, 0.0f);

	const std::vector<GameTime> reference = BuildPathTimestamps(0, start, path, ChargeSpeed);
	REQUIRE(reference.size() == path.size());

	for (const GameTime base : clockBases)
	{
		const std::vector<GameTime> timestamps = BuildPathTimestamps(base, start, path, ChargeSpeed);
		REQUIRE(timestamps.size() == path.size());

		for (size_t i = 0; i < timestamps.size(); ++i)
		{
			INFO("clock base " << base << ", waypoint " << i);
			REQUIRE(timestamps[i] - base == reference[i]);
		}
	}
}

TEST_CASE("BuildPathTimestamps - a charge takes its full duration on a long-running host", "[movement_timing]")
{
	// 20 units at 35 units/second = 571ms. If the arrival time collapses towards the
	// start time the server's arrival timer fires immediately (the unit teleports) and
	// the client derives an absurd path speed from the transmitted duration.
	const std::vector<Vector3> path = MakeChargePath();
	const Vector3 start(0.0f, 0.0f, 0.0f);
	const GameTime expectedDuration = 571;

	for (GameTime days = 0; days <= 60; days += 5)
	{
		const GameTime base = days * OneDayMs + 137;
		const std::vector<GameTime> timestamps = BuildPathTimestamps(base, start, path, ChargeSpeed);

		INFO("host uptime " << days << " days");
		REQUIRE(timestamps.back() > base);
		REQUIRE(timestamps.back() - base >= expectedDuration - 1);
		REQUIRE(timestamps.back() - base <= expectedDuration + 1);
	}
}

TEST_CASE("BuildPathTimestamps - waypoint timestamps never move backwards", "[movement_timing]")
{
	// This is the invariant the helper guarantees for any input: time only ever advances.
	// The old float accumulation broke it at a large clock base, which is what let the
	// arrival timestamp land before the start timestamp.
	const Vector3 start(0.0f, 0.0f, 0.0f);

	SECTION("charge path at a large clock base")
	{
		const std::vector<GameTime> timestamps =
			BuildPathTimestamps(60ull * OneDayMs, start, MakeChargePath(), ChargeSpeed);
		REQUIRE(timestamps.size() == 5);
		REQUIRE(std::is_sorted(timestamps.begin(), timestamps.end()));

		// For a path whose segments are all comfortably above a millisecond of travel,
		// every waypoint gets its own timestamp - waypoints sharing one would collapse in
		// the mover's std::map keyed path and silently drop from the route.
		REQUIRE(std::adjacent_find(timestamps.begin(), timestamps.end()) == timestamps.end());
	}

	SECTION("near-duplicate points, as produced by concatenated nav mesh segments")
	{
		// Sub-millisecond segments legitimately share a timestamp; they must still never
		// go backwards. (Two points 0.001 units apart at 35 units/second are 0.03ms apart.)
		const std::vector<Vector3> path = {
			Vector3(5.0f, 0.0f, 0.0f),
			Vector3(5.001f, 0.0f, 0.0f),
			Vector3(5.002f, 0.0f, 0.0f),
			Vector3(12.0f, 0.0f, 0.0f)
		};

		const std::vector<GameTime> timestamps =
			BuildPathTimestamps(60ull * OneDayMs, start, path, ChargeSpeed);
		REQUIRE(timestamps.size() == path.size());
		REQUIRE(std::is_sorted(timestamps.begin(), timestamps.end()));
		REQUIRE(timestamps.back() > timestamps.front());
	}
}

TEST_CASE("BuildPathTimestamps - handles degenerate input without producing garbage", "[movement_timing]")
{
	const Vector3 start(0.0f, 0.0f, 0.0f);

	SECTION("empty path")
	{
		REQUIRE(BuildPathTimestamps(1234, start, {}, ChargeSpeed).empty());
	}

	SECTION("zero speed does not divide by zero")
	{
		// The result is the degenerate "everything arrives at the start time"; callers must
		// refuse a non-positive speed outright rather than schedule this (UnitMover does),
		// because downstream it reads as "already arrived" and teleports the unit.
		const std::vector<GameTime> timestamps = BuildPathTimestamps(1234, start, MakeChargePath(), 0.0f);
		REQUIRE(timestamps.size() == 5);
		for (const GameTime timestamp : timestamps)
		{
			REQUIRE(timestamp == 1234);
		}
	}

	SECTION("a vanishingly small speed stays a representable timestamp")
	{
		// Without a cap the duration overflows GameTime and the conversion is UB.
		const std::vector<Vector3> path = { Vector3(5.0f, 0.0f, 0.0f) };
		const std::vector<GameTime> timestamps = BuildPathTimestamps(1000, start, path, 1e-30f);
		REQUIRE(timestamps.size() == 1);
		REQUIRE(timestamps[0] > 1000);
		REQUIRE(timestamps[0] < 1000ull + 31ull * 24ull * 60ull * 60ull * 1000ull);
	}

	SECTION("a zero length segment does not move time backwards")
	{
		const std::vector<Vector3> path = { Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f) };
		const std::vector<GameTime> timestamps = BuildPathTimestamps(1000, start, path, ChargeSpeed);
		REQUIRE(timestamps.size() == 2);
		REQUIRE(timestamps[0] == 1000);
		REQUIRE(timestamps[1] > timestamps[0]);
	}
}
