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
