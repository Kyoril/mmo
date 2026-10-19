// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "game/creature_data.h"

using namespace mmo;

TEST_CASE("Creature subtitle wraps the subname in angle brackets", "[creature_subtitle]")
{
	CreatureInfo info;
	info.id = 5;
	info.name = "Eliza Thorne";
	info.subname = "General Goods";

	REQUIRE(info.GetSubtitle() == "<General Goods>");
}

TEST_CASE("Creature subtitle is empty without a subname", "[creature_subtitle]")
{
	CreatureInfo info;
	info.id = 5;
	info.name = "Eliza Thorne";

	REQUIRE(info.GetSubtitle().empty());
}
