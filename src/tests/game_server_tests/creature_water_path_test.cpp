// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The navigation mesh is baked on the ground, so a path through a lake leads along its bottom.
// A creature which can swim has to follow that path at the water surface instead of walking
// along the bottom fully submerged. For a creature which can not swim, water that deep is a wall:
// it is carved out of the navigation mesh so its paths route around the water body.

#include "catch.hpp"

#include <cstring>

#include "game_server/deep_water_nav_filter.h"
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

TEST_CASE("A path into deep water is cut at the water's edge for a creature which can not swim",
	"[creature][water_path]")
{
	// Three steps on dry land (x <= 0), then into the lake.
	std::vector<Vector3> path{ Vector3(-15.0f, 0.0f, 0.0f), Vector3(-10.0f, 0.0f, 0.0f),
		Vector3(-5.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::CanWalk, MakeShore(10.0f));

	CHECK(result.truncated);
	CHECK_FALSE(result.blocked);
	CHECK_FALSE(result.swimming);

	// Everything from the first deep point on is gone: the creature stops in front of the water.
	REQUIRE(path.size() == 3);
	CHECK(path.back().x == Approx(-5.0f));
	CHECK(path.back().y == Approx(0.0f));
}

TEST_CASE("A creature which can not swim refuses a movement that only leads into deep water",
	"[creature][water_path]")
{
	// The creature already stands in the lake and the path keeps it there: nothing is left of it.
	std::vector<Vector3> path{ Vector3(5.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::CanWalk, MakeShore(10.0f));

	CHECK(result.blocked);
	CHECK(result.truncated);
	CHECK_FALSE(result.swimming);
}

TEST_CASE("A creature which can not swim may still walk out of deep water", "[creature][water_path]")
{
	// It stands in the lake (x > 0) and the path leads out onto dry land.
	std::vector<Vector3> path{ Vector3(5.0f, 0.0f, 0.0f), Vector3(-5.0f, 0.0f, 0.0f) };

	const WaterPathResult result = ApplyWaterToPath(path, movement_ability::CanWalk, MakeShore(10.0f));

	CHECK_FALSE(result.blocked);
	CHECK_FALSE(result.truncated);
	REQUIRE(path.size() == 2);
	CHECK(path.back().x == Approx(-5.0f));
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


// ---------------------------------------------------------------------------
// Deep water is carved out of the navigation mesh for a creature which can not swim, so its
// paths are routed around the water body instead of leading into it.
// ---------------------------------------------------------------------------

namespace
{
	/// @brief A zeroed dtMeshTile. Detour declares a private copy constructor, which leaves the
	///	struct without a default constructor, so it is built from raw storage here.
	class TestMeshTile
	{
	public:
		TestMeshTile() { std::memset(m_storage, 0, sizeof(m_storage)); }

		[[nodiscard]] dtMeshTile& Get() { return *reinterpret_cast<dtMeshTile*>(m_storage); }

	private:
		alignas(dtMeshTile) unsigned char m_storage[sizeof(dtMeshTile)];
	};

	/// @brief A one-polygon nav mesh tile whose four corners sit at the given height.
	struct TestNavTile
	{
		explicit TestNavTile(const float cornerY, const float x = 0.0f)
		{
			const float corners[4][3] = {
				{ x, cornerY, 0.0f },
				{ x + 1.0f, cornerY, 0.0f },
				{ x + 1.0f, cornerY, 1.0f },
				{ x, cornerY, 1.0f }
			};

			for (int corner = 0; corner < 4; ++corner)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					verts[corner * 3 + axis] = corners[corner][axis];
				}

				poly.verts[corner] = static_cast<unsigned short>(corner);
			}

			poly.vertCount = 4;
			// A flag the default dtQueryFilter includes, so only the water decides.
			poly.flags = 1;

			tile.Get().verts = verts;
			tile.Get().polys = &poly;
		}

		float verts[12] = {};
		dtPoly poly = {};
		TestMeshTile tile;
	};

	/// @brief A filter which rejects every polygon, to check that the base filter decides first.
	class RejectAllFilter final : public dtQueryFilter
	{
	public:
		bool passFilter(dtPolyRef, const dtMeshTile*, const dtPoly*) const override { return false; }
	};
}

TEST_CASE("A navigation polygon under deep water is recognized", "[creature][water_path]")
{
	// Lake surface at 10, polygon on the bottom at 0.
	const TestNavTile bottom(0.0f);
	CHECK(IsPolyInDeepWater(bottom.verts, bottom.poly, MakeLake(10.0f)));

	// The same polygon with only knee-deep water over it is walked through.
	CHECK_FALSE(IsPolyInDeepWater(bottom.verts, bottom.poly, MakeLake(0.5f)));

	// Without water data no polygon is ever rejected.
	CHECK_FALSE(IsPolyInDeepWater(bottom.verts, bottom.poly, WaterSurfaceQuery()));
}

TEST_CASE("A creature which can not swim is kept off polygons in deep water", "[creature][water_path]")
{
	const dtQueryFilter base;

	// Water only at x > 0: the polygon at x = 1 lies in the lake, the one at x = -2 does not.
	const DeepWaterQueryFilter filter(base, MakeShore(10.0f));

	TestNavTile inWater(0.0f, 1.0f);
	CHECK_FALSE(filter.passFilter(1, &inWater.tile.Get(), &inWater.poly));

	TestNavTile onLand(0.0f, -2.0f);
	CHECK(filter.passFilter(2, &onLand.tile.Get(), &onLand.poly));

	// The default filter still accepts the polygon in the water, so swimmers path through it.
	CHECK(base.passFilter(1, &inWater.tile.Get(), &inWater.poly));
}

TEST_CASE("The deep water filter keeps what the filter below it rejects rejected", "[creature][water_path]")
{
	const RejectAllFilter base;
	const DeepWaterQueryFilter filter(base, MakeShore(10.0f));

	TestNavTile onLand(0.0f, -2.0f);
	CHECK_FALSE(filter.passFilter(1, &onLand.tile.Get(), &onLand.poly));
}
