// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/section_asset.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

#include <limits>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	std::vector<char> Serialize(const SectionAsset& asset, bool* ok = nullptr)
	{
		std::vector<char> bytes;
		io::VectorSink<char> sink(bytes);
		io::Writer writer(sink);
		const bool result = WriteSectionAsset(writer, asset);
		if (ok != nullptr)
		{
			*ok = result;
		}
		return bytes;
	}

	bool Deserialize(const std::vector<char>& bytes, SectionAsset& out)
	{
		io::MemorySource source(bytes);
		io::Reader reader(source);
		return ReadSectionAsset(reader, out);
	}

	SectionVariant MakeVariant(const float originX, const float heightBase)
	{
		SectionVariant v;
		v.originX = originX;
		v.heights.spacing = 2.0f;
		v.heights.originX = -2;
		v.heights.originZ = 0;
		v.heights.width = 5;
		v.heights.depth = 3;
		for (uint32 i = 0; i < 15; ++i)
		{
			v.heights.values.push_back(heightBase + static_cast<float>(i) * 0.25f);
		}

		v.paint.spacing = 1.0f;
		v.paint.originX = -4;
		v.paint.originZ = 0;
		v.paint.width = 4;
		v.paint.depth = 2;
		for (uint32 i = 0; i < 8; ++i)
		{
			v.paint.packed.push_back(0x01020304u * (i + 1));
			v.paint.coverage.push_back(static_cast<uint8>(i * 30));
		}
		return v;
	}

	SectionAsset MakeAsset()
	{
		SectionAsset a;
		a.name = "dirt_road";
		a.length = 24.0f;
		a.halfWidth = 3.0f;
		a.sideFalloff = 5.0f;
		a.layerTextures = { "Tex/a.htex", "Tex/b.htex", "", "Tex/d.htex" };
		a.sourceStamp = 0x1122334455667788ull;

		PropRule r1;
		r1.asset = "Rocks/a.hmsh";
		r1.keepChance = 0.5f;
		r1.positionJitter = 0.3f;
		r1.yawJitterDeg = 180.0f;
		r1.scaleJitter = 0.1f;
		r1.swapGroup = { "Rocks/b.hmsh", "Rocks/c.hmsh" };
		PropRule r2;
		r2.asset = "Trees/t.hmsh";
		a.rules = { r1, r2 };

		SectionVariant v0 = MakeVariant(10.0f, -1.0f);
		SectionPlacement p0;
		p0.placement.key = 77;
		p0.placement.kind = PlacementKind::Entity;
		p0.placement.asset = "Rocks/a.hmsh";
		p0.placement.position = Vector3(2.0f, 0.5f, 10.0f);
		p0.placement.rotation = Quaternion(0.5f, 0.5f, 0.5f, 0.5f);
		p0.placement.scale = Vector3(1.5f, 1.5f, 1.5f);
		p0.placement.collides = false;
		p0.placement.materialOverrides = { { 1, "Mat/x.hmat" }, { 3, "Mat/y.hmat" } };
		p0.heightAboveTerrain = 0.5f;
		SectionPlacement p1;
		p1.placement.key = 78;
		p1.placement.kind = PlacementKind::Foliage;
		p1.placement.asset = "Foliage/bush";
		p1.placement.position = Vector3(-1.0f, 0.0f, 3.0f);
		v0.placements = { p0, p1 };

		SectionVariant v1 = MakeVariant(60.0f, 2.0f);
		SectionPlacement p2;
		p2.placement.key = 99;
		p2.placement.kind = PlacementKind::WorldModel;
		p2.placement.asset = "Wmo/hut.wmo";
		p2.placement.position = Vector3(1.0f, 4.0f, 5.0f);
		p2.placement.kitPaths = { "Kits/a.hkit", "Kits/b.hkit" };
		p2.heightAboveTerrain = 1.25f;
		v1.placements = { p2 };

		a.variants = { v0, v1 };
		return a;
	}

	void CheckPlacementEqual(const Placement& a, const Placement& b)
	{
		CHECK(a.key == b.key);
		CHECK(a.kind == b.kind);
		CHECK(a.asset == b.asset);
		CHECK((a.position == b.position));
		CHECK((a.rotation == b.rotation));
		CHECK((a.scale == b.scale));
		CHECK(a.collides == b.collides);
		CHECK(a.materialOverrides == b.materialOverrides);
		CHECK(a.kitPaths == b.kitPaths);
	}
}

TEST_CASE("Section asset round trips and re-writes byte identical", "[section_asset]")
{
	const SectionAsset asset = MakeAsset();
	bool ok = false;
	const std::vector<char> bytes = Serialize(asset, &ok);
	REQUIRE(ok);
	REQUIRE(!bytes.empty());

	SectionAsset loaded;
	REQUIRE(Deserialize(bytes, loaded));

	CHECK(loaded.name == asset.name);
	CHECK(loaded.length == asset.length);
	CHECK(loaded.halfWidth == asset.halfWidth);
	CHECK(loaded.sideFalloff == asset.sideFalloff);
	CHECK(loaded.layerTextures == asset.layerTextures);
	CHECK(loaded.sourceStamp == asset.sourceStamp);

	REQUIRE(loaded.rules.size() == 2);
	CHECK(loaded.rules[0].asset == "Rocks/a.hmsh");
	CHECK(loaded.rules[0].keepChance == 0.5f);
	CHECK(loaded.rules[0].positionJitter == 0.3f);
	CHECK(loaded.rules[0].yawJitterDeg == 180.0f);
	CHECK(loaded.rules[0].scaleJitter == 0.1f);
	CHECK(loaded.rules[0].swapGroup == asset.rules[0].swapGroup);
	CHECK(loaded.rules[1].asset == "Trees/t.hmsh");

	REQUIRE(loaded.variants.size() == 2);
	for (size_t i = 0; i < 2; ++i)
	{
		const SectionVariant& a = asset.variants[i];
		const SectionVariant& b = loaded.variants[i];
		CHECK(b.originX == a.originX);
		CHECK(b.heights.spacing == a.heights.spacing);
		CHECK(b.heights.originX == a.heights.originX);
		CHECK(b.heights.originZ == a.heights.originZ);
		CHECK(b.heights.width == a.heights.width);
		CHECK(b.heights.depth == a.heights.depth);
		CHECK(b.heights.values == a.heights.values);
		CHECK(b.paint.spacing == a.paint.spacing);
		CHECK(b.paint.originX == a.paint.originX);
		CHECK(b.paint.width == a.paint.width);
		CHECK(b.paint.depth == a.paint.depth);
		CHECK(b.paint.packed == a.paint.packed);
		CHECK(b.paint.coverage == a.paint.coverage);
		REQUIRE(b.placements.size() == a.placements.size());
		for (size_t p = 0; p < a.placements.size(); ++p)
		{
			CheckPlacementEqual(a.placements[p].placement, b.placements[p].placement);
			CHECK(a.placements[p].heightAboveTerrain == b.placements[p].heightAboveTerrain);
		}
	}
	CHECK(loaded.variants[1].placements[0].placement.kind == PlacementKind::WorldModel);
	CHECK(loaded.variants[1].placements[0].placement.kitPaths.size() == 2);

	CHECK(Serialize(loaded) == bytes);
}

TEST_CASE("Section asset rejects truncated and corrupt data", "[section_asset]")
{
	const std::vector<char> bytes = Serialize(MakeAsset());

	// Every strict prefix must fail cleanly.
	for (size_t size = 0; size < bytes.size(); ++size)
	{
		const std::vector<char> cut(bytes.begin(), bytes.begin() + size);
		SectionAsset out;
		CHECK_FALSE(Deserialize(cut, out));
	}

	std::vector<char> badMagic = bytes;
	badMagic[0] = 'X';
	SectionAsset out;
	CHECK_FALSE(Deserialize(badMagic, out));

	std::vector<char> badVersion = bytes;
	badVersion[4] = 99;
	CHECK_FALSE(Deserialize(badVersion, out));
}

TEST_CASE("Section asset write refuses mismatched arrays", "[section_asset]")
{
	SectionAsset asset = MakeAsset();
	asset.variants[0].heights.values.pop_back();
	bool ok = true;
	Serialize(asset, &ok);
	CHECK_FALSE(ok);
}

TEST_CASE("SampleHeight interpolates and clamps", "[section_asset]")
{
	SectionGrid grid;
	grid.spacing = 2.0f;
	grid.originX = -1;
	grid.originZ = 0;
	grid.width = 3;
	grid.depth = 2;
	// Local x = -2, 0, 2; local z = 0, 2.
	grid.values = { 1.0f, 3.0f, 5.0f,
					2.0f, 4.0f, 6.0f };

	CHECK(SampleHeight(grid, -2.0f, 0.0f) == Approx(1.0f));
	CHECK(SampleHeight(grid, 0.0f, 0.0f) == Approx(3.0f));
	CHECK(SampleHeight(grid, 2.0f, 2.0f) == Approx(6.0f));
	CHECK(SampleHeight(grid, 0.0f, 2.0f) == Approx(4.0f));

	// Midpoints.
	CHECK(SampleHeight(grid, -1.0f, 0.0f) == Approx(2.0f));
	CHECK(SampleHeight(grid, 0.0f, 1.0f) == Approx(3.5f));
	CHECK(SampleHeight(grid, -1.0f, 1.0f) == Approx(2.5f));

	// Clamped beyond the edges.
	CHECK(SampleHeight(grid, -50.0f, -50.0f) == Approx(1.0f));
	CHECK(SampleHeight(grid, 50.0f, 50.0f) == Approx(6.0f));
	CHECK(SampleHeight(grid, 50.0f, 0.0f) == Approx(5.0f));

	CHECK(SampleHeight(SectionGrid(), 1.0f, 1.0f) == 0.0f);
}

TEST_CASE("SamplePaint interpolates weights and coverage and clamps", "[section_asset]")
{
	SectionPaint paint;
	paint.spacing = 1.0f;
	paint.originX = 0;
	paint.originZ = 0;
	paint.width = 2;
	paint.depth = 1;
	// Sample 0: layer 0 = 200, layer 2 = 0. Sample 1: layer 0 = 100, layer 2 = 255.
	paint.packed = { 0x000000C8u, 0x00FF0064u };
	paint.coverage = { 0, 200 };

	std::array<float, 4> w{};
	float cov = -1.0f;

	SamplePaint(paint, 0.0f, 0.0f, w, cov);
	CHECK(w[0] == Approx(200.0f));
	CHECK(w[2] == Approx(0.0f));
	CHECK(cov == Approx(0.0f));

	SamplePaint(paint, 1.0f, 0.0f, w, cov);
	CHECK(w[0] == Approx(100.0f));
	CHECK(w[2] == Approx(255.0f));
	CHECK(cov == Approx(200.0f));

	SamplePaint(paint, 0.5f, 0.0f, w, cov);
	CHECK(w[0] == Approx(150.0f));
	CHECK(w[2] == Approx(127.5f));
	CHECK(cov == Approx(100.0f));

	SamplePaint(paint, 9.0f, 9.0f, w, cov);
	CHECK(w[0] == Approx(100.0f));
	CHECK(cov == Approx(200.0f));

	SamplePaint(paint, -9.0f, -9.0f, w, cov);
	CHECK(w[0] == Approx(200.0f));
	CHECK(cov == Approx(0.0f));
}

TEST_CASE("Texture keys compare case-insensitively with either slash", "[section_asset]")
{
	CHECK(NormalizeTextureKey("Textures\\Ground\\Grass.HTEX") == "textures/ground/grass.htex");
	CHECK(TextureKeysMatch("Textures\\Ground\\Grass.HTEX", "textures/ground/grass.htex"));
	CHECK(TextureKeysMatch("a/b.htex", "a/b.htex"));
	CHECK_FALSE(TextureKeysMatch("a/b.htex", "a/c.htex"));
	CHECK_FALSE(TextureKeysMatch("a/b.htex", "a/b.htex2"));
	CHECK_FALSE(TextureKeysMatch("", "a"));
	CHECK(TextureKeysMatch("", ""));
}

TEST_CASE("Section asset round trips variant roles and the corner angle", "[section_asset]")
{
	SectionAsset asset = MakeAsset();
	asset.cornerAngleDeg = 50.0f;
	asset.variants[0].role = SectionVariantRole::InnerCorner;
	asset.variants[1].role = SectionVariantRole::OuterCorner;

	const std::vector<char> bytes = Serialize(asset);
	SectionAsset loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(loaded.cornerAngleDeg == 50.0f);
	REQUIRE(loaded.variants.size() == 2);
	CHECK(loaded.variants[0].role == SectionVariantRole::InnerCorner);
	CHECK(loaded.variants[1].role == SectionVariantRole::OuterCorner);
	CHECK(Serialize(loaded) == bytes);

	// An unknown role byte is corrupt data.
	std::vector<char> badRole = bytes;
	badRole.back() = 9;
	CHECK_FALSE(Deserialize(badRole, loaded));
}

TEST_CASE("Section asset version 2 reads with Straight roles and the default angle", "[section_asset]")
{
	SectionAsset asset = MakeAsset();
	asset.cornerAngleDeg = 50.0f;
	asset.variants[0].role = SectionVariantRole::InnerCorner;

	// A version 2 stream is the version 3 stream without the trailer (angle + one role byte per variant).
	std::vector<char> bytes = Serialize(asset);
	bytes.resize(bytes.size() - 4 - asset.variants.size());
	bytes[4] = 2;

	SectionAsset loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(loaded.cornerAngleDeg == 35.0f);
	REQUIRE(loaded.variants.size() == 2);
	CHECK(loaded.variants[0].role == SectionVariantRole::Straight);
	CHECK(loaded.variants[1].role == SectionVariantRole::Straight);
	CHECK(loaded.variants[1].heights.values == asset.variants[1].heights.values);
}

TEST_CASE("Section asset rejects a non-finite corner angle and clamps it to 0..180", "[section_asset]")
{
	SectionAsset asset = MakeAsset();
	asset.cornerAngleDeg = std::numeric_limits<float>::quiet_NaN();
	SectionAsset loaded;
	CHECK_FALSE(Deserialize(Serialize(asset), loaded));

	asset.cornerAngleDeg = std::numeric_limits<float>::infinity();
	CHECK_FALSE(Deserialize(Serialize(asset), loaded));

	asset.cornerAngleDeg = 500.0f;
	REQUIRE(Deserialize(Serialize(asset), loaded));
	CHECK(loaded.cornerAngleDeg == 180.0f);

	asset.cornerAngleDeg = -20.0f;
	REQUIRE(Deserialize(Serialize(asset), loaded));
	CHECK(loaded.cornerAngleDeg == 0.0f);
}
