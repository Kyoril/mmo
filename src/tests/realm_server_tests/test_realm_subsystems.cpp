// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/realm_subsystem_table.h"

using namespace mmo;

TEST_CASE("FRIENDS is a realm-owned subsystem", "[subsystem]")
{
	game::Subsystem id;
	REQUIRE(game::FindSubsystemByName("FRIENDS", id));
	CHECK(id == game::subsystem::Friends);
	CHECK(game::GetSubsystemOwner(id) == game::subsystem_owner::Realm);
	CHECK(std::string(game::GetSubsystemName(game::subsystem::Friends)) == "FRIENDS");
}

TEST_CASE("Every subsystem table row sits at the index of its id", "[subsystem]")
{
	for (uint8 i = 0; i < game::subsystem::Count_; ++i)
	{
		CHECK(game::SubsystemTable[i].id == i);
	}
}

TEST_CASE("RealmSubsystemTable decides realm-owned availability without a world node", "[subsystem]")
{
	RealmSubsystemTable table;

	CHECK(table.IsAvailable(game::subsystem::Friends, nullptr));

	REQUIRE(table.SetRealmOwned(game::subsystem::Friends, false));
	CHECK_FALSE(table.IsAvailable(game::subsystem::Friends, nullptr));

	// A world node's report never overrides the realm's switch
	auto worldStatus = MakeUnavailableSubsystemArray();
	worldStatus[game::subsystem::Friends] = game::subsystem_status::Available;
	CHECK_FALSE(table.IsAvailable(game::subsystem::Friends, &worldStatus));

	CHECK(table.SetRealmOwned(game::subsystem::Friends, true));
	CHECK(table.IsAvailable(game::subsystem::Friends, nullptr));
}

TEST_CASE("RealmSubsystemTable takes world-owned availability from the player's node", "[subsystem]")
{
	RealmSubsystemTable table;

	CHECK_FALSE(table.IsAvailable(game::subsystem::BugReport, nullptr));

	auto worldStatus = MakeUnavailableSubsystemArray();
	CHECK_FALSE(table.IsAvailable(game::subsystem::BugReport, &worldStatus));

	worldStatus[game::subsystem::BugReport] = game::subsystem_status::Available;
	CHECK(table.IsAvailable(game::subsystem::BugReport, &worldStatus));
}

TEST_CASE("RealmSubsystemTable treats unknown subsystems as unavailable", "[subsystem]")
{
	const RealmSubsystemTable table;
	auto worldStatus = MakeUnavailableSubsystemArray();
	worldStatus.fill(game::subsystem_status::Available);

	CHECK_FALSE(table.IsAvailable(game::subsystem::Count_, &worldStatus));
	CHECK_FALSE(table.IsAvailable(static_cast<game::Subsystem>(200), &worldStatus));
}

TEST_CASE("RealmSubsystemTable composes the realm's switch for realm-owned subsystems", "[subsystem]")
{
	RealmSubsystemTable table;
	table.SetRealmOwned(game::subsystem::Friends, false);

	auto worldStatus = MakeUnavailableSubsystemArray();
	worldStatus.fill(game::subsystem_status::Available);

	const auto list = table.Compose(&worldStatus);
	REQUIRE(list.size() == game::subsystem::Count_);
	CHECK(list[game::subsystem::Friends].first == game::subsystem::Friends);
	CHECK(list[game::subsystem::Friends].second == game::subsystem_status::Unavailable);
	CHECK(list[game::subsystem::BugReport].second == game::subsystem_status::Available);
}
