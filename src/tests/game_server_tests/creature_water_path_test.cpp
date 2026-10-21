// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The navigation mesh is baked on the ground, so a path through a lake leads along its bottom.
// A creature which can swim has to follow that path at the water surface instead of walking
// along the bottom fully submerged, and water deep enough to swim in blocks a creature which
// can not swim like a wall.

#include "catch.hpp"

#include "game_server/objects/game_creature_s.h"
#include "game_server/water_path.h"
#include "shared/proto_data/units.pb.h"

using namespace mmo;

namespace
{
	/// @brief A lake of the given depth covering the whole world, with its surface at surfaceY.
	WaterSurfaceQuery MakeLake(const float surfaceY)
	{
		return [surfaceY](float, float, float& outSurfaceY)
			{
				outSurfaceY = surfaceY;
				return true;
			};
	}

	/// @brief Water only in the half of the world at x > 0, with its surface at surfaceY.
	WaterSurfaceQuery MakeShore(const float surfaceY)
	{
		return [surfaceY](const float x, float, float& outSurfaceY)
			{
				if (x <= 0.0f)
				{
					return false;
				}

				outSurfaceY = surfaceY;
				return true;
			};
	}
}

TEST_CASE("A swimming creature's path through deep water is lifted to the surface", "[creature][water_path]")
{
	// Lake surface at 10, bottom at 0: five units of water above the baked path.
	std::vector<Vector3> path{ Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::Default, MakeLake(10.0f));

	CHECK(result.swimming);
	CHECK_FALSE(result.blocked);
	CHECK(result.destinationSwimming);

	for (const Vector3& point : path)
	{
		// The swimmer floats at the surface, submerged by the surface margin only.
		CHECK(point.y == Approx(10.0f - swim::SurfaceMargin));
	}
}

TEST_CASE("Water too shallow to swim in leaves a path untouched", "[creature][water_path]")
{
	// Half a unit of water above the path: the creature wades through it on the ground.
	std::vector<Vector3> path{ Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::Default, MakeLake(0.5f));

	CHECK_FALSE(result.swimming);
	CHECK_FALSE(result.blocked);
	CHECK_FALSE(result.destinationSwimming);
	CHECK(path[0].y == Approx(0.0f));
	CHECK(path[1].y == Approx(0.0f));
}

TEST_CASE("Only the submerged part of a path is lifted to the surface", "[creature][water_path]")
{
	// The path starts on dry land (x <= 0) and ends in the lake.
	std::vector<Vector3> path{ Vector3(-5.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::Default, MakeShore(10.0f));

	CHECK(result.swimming);
	CHECK(result.destinationSwimming);
	CHECK(path[0].y == Approx(0.0f));
	CHECK(path[1].y == Approx(10.0f - swim::SurfaceMargin));
}

TEST_CASE("A path leaving the water does not count as swimming at its destination", "[creature][water_path]")
{
	// Starts in the lake, ends on dry land (x <= 0).
	std::vector<Vector3> path{ Vector3(5.0f, 0.0f, 0.0f), Vector3(-5.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::Default, MakeShore(10.0f));

	CHECK(result.swimming);
	CHECK_FALSE(result.destinationSwimming);
	CHECK(path[0].y == Approx(10.0f - swim::SurfaceMargin));
	CHECK(path[1].y == Approx(0.0f));
}

TEST_CASE("Deep water blocks a creature which can not swim", "[creature][water_path]")
{
	std::vector<Vector3> path{ Vector3(-5.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::CanWalk, MakeShore(10.0f));

	CHECK(result.blocked);
	CHECK_FALSE(result.swimming);

	// The path is left untouched: the caller refuses the movement entirely.
	CHECK(path[0].y == Approx(0.0f));
	CHECK(path[1].y == Approx(0.0f));
}

TEST_CASE("Shallow water does not block a creature which can not swim", "[creature][water_path]")
{
	std::vector<Vector3> path{ Vector3(-5.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::CanWalk, MakeShore(0.5f));

	CHECK_FALSE(result.blocked);
	CHECK_FALSE(result.swimming);
}

TEST_CASE("A map without water data leaves paths unchanged", "[creature][water_path]")
{
	std::vector<Vector3> path{ Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::Default, WaterSurfaceQuery());

	CHECK_FALSE(result.swimming);
	CHECK_FALSE(result.blocked);
	CHECK(path[0].y == Approx(0.0f));
}

TEST_CASE("Creatures can swim unless their entry says otherwise", "[creature][water_path]")
{
	proto::UnitEntry entry;
	CHECK((GameCreatureS::MovementAbilitiesOf(entry) & movement_ability::CanSwim) != 0);
	CHECK((GameCreatureS::MovementAbilitiesOf(entry) & movement_ability::CanWalk) != 0);

	entry.set_extraflags(unit_extra_flags::CannotSwim);
	CHECK((GameCreatureS::MovementAbilitiesOf(entry) & movement_ability::CanSwim) == 0);
	CHECK((GameCreatureS::MovementAbilitiesOf(entry) & movement_ability::CanWalk) != 0);
}
