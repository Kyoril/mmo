// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/trade_skill.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	TradeSkillThresholds Make(const uint32 required, const uint32 yellow, const uint32 green, const uint32 grey)
	{
		return NormalizeTradeSkillThresholds({ required, yellow, green, grey });
	}
}

TEST_CASE("Trade skill difficulty follows the four colour bands", "[trade_skill]")
{
	const TradeSkillThresholds t = Make(10, 25, 35, 45);
	CHECK(GetTradeSkillDifficulty(9, t) == trade_skill_difficulty::Impossible);
	CHECK(GetTradeSkillDifficulty(10, t) == trade_skill_difficulty::Orange);
	CHECK(GetTradeSkillDifficulty(24, t) == trade_skill_difficulty::Orange);
	CHECK(GetTradeSkillDifficulty(25, t) == trade_skill_difficulty::Yellow);
	CHECK(GetTradeSkillDifficulty(35, t) == trade_skill_difficulty::Green);
	CHECK(GetTradeSkillDifficulty(45, t) == trade_skill_difficulty::Grey);
	CHECK(GetTradeSkillDifficulty(300, t) == trade_skill_difficulty::Grey);
}

TEST_CASE("Trade skill-up chance is certain below yellow and falls linearly to grey", "[trade_skill]")
{
	const TradeSkillThresholds t = Make(10, 25, 35, 45);
	CHECK(GetTradeSkillUpChance(10, t) == Approx(1.0f));
	CHECK(GetTradeSkillUpChance(24, t) == Approx(1.0f));
	CHECK(GetTradeSkillUpChance(25, t) == Approx(1.0f));
	CHECK(GetTradeSkillUpChance(35, t) == Approx(0.5f));
	CHECK(GetTradeSkillUpChance(44, t) == Approx(0.05f));
	CHECK(GetTradeSkillUpChance(45, t) == Approx(0.0f));
	CHECK(GetTradeSkillUpChance(9, t) == Approx(0.0f));
}

TEST_CASE("Trade skill thresholds are normalised", "[trade_skill]")
{
	const TradeSkillThresholds missingGreen = Make(1, 20, 0, 40);
	CHECK(missingGreen.green == 30);

	const TradeSkillThresholds inverted = Make(30, 10, 5, 20);
	CHECK(inverted.yellow == 30);
	CHECK(inverted.green >= inverted.yellow);
	CHECK(inverted.grey >= inverted.green);

	const TradeSkillThresholds neverTrivial = Make(0, 0, 0, 0);
	CHECK(GetTradeSkillDifficulty(500, neverTrivial) == trade_skill_difficulty::Orange);
	CHECK(GetTradeSkillUpChance(500, neverTrivial) == Approx(1.0f));
}
