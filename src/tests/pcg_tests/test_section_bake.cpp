// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain.h"
#include "pcg/section_bake.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

#include <algorithm>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	constexpr uint32 kDefaultPaint = 0x000000FFu;
	constexpr uint32 kDipPaint = 0x00FF0000u;
	const std::array<String, 4> kTextures = { "Tex/a.htex", "Tex/b.htex", "Tex/c.htex", "Tex/d.htex" };

	/// 10x10 tiles (world 0..80), flat 0, default paint everywhere.
	void FlatTerrain(FakeTerrain& t)
	{
		std::fill(t.m_data.outer.begin(), t.m_data.outer.end(), 0.0f);
		std::fill(t.m_data.inner.begin(), t.m_data.inner.end(), 0.0f);
		std::fill(t.m_data.splat.begin(), t.m_data.splat.end(), kDefaultPaint);
	}

	/// 1 m dip at x in [centre-1, centre+1] along z 0..30, painted with layer 2.
	void CarveDip(FakeTerrain& t, const int32 centre)
	{
		for (int32 z = 0; z <= 30; ++z)
		{
			for (int32 x = centre - 1; x <= centre + 1; ++x)
			{
				t.SetOuterHeight(x, z, -1.0f);
				t.SetSplat(x, z, kDipPaint);
			}
		}
		for (int32 z = 0; z < 30; ++z)
		{
			for (int32 x = centre - 1; x < centre + 1; ++x)
			{
				t.SetInnerHeight(x, z, -1.0f);
			}
		}
	}

	SectionDefinition MakeDefinition()
	{
		SectionDefinition def;
		def.name = "road";
		def.length = 20.0f;
		def.halfWidth = 4.0f;
		def.sideFalloff = 4.0f;
		def.variants = { { 10.0f }, { 60.0f } };
		PropRule b;
		b.asset = "Rocks/b.hmsh";
		b.keepChance = 0.25f;
		b.positionJitter = 0.3f;
		b.swapGroup = { "Rocks/c.hmsh" };
		PropRule a;
		a.asset = "Rocks/a.hmsh";
		def.rules = { b, a };
		return def;
	}

	Placement MakePlacement(const uint64 key, const float x, const float y, const float z)
	{
		Placement p;
		p.key = key;
		p.asset = "Rocks/a.hmsh";
		p.position = Vector3(x, y, z);
		return p;
	}
}

TEST_CASE("BakeSection captures heights, paint and placements per variant", "[section_bake]")
{
	FakeTerrain terrain(10);
	FlatTerrain(terrain);
	CarveDip(terrain, 10);

	const std::vector<Placement> placements = {
		MakePlacement(1, 12.0f, 0.5f, 10.0f),   // variant 0, local (2, 10)
		MakePlacement(2, 61.0f, 1.0f, 5.0f),    // variant 1, local (1, 5)
		MakePlacement(3, 30.0f, 0.0f, 5.0f)     // outside every capture rect
	};

	SectionAsset asset;
	String error;
	size_t ignored = 0;
	REQUIRE(BakeSection(MakeDefinition(), terrain, placements, kDefaultPaint, kTextures, 0xABCDull, asset, error, &ignored));
	CHECK(ignored == 1);
	CHECK(asset.name == "road");
	CHECK(asset.length == 20.0f);
	CHECK(asset.halfWidth == 4.0f);
	CHECK(asset.sourceStamp == 0xABCDull);
	CHECK(asset.layerTextures == kTextures);

	// Rules are sorted by asset.
	REQUIRE(asset.rules.size() == 2);
	CHECK(asset.rules[0].asset == "Rocks/a.hmsh");
	CHECK(asset.rules[1].asset == "Rocks/b.hmsh");

	REQUIRE(asset.variants.size() == 2);
	const SectionVariant& v0 = asset.variants[0];
	CHECK(v0.originX == 10.0f);

	// Heights: -1 in the dip, 0 elsewhere, over the whole length.
	CHECK(v0.heights.spacing == 1.0f);
	CHECK(v0.heights.originX == -8);
	CHECK(v0.heights.width == 17);
	CHECK(v0.heights.originZ == 0);
	CHECK(v0.heights.depth == 21);
	for (float z = 0.0f; z <= 20.0f; z += 5.0f)
	{
		CHECK(SampleHeight(v0.heights, -1.0f, z) == Approx(-1.0f));
		CHECK(SampleHeight(v0.heights, 0.0f, z) == Approx(-1.0f));
		CHECK(SampleHeight(v0.heights, 1.0f, z) == Approx(-1.0f));
		CHECK(SampleHeight(v0.heights, -2.0f, z) == Approx(0.0f));
		CHECK(SampleHeight(v0.heights, 2.0f, z) == Approx(0.0f));
		CHECK(SampleHeight(v0.heights, 8.0f, z) == Approx(0.0f));
		CHECK(SampleHeight(v0.heights, -8.0f, z) == Approx(0.0f));
	}

	// Paint coverage only in the dip, with its paint.
	for (int32 x = -8; x <= 8; ++x)
	{
		std::array<float, 4> w{};
		float coverage = 0.0f;
		SamplePaint(v0.paint, static_cast<float>(x), 7.0f, w, coverage);
		if (x >= -1 && x <= 1)
		{
			CHECK(coverage > 0.0f);
			CHECK(w[2] == Approx(255.0f));
		}
		else
		{
			CHECK(coverage == 0.0f);
		}
	}

	// Placements.
	REQUIRE(v0.placements.size() == 1);
	const SectionPlacement& sp = v0.placements[0];
	CHECK(sp.placement.key == 1);
	CHECK(sp.placement.position.x == Approx(2.0f));
	CHECK(sp.placement.position.z == Approx(10.0f));
	CHECK(sp.placement.position.y == Approx(0.5f));
	CHECK(sp.heightAboveTerrain == Approx(0.5f));

	const SectionVariant& v1 = asset.variants[1];
	CHECK(v1.originX == 60.0f);
	REQUIRE(v1.placements.size() == 1);
	CHECK(v1.placements[0].placement.key == 2);
	CHECK(v1.placements[0].placement.position.x == Approx(1.0f));
	CHECK(v1.placements[0].heightAboveTerrain == Approx(1.0f));
	CHECK(SampleHeight(v1.heights, 0.0f, 10.0f) == Approx(0.0f));
}

TEST_CASE("BakeSection is deterministic", "[section_bake]")
{
	FakeTerrain terrain(10);
	FlatTerrain(terrain);
	CarveDip(terrain, 10);
	const std::vector<Placement> placements = { MakePlacement(1, 12.0f, 0.5f, 10.0f) };

	const auto bake = [&]()
	{
		SectionAsset asset;
		String error;
		REQUIRE(BakeSection(MakeDefinition(), terrain, placements, kDefaultPaint, kTextures, 1, asset, error));
		std::vector<char> bytes;
		io::VectorSink<char> sink(bytes);
		io::Writer writer(sink);
		REQUIRE(WriteSectionAsset(writer, asset));
		return bytes;
	};
	CHECK(bake() == bake());
}

TEST_CASE("BakeSection rejects invalid definitions", "[section_bake]")
{
	FakeTerrain terrain(10);
	FlatTerrain(terrain);
	SectionAsset asset;
	String error;

	SECTION("overlapping variants")
	{
		SectionDefinition def = MakeDefinition();
		def.variants = { { 10.0f }, { 15.0f } };
		CHECK_FALSE(BakeSection(def, terrain, {}, kDefaultPaint, kTextures, 0, asset, error));
		CHECK_FALSE(error.empty());
	}
	SECTION("no variants")
	{
		SectionDefinition def = MakeDefinition();
		def.variants.clear();
		CHECK_FALSE(BakeSection(def, terrain, {}, kDefaultPaint, kTextures, 0, asset, error));
		CHECK_FALSE(error.empty());
	}
	SECTION("no straight variant")
	{
		SectionDefinition def = MakeDefinition();
		def.variants[0].role = SectionVariantRole::InnerCorner;
		def.variants[1].role = SectionVariantRole::OuterCorner;
		CHECK_FALSE(BakeSection(def, terrain, {}, kDefaultPaint, kTextures, 0, asset, error));
		CHECK(error.find("straight") != String::npos);
	}
	SECTION("non-positive length")
	{
		SectionDefinition def = MakeDefinition();
		def.length = 0.0f;
		CHECK_FALSE(BakeSection(def, terrain, {}, kDefaultPaint, kTextures, 0, asset, error));
		CHECK_FALSE(error.empty());
	}
	SECTION("non-positive half width")
	{
		SectionDefinition def = MakeDefinition();
		def.halfWidth = -1.0f;
		CHECK_FALSE(BakeSection(def, terrain, {}, kDefaultPaint, kTextures, 0, asset, error));
		CHECK_FALSE(error.empty());
	}
	SECTION("touching rectangles are fine")
	{
		SectionDefinition def = MakeDefinition();
		def.variants = { { 10.0f }, { 26.0f } };
		CHECK(BakeSection(def, terrain, {}, kDefaultPaint, kTextures, 0, asset, error));
	}
}

TEST_CASE("SampleTerrainHeight follows the fan triangulation", "[section_bake]")
{
	FakeTerrain terrain;
	FlatTerrain(terrain);
	terrain.SetOuterHeight(4, 4, 4.0f);
	terrain.SetInnerHeight(4, 4, 2.0f);   // centre of the cell (4..5, 4..5)

	CHECK(SampleTerrainHeight(terrain, 4.0, 4.0) == Approx(4.0f));
	CHECK(SampleTerrainHeight(terrain, 4.5, 4.5) == Approx(2.0f));
	// Halfway between a corner and the centre.
	CHECK(SampleTerrainHeight(terrain, 4.25, 4.25) == Approx(3.0f));
	// Along an edge it is linear between the outer vertices.
	CHECK(SampleTerrainHeight(terrain, 4.5, 4.0) == Approx(2.0f));
	// The last row and column can be sampled exactly.
	CHECK(SampleTerrainHeight(terrain, 16.0, 16.0) == Approx(0.0f));
	CHECK(SampleTerrainHeight(terrain, 15.5, 16.0) == Approx(0.0f));
}

TEST_CASE("Section definition JSON round trips", "[section_bake]")
{
	const SectionDefinition def = MakeDefinition();
	const String json = WriteSectionDefinition(def);
	CHECK_FALSE(json.empty());

	SectionDefinition parsed;
	REQUIRE(ParseSectionDefinition(json, parsed));
	CHECK(parsed.name == def.name);
	CHECK(parsed.length == def.length);
	CHECK(parsed.halfWidth == def.halfWidth);
	CHECK(parsed.sideFalloff == def.sideFalloff);
	REQUIRE(parsed.variants.size() == 2);
	CHECK(parsed.variants[1].originX == 60.0f);
	REQUIRE(parsed.rules.size() == 2);
	CHECK(parsed.rules[0].asset == "Rocks/b.hmsh");
	CHECK(parsed.rules[0].keepChance == 0.25f);
	CHECK(parsed.rules[0].positionJitter == 0.3f);
	CHECK(parsed.rules[0].swapGroup == std::vector<String>{ "Rocks/c.hmsh" });

	CHECK(WriteSectionDefinition(parsed) == json);

	// Keys appear in a fixed (alphabetical) order.
	CHECK(json.find("\"halfWidth\"") < json.find("\"length\""));
	CHECK(json.find("\"length\"") < json.find("\"name\""));

	SectionDefinition bad;
	CHECK_FALSE(ParseSectionDefinition("not json", bad));
	CHECK_FALSE(ParseSectionDefinition("[1,2]", bad));
}

TEST_CASE("Section definition parsing clamps rule values", "[section_bake]")
{
	const String json = R"({"name":"Clamp","rules":[)"
		R"({"asset":"Rocks/a.hmsh","keepChance":1.5,"positionJitter":-2,"yawJitterDeg":-10,"scaleJitter":-0.5},)"
		R"({"asset":"Rocks/b.hmsh","keepChance":-0.25,"positionJitter":0.5,"yawJitterDeg":15,"scaleJitter":0.1}]})";

	SectionDefinition parsed;
	REQUIRE(ParseSectionDefinition(json, parsed));
	REQUIRE(parsed.rules.size() == 2);
	CHECK(parsed.rules[0].keepChance == 1.0f);
	CHECK(parsed.rules[0].positionJitter == 0.0f);
	CHECK(parsed.rules[0].yawJitterDeg == 0.0f);
	CHECK(parsed.rules[0].scaleJitter == 0.0f);
	CHECK(parsed.rules[1].keepChance == 0.0f);
	CHECK(parsed.rules[1].positionJitter == 0.5f);
	CHECK(parsed.rules[1].yawJitterDeg == 15.0f);
	CHECK(parsed.rules[1].scaleJitter == 0.1f);
}

TEST_CASE("Section definition canonical text ignores line endings", "[section_bake]")
{
	const String lf = WriteSectionDefinition(MakeDefinition());
	REQUIRE(lf.find('\n') != String::npos);
	String crlf;
	for (const char c : lf)
	{
		if (c == '\n')
		{
			crlf.push_back('\r');
		}
		crlf.push_back(c);
	}
	REQUIRE(crlf != lf);

	CHECK(CanonicalSectionDefinitionText(lf) == lf);
	CHECK(CanonicalSectionDefinitionText(crlf) == lf);

	// A changed value still changes the canonical text.
	SectionDefinition changed = MakeDefinition();
	changed.length += 1.0f;
	CHECK(CanonicalSectionDefinitionText(WriteSectionDefinition(changed)) != lf);

	// Text that does not parse falls back to the raw text without carriage returns.
	CHECK(CanonicalSectionDefinitionText("not\r\njson\r\n") == "not\njson\n");
	CHECK(CanonicalSectionDefinitionText("not\njson\n") == "not\njson\n");
}

TEST_CASE("Section definition JSON round trips variant roles and the corner angle", "[section_bake]")
{
	SectionDefinition def = MakeDefinition();
	def.cornerAngleDeg = 50.0f;
	def.variants[0].role = SectionVariantRole::InnerCorner;
	def.variants[1].role = SectionVariantRole::OuterCorner;

	const String json = WriteSectionDefinition(def);
	CHECK(json.find("\"innerCorner\"") != String::npos);
	CHECK(json.find("\"outerCorner\"") != String::npos);

	SectionDefinition parsed;
	REQUIRE(ParseSectionDefinition(json, parsed));
	CHECK(parsed.cornerAngleDeg == 50.0f);
	REQUIRE(parsed.variants.size() == 2);
	CHECK(parsed.variants[0].role == SectionVariantRole::InnerCorner);
	CHECK(parsed.variants[1].role == SectionVariantRole::OuterCorner);
	CHECK(WriteSectionDefinition(parsed) == json);
}

TEST_CASE("Section definition without roles keeps its text and defaults", "[section_bake]")
{
	const SectionDefinition def = MakeDefinition();
	const String json = WriteSectionDefinition(def);
	CHECK(json.find("role") == String::npos);
	CHECK(json.find("cornerAngleDeg") == String::npos);

	SectionDefinition parsed;
	REQUIRE(ParseSectionDefinition(json, parsed));
	CHECK(parsed.cornerAngleDeg == 35.0f);
	REQUIRE(parsed.variants.size() == 2);
	CHECK(parsed.variants[0].role == SectionVariantRole::Straight);
	CHECK(CanonicalSectionDefinitionText(json) == json);

	// Unknown role names fall back to Straight.
	SectionDefinition odd;
	REQUIRE(ParseSectionDefinition("{\"variants\":[{\"originX\":1,\"role\":\"bogus\"}]}", odd));
	CHECK(odd.variants[0].role == SectionVariantRole::Straight);
}

TEST_CASE("BakeSection copies variant roles and the corner angle", "[section_bake]")
{
	SectionDefinition def = MakeDefinition();
	def.cornerAngleDeg = 60.0f;
	def.variants[1].role = SectionVariantRole::OuterCorner;
	FakeTerrain terrain(10);
	FlatTerrain(terrain);
	SectionAsset out;
	String error;
	REQUIRE(BakeSection(def, terrain, {}, kDefaultPaint, kTextures, 1ull, out, error));
	CHECK(out.cornerAngleDeg == 60.0f);
	CHECK(out.variants[0].role == SectionVariantRole::Straight);
	CHECK(out.variants[1].role == SectionVariantRole::OuterCorner);
}
