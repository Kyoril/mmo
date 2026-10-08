// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/spells/spell_target_resolver.h"
#include "math/math_utils.h"

using namespace mmo;

namespace
{
	const float halfQuarterTurn = Pi / 4.0f;

	/// Point at a planar distance and angle from the origin, in the engine's facing convention.
	Vector3 PointAlong(const Vector3& origin, const float facing, const float distance)
	{
		return origin + FacingToDirection(Radian(facing)) * distance;
	}
}

// The cone has to open along the same axis as GetForwardVector, or a "frontal" boss attack
// lands behind the boss. This is the bug a hand-rolled cos/sin pair would introduce.
TEST_CASE("IsInPlanarCone opens along the caster's facing", "[spell][cone]")
{
	const Vector3 origin(10.0f, 2.0f, -5.0f);
	const float facings[] = { 0.0f, 1.0f, Pi * 0.5f, Pi, 4.0f };

	for (const float facing : facings)
	{
		CHECK(IsInPlanarCone(origin, Radian(facing), halfQuarterTurn, PointAlong(origin, facing, 5.0f)));
		CHECK_FALSE(IsInPlanarCone(origin, Radian(facing), halfQuarterTurn, PointAlong(origin, facing + Pi, 5.0f)));
	}
}

TEST_CASE("IsInPlanarCone respects the cone width", "[spell][cone]")
{
	const Vector3 origin(0.0f, 0.0f, 0.0f);

	// 90 degree cone: 40 degrees off-axis is inside, 50 degrees is not, on either side.
	const float inside = 40.0f * Pi / 180.0f;
	const float outside = 50.0f * Pi / 180.0f;
	CHECK(IsInPlanarCone(origin, Radian(0.0f), halfQuarterTurn, PointAlong(origin, inside, 4.0f)));
	CHECK(IsInPlanarCone(origin, Radian(0.0f), halfQuarterTurn, PointAlong(origin, -inside, 4.0f)));
	CHECK_FALSE(IsInPlanarCone(origin, Radian(0.0f), halfQuarterTurn, PointAlong(origin, outside, 4.0f)));
	CHECK_FALSE(IsInPlanarCone(origin, Radian(0.0f), halfQuarterTurn, PointAlong(origin, -outside, 4.0f)));
}

TEST_CASE("IsInPlanarCone ignores height and accepts the apex", "[spell][cone]")
{
	const Vector3 origin(0.0f, 0.0f, 0.0f);

	Vector3 raised = PointAlong(origin, 0.0f, 3.0f);
	raised.y = 25.0f;
	CHECK(IsInPlanarCone(origin, Radian(0.0f), halfQuarterTurn, raised));

	CHECK(IsInPlanarCone(origin, Radian(0.0f), halfQuarterTurn, Vector3(0.0f, 1.0f, 0.0f)));
}
