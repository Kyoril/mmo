// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/ai/unreachable_target.h"

using namespace mmo;

TEST_CASE("A chase path is short when it reaches neither its slot nor the victim, measured flat", "[unreachable_target]")
{
	const Vector3 victim(10.0f, 0.0f, 0.0f);
	const Vector3 slot(6.0f, 0.0f, 0.0f);
	constexpr float range = 5.0f;
	constexpr float acceptance = 0.5f;
	const auto shortOf = [&](const Vector3& end, const Vector3& victimAt)
	{
		return UnreachableTargetTracker::EndsShortOfVictim(end, slot, acceptance, victimAt, range);
	};

	// The path got to its slot
	CHECK_FALSE(shortOf(Vector3(6.4f, 0.0f, 0.0f), victim));

	// It stopped well short of the slot, but the victim is in reach from there anyway
	CHECK_FALSE(shortOf(Vector3(10.0f - range - UnreachableTargetTracker::Slack + 0.1f, 0.0f, 0.0f), victim));

	// It stopped short of both
	CHECK(shortOf(Vector3(10.0f - range - UnreachableTargetTracker::Slack - 0.1f, 0.0f, 0.0f), victim));

	// A victim on the run: the slot was planned ahead of it and the path got there, so it is not
	// unreachable even though it is now out of range of the path's end
	CHECK_FALSE(shortOf(Vector3(6.2f, 0.0f, 0.0f), Vector3(-2.0f, 0.0f, 0.0f)));

	// A victim on a ledge right above the path's end is within reach, as for melee range
	CHECK_FALSE(shortOf(Vector3(10.0f, -6.0f, 1.0f), victim));
}

TEST_CASE("A creature evades once its victim stayed unreachable long enough", "[unreachable_target]")
{
	UnreachableTargetTracker tracker;
	constexpr GameTime start = 100000;

	CHECK_FALSE(tracker.ShouldEvade(start));

	tracker.Update(true, start);
	CHECK_FALSE(tracker.ShouldEvade(start + UnreachableTargetTracker::EvadeAfter - 1));

	// Repeated short paths do not restart the streak
	tracker.Update(true, start + 2000);
	CHECK(tracker.ShouldEvade(start + UnreachableTargetTracker::EvadeAfter));
}

TEST_CASE("Reaching the victim once ends the unreachable streak", "[unreachable_target]")
{
	UnreachableTargetTracker tracker;
	constexpr GameTime start = 100000;

	tracker.Update(true, start);
	tracker.Update(false, start + 3000);
	tracker.Update(true, start + 4000);
	CHECK_FALSE(tracker.ShouldEvade(start + UnreachableTargetTracker::EvadeAfter + 1000));
	CHECK(tracker.ShouldEvade(start + 4000 + UnreachableTargetTracker::EvadeAfter));

	tracker.Reset();
	CHECK_FALSE(tracker.ShouldEvade(start + 100000));
}
