// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "synthetic_nav_map.h"

#include "nav_mesh/instance_query_filter.h"
#include "nav_mesh/map.h"

#include "DetourNavMesh.h"

#include <cmath>

using namespace mmo;
using namespace test_nav;

namespace
{
	// Global tile (512, 512) is the first tile of page (32, 32) and starts at world (0, 0).
	constexpr int32 DoorTileX = 512;
	constexpr int32 DoorTileY = 512;

	/// One walled corridor of three quads along x: [floor | door | floor].
	std::optional<std::vector<uint8>> CorridorTile(const int32 x, const int32 y)
	{
		if (x != DoorTileX || y != DoorTileY)
		{
			return std::nullopt;
		}

		return BuildGridTile(x, y, 3, 1, [](const int32 col, int32)
		{
			return static_cast<uint16>(col == 1 ? (1 | nav::DoorPolyFlag) : 1);
		}, false);
	}

	/// Two walled corridors of three quads each; only the lower one has a door in the middle.
	std::optional<std::vector<uint8>> TwoCorridorTile(const int32 x, const int32 y)
	{
		if (x != DoorTileX || y != DoorTileY)
		{
			return std::nullopt;
		}

		return BuildGridTile(x, y, 3, 2, [](const int32 col, const int32 row)
		{
			return static_cast<uint16>(col == 1 && row == 0 ? (1 | nav::DoorPolyFlag) : 1);
		}, false);
	}

	/// Center of quad (col, row) of a cols x rows grid tile.
	Vector3 CellCenter(const int32 col, const int32 row, const int32 cols, const int32 rows)
	{
		const Vector3 origin = TileOrigin(DoorTileX, DoorTileY);
		return { origin.x + (static_cast<float>(col) + 0.5f) * TileSize / static_cast<float>(cols), 0.f,
			origin.z + (static_cast<float>(row) + 0.5f) * TileSize / static_cast<float>(rows) };
	}

	/// The poly ref of the door quad, found by position.
	dtPolyRef DoorRef(const nav::Map& map, const Vector3& doorCenter)
	{
		const float center[3] = { doorCenter.x, doorCenter.y, doorCenter.z };
		const float extents[3] = { 0.1f, 1.f, 0.1f };
		dtQueryFilter filter;
		dtPolyRef ref = 0;
		REQUIRE(dtStatusSucceed(map.GetNavMeshQuery().findNearestPoly(center, extents, &filter, &ref, nullptr)));
		REQUIRE(ref != 0);
		return ref;
	}

	const std::vector<std::pair<int32, int32>> DoorPage = { { 32, 32 } };
}

TEST_CASE("InstanceQueryFilter blocks a door corridor while the door ref is blocked", "[nav_mesh][door]")
{
	SyntheticNavMap files(DoorPage, {}, CorridorTile);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 west = CellCenter(0, 0, 3, 1);
	const Vector3 door = CellCenter(1, 0, 3, 1);
	const Vector3 east = CellCenter(2, 0, 3, 1);

	nav::InstanceQueryFilter filter;
	std::vector<Vector3> path;
	REQUIRE(map.FindPath(west, east, path, false, &filter));
	CHECK(path.back().GetDistanceTo(east) < 0.5f);

	filter.Block({ DoorRef(map, door) });
	path.clear();
	CHECK_FALSE(map.FindPath(west, east, path, false, &filter));

	filter.Unblock({ DoorRef(map, door) });
	path.clear();
	REQUIRE(map.FindPath(west, east, path, false, &filter));
	CHECK(path.back().GetDistanceTo(east) < 0.5f);
}

TEST_CASE("Partial path stops in front of a blocked door", "[nav_mesh][door]")
{
	SyntheticNavMap files(DoorPage, {}, CorridorTile);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 west = CellCenter(0, 0, 3, 1);
	const Vector3 door = CellCenter(1, 0, 3, 1);
	const Vector3 east = CellCenter(2, 0, 3, 1);
	const float doorWestEdge = door.x - TileSize / 6.f;

	nav::InstanceQueryFilter filter;
	filter.Block({ DoorRef(map, door) });

	std::vector<Vector3> path;
	REQUIRE(map.FindPath(west, east, path, true, &filter));
	for (const Vector3& point : path)
	{
		CHECK(point.x <= doorWestEdge + 0.01f);
	}

	// A target standing just past the door, and one 1 m into the door poly, must not be reached
	// either: neither may become a same-poly shortcut. (The door quad's center is more than the
	// 5 m search extent away from any open poly, so only a point near its edge resolves at all.)
	const Vector3 justPast(doorWestEdge + TileSize / 3.f + 0.2f, 0.f, door.z);
	path.clear();
	REQUIRE(map.FindPath(west, justPast, path, true, &filter));
	CHECK(path.back().x <= doorWestEdge + 0.01f);

	const Vector3 onDoor(doorWestEdge + 1.f, 0.f, door.z);
	path.clear();
	REQUIRE(map.FindPath(west, onDoor, path, true, &filter));
	CHECK(path.back().x <= doorWestEdge + 0.01f);
}

TEST_CASE("A blocked door is bypassed through another corridor", "[nav_mesh][door]")
{
	SyntheticNavMap files(DoorPage, {}, TwoCorridorTile);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 west = CellCenter(0, 0, 3, 2);
	const Vector3 door = CellCenter(1, 0, 3, 2);
	const Vector3 east = CellCenter(2, 0, 3, 2);

	nav::InstanceQueryFilter filter;
	filter.Block({ DoorRef(map, door) });

	std::vector<Vector3> path;
	REQUIRE(map.FindPath(west, east, path, false, &filter));
	CHECK(path.back().GetDistanceTo(east) < 0.5f);

	// Every waypoint must stay out of the door quad.
	const float halfW = TileSize / 6.f;
	const float halfH = TileSize / 4.f;
	for (const Vector3& point : path)
	{
		const bool insideDoor = std::abs(point.x - door.x) < halfW - 0.01f && std::abs(point.z - door.z) < halfH - 0.01f;
		CHECK_FALSE(insideDoor);
	}
}

TEST_CASE("InstanceQueryFilter reference counts blocks", "[nav_mesh][door]")
{
	nav::InstanceQueryFilter filter;
	const dtPolyRef ref = 12345;

	filter.Block({ ref });
	filter.Block({ ref });
	filter.Unblock({ ref });
	CHECK(filter.IsBlocked(ref));
	CHECK(filter.GetBlockedCount() == 1);

	filter.Unblock({ ref });
	CHECK_FALSE(filter.IsBlocked(ref));
	CHECK(filter.GetBlockedCount() == 0);

	// Unblocking a ref that is not blocked is a no-op.
	filter.Unblock({ ref });
	CHECK(filter.GetBlockedCount() == 0);
}

TEST_CASE("Random points never land on a blocked door poly", "[nav_mesh][door]")
{
	SyntheticNavMap files(DoorPage, {}, CorridorTile);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 west = CellCenter(0, 0, 3, 1);
	const Vector3 door = CellCenter(1, 0, 3, 1);
	const float doorWestEdge = door.x - TileSize / 6.f;

	nav::InstanceQueryFilter filter;
	filter.Block({ DoorRef(map, door) });

	for (int i = 0; i < 50; ++i)
	{
		Vector3 point;
		if (map.FindRandomPointAroundCircle(west, TileSize, point, &filter))
		{
			CHECK(point.x <= doorWestEdge + 0.01f);
		}
	}
}
