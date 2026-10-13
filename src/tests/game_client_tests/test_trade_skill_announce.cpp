// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/trade_skill_announce.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Skill increase is announced only when the value of a known skill rose", "[trade_skill]")
{
	const std::map<uint32, TradeSkillData> cached{ { 7, { 7, 12, 75 } } };

	SECTION("a higher value is announced")
	{
		CHECK(ShouldAnnounceSkillIncrease(cached, { 7, 13, 75 }));
	}

	SECTION("an unchanged or lower value is not announced")
	{
		CHECK_FALSE(ShouldAnnounceSkillIncrease(cached, { 7, 12, 75 }));
		CHECK_FALSE(ShouldAnnounceSkillIncrease(cached, { 7, 11, 75 }));
	}

	SECTION("a pure cap raise is not announced")
	{
		CHECK_FALSE(ShouldAnnounceSkillIncrease(cached, { 7, 12, 150 }));
	}

	SECTION("the initial learn of a skill is not announced")
	{
		CHECK_FALSE(ShouldAnnounceSkillIncrease(cached, { 9, 1, 75 }));
		CHECK_FALSE(ShouldAnnounceSkillIncrease({}, { 7, 1, 75 }));
	}
}
