// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/section_path.h"
#include "pcg/section_path_record.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

#include <array>
#include <cmath>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	/// Section L 20, halfWidth 3, sideFalloff 4. The variant has a non-zero height grid (-0.5 for |x| <= 2), painted
	/// coverage on the same strip and one prop placed 2 m above the terrain.
	SectionAsset MakeKeepGroundAsset()
	{
		SectionAsset asset;
		asset.name = "keep";
		asset.length = 20.0f;
		asset.halfWidth = 3.0f;
		asset.sideFalloff = 4.0f;
		SectionVariant v;
		v.heights.spacing = 1.0f;
		v.heights.originX = -8;
		v.heights.originZ = 0;
		v.heights.width = 17;
		v.heights.depth = 21;
		v.paint.spacing = 1.0f;
		v.paint.originX = -8;
		v.paint.originZ = 0;
		v.paint.width = 17;
		v.paint.depth = 21;
		for (uint32 j = 0; j < 21; ++j)
		{
			for (uint32 i = 0; i < 17; ++i)
			{
				const int32 x = -8 + static_cast<int32>(i);
				const bool strip = std::abs(x) <= 2;
				v.heights.values.push_back(strip ? -0.5f : 0.0f);
				v.paint.packed.push_back(strip ? (55u | (200u << 16)) : 255u);
				v.paint.coverage.push_back(strip ? 255 : 0);
			}
		}
		SectionPlacement prop;
		prop.placement.key = 1000;
		prop.placement.kind = PlacementKind::Entity;
		prop.placement.asset = "Props/rock.hmsh";
		prop.placement.position = Vector3(1.0f, 123.0f, 5.0f);
		prop.heightAboveTerrain = 2.0f;
		v.placements.push_back(prop);
		asset.variants.push_back(v);
		return asset;
	}

	PathSpline MakeLine()
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::Linear;
		SplinePoint a;
		a.x = 0.0f; a.z = 0.0f; a.y = 10.0f;
		SplinePoint b;
		b.x = 0.0f; b.z = 100.0f; b.y = 10.0f;
		spline.points = { a, b };
		return spline;
	}

	std::array<int8, 4> Identity(float, float)
	{
		return { 0, 1, 2, 3 };
	}

	float Flat(float, float)
	{
		return 0.0f;
	}

	bool Prepare(const SectionPathParams& params, const std::function<float(float, float)>& baseGround, SectionPathShape& shape, String& error)
	{
		const PathSpline spline = MakeLine();
		return PrepareSectionPath(spline, { 1, 2 }, MakeKeepGroundAsset(), params, baseGround, shape, error);
	}

	std::vector<char> Serialize(const SectionPathRecord& record)
	{
		std::vector<char> buffer;
		io::VectorSink<char> sink(buffer);
		io::Writer writer(sink);
		REQUIRE(WriteSectionPathRecord(writer, record));
		return buffer;
	}

	bool Deserialize(const std::vector<char>& bytes, SectionPathRecord& out)
	{
		io::MemorySource source(bytes);
		io::Reader reader(source);
		return ReadSectionPathRecord(reader, out);
	}

	SectionPathRecord MakeRecord(const SectionHeightMode mode)
	{
		SectionPathRecord r;
		r.id = 3;
		r.name = "Field edge";
		r.sectionAsset = "Sections/Edge.hsec";
		r.params.heightMode = mode;
		SplinePoint a;
		a.x = 0.0f; a.z = 0.0f;
		SplinePoint b;
		b.x = 10.0f; b.z = 0.0f;
		r.spline.points = { a, b };
		r.pointIds = { 1, 2 };
		r.nextPointId = 3;
		return r;
	}
}

TEST_CASE("keepGround contributes no height but still paints", "[section_keep_ground]")
{
	SectionPathParams keep;
	keep.heightMode = SectionHeightMode::KeepGround;
	SectionPathParams follow;
	follow.heightMode = SectionHeightMode::FollowGround;

	SectionPathShape keepShape;
	SectionPathShape followShape;
	String error;
	REQUIRE(Prepare(keep, nullptr, keepShape, error));
	REQUIRE(Prepare(follow, Flat, followShape, error));

	bool painted = false;
	for (const float z : { 5.0f, 30.0f, 47.5f, 80.0f })
	{
		for (float x = -6.0f; x <= 6.0f; x += 0.75f)
		{
			const TerrainTarget k = EvaluateSectionTerrain(keepShape, x, z, 0.0f, Identity);
			const TerrainTarget f = EvaluateSectionTerrain(followShape, x, z, 0.0f, Identity);
			CHECK(k.heightWeight == 0.0f);
			CHECK(k.splatWeight == f.splatWeight);
			painted = painted || k.splatWeight > 0.0f;
		}
	}
	CHECK(painted);
}

TEST_CASE("keepGround needs no base ground and puts props on the ground", "[section_keep_ground]")
{
	SectionPathParams params;
	params.heightMode = SectionHeightMode::KeepGround;
	SectionPathShape shape;
	String error;
	REQUIRE(Prepare(params, nullptr, shape, error));

	const auto ground = [](float x, float) { return 0.1f * x + 7.0f; };
	std::vector<Placement> out;
	BuildSectionPlacements(shape, ground, out);
	REQUIRE_FALSE(out.empty());
	for (const Placement& p : out)
	{
		CHECK(p.position.y == Approx(ground(p.position.x, p.position.z) + 2.0f).margin(1e-4));
	}
}

TEST_CASE("keepGround refuses the mass layer", "[section_keep_ground]")
{
	SectionPathParams params;
	params.heightMode = SectionHeightMode::KeepGround;
	params.massEnabled = true;
	SectionPathShape shape;
	String error;
	CHECK_FALSE(Prepare(params, Flat, shape, error));
	CHECK(error.find("keepGround") != String::npos);
}

TEST_CASE("SetSectionPathParam parses keepGround", "[section_keep_ground]")
{
	SectionPathParams params;
	String error;
	REQUIRE(SetSectionPathParam(params, "heightMode", "keepGround", error));
	CHECK(params.heightMode == SectionHeightMode::KeepGround);
	CHECK(static_cast<int>(SectionHeightMode::KeepGround) == 3);
}

TEST_CASE("section path record version 5 keeps keepGround, version 4 bytes still read", "[section_keep_ground]")
{
	const std::vector<char> bytes = Serialize(MakeRecord(SectionHeightMode::KeepGround));
	REQUIRE(bytes[4] == 5);
	SectionPathRecord loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(loaded.params.heightMode == SectionHeightMode::KeepGround);
	CHECK(Serialize(loaded) == bytes);

	// A version 4 stream of another mode reads as before; with height mode byte 3 it is refused.
	std::vector<char> v4 = Serialize(MakeRecord(SectionHeightMode::SmoothedGround));
	v4[4] = 4;
	REQUIRE(Deserialize(v4, loaded));
	CHECK(loaded.params.heightMode == SectionHeightMode::SmoothedGround);

	std::vector<char> v4Keep = bytes;
	v4Keep[4] = 4;
	CHECK_FALSE(Deserialize(v4Keep, loaded));
}
