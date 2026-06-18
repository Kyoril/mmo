// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/world/world_instance.h"
#include "math/vector3.h"

using namespace mmo;

// ---------------------------------------------------------------------------
// SimpleMapData — always reports LOS as clear (no collision geometry).
// ---------------------------------------------------------------------------

TEST_CASE("SimpleMapData::IsInLineOfSight always returns true", "[los][simple_map]")
{
	SimpleMapData map;

	CHECK(map.IsInLineOfSight(Vector3(0, 0, 0), Vector3(10, 0, 0)));
	CHECK(map.IsInLineOfSight(Vector3(-100, 5, -100), Vector3(100, 5, 100)));
	CHECK(map.IsInLineOfSight(Vector3(0, 0, 0), Vector3(0, 0, 0)));
}

TEST_CASE("SimpleMapData::IsInLineOfSightEx always returns true and sets hitPoint to posB", "[los][simple_map]")
{
	SimpleMapData map;

	const Vector3 from(1.f, 0.f, 2.f);
	const Vector3 to(50.f, 3.f, 20.f);
	Vector3 hit;

	const bool result = map.IsInLineOfSightEx(from, to, hit);

	CHECK(result == true);
	CHECK(hit.x == Approx(to.x));
	CHECK(hit.y == Approx(to.y));
	CHECK(hit.z == Approx(to.z));
}

TEST_CASE("SimpleMapData::IsInLineOfSightEx identical positions returns true", "[los][simple_map]")
{
	SimpleMapData map;

	const Vector3 pos(5.f, 1.f, 5.f);
	Vector3 hit;

	CHECK(map.IsInLineOfSightEx(pos, pos, hit));
	CHECK(hit.x == Approx(pos.x));
	CHECK(hit.y == Approx(pos.y));
	CHECK(hit.z == Approx(pos.z));
}

TEST_CASE("SimpleMapData satisfies the MapData interface", "[los][simple_map]")
{
	// Verify that SimpleMapData can be used through the abstract MapData pointer.
	std::unique_ptr<MapData> map = std::make_unique<SimpleMapData>();

	const Vector3 a(0, 0, 0);
	const Vector3 b(10, 0, 10);
	Vector3 hit;

	CHECK(map->IsInLineOfSight(a, b));
	CHECK(map->IsInLineOfSightEx(a, b, hit));
	CHECK(hit.x == Approx(b.x));
	CHECK(hit.z == Approx(b.z));
}
