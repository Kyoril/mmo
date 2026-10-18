// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot/bot_path_follower.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	BotPathFollower StartAlongX(const GameTime now)
	{
		BotPathFollower follower;
		follower.Start({ Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f), Vector3(20.0f, 0.0f, 0.0f) }, now);
		return follower;
	}
}

TEST_CASE("The follower skips waypoints within reach and steers at the next one", "[bot_path_follower]")
{
	BotPathFollower follower = StartAlongX(0);

	// The first waypoint is the start position itself.
	const BotPathStep first = follower.Advance(Vector3(0.0f, 0.0f, 0.0f), 0);
	REQUIRE(first.kind == BotPathStep::Steer);
	CHECK(first.steerTarget.x == Approx(10.0f));

	// Within the acceptance radius of the second waypoint: on to the third. Height is ignored.
	const BotPathStep second = follower.Advance(Vector3(9.5f, 3.0f, 0.0f), 100);
	REQUIRE(second.kind == BotPathStep::Steer);
	CHECK(second.steerTarget.x == Approx(20.0f));
	CHECK(follower.IsActive());
}

TEST_CASE("The follower finishes at the last waypoint", "[bot_path_follower]")
{
	BotPathFollower follower = StartAlongX(0);

	const BotPathStep step = follower.Advance(Vector3(19.2f, 0.0f, 0.0f), 100);
	CHECK(step.kind == BotPathStep::Finished);
	CHECK_FALSE(follower.IsActive());
	CHECK(follower.GetGoal().x == Approx(20.0f));
}

TEST_CASE("The follower reports stuck after a window without progress, and not before", "[bot_path_follower]")
{
	BotPathFollower follower = StartAlongX(1000);
	const Vector3 blocked(3.0f, 0.0f, 0.0f);

	CHECK(follower.Advance(blocked, 1000).kind == BotPathStep::Steer);
	CHECK(follower.Advance(Vector3(3.2f, 0.0f, 0.0f), 1000 + BotPathFollower::StuckWindowMs - 1).kind == BotPathStep::Steer);

	CHECK(follower.Advance(Vector3(3.3f, 0.0f, 0.0f), 1000 + BotPathFollower::StuckWindowMs).kind == BotPathStep::Stuck);
	CHECK_FALSE(follower.IsActive());
}

TEST_CASE("Slow but steady progress is not stuck", "[bot_path_follower]")
{
	BotPathFollower follower = StartAlongX(0);

	float x = 1.5f;
	for (GameTime now = 0; now <= 10 * BotPathFollower::StuckWindowMs; now += BotPathFollower::StuckWindowMs / 2)
	{
		REQUIRE(follower.Advance(Vector3(x, 0.0f, 0.0f), now).kind == BotPathStep::Steer);
		x += BotPathFollower::StuckDistance;
	}
}

TEST_CASE("Start resets the progress mark and Clear deactivates", "[bot_path_follower]")
{
	BotPathFollower follower = StartAlongX(0);
	CHECK(follower.Advance(Vector3(3.0f, 0.0f, 0.0f), 0).kind == BotPathStep::Steer);

	// A new path started late must not inherit the old path's progress window.
	follower.Start({ Vector3(3.0f, 0.0f, 0.0f), Vector3(3.0f, 0.0f, 10.0f) }, 5000);
	CHECK(follower.Advance(Vector3(3.0f, 0.0f, 0.0f), 5000).kind == BotPathStep::Steer);
	CHECK(follower.Advance(Vector3(3.0f, 0.0f, 0.1f), 5000 + BotPathFollower::StuckWindowMs - 1).kind == BotPathStep::Steer);

	follower.Clear();
	CHECK_FALSE(follower.IsActive());
}

TEST_CASE("An empty path is finished right away", "[bot_path_follower]")
{
	BotPathFollower follower;
	follower.Start({}, 0);
	CHECK_FALSE(follower.IsActive());
	CHECK(follower.Advance(Vector3(), 0).kind == BotPathStep::Finished);
}
