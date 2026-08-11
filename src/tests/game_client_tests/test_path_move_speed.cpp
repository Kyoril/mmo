// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/path_movement_utils.h"

using namespace mmo;

namespace
{
	/// Typical player run speed, used as the reference the bounds are relative to.
	constexpr float RunSpeed = 7.0f;
}

TEST_CASE("path move speed: a plausible duration is used as announced", "[path][speed]")
{
	SECTION("a charge covers 20 units in 571ms")
	{
		// The charge spell moves at 35 units/second - roughly five times run speed.
		const float speed = DerivePathMoveSpeed(20.0f, 571, RunSpeed);
		CHECK(speed == Approx(35.0f).epsilon(0.01));
	}

	SECTION("a patrol route walked at walk speed is not rejected")
	{
		// 1500 units at 2.5 units/second takes ten minutes. An absolute duration bound
		// would have thrown this away and run the NPC along its walk route.
		bool rejected = true;
		const float speed = DerivePathMoveSpeed(1500.0f, 600000, RunSpeed, &rejected);
		CHECK_FALSE(rejected);
		CHECK(speed == Approx(2.5f).epsilon(0.01));
	}
}

TEST_CASE("path move speed: a degenerate duration falls back to run speed", "[path][speed]")
{
	SECTION("an underflowed duration would otherwise freeze the unit forever")
	{
		// Regression: when the server's arrival timestamp ended up before its start
		// timestamp, the unsigned subtraction on the wire produced ~1.8e19 ms. The derived
		// speed is then ~0, the travelled distance never grows, the path never completes,
		// and a locally controlled player stays stuck until relog.
		bool rejected = false;
		const float speed = DerivePathMoveSpeed(20.0f, 18446744073709551000ull, RunSpeed, &rejected);
		CHECK(rejected);
		CHECK(speed == Approx(RunSpeed));
	}

	SECTION("a near-zero duration would otherwise teleport the unit")
	{
		bool rejected = false;
		const float speed = DerivePathMoveSpeed(20.0f, 3, RunSpeed, &rejected);
		CHECK(rejected);
		CHECK(speed == Approx(RunSpeed));
	}

	SECTION("a duration of zero is not a rejection, there is nothing to derive from")
	{
		bool rejected = true;
		const float speed = DerivePathMoveSpeed(20.0f, 0, RunSpeed, &rejected);
		CHECK_FALSE(rejected);
		CHECK(speed == Approx(RunSpeed));
	}

	SECTION("an empty path is not a rejection either")
	{
		bool rejected = true;
		const float speed = DerivePathMoveSpeed(0.0f, 571, RunSpeed, &rejected);
		CHECK_FALSE(rejected);
		CHECK(speed == Approx(RunSpeed));
	}
}

TEST_CASE("path move speed: the accepted band is where it is documented", "[path][speed]")
{
	// Pinned so the factors cannot drift without a test noticing. Values sit clearly
	// inside or outside the band rather than exactly on it: the announced duration is a
	// whole number of milliseconds, so a speed placed exactly on the bound lands on
	// either side of it depending on rounding.
	constexpr float length = 100.0f;

	const auto durationFor = [](const float speed)
	{
		return static_cast<GameTime>((length / speed) * 1000.0f + 0.5f);
	};

	SECTION("just inside the fast bound is accepted")
	{
		const float speed = RunSpeed * MaxPathSpeedFactor * 0.95f;
		bool rejected = true;
		CHECK(DerivePathMoveSpeed(length, durationFor(speed), RunSpeed, &rejected) == Approx(speed).epsilon(0.01));
		CHECK_FALSE(rejected);
	}

	SECTION("beyond the fast bound falls back to run speed")
	{
		const float speed = RunSpeed * MaxPathSpeedFactor * 1.5f;
		bool rejected = false;
		CHECK(DerivePathMoveSpeed(length, durationFor(speed), RunSpeed, &rejected) == Approx(RunSpeed));
		CHECK(rejected);
	}

	SECTION("just inside the slow bound is accepted")
	{
		const float speed = RunSpeed * MinPathSpeedFactor * 1.05f;
		bool rejected = true;
		CHECK(DerivePathMoveSpeed(length, durationFor(speed), RunSpeed, &rejected) == Approx(speed).epsilon(0.01));
		CHECK_FALSE(rejected);
	}

	SECTION("below the slow bound falls back to run speed")
	{
		const float speed = RunSpeed * MinPathSpeedFactor * 0.5f;
		bool rejected = false;
		CHECK(DerivePathMoveSpeed(length, durationFor(speed), RunSpeed, &rejected) == Approx(RunSpeed));
		CHECK(rejected);
	}
}

TEST_CASE("path move speed: without a reference speed the duration is trusted", "[path][speed]")
{
	// A unit whose run speed has not been replicated yet must not be handed a fallback of
	// zero - that would freeze it just as surely as the bug the bound exists to prevent.
	bool rejected = true;
	const float speed = DerivePathMoveSpeed(20.0f, 571, 0.0f, &rejected);
	CHECK_FALSE(rejected);
	CHECK(speed == Approx(35.0f).epsilon(0.01));
}
