// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/oasis_record.h"
#include "pcg/oasis_recipe.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

#include <cstring>

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
		r.content.influence = WorldRect{ -20.0f, -21.0f, 22.0f, 23.0f };

		r.content.terrain.outer = { { 1, 2, 3.0f, 4.0f }, { 5, 6, 7.0f, 8.0f } };
		r.content.terrain.inner = { { -1, -2, 0.5f, 0.75f } };
		r.content.terrain.waterHeights = { { 9, 9, 1.0f, 2.0f } };
		r.content.terrain.splat = { { 3, 4, 0xAABBCCDDu, 0x11223344u } };
		r.content.terrain.waterQuads = { { 7, 8, uint8(0), uint8(2) } };
		r.content.terrain.materials = { { 1, 1, String("Old.hmat"), String("Sand.hmat") } };
		r.content.terrain.touchedOuter = IntRect{ -3, -4, 5, 6 };

		r.content.placements.push_back(MakeOwned(10, 100, PlacementKind::Entity, "Rock01.hmsh", 1.0f));
		r.content.placements.push_back(MakeOwned(20, 200, PlacementKind::Foliage, "Palm01.hmsh", 2.0f));
		r.content.placements.push_back(MakeOwned(30, 300, PlacementKind::Foliage, "Bush.hmsh", 3.0f));
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

	/// Bytes per loop point in a version 1 record (x, z, width).
	constexpr size_t V1PointBytes = 12;

	/// Bytes per loop point a version 2 record adds (auto flag, in x/z, out x/z).
	constexpr size_t V2PointExtraBytes = 17;

	/// Bytes per loop point a version 3 record adds (y, in y, out y).
	constexpr size_t V3PointExtraBytes = 12;

	/// Bytes the version 4 layout adds to one placement (override and kit lists).
	size_t PlacementExtraBytes(const Placement& p)
	{
		size_t bytes = 4 + 4;
		for (const PlacementMaterialOverride& o : p.materialOverrides)
		{
			bytes += 1 + 4 + o.materialName.size();
		}
		for (const String& kit : p.kitPaths)
		{
			bytes += 4 + kit.size();
		}
		return bytes;
	}

	/// Total bytes of one placement in the current (version 4) layout.
	size_t PlacementBytes(const OwnedPlacement& op)
	{
		// key, unique id, kind, then key, kind, asset, transform (10 floats), collides.
		return 8 + 8 + 1 + 8 + 1 + 4 + op.written.asset.size() + 40 + 1 + PlacementExtraBytes(op.written);
	}

	/// Rewrites a current (version 4) record of the given record into an older layout: the version
	/// field, loop points with only the fields that version had, and placements without version 4 data.
	std::vector<char> ToOlderVersion(const std::vector<char>& current, const OasisRecord& record, const uint32 version)
	{
		// magic, version, id, name (length + chars), seed.
		size_t offset = 4 + 4 + 8 + 4 + record.name.size() + 8;
		uint32 paramsSize = 0;
		std::memcpy(&paramsSize, &current[offset], sizeof(paramsSize));
		// params block, closed flag, interpolation, point count.
		offset += 4 + paramsSize + 1 + 1 + 4;

		size_t placementBytes = 0;
		for (const OwnedPlacement& op : record.content.placements)
		{
			placementBytes += PlacementBytes(op);
		}
		const size_t placementsStart = current.size() - placementBytes;

		size_t keep = V1PointBytes + V2PointExtraBytes + V3PointExtraBytes;
		if (version == 1)
		{
			keep = V1PointBytes;
		}
		else if (version == 2)
		{
			keep = V1PointBytes + V2PointExtraBytes;
		}
		std::vector<char> older(current.begin(), current.begin() + offset);
		std::memcpy(&older[4], &version, sizeof(version));
		for (size_t i = 0; i < record.loop.points.size(); ++i)
		{
			older.insert(older.end(), current.begin() + offset, current.begin() + offset + keep);
			offset += V1PointBytes + V2PointExtraBytes + V3PointExtraBytes;
		}
		older.insert(older.end(), current.begin() + offset, current.begin() + placementsStart);

		// Placements: drop the trailing override and kit lists.
		size_t cursor = placementsStart;
		for (const OwnedPlacement& op : record.content.placements)
		{
			const size_t total = PlacementBytes(op);
			older.insert(older.end(), current.begin() + cursor, current.begin() + cursor + total - PlacementExtraBytes(op.written));
			cursor += total;
		}
		return older;
	}
}
TEST_CASE("Oasis record keeps manual tangents bit-identically", "[oasis_record]")
{
	OasisRecord in = MakePopulated();
	SplinePoint& manual = in.loop.points[1];
	manual.autoTangents = false;
	manual.inX = -1.25f;
	manual.inZ = 3.5f;
	manual.outX = 7.75f;
	manual.outZ = -0.125f;

	const std::vector<char> bytes = Serialize(in);
	OasisRecord out;
	REQUIRE(Deserialize(bytes, out));
	REQUIRE(out.loop.points.size() == in.loop.points.size());
	for (size_t i = 0; i < in.loop.points.size(); ++i)
	{
		const SplinePoint& a = in.loop.points[i];
		const SplinePoint& b = out.loop.points[i];
		CHECK(b.autoTangents == a.autoTangents);
		CHECK(b.inX == a.inX);
		CHECK(b.inZ == a.inZ);
		CHECK(b.outX == a.outX);
		CHECK(b.outZ == a.outZ);
	}
	CHECK_FALSE(out.loop.points[1].autoTangents);
	CHECK(out.loop.points[0].autoTangents);

	CHECK(Serialize(out) == bytes);
	CHECK(Serialize(in) == bytes);
}

TEST_CASE("Oasis record reads version 1 files with automatic tangents", "[oasis_record]")
{
	const OasisRecord in = MakePopulated();
	const std::vector<char> current = Serialize(in);
	const std::vector<char> v1 = ToOlderVersion(current, in, 1);
	REQUIRE(v1.size() == current.size() - in.loop.points.size() * (V2PointExtraBytes + V3PointExtraBytes) - 3 * 8);

	OasisRecord out;
	REQUIRE(Deserialize(v1, out));
	CHECK_FALSE(out.heightsValid);
	REQUIRE(out.loop.points.size() == 4);
	for (const SplinePoint& p : out.loop.points)
	{
		CHECK(p.autoTangents);
		CHECK(p.inX == 0.0f);
		CHECK(p.outZ == 0.0f);
	}
	CHECK(out.loop.points[3].x == -10.0f);
	CHECK(out.loop.points[2].width == 3.0f);
	CHECK(out.name == in.name);
	REQUIRE(out.content.placements.size() == 3);
	CHECK(out.content.placements[2].written.asset == "Bush.hmsh");

	// Written back, it is the current version.
	CHECK(Serialize(out) == current);
}

TEST_CASE("Oasis record keeps heights and tangent heights bit-identically", "[oasis_record]")
{
	OasisRecord in = MakePopulated();
	for (size_t i = 0; i < in.loop.points.size(); ++i)
	{
		in.loop.points[i].y = 31.0f + 0.375f * static_cast<float>(i);
	}
	SplinePoint& manual = in.loop.points[2];
	manual.autoTangents = false;
	manual.inX = 2.0f;
	manual.inY = -1.5f;
	manual.inZ = 0.25f;
	manual.outX = -2.0f;
	manual.outY = 4.125f;
	manual.outZ = -0.75f;

	const std::vector<char> bytes = Serialize(in);
	CHECK(bytes[4] == 4);

	OasisRecord out;
	REQUIRE(Deserialize(bytes, out));
	CHECK(out.heightsValid);
	REQUIRE(out.loop.points.size() == in.loop.points.size());
	for (size_t i = 0; i < in.loop.points.size(); ++i)
	{
		CHECK(out.loop.points[i].y == in.loop.points[i].y);
		CHECK(out.loop.points[i].inY == in.loop.points[i].inY);
		CHECK(out.loop.points[i].outY == in.loop.points[i].outY);
	}
	CHECK(out.loop.points[2].outY == 4.125f);
	CHECK(Serialize(out) == bytes);
	CHECK(Serialize(in) == bytes);
}

TEST_CASE("Oasis record reads version 2 files without heights", "[oasis_record]")
{
	OasisRecord in = MakePopulated();
	SplinePoint& manual = in.loop.points[1];
	manual.autoTangents = false;
	manual.inX = -1.25f;
	manual.outZ = 6.5f;
	const std::vector<char> current = Serialize(in);
	const std::vector<char> v2 = ToOlderVersion(current, in, 2);
	REQUIRE(v2.size() == current.size() - in.loop.points.size() * V3PointExtraBytes - 3 * 8);

	OasisRecord out;
	REQUIRE(Deserialize(v2, out));
	CHECK_FALSE(out.heightsValid);
	REQUIRE(out.loop.points.size() == 4);
	CHECK_FALSE(out.loop.points[1].autoTangents);
	CHECK(out.loop.points[1].inX == -1.25f);
	CHECK(out.loop.points[1].outZ == 6.5f);
	for (const SplinePoint& p : out.loop.points)
	{
		CHECK(p.y == 0.0f);
		CHECK(p.inY == 0.0f);
		CHECK(p.outY == 0.0f);
	}
	REQUIRE(out.content.placements.size() == 3);
	CHECK(out.content.placements[1].written.asset == "Palm01.hmsh");
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
	CHECK(out.content.influence.minZ == -21.0f);
	CHECK(out.content.influence.maxZ == 23.0f);

	REQUIRE(out.content.terrain.outer.size() == 2);
	CHECK(out.content.terrain.outer[1].written == 8.0f);
	REQUIRE(out.content.terrain.inner.size() == 1);
	CHECK(out.content.terrain.inner[0].x == -1);
	CHECK(out.content.terrain.inner[0].base == 0.5f);
	REQUIRE(out.content.terrain.waterHeights.size() == 1);
	REQUIRE(out.content.terrain.splat.size() == 1);
	CHECK(out.content.terrain.splat[0].base == 0xAABBCCDDu);
	CHECK(out.content.terrain.splat[0].written == 0x11223344u);
	REQUIRE(out.content.terrain.waterQuads.size() == 1);
	CHECK(out.content.terrain.waterQuads[0].written == 2);
	REQUIRE(out.content.terrain.materials.size() == 1);
	CHECK(out.content.terrain.materials[0].base == "Old.hmat");
	CHECK(out.content.terrain.materials[0].written == "Sand.hmat");
	CHECK(out.content.terrain.touchedOuter.minX == -3);
	CHECK(out.content.terrain.touchedOuter.maxZ == 6);

	REQUIRE(out.content.placements.size() == 3);
	for (size_t i = 0; i < 3; ++i)
	{
		CHECK(out.content.placements[i].key == in.content.placements[i].key);
		CHECK(out.content.placements[i].uniqueId == in.content.placements[i].uniqueId);
		CHECK(out.content.placements[i].kind == in.content.placements[i].kind);
		CHECK(out.content.placements[i].written.asset == in.content.placements[i].written.asset);
		CHECK(out.content.placements[i].written.position == in.content.placements[i].written.position);
		CHECK((out.content.placements[i].written.rotation == in.content.placements[i].written.rotation));
		CHECK(out.content.placements[i].written.scale == in.content.placements[i].written.scale);
		CHECK(out.content.placements[i].written.collides == in.content.placements[i].written.collides);
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

TEST_CASE("Oasis record keeps world models, kits and material overrides bit-identically", "[oasis_record]")
{
	OasisRecord in = MakePopulated();
	OwnedPlacement wmo = MakeOwned(40, 400, PlacementKind::WorldModel, "World/Hut.wmo", 4.0f);
	wmo.written.kitPaths = { "Kits/Hut/Interior.hkit", "Kits/Hut/Props.hkit" };
	OwnedPlacement entity = MakeOwned(50, 500, PlacementKind::Entity, "Rock02.hmsh", 5.0f);
	entity.written.materialOverrides = { { 0, "Mat/Sand.hmat" }, { 3, "Mat/Moss.hmat" } };
	in.content.placements.push_back(wmo);
	in.content.placements.push_back(entity);

	const std::vector<char> bytes = Serialize(in);
	CHECK(bytes[4] == 4);

	OasisRecord out;
	REQUIRE(Deserialize(bytes, out));
	REQUIRE(out.content.placements.size() == 5);
	CHECK(out.content.placements[3].kind == PlacementKind::WorldModel);
	CHECK(out.content.placements[3].written.kind == PlacementKind::WorldModel);
	REQUIRE(out.content.placements[3].written.kitPaths.size() == 2);
	CHECK(out.content.placements[3].written.kitPaths[0] == "Kits/Hut/Interior.hkit");
	CHECK(out.content.placements[3].written.kitPaths[1] == "Kits/Hut/Props.hkit");
	CHECK(out.content.placements[3].written.materialOverrides.empty());
	REQUIRE(out.content.placements[4].written.materialOverrides.size() == 2);
	CHECK(out.content.placements[4].written.materialOverrides[1].materialIndex == 3);
	CHECK(out.content.placements[4].written.materialOverrides[1].materialName == "Mat/Moss.hmat");
	CHECK(out.content.placements[4].written.kitPaths.empty());

	CHECK(Serialize(out) == bytes);
	CHECK(Serialize(in) == bytes);
}

TEST_CASE("Oasis record reads version 3 files without overrides or kits", "[oasis_record]")
{
	const OasisRecord in = MakePopulated();
	const std::vector<char> current = Serialize(in);
	const std::vector<char> v3 = ToOlderVersion(current, in, 3);
	REQUIRE(v3.size() == current.size() - 3 * 8);

	OasisRecord out;
	REQUIRE(Deserialize(v3, out));
	CHECK(out.heightsValid);
	REQUIRE(out.content.placements.size() == 3);
	for (const OwnedPlacement& op : out.content.placements)
	{
		CHECK(op.written.materialOverrides.empty());
		CHECK(op.written.kitPaths.empty());
	}
	CHECK(out.content.placements[1].written.asset == "Palm01.hmsh");

	// Written back, it is the current version.
	CHECK(Serialize(out) == current);
}
