// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "world_server/anti_cheat_tracker.h"

using namespace mmo;

TEST_CASE("AntiCheatTracker - below threshold does not kick", "[anti_cheat]")
{
	AntiCheatTracker tracker;
	// 4 violations at 1-second intervals
	tracker.RecordViolation(0);
	tracker.RecordViolation(1000);
	tracker.RecordViolation(2000);
	tracker.RecordViolation(3000);
	REQUIRE_FALSE(tracker.ShouldKick(3000));
}

TEST_CASE("AntiCheatTracker - at threshold kicks", "[anti_cheat]")
{
	AntiCheatTracker tracker;
	// 5 violations at 1-second intervals
	tracker.RecordViolation(0);
	tracker.RecordViolation(1000);
	tracker.RecordViolation(2000);
	tracker.RecordViolation(3000);
	tracker.RecordViolation(4000);
	REQUIRE(tracker.ShouldKick(4000));
}

TEST_CASE("AntiCheatTracker - window pruning clears old violations", "[anti_cheat]")
{
	AntiCheatTracker tracker;
	// 5 violations at t=0..4000
	tracker.RecordViolation(0);
	tracker.RecordViolation(1000);
	tracker.RecordViolation(2000);
	tracker.RecordViolation(3000);
	tracker.RecordViolation(4000);
	// At t=15000 all are outside the 10-second window
	REQUIRE_FALSE(tracker.ShouldKick(15000));
}

TEST_CASE("AntiCheatTracker - no double-count; 4th returns false, 5th returns true", "[anti_cheat]")
{
	AntiCheatTracker tracker;
	// 4 violations
	tracker.RecordViolation(0);
	REQUIRE_FALSE(tracker.ShouldKick(0));
	tracker.RecordViolation(1000);
	REQUIRE_FALSE(tracker.ShouldKick(1000));
	tracker.RecordViolation(2000);
	REQUIRE_FALSE(tracker.ShouldKick(2000));
	tracker.RecordViolation(3000);
	REQUIRE_FALSE(tracker.ShouldKick(3000));
	// 5th violation — now should kick
	tracker.RecordViolation(4000);
	REQUIRE(tracker.ShouldKick(4000));
}
