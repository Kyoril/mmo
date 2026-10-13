// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/world/map_static_data.h"
#include "../nav_mesh_tests/synthetic_nav_map.h"

#include "game_server/world/world_instance.h"
#include "nav_mesh/door_volume.h"
#include "nav_mesh/instance_query_filter.h"

#include <memory>

using namespace mmo;

namespace
{
	// No files exist for this directory: the nav map is invalid, the collision map empty.
	constexpr const char* MissingMap = "Worlds/__static_data_test__/__static_data_test__";
}

TEST_CASE("MapStaticDataRegistry shares one MapStaticData per directory", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	const auto a = registry.Acquire(MissingMap);
	const auto b = registry.Acquire(MissingMap);
	REQUIRE(a);
	CHECK(a == b);
	CHECK(registry.GetLiveCount() == 1);

	const auto other = registry.Acquire("Worlds/__other__/__other__");
	CHECK(other != a);
	CHECK(registry.GetLiveCount() == 2);
}

TEST_CASE("MapStaticDataRegistry frees data once the last user is gone", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	std::weak_ptr<MapStaticData> weak;
	{
		const auto a = registry.Acquire(MissingMap);
		weak = a;
		CHECK(registry.GetLiveCount() == 1);
	}

	CHECK(weak.expired());
	CHECK(registry.GetLiveCount() == 0);
}

TEST_CASE("MapStaticDataRegistry reloads after the last instance is gone", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	{
		const auto first = registry.Acquire(MissingMap);
		REQUIRE(first);
	}

	const auto second = registry.Acquire(MissingMap);
	REQUIRE(second);
	CHECK(second->GetDirectory() == MissingMap);
	CHECK(registry.GetLiveCount() == 1);

	// Update on data without nav pages must be a harmless no-op.
	registry.Update();
}

namespace
{
	/// Walled corridor [floor | door | floor] on global tile (512, 512), world [0, TileSize]^2.
	std::optional<std::vector<uint8>> CorridorTile(const int32 x, const int32 y)
	{
		if (x != 512 || y != 512)
		{
			return std::nullopt;
		}

		return test_nav::BuildGridTile(x, y, 3, 1, [](const int32 col, int32)
		{
			return static_cast<uint16>(col == 1 ? (1 | nav::DoorPolyFlag) : 1);
		}, false);
	}

	/// The door volume covering the corridor's middle quad.
	nav::DoorVolume MiddleDoor()
	{
		nav::DoorVolume volume;
		volume.center = Vector3(test_nav::TileSize * 0.5f, 0.f, test_nav::TileSize * 0.5f);
		volume.halfX = test_nav::TileSize / 6.f;
		volume.halfZ = test_nav::TileSize * 0.5f;
		volume.minY = -1.f;
		volume.maxY = 1.f;
		return volume;
	}

	const Vector3 West(test_nav::TileSize / 6.f, 0.f, test_nav::TileSize * 0.5f);
	const Vector3 East(test_nav::TileSize * 5.f / 6.f, 0.f, test_nav::TileSize * 0.5f);

	bool Reaches(const MapData& map, const Vector3& from, const Vector3& to)
	{
		std::vector<Vector3> path;
		return map.CalculatePath(from, to, path) && !path.empty() && path.back().GetDistanceTo(to) < 0.5f;
	}
}

TEST_CASE("A closed door blocks paths and opening it frees them", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } }, {}, CorridorTile);
	MapStaticDataRegistry registry;
	NavMapData map(registry.Acquire(test_nav::SyntheticNavMap::Name));

	CHECK(Reaches(map, West, East));

	const uint64 obstacle = map.AddNavObstacle(MiddleDoor(), true);
	REQUIRE(obstacle != 0);
	CHECK_FALSE(Reaches(map, West, East));

	map.SetNavObstacleBlocking(obstacle, false);
	CHECK(Reaches(map, West, East));

	map.SetNavObstacleBlocking(obstacle, true);
	CHECK_FALSE(Reaches(map, West, East));

	map.RemoveNavObstacle(obstacle);
	CHECK(Reaches(map, West, East));
}

TEST_CASE("Door state is per instance", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } }, {}, CorridorTile);
	MapStaticDataRegistry registry;
	NavMapData instanceA(registry.Acquire(test_nav::SyntheticNavMap::Name));
	NavMapData instanceB(registry.Acquire(test_nav::SyntheticNavMap::Name));
	REQUIRE(&instanceA.GetStaticData() == &instanceB.GetStaticData());

	REQUIRE(instanceA.AddNavObstacle(MiddleDoor(), true) != 0);
	CHECK_FALSE(Reaches(instanceA, West, East));
	CHECK(Reaches(instanceB, West, East));
}

TEST_CASE("Door registered before streaming completes", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } }, {}, CorridorTile);
	MapStaticDataRegistry registry;
	NavMapData map(registry.Acquire(test_nav::SyntheticNavMap::Name));

	// No registry.Update() has run: the door page is installed on demand by AddNavObstacle.
	const uint64 obstacle = map.AddNavObstacle(MiddleDoor(), true);
	REQUIRE(obstacle != 0);
	CHECK_FALSE(Reaches(map, West, East));
}

TEST_CASE("A door on a map without door polys registers no nav obstacle", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } });
	MapStaticDataRegistry registry;
	NavMapData map(registry.Acquire(test_nav::SyntheticNavMap::Name));

	CHECK(map.AddNavObstacle(MiddleDoor(), true) == 0);
	CHECK(Reaches(map, West, East));
}

TEST_CASE("A door without door polys reports whether walkable polys run under it", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } });
	MapStaticDataRegistry registry;
	NavMapData map(registry.Acquire(test_nav::SyntheticNavMap::Name));

	SECTION("Walkable floor under the door: the nav mesh predates the door")
	{
		REQUIRE(map.AddNavObstacle(MiddleDoor(), true) == 0);
		CHECK(map.HasWalkableNavPolysIn(MiddleDoor()));
	}

	SECTION("No polys under the door: nothing to path through")
	{
		// The same footprint, high above the floor (e.g. an arch seal in a wall).
		nav::DoorVolume raised = MiddleDoor();
		raised.minY = 50.f;
		raised.maxY = 52.f;
		REQUIRE(map.AddNavObstacle(raised, true) == 0);
		CHECK_FALSE(map.HasWalkableNavPolysIn(raised));
	}
}

TEST_CASE("Doors without door polys are reported once per map", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } });
	MapStaticDataRegistry registry;
	NavMapData instanceA(registry.Acquire(test_nav::SyntheticNavMap::Name));
	NavMapData instanceB(registry.Acquire(test_nav::SyntheticNavMap::Name));

	CHECK(instanceA.MarkDoorWithoutNavPolysReported(15));
	CHECK_FALSE(instanceA.MarkDoorWithoutNavPolysReported(15));
	CHECK_FALSE(instanceB.MarkDoorWithoutNavPolysReported(15));
	CHECK(instanceB.MarkDoorWithoutNavPolysReported(16));
}
