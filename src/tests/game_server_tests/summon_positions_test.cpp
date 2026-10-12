// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/spells/spell_effects.h"
#include "math/math_utils.h"

#include <cmath>

using namespace mmo;

namespace
{
	float PlanarDistance(const Vector3& a, const Vector3& b)
	{
		return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
	}
}

// Mourning Voices relies on this: its two choristers must end up on opposite sides of the boss,
// so the group has to split to reach both.
TEST_CASE("SummonPositions puts two summons on opposite sides of the caster", "[spell][summon]")
{
	const Vector3 origin(5.0f, 1.0f, -3.0f);
	const auto positions = SummonPositions(origin, Radian(0.7f), 2, 12.0f);

	REQUIRE(positions.size() == 2);
	CHECK(PlanarDistance(positions[0], origin) == Approx(12.0f));
	CHECK(PlanarDistance(positions[1], origin) == Approx(12.0f));
	CHECK(PlanarDistance(positions[0], positions[1]) == Approx(24.0f));
}

TEST_CASE("SummonPositions keeps the caster's height and never places in front", "[spell][summon]")
{
	const Vector3 origin(0.0f, 7.5f, 0.0f);
	const Radian facing(0.0f);
	const Vector3 ahead = origin + FacingToDirection(facing) * 3.0f;

	for (const Vector3& position : SummonPositions(origin, facing, 2, 3.0f))
	{
		CHECK(position.y == Approx(7.5f));
		CHECK(PlanarDistance(position, ahead) > 1.0f);
	}
}

TEST_CASE("SummonPositions spreads any count evenly", "[spell][summon]")
{
	const Vector3 origin(0.0f, 0.0f, 0.0f);
	const auto positions = SummonPositions(origin, Radian(0.0f), 4, 2.0f);

	REQUIRE(positions.size() == 4);
	// Neighbours on a four-point circle of radius 2 are 2 * sqrt(2) apart.
	for (size_t i = 0; i < positions.size(); ++i)
	{
		CHECK(PlanarDistance(positions[i], positions[(i + 1) % positions.size()]) == Approx(2.0f * std::sqrt(2.0f)));
	}
}
