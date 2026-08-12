// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/path_movement_utils.h"

#include <vector>

using namespace mmo;

namespace
{
	// Builds segment lengths the same way GameUnitC::SetMovementPath does.
	std::vector<float> BuildSegmentLengths(const Vector3& start, const std::vector<Vector3>& waypoints)
	{
		std::vector<float> lengths;
		Vector3 currentPos = start;
		for (const auto& waypoint : waypoints)
		{
			lengths.push_back((waypoint - currentPos).GetLength());
			currentPos = waypoint;
		}
		return lengths;
	}

	float TotalLength(const std::vector<float>& lengths)
	{
		float total = 0.0f;
		for (const float length : lengths)
		{
			total += length;
		}
		return total;
	}
}

TEST_CASE("path sampling: constant speed through collinear waypoints", "[path][sampling]")
{
	// Regression: NPC route paths contain many intermediate waypoints that lie on a
	// straight line. Sampling at equal distance steps must produce equal displacement
	// steps - any dip or spike shows up in game as movement stutter.
	const Vector3 start(0.0f, 0.0f, 0.0f);
	const std::vector<Vector3> waypoints = {
		Vector3(5.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(15.0f, 0.0f, 0.0f),
	};
	const auto lengths = BuildSegmentLengths(start, waypoints);

	constexpr float step = 0.1f;
	Vector3 previous = SamplePathPosition(start, waypoints, lengths, 0.0f);
	for (float distance = step; distance <= 15.0f; distance += step)
	{
		const Vector3 current = SamplePathPosition(start, waypoints, lengths, distance);
		const float displacement = (current - previous).GetLength();
		INFO("distance " << distance);
		CHECK(displacement == Approx(step).margin(0.005f));
		previous = current;
	}
}

TEST_CASE("path sampling: constant speed through a right-angle turn", "[path][sampling]")
{
	// Even at genuine turns the unit must keep moving at full speed: the server times the
	// path linearly, and any speed dip at a corner reads as a hitch on screen.
	const Vector3 start(0.0f, 0.0f, 0.0f);
	const std::vector<Vector3> waypoints = {
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 10.0f),
	};
	const auto lengths = BuildSegmentLengths(start, waypoints);

	constexpr float step = 0.1f;
	Vector3 previous = SamplePathPosition(start, waypoints, lengths, 0.0f);
	for (float distance = step; distance <= 20.0f; distance += step)
	{
		const Vector3 current = SamplePathPosition(start, waypoints, lengths, distance);
		const float displacement = (current - previous).GetLength();
		INFO("distance " << distance);
		// The sample crossing the corner itself may cut the corner slightly (two half
		// steps at an angle), but it must never stall or dash.
		CHECK(displacement >= step * 0.7f);
		CHECK(displacement <= step * 1.3f);
		previous = current;
	}
}

TEST_CASE("path sampling: exact positions at start, middle and end", "[path][sampling]")
{
	const Vector3 start(0.0f, 0.0f, 0.0f);
	const std::vector<Vector3> waypoints = {
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 10.0f),
	};
	const auto lengths = BuildSegmentLengths(start, waypoints);
	const float total = TotalLength(lengths);

	const Vector3 atStart = SamplePathPosition(start, waypoints, lengths, 0.0f);
	CHECK(atStart.x == Approx(0.0f).margin(0.001f));
	CHECK(atStart.z == Approx(0.0f).margin(0.001f));

	const Vector3 midFirstSegment = SamplePathPosition(start, waypoints, lengths, 5.0f);
	CHECK(midFirstSegment.x == Approx(5.0f).margin(0.001f));
	CHECK(midFirstSegment.z == Approx(0.0f).margin(0.001f));

	const Vector3 atEnd = SamplePathPosition(start, waypoints, lengths, total);
	CHECK(atEnd.x == Approx(10.0f).margin(0.001f));
	CHECK(atEnd.z == Approx(10.0f).margin(0.001f));

	// Beyond the end clamps to the destination.
	const Vector3 beyond = SamplePathPosition(start, waypoints, lengths, total + 100.0f);
	CHECK(beyond.x == Approx(10.0f).margin(0.001f));
	CHECK(beyond.z == Approx(10.0f).margin(0.001f));
}

TEST_CASE("path sampling: empty path returns the start position", "[path][sampling]")
{
	const Vector3 start(3.0f, 1.0f, -2.0f);
	const Vector3 pos = SamplePathPosition(start, {}, {}, 5.0f);
	CHECK(pos.x == Approx(3.0f).margin(0.001f));
	CHECK(pos.y == Approx(1.0f).margin(0.001f));
	CHECK(pos.z == Approx(-2.0f).margin(0.001f));
}

TEST_CASE("path sampling: zero-length segments are skipped", "[path][sampling]")
{
	const Vector3 start(0.0f, 0.0f, 0.0f);
	const std::vector<Vector3> waypoints = {
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f), // duplicate waypoint -> zero-length segment
		Vector3(10.0f, 0.0f, 10.0f),
	};
	const auto lengths = BuildSegmentLengths(start, waypoints);

	const Vector3 pos = SamplePathPosition(start, waypoints, lengths, 15.0f);
	CHECK(pos.x == Approx(10.0f).margin(0.001f));
	CHECK(pos.z == Approx(5.0f).margin(0.001f));
}
