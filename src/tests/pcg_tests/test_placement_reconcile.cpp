// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/placement_reconcile.h"

#include <set>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	Placement MakePlacement(uint64 key, const char* asset, float x, PlacementKind kind = PlacementKind::Entity)
	{
		Placement p;
		p.key = key;
		p.kind = kind;
		p.asset = asset;
		p.position = Vector3(x, 0.0f, 0.0f);
		return p;
	}

	OwnedPlacement MakeOwned(uint64 key, uint64 uniqueId, const char* asset, float x, PlacementKind kind = PlacementKind::Entity)
	{
		OwnedPlacement op;
		op.key = key;
		op.uniqueId = uniqueId;
		op.kind = kind;
		op.written = MakePlacement(key, asset, x, kind);
		return op;
	}

	auto ExistsAll()
	{
		return [](uint64, PlacementKind) { return true; };
	}
}

TEST_CASE("Reconcile keeps identical placements", "[reconcile]")
{
	const auto result = Reconcile({ MakeOwned(1, 101, "A", 1.0f) }, { MakePlacement(1, "A", 1.0f) }, ExistsAll());
	REQUIRE(result.keep.size() == 1);
	CHECK(result.keep[0].uniqueId == 101);
	CHECK(result.update.empty());
	CHECK(result.remove.empty());
	CHECK(result.create.empty());
}

TEST_CASE("Reconcile updates a moved placement and keeps its id", "[reconcile]")
{
	const auto result = Reconcile({ MakeOwned(1, 101, "A", 1.0f) }, { MakePlacement(1, "A", 5.0f) }, ExistsAll());
	REQUIRE(result.update.size() == 1);
	CHECK(result.update[0].uniqueId == 101);
	CHECK(result.update[0].written.position.x == 5.0f);
	CHECK(result.keep.empty());
	CHECK(result.remove.empty());
	CHECK(result.create.empty());
}

TEST_CASE("Reconcile replaces a placement whose asset changed", "[reconcile]")
{
	const auto result = Reconcile({ MakeOwned(1, 101, "A", 1.0f) }, { MakePlacement(1, "B", 1.0f) }, ExistsAll());
	REQUIRE(result.remove.size() == 1);
	CHECK(result.remove[0] == 101);
	REQUIRE(result.create.size() == 1);
	CHECK(result.create[0].asset == "B");
	CHECK(result.keep.empty());
	CHECK(result.update.empty());
}

TEST_CASE("Reconcile removes placements that are gone and creates new ones", "[reconcile]")
{
	const auto result = Reconcile({ MakeOwned(1, 101, "A", 1.0f) }, { MakePlacement(2, "A", 1.0f) }, ExistsAll());
	REQUIRE(result.remove.size() == 1);
	CHECK(result.remove[0] == 101);
	REQUIRE(result.create.size() == 1);
	CHECK(result.create[0].key == 2);
}

TEST_CASE("Reconcile recreates an owned object deleted by hand", "[reconcile]")
{
	const auto exists = [](uint64 id, PlacementKind) { return id != 101; };

	SECTION("key still wanted")
	{
		const auto result = Reconcile({ MakeOwned(1, 101, "A", 1.0f) }, { MakePlacement(1, "A", 1.0f) }, exists);
		REQUIRE(result.create.size() == 1);
		CHECK(result.create[0].key == 1);
		CHECK(result.remove.empty());
		CHECK(result.keep.empty());
		CHECK(result.update.empty());
	}

	SECTION("key no longer wanted")
	{
		const auto result = Reconcile({ MakeOwned(1, 101, "A", 1.0f) }, {}, exists);
		CHECK(result.remove.empty());
		CHECK(result.create.empty());
	}
}

TEST_CASE("Reconcile with empty next removes everything that exists", "[reconcile]")
{
	const auto exists = [](uint64 id, PlacementKind) { return id != 102; };
	const auto result = Reconcile({ MakeOwned(1, 101, "A", 1.0f), MakeOwned(2, 102, "A", 2.0f), MakeOwned(3, 103, "P", 3.0f, PlacementKind::Foliage) },
		{}, exists);
	REQUIRE(result.remove.size() == 2);
	CHECK(result.remove[0] == 101);
	CHECK(result.remove[1] == 103);
	CHECK(result.create.empty());
	CHECK(result.keep.empty());
	CHECK(result.update.empty());
}

TEST_CASE("Reconcile passes the owned kind to exists", "[reconcile]")
{
	std::set<std::pair<uint64, PlacementKind>> asked;
	const auto exists = [&asked](uint64 id, PlacementKind kind) { asked.emplace(id, kind); return true; };
	Reconcile({ MakeOwned(1, 101, "P", 1.0f, PlacementKind::Foliage) }, { MakePlacement(1, "P", 1.0f, PlacementKind::Foliage) }, exists);
	CHECK(asked.count({ 101, PlacementKind::Foliage }) == 1);
}

TEST_CASE("Reconcile replaces a placement whose kit paths changed", "[reconcile]")
{
	OwnedPlacement owned = MakeOwned(1, 101, "W.wmo", 1.0f, PlacementKind::WorldModel);
	owned.written.kitPaths = { "Kits/A.hkit" };
	Placement want = MakePlacement(1, "W.wmo", 1.0f, PlacementKind::WorldModel);
	want.kitPaths = { "Kits/A.hkit", "Kits/B.hkit" };

	const auto result = Reconcile({ owned }, { want }, ExistsAll());
	REQUIRE(result.remove.size() == 1);
	CHECK(result.remove[0] == 101);
	REQUIRE(result.create.size() == 1);
	CHECK(result.create[0].kitPaths.size() == 2);
	CHECK(result.keep.empty());
	CHECK(result.update.empty());
}

TEST_CASE("Reconcile replaces a placement whose material overrides changed", "[reconcile]")
{
	OwnedPlacement owned = MakeOwned(1, 101, "A", 1.0f);
	owned.written.materialOverrides = { { 0, "M1.hmat" } };
	Placement want = MakePlacement(1, "A", 1.0f);
	want.materialOverrides = { { 0, "M2.hmat" } };

	const auto result = Reconcile({ owned }, { want }, ExistsAll());
	REQUIRE(result.remove.size() == 1);
	REQUIRE(result.create.size() == 1);
	CHECK(result.create[0].materialOverrides[0].materialName == "M2.hmat");
	CHECK(result.keep.empty());
	CHECK(result.update.empty());
}

TEST_CASE("Reconcile replaces a placement whose kind changed from entity to world model", "[reconcile]")
{
	const auto result = Reconcile({ MakeOwned(1, 101, "A", 1.0f) }, { MakePlacement(1, "A", 1.0f, PlacementKind::WorldModel) }, ExistsAll());
	REQUIRE(result.remove.size() == 1);
	REQUIRE(result.create.size() == 1);
	CHECK(result.create[0].kind == PlacementKind::WorldModel);
	CHECK(result.keep.empty());
	CHECK(result.update.empty());
}

TEST_CASE("Reconcile keeps identical placements with kits and overrides", "[reconcile]")
{
	OwnedPlacement owned = MakeOwned(1, 101, "W.wmo", 1.0f, PlacementKind::WorldModel);
	owned.written.kitPaths = { "Kits/A.hkit" };
	Placement want = MakePlacement(1, "W.wmo", 1.0f, PlacementKind::WorldModel);
	want.kitPaths = { "Kits/A.hkit" };

	const auto result = Reconcile({ owned }, { want }, ExistsAll());
	REQUIRE(result.keep.size() == 1);
	CHECK(result.remove.empty());
	CHECK(result.create.empty());
}
