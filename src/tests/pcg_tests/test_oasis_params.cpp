// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/oasis.h"
#include "pcg/oasis_recipe.h"

#include <cmath>
#include <limits>

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

	float Flat(float, float)
	{
		return 20.0f;
	}

	float ZeroSlope(float, float)
	{
		return 0.0f;
	}
}

TEST_CASE("Sanitize leaves the defaults unchanged", "[oasis_params]")
{
	OasisParams params;
	const OasisParams defaults;
	SanitizeParams(params);
	CHECK(params.basinDepth == defaults.basinDepth);
	CHECK(params.rimWidth == defaults.rimWidth);
	CHECK(params.waterInset == defaults.waterInset);
	CHECK(params.rockSpacing == defaults.rockSpacing);
	CHECK(params.terraceSteps == defaults.terraceSteps);
	CHECK(params.palmScaleMax == defaults.palmScaleMax);
}

TEST_CASE("Sanitize clamps zero, negative and huge values", "[oasis_params]")
{
	OasisParams params;
	params.rockSpacing = 0.0f;
	params.palmCell = -3.0f;
	params.bushCell = 0.0f;
	params.rimWidth = 0.0f;
	params.outerFalloff = -1.0f;
	params.waterInset = 0.0f;
	params.basinDepth = 1.0e30f;
	params.maxSlopeDeg = 500.0f;
	params.terraceSteps = 4000000000u;
	SanitizeParams(params);

	CHECK(params.rockSpacing >= 1.0f);
	CHECK(params.palmCell >= 1.0f);
	CHECK(params.bushCell >= 1.0f);
	CHECK(params.rimWidth > 0.0f);
	CHECK(params.outerFalloff > 0.0f);
	CHECK(params.waterInset >= 2.0f);
	CHECK(params.basinDepth == 60.0f);
	CHECK(params.maxSlopeDeg == 90.0f);
	CHECK(params.terraceSteps == 16u);
}

TEST_CASE("Sanitize replaces non finite values by the defaults", "[oasis_params]")
{
	const OasisParams defaults;
	OasisParams params;
	params.rockSpacing = std::numeric_limits<float>::quiet_NaN();
	params.rimWidth = std::numeric_limits<float>::infinity();
	params.basinDepth = -std::numeric_limits<float>::infinity();
	SanitizeParams(params);

	CHECK(params.rockSpacing == defaults.rockSpacing);
	CHECK(params.rimWidth == defaults.rimWidth);
	CHECK(params.basinDepth == defaults.basinDepth);
}

TEST_CASE("Sanitize swaps inverted min and max pairs", "[oasis_params]")
{
	OasisParams params;
	params.rockScaleMin = 2.0f;
	params.rockScaleMax = 0.5f;
	params.palmScaleMin = 3.0f;
	params.palmScaleMax = 1.0f;
	SanitizeParams(params);

	CHECK(params.rockScaleMin == 0.5f);
	CHECK(params.rockScaleMax == 2.0f);
	CHECK(params.palmScaleMin == 1.0f);
	CHECK(params.palmScaleMax == 3.0f);
}

TEST_CASE("Sanitize is idempotent", "[oasis_params]")
{
	OasisParams params;
	params.rockSpacing = -4.0f;
	params.rockScaleMin = 9.0f;
	params.rockScaleMax = 0.0f;
	SanitizeParams(params);
	const OasisParams once = params;
	SanitizeParams(params);
	CHECK(params.rockSpacing == once.rockSpacing);
	CHECK(params.rockScaleMin == once.rockScaleMin);
	CHECK(params.rockScaleMax == once.rockScaleMax);
}

TEST_CASE("Prepare sanitizes its copy of the parameters", "[oasis_params]")
{
	OasisParams params;
	params.rockSpacing = 0.0f;
	params.outerFalloff = 0.0f;
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 1, params, Flat, shape) == OasisError::None);
	CHECK(shape.params.rockSpacing >= 1.0f);
	CHECK(shape.params.outerFalloff > 0.0f);
	// The caller's value is untouched.
	CHECK(params.rockSpacing == 0.0f);
}

TEST_CASE("Recipe parsing sanitizes and survives out of range numbers", "[oasis_params]")
{
	OasisParams params;
	OasisAssets assets;
	REQUIRE(ParseOasisRecipe(R"({"params":{"rockSpacing":0,"terraceSteps":1e30,"basinDepth":1e300,"rimWidth":-5},
		"assets":{"cliffs":["cliff_a"]}})", params, assets));

	CHECK(params.rockSpacing >= 1.0f);
	CHECK(params.terraceSteps == 16u);
	CHECK(params.basinDepth == 60.0f);
	CHECK(params.rimWidth > 0.0f);
}

TEST_CASE("BuildPlacements terminates for a recipe with rockSpacing 0", "[oasis_params]")
{
	OasisParams params;
	OasisAssets assets;
	REQUIRE(ParseOasisRecipe(R"({"params":{"rockSpacing":0,"palmCell":0,"bushCell":0},
		"assets":{"cliffs":["cliff_a"],"palms":["palm_a"],"bushes":["bush_a"]}})", params, assets));

	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 3, params, Flat, shape) == OasisError::None);
	std::vector<Placement> out;
	BuildPlacements(shape, assets, Flat, ZeroSlope, out);
	CHECK(!out.empty());
	CHECK(out.size() < 100000);
}
