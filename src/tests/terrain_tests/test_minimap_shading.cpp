// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "terrain/minimap_shading.h"

#include <array>

using namespace mmo;
using namespace mmo::terrain;
using namespace mmo::terrain::minimap_shading;

namespace
{
	constexpr uint32 QuadsPerPageSide = constants::TilesPerPage * water_lookup::QuadsPerTileSide;

	/// One page of water arrays the test can paint quads into.
	struct TestPageWater
	{
		std::array<uint64, constants::TilesPerPage * constants::TilesPerPage> masks{};
		std::array<uint8, constants::TilesPerPage * constants::TilesPerPage> types{};
		std::vector<float> heights = std::vector<float>(constants::OuterVerticesPerPageSide * constants::OuterVerticesPerPageSide, 0.0f);

		void SetQuad(const uint32 gx, const uint32 gz)
		{
			const uint32 tile = gx / water_lookup::QuadsPerTileSide + (gz / water_lookup::QuadsPerTileSide) * constants::TilesPerPage;
			masks[tile] |= 1ULL << (gx % water_lookup::QuadsPerTileSide + (gz % water_lookup::QuadsPerTileSide) * water_lookup::QuadsPerTileSide);
		}

		water_lookup::PageWaterView View() const
		{
			return water_lookup::PageWaterView{ masks.data(), types.data(), heights.data() };
		}
	};

	float Luminance(const Rgb& c)
	{
		return c.r * 0.2126f + c.g * 0.7152f + c.b * 0.0722f;
	}
}

TEST_CASE("MinimapShading_Presence_Shoreline_Lies_On_Straight_Mask_Edges", "[minimap_shading]")
{
	// Water in every quad column x < 64: the 0.5 iso-line must sit exactly on the quad boundary.
	TestPageWater water;
	for (uint32 gz = 0; gz < QuadsPerPageSide; ++gz)
	{
		for (uint32 gx = 0; gx < 64; ++gx)
		{
			water.SetQuad(gx, gz);
		}
	}

	const float edge = 64.0f * water_lookup::QuadSize;
	const float z = 100.0f;
	CHECK(WaterPresence(water.View(), edge, z) == Approx(0.5f));
	CHECK(WaterPresence(water.View(), edge - water_lookup::QuadSize, z) == Approx(1.0f));
	CHECK(WaterPresence(water.View(), edge + water_lookup::QuadSize, z) == Approx(0.0f));
}

TEST_CASE("MinimapShading_Presence_Rounds_Convex_Corners", "[minimap_shading]")
{
	// A single water quad: its corner point must read as dry (rounded), its centre as wet.
	TestPageWater water;
	water.SetQuad(10, 10);

	const float q = water_lookup::QuadSize;
	CHECK(WaterPresence(water.View(), 10.5f * q, 10.5f * q) == Approx(1.0f));
	CHECK(WaterPresence(water.View(), 10.0f * q, 10.0f * q) < 0.5f);
}

TEST_CASE("MinimapShading_Presence_Without_Water_Data_Is_Dry", "[minimap_shading]")
{
	const water_lookup::PageWaterView empty{};
	CHECK(WaterPresence(empty, 100.0f, 100.0f) == 0.0f);
	CHECK(WaterSurfaceHeight(empty, 100.0f, 100.0f) == 0.0f);
}

TEST_CASE("MinimapShading_Near_Shore_Height_Ignores_Dry_Vertices", "[minimap_shading]")
{
	// One quad with its four corners at height 5; every other vertex is the zero default.
	TestPageWater water;
	water.SetQuad(20, 20);
	for (uint32 z = 20; z <= 21; ++z)
	{
		for (uint32 x = 20; x <= 21; ++x)
		{
			water.heights[x + z * constants::OuterVerticesPerPageSide] = 5.0f;
		}
	}

	// Just outside the quad, the plain blend sags toward zero while the shore-aware one stays level.
	const float q = water_lookup::QuadSize;
	CHECK(WaterSurfaceHeight(water.View(), 21.5f * q, 20.5f * q) < 4.0f);
	CHECK(WaterSurfaceHeightNearShore(water.View(), 21.5f * q, 20.5f * q) == Approx(5.0f));
	CHECK(WaterSurfaceHeightNearShore(water.View(), 20.5f * q, 20.5f * q) == Approx(5.0f));
}

TEST_CASE("MinimapShading_Deeper_Water_Is_Darker", "[minimap_shading]")
{
	const Rgb sand = FromSrgb8(200, 180, 130);
	const WaterLook look = LookFor(WaterType::Water);

	// Far from the shore so foam does not interfere; same position so the ripple pattern matches.
	const Rgb shallow = ShadeWater(look, sand, WaterType::Water, 1.0f, 50.0f, 10.0f, 10.0f);
	const Rgb deep = ShadeWater(look, sand, WaterType::Water, 12.0f, 50.0f, 10.0f, 10.0f);
	CHECK(Luminance(deep) < Luminance(shallow));
	// Deep water is blue-dominated.
	CHECK(deep.b > deep.r);
}

TEST_CASE("MinimapShading_Shoreline_Gets_Foam", "[minimap_shading]")
{
	const Rgb sand = FromSrgb8(200, 180, 130);
	const WaterLook look = LookFor(WaterType::Ocean);

	const Rgb open = ShadeWater(look, sand, WaterType::Ocean, 6.0f, 40.0f, 3.0f, 7.0f);
	const Rgb shore = ShadeWater(look, sand, WaterType::Ocean, 0.0f, 0.0f, 3.0f, 7.0f);
	CHECK(Luminance(shore) > Luminance(open));
}

TEST_CASE("MinimapShading_Lava_Reads_As_Hot", "[minimap_shading]")
{
	const Rgb rock = FromSrgb8(90, 80, 70);
	const WaterLook look = LookFor(WaterType::Lava);
	const Rgb lava = ShadeWater(look, rock, WaterType::Lava, 0.0f, 0.0f, 0.0f, 0.0f);
	CHECK(lava.r > lava.g);
	CHECK(lava.g > lava.b);
}

TEST_CASE("MinimapShading_Bank_Darkens_Only_Near_Water", "[minimap_shading]")
{
	const Rgb grass = FromSrgb8(90, 140, 60);
	const WaterLook look = LookFor(WaterType::Water);

	CHECK(Luminance(ShadeBank(grass, look, 0.0f)) < Luminance(grass));
	const Rgb far = ShadeBank(grass, look, look.bankDistance + 1.0f);
	CHECK(far.r == grass.r);
	CHECK(far.g == grass.g);
	CHECK(far.b == grass.b);
}

TEST_CASE("MinimapShading_Relief_Keeps_Flat_Ground_And_Lights_Northwest_Slopes", "[minimap_shading]")
{
	CHECK(ReliefLight(0.0f, 1.0f, 0.0f) == Approx(1.0f));
	// North is -Z, west is -X: a slope facing north-west is brighter, one facing south-east darker.
	CHECK(ReliefLight(-0.5f, 0.7f, -0.5f) > 1.0f);
	CHECK(ReliefLight(0.5f, 0.7f, 0.5f) < 1.0f);
}

TEST_CASE("MinimapShading_Noise_Is_Deterministic_And_Bounded", "[minimap_shading]")
{
	for (int32 i = 0; i < 200; ++i)
	{
		const float x = static_cast<float>(i) * 1.37f - 100.0f;
		const float z = static_cast<float>(i) * -0.71f + 33.0f;
		const float n = Fbm(x, z, 4);
		CHECK(n >= 0.0f);
		CHECK(n <= 1.0f);
		CHECK(n == Fbm(x, z, 4));
	}
}

TEST_CASE("MinimapShading_Chamfer_Distance_Measures_Pixels_To_Target", "[minimap_shading]")
{
	constexpr uint32 size = 16;
	std::vector<uint8> target(size * size, 0);
	target[5 * size + 5] = 1;

	std::vector<int32> nearest;
	const std::vector<float> dist = ChamferDistance(target, size, &nearest);

	CHECK(dist[5 * size + 5] == 0.0f);
	CHECK(dist[5 * size + 9] == Approx(4.0f));
	CHECK(dist[9 * size + 9] == Approx(4.0f * 4.0f / 3.0f));
	CHECK(nearest[0] == static_cast<int32>(5 * size + 5));
}

TEST_CASE("MinimapShading_Chamfer_Without_Targets_Is_Unreachable", "[minimap_shading]")
{
	constexpr uint32 size = 8;
	const std::vector<uint8> target(size * size, 0);
	std::vector<int32> nearest;
	const std::vector<float> dist = ChamferDistance(target, size, &nearest);
	CHECK(dist[0] == std::numeric_limits<float>::max());
	CHECK(nearest[0] == -1);
}
