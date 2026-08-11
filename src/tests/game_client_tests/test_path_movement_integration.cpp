// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Integration-style traversal tests for the client path sampling used by
// GameUnitC::UpdatePathMovement. Basic sampling properties (constant speed,
// exact endpoints, degenerate paths) are covered in test_path_sampling.cpp;
// this file walks longer, more realistic multi-waypoint routes.

#include "catch.hpp"

#include "game_client/path_movement_utils.h"

#include <cmath>
#include <vector>

using namespace mmo;

namespace
{
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

TEST_CASE("path traversal: zigzag route with 90 degree corners", "[path][integration]")
{
	const Vector3 start(0.0f, 0.0f, 0.0f);
	const std::vector<Vector3> zigzag = {
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 10.0f),
		Vector3(0.0f, 0.0f, 10.0f),
		Vector3(0.0f, 0.0f, 20.0f),
		Vector3(10.0f, 0.0f, 20.0f),
		Vector3(10.0f, 0.0f, 30.0f),
	};
	const auto lengths = BuildSegmentLengths(start, zigzag);
	const float total = TotalLength(lengths);
	CHECK(total == Approx(60.0f).margin(0.01f));

	// Walk the whole route in equal steps: displacement per step must stay at step
	// size (within corner-cutting tolerance at the five 90-degree corners).
	constexpr float step = 0.25f;
	Vector3 previous = SamplePathPosition(start, zigzag, lengths, 0.0f);
	for (float distance = step; distance <= total; distance += step)
	{
		const Vector3 current = SamplePathPosition(start, zigzag, lengths, distance);
		const float displacement = (current - previous).GetLength();
		INFO("distance " << distance);
		CHECK(displacement >= step * 0.7f);
		CHECK(displacement <= step * 1.01f);
		previous = current;
	}

	// Route ends exactly on the final waypoint.
	const Vector3 end = SamplePathPosition(start, zigzag, lengths, total);
	CHECK(end.x == Approx(10.0f).margin(0.001f));
	CHECK(end.z == Approx(30.0f).margin(0.001f));
}

TEST_CASE("path traversal: closed patrol loop returns to its start", "[path][integration]")
{
	// Approximate a circular patrol with 8 waypoints; the route ends where it began,
	// like a full patrol chain built by the server for a loop with no wait times.
	constexpr float radius = 10.0f;
	std::vector<Vector3> loop;
	for (int i = 1; i <= 8; ++i)
	{
		const float angle = (i % 8) / 8.0f * 6.2831853f;
		loop.emplace_back(radius * std::cos(angle), 0.0f, radius * std::sin(angle));
	}

	const Vector3 start(radius, 0.0f, 0.0f); // matches the final waypoint
	const auto lengths = BuildSegmentLengths(start, loop);
	const float total = TotalLength(lengths);
	CHECK(total > 0.0f);

	// Every sample stays on the circle's rough radius (polyline chords cut inside).
	for (float distance = 0.0f; distance <= total; distance += total / 64.0f)
	{
		const Vector3 pos = SamplePathPosition(start, loop, lengths, distance);
		const float distFromCenter = std::sqrt(pos.x * pos.x + pos.z * pos.z);
		INFO("distance " << distance);
		CHECK(distFromCenter > radius * 0.9f);
		CHECK(distFromCenter < radius * 1.01f);
	}

	// The loop ends exactly at the start position.
	const Vector3 end = SamplePathPosition(start, loop, lengths, total);
	CHECK(end.x == Approx(start.x).margin(0.001f));
	CHECK(end.z == Approx(start.z).margin(0.001f));
}
