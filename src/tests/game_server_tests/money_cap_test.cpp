// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <limits>

using namespace mmo;

// Trades, mail and quest rewards all add money. A plain add wrapped a near-capped balance around
// to almost nothing; AddMoney now saturates and CanAddMoney lets callers refuse instead.

TEST_CASE("AddMoney never wraps past the money cap", "[money]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto player = test::MakeUnit(project, timers);
	constexpr uint32 cap = std::numeric_limits<uint32>::max();

	player->Set<uint32>(object_fields::Money, cap - 10);
	CHECK(player->CanAddMoney(10));
	CHECK_FALSE(player->CanAddMoney(11));

	player->AddMoney(100);
	CHECK(player->Get<uint32>(object_fields::Money) == cap);

	player->Set<uint32>(object_fields::Money, 5);
	player->AddMoney(7);
	CHECK(player->Get<uint32>(object_fields::Money) == 12);
}
