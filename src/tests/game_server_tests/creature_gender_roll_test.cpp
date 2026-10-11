// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Most NPCs are a specific person and must always look the same, so a unit only ever spawns with
// its male model. Units flagged random_gender (guards, for example) roll between both models.

#include "catch.hpp"

#include "game_server/objects/game_creature_s.h"
#include "shared/proto_data/units.pb.h"

using namespace mmo;

namespace
{
	proto::UnitEntry MakeEntry(const uint32 maleModel, const uint32 femaleModel, const bool randomGender)
	{
		proto::UnitEntry entry;
		entry.set_malemodel(maleModel);
		entry.set_femalemodel(femaleModel);
		entry.set_random_gender(randomGender);
		return entry;
	}

	std::pair<int, int> CountRolls(const proto::UnitEntry& entry)
	{
		RandomnessGenerator generator(1234);
		int male = 0, female = 0;
		for (int i = 0; i < 1000; ++i)
		{
			const uint32 displayId = GameCreatureS::RollDisplayId(entry, generator);
			if (displayId == entry.malemodel())
			{
				++male;
			}
			else if (displayId == entry.femalemodel())
			{
				++female;
			}
		}
		return { male, female };
	}
}

TEST_CASE("Units without random_gender always use the male model", "[creature][gender]")
{
	const auto [male, female] = CountRolls(MakeEntry(24, 25, false));
	CHECK(male == 1000);
	CHECK(female == 0);
}

TEST_CASE("random_gender is off by default", "[creature][gender]")
{
	proto::UnitEntry entry;
	CHECK_FALSE(entry.random_gender());
}

TEST_CASE("random_gender units spawn with both models", "[creature][gender]")
{
	const auto [male, female] = CountRolls(MakeEntry(24, 25, true));
	CHECK(male + female == 1000);
	CHECK(male > 400);
	CHECK(female > 400);
}

TEST_CASE("random_gender without a female model stays male", "[creature][gender]")
{
	const auto [male, female] = CountRolls(MakeEntry(24, 0, true));
	CHECK(male == 1000);
}
