// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/oasis_record.h"
#include "pcg/oasis_recipe.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	OwnedPlacement MakeOwned(uint64 key, uint64 uniqueId, PlacementKind kind, const char* asset, float x)
	{
		OwnedPlacement op;
		op.key = key;
		op.uniqueId = uniqueId;
		op.kind = kind;
		op.written.key = key;
		op.written.kind = kind;
		op.written.asset = asset;
		op.written.position = Vector3(x, 2.0f, -3.5f);
		op.written.rotation = Quaternion(0.9f, 0.1f, 0.3f, 0.2f);
		op.written.scale = Vector3(1.1f, 0.9f, 1.0f);
		op.written.collides = (kind == PlacementKind::Entity);
		return op;
	}

	OasisRecord MakePopulated()
	{
		OasisRecord r;
		r.id = 42;
		r.name = "Korash Oasis";
		r.seed = 0x1234567890ABCDEFull;
		r.params.basinDepth = 8.0f;
		r.params.terraceSteps = 5;
		r.params.waterPlantDensity = 0.33f;
		r.params.maxSlopeDeg = 27.5f;
		r.params.shelfSink = 3.5f;
		r.params.clumpSink = 0.25f;
		r.loop.closed = true;
		r.loop.interpolation = SplineInterpolation::CatmullRom;
		r.loop.points = { { 0.0f, 10.0f, 1.0f }, { 10.0f, 0.0f, 2.0f }, { 0.0f, -10.0f, 3.0f }, { -10.0f, 0.0f, 4.0f } };
		r.influence = WorldRect{ -20.0f, -21.0f, 22.0f, 23.0f };

		r.terrain.outer = { { 1, 2, 3.0f, 4.0f }, { 5, 6, 7.0f, 8.0f } };
		r.terrain.inner = { { -1, -2, 0.5f, 0.75f } };
		r.terrain.waterHeights = { { 9, 9, 1.0f, 2.0f } };
		r.terrain.splat = { { 3, 4, 0xAABBCCDDu, 0x11223344u } };
		r.terrain.waterQuads = { { 7, 8, uint8(0), uint8(2) } };
		r.terrain.materials = { { 1, 1, String("Old.hmat"), String("Sand.hmat") } };
		r.terrain.touchedOuter = IntRect{ -3, -4, 5, 6 };

		r.placements.push_back(MakeOwned(10, 100, PlacementKind::Entity, "Rock01.hmsh", 1.0f));
		r.placements.push_back(MakeOwned(20, 200, PlacementKind::Foliage, "Palm01.hmsh", 2.0f));
		r.placements.push_back(MakeOwned(30, 300, PlacementKind::Foliage, "Bush.hmsh", 3.0f));
		return r;
	}

	std::vector<char> Serialize(const OasisRecord& record)
	{
		std::vector<char> buffer;
		io::VectorSink<char> sink(buffer);
		io::Writer writer(sink);
		REQUIRE(WriteOasisRecord(writer, record));
		return buffer;
	}

	bool Deserialize(const std::vector<char>& bytes, OasisRecord& out)
	{
		io::MemorySource source(bytes);
		io::Reader reader(source);
		return ReadOasisRecord(reader, out);
	}
}

TEST_CASE("Oasis record round trips", "[oasis_record]")
{
	const OasisRecord in = MakePopulated();
	const std::vector<char> bytes = Serialize(in);

	OasisRecord out;
	REQUIRE(Deserialize(bytes, out));

	CHECK(out.id == in.id);
	CHECK(out.name == in.name);
	CHECK(out.seed == in.seed);
	CHECK(out.params.basinDepth == 8.0f);
	CHECK(out.params.terraceSteps == 5);
	CHECK(out.params.waterPlantDensity == 0.33f);
	CHECK(out.params.maxSlopeDeg == 27.5f);
	CHECK(out.params.shelfSink == 3.5f);
	CHECK(out.params.clumpSink == 0.25f);
	CHECK(out.params.rimWidth == in.params.rimWidth);
	CHECK(out.loop.closed);
	CHECK(out.loop.interpolation == SplineInterpolation::CatmullRom);
	REQUIRE(out.loop.points.size() == 4);
	CHECK(out.loop.points[2].width == 3.0f);
	CHECK(out.loop.points[3].x == -10.0f);
	CHECK(out.influence.minZ == -21.0f);
	CHECK(out.influence.maxZ == 23.0f);

	REQUIRE(out.terrain.outer.size() == 2);
	CHECK(out.terrain.outer[1].written == 8.0f);
	REQUIRE(out.terrain.inner.size() == 1);
	CHECK(out.terrain.inner[0].x == -1);
	CHECK(out.terrain.inner[0].base == 0.5f);
	REQUIRE(out.terrain.waterHeights.size() == 1);
	REQUIRE(out.terrain.splat.size() == 1);
	CHECK(out.terrain.splat[0].base == 0xAABBCCDDu);
	CHECK(out.terrain.splat[0].written == 0x11223344u);
	REQUIRE(out.terrain.waterQuads.size() == 1);
	CHECK(out.terrain.waterQuads[0].written == 2);
	REQUIRE(out.terrain.materials.size() == 1);
	CHECK(out.terrain.materials[0].base == "Old.hmat");
	CHECK(out.terrain.materials[0].written == "Sand.hmat");
	CHECK(out.terrain.touchedOuter.minX == -3);
	CHECK(out.terrain.touchedOuter.maxZ == 6);

	REQUIRE(out.placements.size() == 3);
	for (size_t i = 0; i < 3; ++i)
	{
		CHECK(out.placements[i].key == in.placements[i].key);
		CHECK(out.placements[i].uniqueId == in.placements[i].uniqueId);
		CHECK(out.placements[i].kind == in.placements[i].kind);
		CHECK(out.placements[i].written.asset == in.placements[i].written.asset);
		CHECK(out.placements[i].written.position == in.placements[i].written.position);
		CHECK((out.placements[i].written.rotation == in.placements[i].written.rotation));
		CHECK(out.placements[i].written.scale == in.placements[i].written.scale);
		CHECK(out.placements[i].written.collides == in.placements[i].written.collides);
	}

	// Re-serializing the loaded record gives the same bytes.
	CHECK(Serialize(out) == bytes);
}

TEST_CASE("Oasis record writes are deterministic", "[oasis_record]")
{
	const OasisRecord record = MakePopulated();
	CHECK(Serialize(record) == Serialize(record));
}

TEST_CASE("Oasis record rejects bad magic, version and truncation", "[oasis_record]")
{
	const std::vector<char> good = Serialize(MakePopulated());
	OasisRecord out;

	std::vector<char> badMagic = good;
	badMagic[0] = 'X';
	CHECK_FALSE(Deserialize(badMagic, out));

	std::vector<char> badVersion = good;
	badVersion[4] = 99;
	CHECK_FALSE(Deserialize(badVersion, out));

	for (size_t cut : { size_t(0), size_t(3), size_t(10), good.size() / 2, good.size() - 1 })
	{
		const std::vector<char> truncated(good.begin(), good.begin() + cut);
		CHECK_FALSE(Deserialize(truncated, out));
	}
}

TEST_CASE("Oasis recipe parses overrides and assets", "[oasis_recipe]")
{
	const String json = R"({
		"basinDepth": 8,
		"terraceSteps": 3,
		"cliffs": ["Models/Cliff01.hmsh"],
		"shelves": ["Models/Shelf01.hmsh"],
		"clumps": ["Models/Clump01.hmsh"],
		"palms": ["Models/Palm01.hmsh"],
		"bushes": ["Models/Bush.hmsh"],
		"waterPlants": ["Models/Marshtail.hmsh"]
	})";

	OasisParams params;
	OasisAssets assets;
	REQUIRE(ParseOasisRecipe(json, params, assets));

	const OasisParams defaults;
	CHECK(params.basinDepth == 8.0f);
	CHECK(params.terraceSteps == 3);
	CHECK(params.rimWidth == defaults.rimWidth);
	CHECK(params.waterPlantDensity == defaults.waterPlantDensity);
	REQUIRE(assets.cliffs.size() == 1);
	CHECK(assets.cliffs[0] == "Models/Cliff01.hmsh");
	CHECK(assets.shelves.size() == 1);
	CHECK(assets.clumps.size() == 1);
	CHECK(assets.palms.size() == 1);
	CHECK(assets.bushes.size() == 1);
	CHECK(assets.waterPlants.size() == 1);
}

TEST_CASE("Oasis recipe parses materials", "[oasis_recipe]")
{
	OasisParams params;
	OasisAssets assets;
	REQUIRE(ParseOasisRecipe(R"({"assets":{"terrainMaterial":"Terrain/Sand.hmat","waterMaterial":"Water/Oasis.hmat"},"params":{"rimWidth":20.5}})", params, assets));
	CHECK(assets.terrainMaterial == "Terrain/Sand.hmat");
	CHECK(assets.waterMaterial == "Water/Oasis.hmat");
	CHECK(params.rimWidth == 20.5f);
}

TEST_CASE("Oasis recipe rejects malformed JSON", "[oasis_recipe]")
{
	OasisParams params;
	OasisAssets assets;
	CHECK_FALSE(ParseOasisRecipe("{ not json", params, assets));
	CHECK_FALSE(ParseOasisRecipe("[1,2]", params, assets));
	CHECK_FALSE(ParseOasisRecipe("", params, assets));
}
