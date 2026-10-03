// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/bug_report_rate_limiter.h"
#include "realm_server/realm_subsystem_table.h"

using namespace mmo;

TEST_CASE("BugReportRateLimiter enforces the minimum interval per character", "[bug_report]")
{
	BugReportRateLimiter limiter(30000, 20, 3600000);
	CHECK(limiter.TryConsume(1, 1000));
	CHECK_FALSE(limiter.TryConsume(1, 30999));
	CHECK(limiter.TryConsume(1, 31000));

	// Other characters are independent.
	CHECK(limiter.TryConsume(2, 31000));
}

TEST_CASE("BugReportRateLimiter caps reports per window", "[bug_report]")
{
	BugReportRateLimiter limiter(0, 3, 1000);
	CHECK(limiter.TryConsume(7, 0));
	CHECK(limiter.TryConsume(7, 10));
	CHECK(limiter.TryConsume(7, 20));
	CHECK_FALSE(limiter.TryConsume(7, 30));

	// The first report leaves the window at 1000.
	CHECK(limiter.TryConsume(7, 1000));
	CHECK_FALSE(limiter.TryConsume(7, 1001));
}

TEST_CASE("A rejected attempt does not count against the window", "[bug_report]")
{
	BugReportRateLimiter limiter(100, 2, 10000);
	CHECK(limiter.TryConsume(1, 0));
	CHECK_FALSE(limiter.TryConsume(1, 50));
	CHECK_FALSE(limiter.TryConsume(1, 60));
	CHECK(limiter.TryConsume(1, 100));
}

TEST_CASE("RealmSubsystemTable takes world-owned status from the node", "[subsystem]")
{
	RealmSubsystemTable table;

	const auto noWorld = table.Compose(nullptr);
	REQUIRE(noWorld.size() == game::subsystem::Count_);
	CHECK(noWorld[game::subsystem::BugReport].second == game::subsystem_status::Unavailable);

	auto worldStatus = MakeUnavailableSubsystemArray();
	worldStatus[game::subsystem::BugReport] = game::subsystem_status::Available;
	const auto onWorld = table.Compose(&worldStatus);
	CHECK(onWorld[game::subsystem::BugReport].first == game::subsystem::BugReport);
	CHECK(onWorld[game::subsystem::BugReport].second == game::subsystem_status::Available);
}

TEST_CASE("RealmSubsystemTable ignores realm toggles of world-owned subsystems", "[subsystem]")
{
	RealmSubsystemTable table;
	CHECK_FALSE(table.SetRealmOwned(game::subsystem::BugReport, false));
	CHECK_FALSE(table.SetRealmOwned(static_cast<game::Subsystem>(200), false));
}
