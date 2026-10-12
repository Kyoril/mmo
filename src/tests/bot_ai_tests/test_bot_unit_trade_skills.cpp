// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_core/bot_unit.h"

using namespace mmo;

TEST_CASE("BotUnit stores, replaces and clears trade skills", "[bot_unit][trade_skill]")
{
	BotUnit unit;
	CHECK(unit.GetTradeSkill(197) == nullptr);

	unit.SetTradeSkill(197, 1, 75);
	const TradeSkillData* skill = unit.GetTradeSkill(197);
	REQUIRE(skill != nullptr);
	CHECK(skill->value == 1);
	CHECK(skill->max == 75);

	unit.SetTradeSkill(197, 2, 150);
	skill = unit.GetTradeSkill(197);
	REQUIRE(skill != nullptr);
	CHECK(skill->value == 2);
	CHECK(skill->max == 150);

	unit.ClearTradeSkills();
	CHECK(unit.GetTradeSkill(197) == nullptr);
}
