// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/terrain_debug.h"
#include "pcg/section_path.h"

#include <cmath>
#include <map>
#include <set>
#include <utility>

using namespace mmo;
using namespace mmo::pcg;
using namespace mmo::pcg::graph;

namespace
{
	constexpr uint32 kRaise = 0xFF00FF00u;
	constexpr uint32 kLower = 0xFFFF0000u;

	PathSpline Square(const float x0, const float z0, const float size)
	{
		PathSpline s;
		s.closed = true;
		SplinePoint a; a.x = x0; a.z = z0;
		SplinePoint b; b.x = x0 + size; b.z = z0;
		SplinePoint c; c.x = x0 + size; c.z = z0 + size;
		SplinePoint d; d.x = x0; d.z = z0 + size;
		s.points = { a, b, c, d };
		return s;
	}

	TerrainOpInput MakeOp(const TerrainOpType type, const PathSpline& spline, const uint32 order, const AttributeSet& params = {})
	{
		TerrainOpInput in;
		in.op.type = type;
		in.op.order = order;
		in.op.params = params;
		in.spline = spline;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			in.pointIds.push_back(100 + i);
		}
		in.sourceId = 9;
		return in;
	}

	float Flat2(float, float)
	{
		return 2.0f;
	}

	bool IsTick(const DebugLine& line)
	{
		return line.a.x == line.b.x && line.a.z == line.b.z;
	}

	const PathSpline kBigRegion = Square(-500.0f, -500.0f, 1000.0f);

	void PrepareNoise(PreparedTerrain& t)
	{
		String error;
		REQUIRE(t.Prepare({ MakeOp(TerrainOpType::Noise, Square(0.0f, 0.0f, 100.0f), 3, { { "amplitude", 3.0f } }) }, Flat2, {},
			kBigRegion, 0.0f, error));
	}
}

TEST_CASE("A Noise op gives at most one tick per grid sample inside the influence", "[graph_terrain_debug]")
{
	PreparedTerrain t;
	PrepareNoise(t);
	const WorldRect influence = t.GetInfluence();

	const DebugGeometry g = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 500'000);
	CHECK(g.linesDrawn == g.lines.size());
	CHECK(g.linesTotal == g.lines.size());
	CHECK(g.pointsTotal == 0);

	std::set<std::pair<float, float>> samples;
	size_t outlines = 0;
	for (const DebugLine& line : g.lines)
	{
		if (!IsTick(line))
		{
			++outlines;
			continue;
		}
		// On the grid, inside the influence, from the base height, at most once per sample.
		CHECK(std::fmod(line.a.x, 4.0f) == 0.0f);
		CHECK(std::fmod(line.a.z, 4.0f) == 0.0f);
		CHECK(line.a.x >= influence.minX);
		CHECK(line.a.x <= influence.maxX);
		CHECK(line.a.z >= influence.minZ);
		CHECK(line.a.z <= influence.maxZ);
		CHECK(line.a.y == 2.0f);
		CHECK(line.b.y != line.a.y);
		CHECK(samples.emplace(line.a.x, line.a.z).second);

		// The tick ends at the composed height.
		const TerrainTarget target = t.Evaluate(line.a.x, line.a.z, 2.0f, {});
		REQUIRE(target.heightWeight > 0.0f);
		CHECK(line.b.y == Approx(2.0f + (target.height - 2.0f) * std::min(1.0f, target.heightWeight)).margin(1e-5f));
	}

	// 0..100 on a 4 m grid: 26 x 26 samples, the edge ones have no change (weight 0 at the area edge).
	CHECK(samples.size() > 400);
	CHECK(samples.size() <= 26 * 26);
	CHECK(outlines > 0);
}

TEST_CASE("Terrain debug respects the line budget", "[graph_terrain_debug]")
{
	PreparedTerrain t;
	PrepareNoise(t);

	const DebugGeometry full = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 500'000);
	REQUIRE(full.linesTotal > 100);

	const DebugGeometry limited = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 100);
	CHECK(limited.lines.size() <= 100);
	CHECK(limited.linesDrawn == limited.lines.size());
	CHECK(limited.linesTotal == full.linesTotal);
	for (size_t i = 0; i < limited.lines.size(); ++i)
	{
		CHECK(limited.lines[i].a == full.lines[i].a);
		CHECK(limited.lines[i].b == full.lines[i].b);
	}

	const DebugGeometry none = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 0);
	CHECK(none.lines.empty());
	CHECK(none.linesTotal == full.linesTotal);
}

TEST_CASE("Empty prepared terrain gives no lines", "[graph_terrain_debug]")
{
	PreparedTerrain t;
	String error;
	REQUIRE(t.Prepare({}, Flat2, {}, kBigRegion, 0.0f, error));
	const DebugGeometry g = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 500'000);
	CHECK(g.lines.empty());
	CHECK(g.linesTotal == 0);

	const PreparedTerrain unprepared;
	CHECK(BuildTerrainDebugGeometry(unprepared, Flat2, 4.0f, kRaise, kLower, 500'000).lines.empty());
}

TEST_CASE("Raise ticks use argbRaise, lower ticks use argbLower", "[graph_terrain_debug]")
{
	PreparedTerrain t;
	PrepareNoise(t);

	const DebugGeometry g = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 500'000);
	size_t raised = 0;
	size_t lowered = 0;
	for (const DebugLine& line : g.lines)
	{
		if (!IsTick(line))
		{
			CHECK(line.argb == kRaise);
			continue;
		}
		if (line.b.y > line.a.y)
		{
			CHECK(line.argb == kRaise);
			++raised;
		}
		else
		{
			CHECK(line.argb == kLower);
			++lowered;
		}
	}
	CHECK(raised > 0);
	CHECK(lowered > 0);
}

TEST_CASE("Section ops draw their copy rects", "[graph_terrain_debug]")
{
	SectionAsset asset;
	asset.name = "Flat";
	asset.length = 20.0f;
	asset.halfWidth = 3.0f;
	asset.sideFalloff = 4.0f;
	SectionVariant v;
	v.heights.spacing = 1.0f;
	v.heights.originX = -8;
	v.heights.width = 17;
	v.heights.depth = 21;
	v.heights.values.assign(17 * 21, -0.5f);
	asset.variants.push_back(v);
	const std::map<String, SectionAsset> assets{ { "Sections/Flat.hsec", asset } };
	const SectionAssetProvider provider = [&assets](const String& path, String& error) -> const SectionAsset*
	{
		const auto it = assets.find(path);
		if (it == assets.end())
		{
			error = "not found";
			return nullptr;
		}
		return &it->second;
	};

	PathSpline line;
	SplinePoint a; a.x = 0.0f; a.z = 0.0f;
	SplinePoint b; b.x = 100.0f; b.z = 0.0f;
	line.points = { a, b };
	TerrainOpInput op = MakeOp(TerrainOpType::SectionPath, line, 4);
	op.op.asset = "Sections/Flat.hsec";

	PreparedTerrain t;
	String error;
	REQUIRE(t.Prepare({ op }, Flat2, provider, kBigRegion, 0.0f, error));
	std::vector<TerrainOpOutline> outlines;
	t.GetOutlines(outlines);
	REQUIRE(outlines.size() == 1);
	REQUIRE(!outlines[0].copyRects.empty());

	const DebugGeometry g = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 500'000);
	for (const WorldRect& rect : outlines[0].copyRects)
	{
		// Every edge of the rect is drawn: lines lying on each of its four sides.
		bool minX = false, maxX = false, minZ = false, maxZ = false;
		for (const DebugLine& l : g.lines)
		{
			minX = minX || (l.a.x == rect.minX && l.b.x == rect.minX && l.a.z != l.b.z);
			maxX = maxX || (l.a.x == rect.maxX && l.b.x == rect.maxX && l.a.z != l.b.z);
			minZ = minZ || (l.a.z == rect.minZ && l.b.z == rect.minZ && l.a.x != l.b.x);
			maxZ = maxZ || (l.a.z == rect.maxZ && l.b.z == rect.maxZ && l.a.x != l.b.x);
		}
		CHECK(minX);
		CHECK(maxX);
		CHECK(minZ);
		CHECK(maxZ);
	}
}
