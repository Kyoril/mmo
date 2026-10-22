// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/character_start.h"
#include "math/degree.h"
#include "proto_data/project.h"

using namespace mmo;

TEST_CASE("Race start rotation is read as radians", "[character_start]")
{
	proto::RaceEntry race;
	race.set_startrotation(1.3178176f);

	const Radian facing = GetRaceStartFacing(race);

	CHECK(facing.GetValueRadians() == Approx(1.3178176f));
	// What CreateCharacter receives: the same angle, about 75.5 degrees, not 1.3 degrees.
	CHECK(Degree(facing).GetValueDegrees() == Approx(75.5f).margin(0.1f));
}

TEST_CASE("Race start rotation of zero faces along +X", "[character_start]")
{
	proto::RaceEntry race;
	race.set_startrotation(0.0f);

	CHECK(GetRaceStartFacing(race).GetValueRadians() == Approx(0.0f));
}
