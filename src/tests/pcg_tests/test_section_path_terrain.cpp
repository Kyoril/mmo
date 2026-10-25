// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/pcg_random.h"
#include "pcg/section_path.h"

#include <cmath>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	/// Synthetic section: L 20, halfWidth 3, sideFalloff 4. Every variant is -bed for |x| <= 2 else 0, and paints
	/// layer 0 = 55 / layer 2 = 200 with coverage 255 for |x| <= 2.
	SectionAsset MakeAsset(const std::vector<float>& beds)
	{
		SectionAsset asset;
		asset.name = "test";
		asset.length = 20.0f;
		asset.halfWidth = 3.0f;
		asset.sideFalloff = 4.0f;
		for (const float bed : beds)
		{
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
					const bool road = std::abs(x) <= 2;
					v.heights.values.push_back(road ? -bed : 0.0f);
					v.paint.packed.push_back(road ? (55u | (200u << 16)) : 255u);
					v.paint.coverage.push_back(road ? 255 : 0);
				}
			}
			asset.variants.push_back(v);
		}
		return asset;
	}

	PathSpline Line(const float x0, const float z0, const float x1, const float z1, const float y0, const float y1)
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::Linear;
		SplinePoint a; a.x = x0; a.z = z0; a.y = y0;
		SplinePoint b; b.x = x1; b.z = z1; b.y = y1;
		spline.points = { a, b };
		return spline;
	}

	float Flat0(float, float)
	{
		return 0.0f;
	}

	std::array<int8, 4> Identity(float, float)
	{
		return { 0, 1, 2, 3 };
	}

	void Prepare(const PathSpline& spline, const SectionAsset& asset, const SectionPathParams& params, SectionPathShape& shape)
	{
		std::vector<uint64> ids;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			ids.push_back(1 + i);
		}
		String error;
		REQUIRE(PrepareSectionPath(spline, ids, asset, params, Flat0, shape, error));
		CHECK(error.empty());
	}
}

TEST_CASE("A flat spline carves the bed with full weight on the centre line", "[section_path_terrain]")
{
	const SectionAsset asset = MakeAsset({ 0.5f });
	SectionPathShape shape;
	Prepare(Line(0.0f, 0.0f, 0.0f, 100.0f, 10.0f, 10.0f), asset, SectionPathParams(), shape);

	const TerrainTarget centre = EvaluateSectionTerrain(shape, 0.0f, 30.0f, 0.0f, Identity);
	CHECK(centre.height == Approx(9.5f));
	CHECK(centre.heightWeight == Approx(1.0f));
	CHECK_FALSE(centre.setMaterial);

	const TerrainTarget side = EvaluateSectionTerrain(shape, 5.0f, 30.0f, 0.0f, Identity);
	CHECK(side.heightWeight > 0.0f);
	CHECK(side.heightWeight < 1.0f);

	const TerrainTarget outside = EvaluateSectionTerrain(shape, 8.0f, 30.0f, 0.0f, Identity);
	CHECK(outside.heightWeight == 0.0f);
	CHECK(outside.splatWeight == 0.0f);
}

TEST_CASE("The centre line follows a rising spline", "[section_path_terrain]")
{
	const SectionAsset asset = MakeAsset({ 0.5f });
	SectionPathShape shape;
	Prepare(Line(0.0f, 0.0f, 0.0f, 100.0f, 10.0f, 20.0f), asset, SectionPathParams(), shape);

	const TerrainTarget centre = EvaluateSectionTerrain(shape, 0.0f, 50.0f, 0.0f, Identity);
	CHECK(centre.height == Approx(14.5f).margin(0.01f));
}

TEST_CASE("Copy joins cross-fade between different variants", "[section_path_terrain]")
{
	// Edge heights of the two variants differ by 0.4.
	const SectionAsset asset = MakeAsset({ 0.5f, 0.1f });
	SectionPathParams params;
	params.sequentialVariants = true;
	SectionPathShape shape;
	Prepare(Line(0.0f, 0.0f, 0.0f, 100.0f, 10.0f, 10.0f), asset, params, shape);

	REQUIRE(shape.layout.copies.size() == 5);
	REQUIRE(GetVariant(shape, shape.layout.copies[0]) != GetVariant(shape, shape.layout.copies[1]));

	const float eps = 0.01f;
	const TerrainTarget before = EvaluateSectionTerrain(shape, 0.0f, 20.0f - eps, 0.0f, Identity);
	const TerrainTarget after = EvaluateSectionTerrain(shape, 0.0f, 20.0f + eps, 0.0f, Identity);
	CHECK(std::fabs(before.height - after.height) < 0.01f);

	// Away from the join each copy carries its own bed.
	const TerrainTarget mid0 = EvaluateSectionTerrain(shape, 0.0f, 10.0f, 0.0f, Identity);
	const TerrainTarget mid1 = EvaluateSectionTerrain(shape, 0.0f, 30.0f, 0.0f, Identity);
	CHECK(std::fabs(mid0.height - mid1.height) == Approx(0.4f).margin(1e-4f));
}

TEST_CASE("Tight bends are deterministic and reported", "[section_path_terrain]")
{
	PathSpline spline;
	spline.interpolation = SplineInterpolation::CatmullRom;
	const float pts[5][2] = { { 0.0f, 0.0f }, { 0.0f, 10.0f }, { 3.0f, 13.0f }, { 6.0f, 10.0f }, { 6.0f, 0.0f } };
	for (const auto& p : pts)
	{
		SplinePoint sp;
		sp.x = p[0];
		sp.z = p[1];
		sp.y = 5.0f;
		spline.points.push_back(sp);
	}

	const SectionAsset asset = MakeAsset({ 0.5f, 0.3f });
	SectionPathShape a;
	SectionPathShape b;
	Prepare(spline, asset, SectionPathParams(), a);
	Prepare(spline, asset, SectionPathParams(), b);
	CHECK(a.layout.minimumBendRadius < 7.0f);

	size_t touched = 0;
	for (float z = -8.0f; z <= 22.0f; z += 0.5f)
	{
		for (float x = -8.0f; x <= 14.0f; x += 0.5f)
		{
			const TerrainTarget ta = EvaluateSectionTerrain(a, x, z, 0.0f, Identity);
			const TerrainTarget tb = EvaluateSectionTerrain(b, x, z, 0.0f, Identity);
			REQUIRE(ta.height == tb.height);
			REQUIRE(ta.heightWeight == tb.heightWeight);
			REQUIRE(ta.splat == tb.splat);
			REQUIRE(ta.splatWeight == tb.splatWeight);
			if (ta.heightWeight > 0.0f)
			{
				++touched;
			}
		}
	}
	CHECK(touched > 0);
}

TEST_CASE("Unavailable paint layers are dropped and the rest renormalised", "[section_path_terrain]")
{
	const SectionAsset asset = MakeAsset({ 0.5f });
	SectionPathShape shape;
	Prepare(Line(0.0f, 0.0f, 100.0f, 0.0f, 10.0f, 10.0f), asset, SectionPathParams(), shape);

	const LayerRemapFunction remap = [](const float x, float) -> std::array<int8, 4>
	{
		if (x > 50.0f)
		{
			return { 0, 1, -1, 3 };
		}
		return { 0, 1, 2, 3 };
	};

	const TerrainTarget west = EvaluateSectionTerrain(shape, 30.0f, 0.0f, 0.0f, remap);
	CHECK(west.splatWeight == Approx(1.0f));
	CHECK(west.splat[2] > 0);
	CHECK(west.splat[0] + west.splat[1] + west.splat[2] + west.splat[3] == 255);

	for (float x = 51.0f; x < 95.0f; x += 3.7f)
	{
		const TerrainTarget east = EvaluateSectionTerrain(shape, x, 0.0f, 0.0f, remap);
		CHECK(east.splatWeight > 0.0f);
		CHECK(east.splat[2] == 0);
		CHECK(east.splat[0] + east.splat[1] + east.splat[2] + east.splat[3] == 255);
	}

	// Remapping to another tile index moves the weight there.
	const LayerRemapFunction moved = [](float, float) -> std::array<int8, 4> { return { 0, 1, 3, -1 }; };
	const TerrainTarget m = EvaluateSectionTerrain(shape, 30.0f, 0.0f, 0.0f, moved);
	CHECK(m.splat[3] == 200);
	CHECK(m.splat[0] == 55);
}

TEST_CASE("Follow-ground mode takes the centre height from the base ground", "[section_path_terrain]")
{
	const SectionAsset asset = MakeAsset({ 0.5f });
	SectionPathParams params;
	params.heightMode = SectionHeightMode::FollowGround;
	SectionPathShape shape;
	String error;
	const std::function<float(float, float)> ground = [](float, const float z) { return 2.0f + z * 0.1f; };
	REQUIRE(PrepareSectionPath(Line(0.0f, 0.0f, 0.0f, 100.0f, 50.0f, 50.0f), { 7, 8 }, asset, params, ground, shape, error));
	const TerrainTarget t = EvaluateSectionTerrain(shape, 1.0f, 30.0f, 0.0f, Identity);
	CHECK(t.height == Approx(4.5f).margin(1e-3f));
}

TEST_CASE("Invalid section path input is refused", "[section_path_terrain]")
{
	const SectionAsset asset = MakeAsset({ 0.5f });
	SectionPathShape shape;
	String error;
	CHECK_FALSE(PrepareSectionPath(Line(0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f), { 1 }, asset, SectionPathParams(), Flat0, shape, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(PrepareSectionPath(Line(0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f), { 1, 1 }, asset, SectionPathParams(), Flat0, shape, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(PrepareSectionPath(Line(0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f), { 1, 2 }, MakeAsset({}), SectionPathParams(), Flat0, shape, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("The influence covers the corridor", "[section_path_terrain]")
{
	const SectionAsset asset = MakeAsset({ 0.5f });
	SectionPathShape shape;
	Prepare(Line(0.0f, 0.0f, 0.0f, 100.0f, 10.0f, 10.0f), asset, SectionPathParams(), shape);
	const WorldRect r = GetInfluenceRect(shape);
	CHECK(r.minX <= -7.0f);
	CHECK(r.maxX >= 7.0f);
	CHECK(r.minZ <= -7.0f);
	CHECK(r.maxZ >= 107.0f);
}

TEST_CASE("The accelerated projection matches the full scan exactly", "[section_path_terrain]")
{
	// Winding closed and open splines, including near-touching parts.
	for (int closed = 0; closed < 2; ++closed)
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::CatmullRom;
		spline.closed = closed == 1;
		const float pts[8][2] = { { 0.0f, 0.0f }, { 40.0f, 10.0f }, { 50.0f, 60.0f }, { 10.0f, 70.0f }, { 5.0f, 30.0f },
			{ 30.0f, 35.0f }, { 80.0f, 20.0f }, { 120.0f, 90.0f } };
		for (const auto& p : pts)
		{
			SplinePoint sp;
			sp.x = p[0];
			sp.z = p[1];
			spline.points.push_back(sp);
		}

		std::vector<uint64> ids;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			ids.push_back(10 + i);
		}
		SectionPathShape shape;
		String error;
		REQUIRE(PrepareSectionPath(spline, ids, MakeAsset({ 0.5f }), SectionPathParams(), Flat0, shape, error));

		const WorldRect r = GetInfluenceRect(shape);
		const float reach = shape.asset.halfWidth + shape.asset.sideFalloff;
		size_t hits = 0;
		for (int64 i = 0; i < 4000; ++i)
		{
			const float x = HashRange(Hash(77, i, closed, 1), r.minX - 5.0f, r.maxX + 5.0f);
			const float z = HashRange(Hash(77, i, closed, 2), r.minZ - 5.0f, r.maxZ + 5.0f);
			for (const float maxAcross : { reach, 0.5f * reach, 100.0f })
			{
				float d0 = -1.0f, a0 = -1.0f, d1 = -1.0f, a1 = -1.0f;
				const bool full = ProjectToPath(*shape.path, x, z, maxAcross, d0, a0);
				const bool fast = ProjectToSectionPath(shape, x, z, maxAcross, d1, a1);
				REQUIRE(full == fast);
				if (full)
				{
					REQUIRE(d0 == d1);
					REQUIRE(a0 == a1);
					++hits;
				}
			}
		}
		CHECK(hits > 1000);
	}
}

namespace
{
	/// Prepares a path with a base ground; ids 1..n.
	void PrepareOnBase(const PathSpline& spline, const SectionAsset& asset, const SectionPathParams& params,
		const std::function<float(float, float)>& base, SectionPathShape& shape)
	{
		std::vector<uint64> ids;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			ids.push_back(1 + i);
		}
		String error;
		REQUIRE(PrepareSectionPath(spline, ids, asset, params, base, shape, error));
	}

	float StepBase(float, const float z)
	{
		return z < 50.0f ? 0.0f : 10.0f;
	}
}

TEST_CASE("Smoothed ground ramps over a base step without spikes", "[section_path_terrain]")
{
	SectionPathParams params;
	params.heightMode = SectionHeightMode::SmoothedGround;
	params.groundSmoothing = 20.0f;
	SectionPathShape shape;
	PrepareOnBase(Line(0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f), MakeAsset({ 0.0f }), params, StepBase, shape);

	const std::vector<SplineSample>& samples = shape.path->GetSamples();
	REQUIRE(shape.centreHeights.size() == samples.size());
	for (size_t i = 0; i < samples.size(); ++i)
	{
		const float h = shape.centreHeights[i];
		const float d = samples[i].distance;
		if (d <= 39.0f)
		{
			CHECK(h == Approx(0.0f).margin(1e-3f));
		}
		if (d >= 61.0f)
		{
			CHECK(h == Approx(10.0f).margin(1e-3f));
		}
		if (i > 0)
		{
			// Monotonic, and never steeper than the step spread over the window.
			const float rise = h - shape.centreHeights[i - 1];
			CHECK(rise >= -1e-4f);
			CHECK(rise <= (d - samples[i - 1].distance) * 10.0f / 20.0f + 1e-3f);
		}
	}

	// The terrain target carries the smoothed centre height.
	const TerrainTarget mid = EvaluateSectionTerrain(shape, 0.0f, 50.0f, 0.0f, Identity);
	CHECK(mid.height == Approx(5.0f).margin(0.2f));
}

TEST_CASE("Smoothed ground on a flat base is the base", "[section_path_terrain]")
{
	SectionPathParams params;
	params.heightMode = SectionHeightMode::SmoothedGround;
	SectionPathShape shape;
	PrepareOnBase(Line(0.0f, 0.0f, 30.0f, 80.0f, 3.0f, 9.0f), MakeAsset({ 0.0f }), params, [](float, float) { return 7.5f; }, shape);
	for (const float h : shape.centreHeights)
	{
		CHECK(h == Approx(7.5f).margin(1e-4f));
	}
}

TEST_CASE("Smoothed ground with a zero window equals follow-ground", "[section_path_terrain]")
{
	const auto bumpy = [](const float x, const float z) { return std::sin(x * 0.3f) * 4.0f + StepBase(x, z); };
	const PathSpline spline = Line(0.0f, 0.0f, 20.0f, 100.0f, 0.0f, 0.0f);

	SectionPathParams params;
	params.heightMode = SectionHeightMode::FollowGround;
	SectionPathShape ground;
	PrepareOnBase(spline, MakeAsset({ 0.0f }), params, bumpy, ground);

	params.heightMode = SectionHeightMode::SmoothedGround;
	params.groundSmoothing = 0.0f;
	SectionPathShape smoothed;
	PrepareOnBase(spline, MakeAsset({ 0.0f }), params, bumpy, smoothed);

	CHECK(smoothed.centreHeights == ground.centreHeights);
}

TEST_CASE("Smoothed ground wraps around a closed loop", "[section_path_terrain]")
{
	PathSpline spline;
	spline.interpolation = SplineInterpolation::Linear;
	spline.closed = true;
	for (const auto& p : { std::make_pair(0.0f, 0.0f), std::make_pair(100.0f, 0.0f), std::make_pair(100.0f, 100.0f), std::make_pair(0.0f, 100.0f) })
	{
		SplinePoint sp;
		sp.x = p.first;
		sp.z = p.second;
		spline.points.push_back(sp);
	}

	SectionPathParams params;
	params.heightMode = SectionHeightMode::SmoothedGround;
	params.groundSmoothing = 20.0f;
	SectionPathShape shape;
	PrepareOnBase(spline, MakeAsset({ 0.0f }), params, [](const float x, float) { return x < 0.1f ? 10.0f : 0.0f; }, shape);
	REQUIRE_FALSE(shape.reversed);
	REQUIRE(shape.path->GetSamples().front().distance == 0.0f);

	// The window around the loop start covers the last 10 m of the closing side (10 high) and the first 10 m of the
	// first side (10 at its start, 0 from 0.5 m on): (100 + 2.5) / 20.
	CHECK(shape.centreHeights.front() == Approx(5.125f).margin(0.01f));
}

TEST_CASE("Smoothed ground needs the base ground", "[section_path_terrain]")
{
	SectionPathParams params;
	params.heightMode = SectionHeightMode::SmoothedGround;
	SectionPathShape shape;
	String error;
	CHECK_FALSE(PrepareSectionPath(Line(0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f), { 1, 2 }, MakeAsset({ 0.0f }), params, {}, shape, error));
	CHECK_FALSE(error.empty());
}
