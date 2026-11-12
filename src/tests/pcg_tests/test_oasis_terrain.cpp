// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/oasis.h"
#include "pcg/pcg_random.h"

#include <cmath>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	PathSpline MakeCircle(float radius, uint32 count = 16)
	{
		PathSpline spline;
		spline.closed = true;
		spline.interpolation = SplineInterpolation::CatmullRom;
		for (uint32 i = 0; i < count; ++i)
		{
			const float a = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(count);
			spline.points.push_back(SplinePoint{ radius * std::cos(a), radius * std::sin(a), 0.0f });
		}
		return spline;
	}

	float Flat20(float, float)
	{
		return 20.0f;
	}

	uint32 DominantLayer(const TerrainTarget& t)
	{
		uint32 best = 0;
		for (uint32 i = 1; i < 4; ++i)
		{
			if (t.splat[i] > t.splat[best])
			{
				best = i;
			}
		}
		return best;
	}
}

TEST_CASE("Oasis centre is a flooded basin", "[oasis_terrain]")
{
	OasisShape shape;
	const OasisParams params;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 7, params, Flat20, shape) == OasisError::None);

	// Floor 20 - 6 = 14, the water 0.6 below it, the pool bed waterDepth (1.2) below the water.
	const TerrainTarget t = EvaluateTerrain(shape, 0.0f, 0.0f, 0.0f);
	CHECK(t.heightWeight == Approx(1.0f));
	CHECK(t.height == Approx(12.2f).margin(params.floorNoiseAmplitude + 0.001f));
	CHECK(t.waterType == kWaterTypeWater);
	CHECK(t.waterHeight == Approx(t.height + 1.2f).margin(0.5f));
	CHECK(GetWaterLevel(shape) == Approx(13.4f));
	CHECK(t.setMaterial);
}

TEST_CASE("Oasis pool is carved into a dry basin floor", "[oasis_terrain]")
{
	OasisShape shape;
	OasisParams params;
	params.rimWidth = 8.0f;
	params.waterInset = 16.0f;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 7, params, Flat20, shape) == OasisError::None);
	const float waterLevel = GetWaterLevel(shape);

	// The land between the rim and the pool edge stays above the water.
	for (float r = 40.0f - 0.5f; r > 40.0f - params.waterInset; r -= 0.5f)
	{
		const TerrainTarget t = EvaluateTerrain(shape, r, 0.0f, 0.0f);
		CHECK(t.height > waterLevel);
	}

	// Well inside the pool edge the ground is under water and the quads are water.
	for (float r = 0.0f; r < 40.0f - params.waterInset - 5.0f; r += 0.5f)
	{
		const TerrainTarget t = EvaluateTerrain(shape, r, 0.0f, 0.0f);
		CHECK(t.height < waterLevel);
		CHECK(t.waterType == kWaterTypeWater);
		CHECK(t.waterHeight == Approx(waterLevel));
	}

	// Water quads reach a little past the pool edge, under the dry shore, but not to the rim.
	CHECK(EvaluateTerrain(shape, 40.0f - params.waterInset + 2.0f, 0.0f, 0.0f).waterType == kWaterTypeWater);
	CHECK(EvaluateTerrain(shape, 40.0f - params.waterInset + 6.0f, 0.0f, 0.0f).waterType == 0);
}

TEST_CASE("Oasis does not touch terrain beyond the falloff", "[oasis_terrain]")
{
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 7, OasisParams(), Flat20, shape) == OasisError::None);

	const TerrainTarget t = EvaluateTerrain(shape, 60.0f, 0.0f, 0.0f);
	CHECK(t.heightWeight == 0.0f);
	CHECK(t.splatWeight == 0.0f);
	CHECK_FALSE(t.setMaterial);
	CHECK(t.waterType == 0);
}

TEST_CASE("Oasis rim is grassy and dry", "[oasis_terrain]")
{
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 7, OasisParams(), Flat20, shape) == OasisError::None);

	const TerrainTarget t = EvaluateTerrain(shape, 39.0f, 0.0f, 0.0f);
	CHECK((DominantLayer(t) == 2 || DominantLayer(t) == 3));
	CHECK(t.waterType == 0);
	CHECK(t.splatWeight == Approx(1.0f));
}

TEST_CASE("Oasis rim height uses a percentile, not the maximum", "[oasis_terrain]")
{
	OasisShape shape;
	const auto dune = [](float x, float) { return x > 30.0f ? 50.0f : 20.0f; };
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 7, OasisParams(), dune, shape) == OasisError::None);

	const TerrainTarget outside = EvaluateTerrain(shape, 41.0f, 0.0f, 0.0f);
	CHECK(outside.height == Approx(20.0f));
	// Pool bed: floor 14, water 0.6 below, bed 1.2 below the water.
	const TerrainTarget centre = EvaluateTerrain(shape, 0.0f, 0.0f, 0.0f);
	CHECK(centre.height == Approx(12.2f).margin(0.5f));
}

TEST_CASE("Oasis terrain is deterministic and seed sensitive", "[oasis_terrain]")
{
	OasisShape a, b, c;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 7, OasisParams(), Flat20, a) == OasisError::None);
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 7, OasisParams(), Flat20, b) == OasisError::None);
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 8, OasisParams(), Flat20, c) == OasisError::None);

	uint32 splatDiffers = 0;
	for (int64 i = 0; i < 100; ++i)
	{
		const float x = HashRange(Hash(1, i, 0), -55.0f, 55.0f);
		const float z = HashRange(Hash(1, i, 1), -55.0f, 55.0f);
		const TerrainTarget ta = EvaluateTerrain(a, x, z, 0.0f);
		const TerrainTarget tb = EvaluateTerrain(b, x, z, 0.0f);
		const TerrainTarget tc = EvaluateTerrain(c, x, z, 0.0f);
		CHECK(ta.height == tb.height);
		CHECK(ta.heightWeight == tb.heightWeight);
		CHECK(ta.splat == tb.splat);
		CHECK(ta.splatWeight == tb.splatWeight);
		CHECK(ta.waterType == tb.waterType);
		CHECK(ta.waterHeight == tb.waterHeight);
		if (ta.splat != tc.splat)
		{
			++splatDiffers;
		}
	}
	CHECK(splatDiffers > 0);
}

TEST_CASE("Oasis influence rect covers the loop plus falloff", "[oasis_terrain]")
{
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 7, OasisParams(), Flat20, shape) == OasisError::None);

	const WorldRect r = GetInfluenceRect(shape);
	CHECK(r.minX <= -40.0f - 12.0f + 0.5f);
	CHECK(r.maxX >= 40.0f + 12.0f - 0.5f);
	CHECK(r.minZ < -50.0f);
	CHECK(r.maxZ > 50.0f);
}

TEST_CASE("PrepareOasis rejects invalid loops", "[oasis_terrain]")
{
	OasisShape shape;

	PathSpline two;
	two.points = { SplinePoint{ 0, 0, 0 }, SplinePoint{ 10, 0, 0 } };
	CHECK(PrepareOasis(two, 1, OasisParams(), Flat20, shape) == OasisError::TooFewPoints);

	PathSpline line;
	line.points = { SplinePoint{ 0, 0, 0 }, SplinePoint{ 10, 0, 0 }, SplinePoint{ 20, 0, 0 } };
	CHECK(PrepareOasis(line, 1, OasisParams(), Flat20, shape) == OasisError::ZeroArea);

	PathSpline bowTie;
	bowTie.points = { SplinePoint{ 0, 0, 0 }, SplinePoint{ 20, 20, 0 }, SplinePoint{ 20, 0, 0 }, SplinePoint{ 0, 20, 0 } };
	CHECK(PrepareOasis(bowTie, 1, OasisParams(), Flat20, shape) == OasisError::SelfIntersecting);
}
