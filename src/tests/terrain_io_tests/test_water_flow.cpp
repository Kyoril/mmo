// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "base/typedefs.h"
#include "terrain/constants.h"
#include "terrain_io/water_flow.h"

#include <cmath>
#include <vector>

using namespace mmo;
using namespace mmo::terrain_io::water_flow;

namespace
{
	constexpr uint32 Side = terrain::constants::OuterVerticesPerPageSide;
	const float PageSize = static_cast<float>(terrain::constants::PageSize);

	size_t Index(const uint32 x, const uint32 z)
	{
		return static_cast<size_t>(z) * Side + x;
	}

	/// Half width of StraightRiver: wider than the smoothing (about 37 m), so its centre runs at full speed.
	constexpr float RiverHalfWidth = 60.0f;

	/// A straight river along +X through the middle of the page at z = PageSize / 2.
	std::vector<FlowPath> StraightRiver()
	{
		return { { { -100.0f, PageSize * 0.5f, RiverHalfWidth }, { PageSize * 2.0f + 100.0f, PageSize * 0.5f, RiverHalfWidth } } };
	}
}

TEST_CASE("Water flow runs downstream along the path", "[water_flow]")
{
	std::vector<float> uvs;
	std::vector<uint8> speeds;
	ComputePageFlow(StraightRiver(), FlowSettings{}, 0.0f, 0.0f, uvs, speeds);

	REQUIRE(uvs.size() == static_cast<size_t>(Side) * Side * 2);
	REQUIRE(speeds.size() == static_cast<size_t>(Side) * Side);

	// u grows downstream (+X) and v grows to the left of the flow (+Z for flow along +X).
	const uint32 mid = Side / 2;
	CHECK(uvs[Index(mid + 10, mid) * 2] > uvs[Index(mid, mid) * 2]);
	CHECK(uvs[Index(mid, mid + 10) * 2 + 1] > uvs[Index(mid, mid) * 2 + 1]);

	// Full speed in the channel, the minimum far beyond its banks.
	CHECK(speeds[Index(mid, mid)] == 255);
	CHECK(speeds[Index(mid, 0)] == static_cast<uint8>(std::lround(FlowSettings{}.minSpeed * 255.0f)));
}

TEST_CASE("Water flow of neighbouring pages agrees on the shared edge", "[water_flow]")
{
	// A bend, so the smoothing has something to do near the page border.
	const std::vector<FlowPath> paths = { { { 100.0f, 100.0f, 15.0f }, { PageSize - 20.0f, 300.0f, 15.0f }, { PageSize + 200.0f, 150.0f, 25.0f } } };

	std::vector<float> leftUVs, rightUVs;
	std::vector<uint8> leftSpeeds, rightSpeeds;
	ComputePageFlow(paths, FlowSettings{}, 0.0f, 0.0f, leftUVs, leftSpeeds);
	ComputePageFlow(paths, FlowSettings{}, PageSize, 0.0f, rightUVs, rightSpeeds);

	for (uint32 z = 0; z < Side; ++z)
	{
		const size_t left = Index(Side - 1, z);
		const size_t right = Index(0, z);
		CHECK(leftUVs[left * 2] == Approx(rightUVs[right * 2]).margin(1e-3));
		CHECK(leftUVs[left * 2 + 1] == Approx(rightUVs[right * 2 + 1]).margin(1e-3));
		CHECK(leftSpeeds[left] == rightSpeeds[right]);
	}
}

TEST_CASE("Water flow reach covers the channel plus falloff only", "[water_flow]")
{
	const std::vector<FlowPath> paths = StraightRiver();
	const FlowSettings settings;
	const float riverZ = PageSize * 0.5f;

	CHECK(IsRectInReach(paths, settings, 0.0f, 0.0f, PageSize, PageSize));
	CHECK(IsRectInReach(paths, settings, 0.0f, riverZ + RiverHalfWidth + settings.falloff - 1.0f, 50.0f, riverZ + 500.0f));
	CHECK_FALSE(IsRectInReach(paths, settings, 0.0f, riverZ + RiverHalfWidth + settings.falloff + 1.0f, 50.0f, riverZ + 500.0f));

	// A single point is no river.
	CHECK_FALSE(IsRectInReach({ { { 0.0f, 0.0f, 10.0f } } }, settings, -10.0f, -10.0f, 10.0f, 10.0f));
	CHECK_FALSE(HasUsablePath({ { { 0.0f, 0.0f, 10.0f } } }));
	CHECK(HasUsablePath(paths));
}
