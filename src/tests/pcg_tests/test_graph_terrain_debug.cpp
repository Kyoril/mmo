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

TEST_CASE("Height ticks end at hand-computed heights", "[graph_terrain_debug]")
{
	// Carve along z = 0 on flat ground at 2: width 4 (per side), depth 1.5, bank 3. On the 2 m grid at x = 20 the
	// profile is 1 for |z| <= 4, 1 - smoothstep(2 / 3) = 7 / 27 at |z| = 6 and 0 from |z| = 7 on.
	PathSpline line;
	SplinePoint a; a.x = 0.0f; a.z = 0.0f;
	SplinePoint b; b.x = 40.0f; b.z = 0.0f;
	line.points = { a, b };
	PreparedTerrain t;
	String error;
	REQUIRE(t.Prepare({ MakeOp(TerrainOpType::Carve, line, 1, { { "width", 4.0f }, { "depth", 1.5f }, { "bank", 3.0f } }) }, Flat2, {},
		kBigRegion, 0.0f, error));

	const DebugGeometry g = BuildTerrainDebugGeometry(t, Flat2, 2.0f, kRaise, kLower, 500'000);
	std::map<float, DebugLine> ticks;
	for (const DebugLine& tick : g.lines)
	{
		if (IsTick(tick) && tick.a.x == 20.0f)
		{
			CHECK(ticks.emplace(tick.a.z, tick).second);
		}
	}

	const std::map<float, float> expected{
		{ -6.0f, 2.0f - 1.5f * 7.0f / 27.0f }, { -4.0f, 0.5f }, { -2.0f, 0.5f }, { 0.0f, 0.5f }, { 2.0f, 0.5f }, { 4.0f, 0.5f },
		{ 6.0f, 2.0f - 1.5f * 7.0f / 27.0f },
	};
	REQUIRE(ticks.size() == expected.size());
	for (const auto& [z, height] : expected)
	{
		const auto tick = ticks.find(z);
		REQUIRE(tick != ticks.end());
		CHECK(tick->second.a.y == 2.0f);
		CHECK(tick->second.b.y == Approx(height).margin(1e-5f));
		CHECK(tick->second.argb == kLower);
	}
}

TEST_CASE("Terrain debug respects the line budget", "[graph_terrain_debug]")
{
	PreparedTerrain t;
	PrepareNoise(t);

	const DebugGeometry full = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 500'000);
	REQUIRE(full.linesTotal > 100);

	size_t fullOutlines = 0;
	for (const DebugLine& line : full.lines)
	{
		fullOutlines += IsTick(line) ? 0 : 1;
	}
	// 0..100 on a 4 m grid.
	const size_t gridSamples = 26 * 26;

	// Once the budget is spent, the rest is estimated: every remaining grid sample counts as a tick.
	const DebugGeometry limited = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, fullOutlines + 100);
	CHECK(limited.lines.size() == fullOutlines + 100);
	CHECK(limited.linesDrawn == limited.lines.size());
	CHECK(limited.linesTotal >= full.linesTotal);
	CHECK(limited.linesTotal <= fullOutlines + gridSamples);
	for (size_t i = 0; i < limited.lines.size(); ++i)
	{
		CHECK(limited.lines[i].a == full.lines[i].a);
		CHECK(limited.lines[i].b == full.lines[i].b);
	}

	// Cut inside the outline: still a prefix.
	const DebugGeometry cut = BuildTerrainDebugGeometry(t, Flat2, 4.0f, kRaise, kLower, 10);
	REQUIRE(cut.lines.size() == 10);
	for (size_t i = 0; i < cut.lines.size(); ++i)
	{
		CHECK(cut.lines[i].a == full.lines[i].a);
	}
	CHECK(cut.linesTotal == fullOutlines + gridSamples);

	// A spent budget evaluates nothing: the base terrain is never read.
	size_t baseReads = 0;
	const auto countingBase = [&baseReads](const float, const float)
	{
		++baseReads;
		return 2.0f;
	};
	const DebugGeometry none = BuildTerrainDebugGeometry(t, countingBase, 4.0f, kRaise, kLower, 0);
	CHECK(none.lines.empty());
	CHECK(none.linesTotal == fullOutlines + gridSamples);
	CHECK(baseReads == 0);
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

TEST_CASE("A lake basin draws its shore loop and one depth tick at the centroid", "[graph_terrain_debug]")
{
	PreparedTerrain t;
	String error;
	REQUIRE(t.Prepare({ MakeOp(TerrainOpType::WaterArea, Square(0.0f, 0.0f, 40.0f), 3, { { "level", 1.0f }, { "depth", 3.0f } }) },
		Flat2, {}, kBigRegion, 0.0f, error));

	// A step wider than the area: no height ticks, so only the outline and the depth tick remain.
	const DebugGeometry g = BuildTerrainDebugGeometry(t, Flat2, 1000.0f, kRaise, kLower, 500'000);
	size_t loop = 0;
	size_t ticks = 0;
	for (const DebugLine& l : g.lines)
	{
		if (IsTick(l))
		{
			++ticks;
			CHECK(l.a.x == Approx(20.0f));
			CHECK(l.a.z == Approx(20.0f));
			CHECK(l.a.y == Approx(-2.0f));
			CHECK(l.b.y == Approx(1.0f));
		}
		else
		{
			++loop;
		}
	}
	CHECK(loop >= 4);
	CHECK(ticks == 1);

	// No basin, no depth tick.
	PreparedTerrain flat;
	REQUIRE(flat.Prepare({ MakeOp(TerrainOpType::WaterArea, Square(0.0f, 0.0f, 40.0f), 3, { { "level", 1.0f } }) }, Flat2, {}, kBigRegion, 0.0f, error));
	const DebugGeometry f = BuildTerrainDebugGeometry(flat, Flat2, 1000.0f, kRaise, kLower, 500'000);
	for (const DebugLine& l : f.lines)
	{
		CHECK(!IsTick(l));
	}
}

TEST_CASE("A river draws its surface line and its bed edges", "[graph_terrain_debug]")
{
	// A river along z = 10 on flat ground at 2 (spline on the ground): surface 1.6, bed edges at z = 5 and 15.
	PathSpline line;
	SplinePoint a; a.x = 110.0f; a.z = 10.0f; a.y = 2.0f;
	SplinePoint b; b.x = 150.0f; b.z = 10.0f; b.y = 2.0f;
	line.points = { a, b };
	PreparedTerrain t;
	String error;
	REQUIRE(t.Prepare({ MakeOp(TerrainOpType::River, line, 3) }, Flat2, {}, kBigRegion, 0.0f, error));

	// A step wider than the river (its influence holds no grid sample): no height ticks.
	const DebugGeometry g = BuildTerrainDebugGeometry(t, Flat2, 1000.0f, kRaise, kLower, 500'000);
	size_t surface = 0;
	size_t bed = 0;
	for (const DebugLine& l : g.lines)
	{
		CHECK_FALSE(IsTick(l));
		if (l.argb == TerrainDebugRiverSurfaceArgb)
		{
			++surface;
			CHECK(l.a.z == Approx(10.0f));
			CHECK(l.a.y == Approx(1.6f + TerrainDebugOutlineLift));
		}
		else
		{
			CHECK(l.argb == TerrainDebugRiverBedArgb);
			++bed;
			CHECK(std::abs(l.a.z - 10.0f) == Approx(5.0f));
			// On the composed ground: the bed edge lies on the bed floor, 0.4 m + 1.5 m deep.
			CHECK(l.a.y == Approx(0.1f + TerrainDebugOutlineLift).margin(1e-4));
		}
	}
	CHECK(surface == 40);
	CHECK(bed == 80);
	CHECK(TerrainDebugRiverSurfaceArgb != TerrainDebugRiverBedArgb);

	// The budget holds.
	const DebugGeometry cut = BuildTerrainDebugGeometry(t, Flat2, 1000.0f, kRaise, kLower, 50);
	CHECK(cut.lines.size() == 50);
	CHECK(cut.linesTotal == g.linesTotal);
}
