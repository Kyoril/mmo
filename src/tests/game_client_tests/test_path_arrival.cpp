// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/path_movement_utils.h"

using namespace mmo;

// Thresholds mirror the values used by GameUnitC::UpdatePathMovement.
namespace
{
	constexpr float arrivalThreshold = 0.15f;
	constexpr float arrivalHeightTolerance = 3.5f;
}

TEST_CASE("path arrival: closed-loop path does not complete at its start", "[path][arrival]")
{
	// Regression: a patrol chain covering a full waypoint loop starts exactly at its own
	// final destination. On the first frame the unit stands 0 units from the destination
	// but has traveled almost nothing - the path must NOT count as completed, otherwise
	// the NPC freezes on the client while the server walks the whole loop.
	const float totalLength = 120.0f;
	const float finalSegmentLength = 10.0f;
	const float traveled = 0.05f;

	CHECK_FALSE(HasReachedPathDestination(
		traveled, totalLength, finalSegmentLength,
		0.0f,	// horizontally exactly on the destination
		0.0f,
		arrivalThreshold, arrivalHeightTolerance));
}

TEST_CASE("path arrival: self-crossing path does not complete mid-route", "[path][arrival]")
{
	// A route whose middle passes within the arrival threshold of the endpoint must keep
	// going until progress has actually entered the final segment.
	const float totalLength = 80.0f;
	const float finalSegmentLength = 5.0f;
	const float traveled = 40.0f; // mid-route, before the final segment starts at 75

	CHECK_FALSE(HasReachedPathDestination(
		traveled, totalLength, finalSegmentLength,
		0.1f,	// brushing past the destination
		0.0f,
		arrivalThreshold, arrivalHeightTolerance));
}

TEST_CASE("path arrival: completes near destination on the final segment", "[path][arrival]")
{
	const float totalLength = 120.0f;
	const float finalSegmentLength = 10.0f;
	const float traveled = 119.5f; // well inside the final segment

	CHECK(HasReachedPathDestination(
		traveled, totalLength, finalSegmentLength,
		0.1f,
		1.0f,
		arrivalThreshold, arrivalHeightTolerance));
}

TEST_CASE("path arrival: single-segment chase path completes immediately when close", "[path][arrival]")
{
	// A direct MoveTo (combat chase) has one segment: the whole path IS the final segment,
	// so proximity completion must work from the first frame (short chases can start
	// within the arrival threshold already).
	const float totalLength = 0.1f;
	const float finalSegmentLength = 0.1f;
	const float traveled = 0.0f;

	CHECK(HasReachedPathDestination(
		traveled, totalLength, finalSegmentLength,
		0.1f,
		0.0f,
		arrivalThreshold, arrivalHeightTolerance));
}

TEST_CASE("path arrival: not complete when horizontally too far", "[path][arrival]")
{
	CHECK_FALSE(HasReachedPathDestination(
		119.5f, 120.0f, 10.0f,
		0.5f,	// beyond the 0.15 arrival threshold
		0.0f,
		arrivalThreshold, arrivalHeightTolerance));
}

TEST_CASE("path arrival: not complete when height difference exceeds tolerance", "[path][arrival]")
{
	// Stacked geometry (e.g. staircase directly above): horizontal distance is tiny but
	// the unit is on the wrong floor.
	CHECK_FALSE(HasReachedPathDestination(
		119.5f, 120.0f, 10.0f,
		0.1f,
		4.0f,	// beyond the 3.5 height tolerance
		arrivalThreshold, arrivalHeightTolerance));
}

TEST_CASE("path arrival: zero-length path counts as arrived", "[path][arrival]")
{
	// Degenerate stop-here path: everything is zero, must complete so movement flags reset.
	CHECK(HasReachedPathDestination(
		0.0f, 0.0f, 0.0f,
		0.0f,
		0.0f,
		arrivalThreshold, arrivalHeightTolerance));
}
