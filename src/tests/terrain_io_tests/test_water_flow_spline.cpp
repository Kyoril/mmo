// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "base/typedefs.h"
#include "math/path_spline.h"
#include "terrain/constants.h"
#include "terrain_io/water_flow.h"

#include <cstring>
#include <vector>

using namespace mmo;
using namespace mmo::terrain_io::water_flow;

namespace
{
	FlowPath ThreePointPath()
	{
		return { { 10.0f, 20.0f, 4.0f }, { 150.0f, 90.0f, 8.0f }, { 300.0f, 60.0f, 12.0f } };
	}
}

TEST_CASE("ToSpline produces an open linear spline carrying half widths", "[water_flow]")
{
	const PathSpline spline = ToSpline(ThreePointPath());

	REQUIRE(spline.points.size() == 3);
	CHECK_FALSE(spline.closed);
	CHECK(spline.interpolation == SplineInterpolation::Linear);
	CHECK(spline.points[1].x == 150.0f);
	CHECK(spline.points[1].z == 90.0f);
	CHECK(spline.points[1].width == 8.0f);
}

TEST_CASE("FromSpline is the exact inverse of ToSpline", "[water_flow]")
{
	const FlowPath original = ThreePointPath();
	const FlowPath roundTrip = FromSpline(ToSpline(original));

	REQUIRE(roundTrip.size() == original.size());
	for (size_t i = 0; i < original.size(); ++i)
	{
		CHECK(roundTrip[i].x == original[i].x);
		CHECK(roundTrip[i].z == original[i].z);
		CHECK(roundTrip[i].halfWidth == original[i].halfWidth);
	}
}

TEST_CASE("Round-tripped path gives byte-identical page flow", "[water_flow]")
{
	const FlowPath original = ThreePointPath();
	const FlowPath roundTrip = FromSpline(ToSpline(original));
	const FlowSettings settings;

	std::vector<float> uvsA, uvsB;
	std::vector<uint8> speedsA, speedsB;
	ComputePageFlow({ original }, settings, 0.0f, 0.0f, uvsA, speedsA);
	ComputePageFlow({ roundTrip }, settings, 0.0f, 0.0f, uvsB, speedsB);

	REQUIRE(uvsA.size() == uvsB.size());
	REQUIRE(speedsA.size() == speedsB.size());
	REQUIRE_FALSE(uvsA.empty());
	CHECK(std::memcmp(uvsA.data(), uvsB.data(), uvsA.size() * sizeof(float)) == 0);
	CHECK(speedsA == speedsB);
}
