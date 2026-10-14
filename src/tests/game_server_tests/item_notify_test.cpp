// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/item_notify.h"

using namespace mmo;

TEST_CASE("Created items are announced to other members of the creator's group only", "[item_notify]")
{
	constexpr uint64 creator = 100;
	constexpr uint64 groupId = 5;

	CHECK(ShouldNotifyOfCreatedItem(creator, groupId, 101, groupId));
	CHECK_FALSE(ShouldNotifyOfCreatedItem(creator, groupId, creator, groupId));
	CHECK_FALSE(ShouldNotifyOfCreatedItem(creator, groupId, 101, 6));
	CHECK_FALSE(ShouldNotifyOfCreatedItem(creator, groupId, 101, 0));
}

TEST_CASE("Ungrouped creators announce to nobody", "[item_notify]")
{
	CHECK_FALSE(ShouldNotifyOfCreatedItem(100, 0, 101, 0));
	CHECK_FALSE(ShouldNotifyOfCreatedItem(100, 0, 101, 5));
}

TEST_CASE("Created items split across slots are pushed once with the summed amount", "[item_notify]")
{
	SECTION("No items added")
	{
		CHECK_FALSE(AggregateAddedItems({}).has_value());
		CHECK_FALSE(AggregateAddedItems({ { 23, 0 } }).has_value());
	}

	SECTION("Single slot")
	{
		const auto push = AggregateAddedItems({ { 23, 3 } });
		REQUIRE(push.has_value());
		CHECK(push->slot == 23);
		CHECK(push->amount == 3);
	}

	SECTION("Topped off stack spilling into a new slot")
	{
		const auto push = AggregateAddedItems({ { 24, 2 }, { 23, 1 } });
		REQUIRE(push.has_value());
		CHECK(push->slot == 23);
		CHECK(push->amount == 3);
	}

	SECTION("Amount is clamped to the wire field")
	{
		const auto push = AggregateAddedItems({ { 23, 0xFFFF }, { 24, 10 } });
		REQUIRE(push.has_value());
		CHECK(push->amount == 0xFFFF);
	}
}
