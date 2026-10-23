// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/ocean_grid.h"
#include "deferred_shading/ocean_shore_distance.h"
#include "deferred_shading/ocean_spectrum.h"

#include <cmath>
#include <vector>

using namespace mmo;
using namespace mmo::ocean_spectrum;

TEST_CASE("JONSWAP fit matches published fetch-limited values", "[ocean]")
{
	// U = 10 m/s over 100 km: peak period ~ 6 s (Hasselmann et al. 1973 fit).
	const JonswapParameters parameters = ComputeJonswap(10.0f, 100.0f);
	const float peakPeriod = 2.0f * ocean_spectrum::Pi / parameters.peakOmega;
	CHECK(peakPeriod > 5.0f);
	CHECK(peakPeriod < 7.5f);
	CHECK(parameters.alpha > 0.005f);
	CHECK(parameters.alpha < 0.02f);
}

TEST_CASE("Significant wave height grows with wind and fetch", "[ocean]")
{
	const float calm = SignificantWaveHeight(5.0f, 50.0f);
	const float breeze = SignificantWaveHeight(10.0f, 50.0f);
	const float longFetch = SignificantWaveHeight(10.0f, 300.0f);

	CHECK(calm < breeze);
	CHECK(breeze < longFetch);

	// The shipped default must stay a lively sea, not a storm. (The renderer damps the finer
	// cascades and the short-wave tail, so the visible sea is calmer than this spectral value.)
	const OceanSettings defaults;
	const float hs = SignificantWaveHeight(defaults.windSpeed, defaults.fetch) * defaults.waveScale;
	CHECK(hs > 1.0f);
	CHECK(hs < 4.5f);
}

TEST_CASE("Directional spreading integrates to one", "[ocean]")
{
	for (const float exponent : { 0.5f, 2.0f, 6.0f, 20.0f })
	{
		const float norm = SpreadNormalization(exponent);
		constexpr int steps = 20000;
		double sum = 0.0;
		for (int i = 0; i < steps; ++i)
		{
			const double theta = -ocean_spectrum::Pi + (i + 0.5) * (2.0 * ocean_spectrum::Pi / steps);
			sum += norm * std::pow(std::abs(std::cos(theta * 0.5)), 2.0 * exponent) * (2.0 * ocean_spectrum::Pi / steps);
		}

		CHECK(sum == Approx(1.0).epsilon(1e-3));
	}
}

TEST_CASE("Cascade bands tile the spectrum without overlap", "[ocean]")
{
	const OceanSettings defaults;
	const auto bands = ComputeCascadeBands(defaults.cascadeSizes);

	CHECK(bands[0].low == 0.0f);
	for (uint32 i = 1; i < OceanCascadeCount; ++i)
	{
		CHECK(bands[i].low == bands[i - 1].high);
		CHECK(bands[i].high > bands[i].low);
	}

	// Every cascade resolves the waves it owns: its band starts above its fundamental frequency
	// (except the first, which owns the longest waves) and ends below its Nyquist frequency.
	for (uint32 i = 0; i + 1 < OceanCascadeCount; ++i)
	{
		const float nyquist = ocean_spectrum::Pi * static_cast<float>(OceanFftResolution) / defaults.cascadeSizes[i];
		CHECK(bands[i].high < nyquist);
	}
	for (uint32 i = 1; i < OceanCascadeCount; ++i)
	{
		CHECK(bands[i].low >= 2.0f * ocean_spectrum::Pi / defaults.cascadeSizes[i]);
	}
}

TEST_CASE("Ocean grid is one closed fan of near-square cells", "[ocean]")
{
	const OceanGrid grid = BuildOceanGrid(64, 1.0f, 1000.0f);

	REQUIRE(grid.vertices.size() % 3 == 0);
	REQUIRE(grid.indices.size() % 3 == 0);
	const size_t vertexCount = grid.vertices.size() / 3;
	for (const uint32 index : grid.indices)
	{
		REQUIRE(index < vertexCount);
	}

	// Directions are unit length and radii only grow outward, ending exactly at the outer radius.
	float previousRadius = 0.0f;
	for (size_t v = 1; v < vertexCount; ++v)
	{
		const float x = grid.vertices[v * 3];
		const float y = grid.vertices[v * 3 + 1];
		const float radius = grid.vertices[v * 3 + 2];
		CHECK(std::sqrt(x * x + y * y) == Approx(1.0f).epsilon(1e-4));
		CHECK(radius >= previousRadius);
		previousRadius = radius;
	}
	CHECK(previousRadius == 1000.0f);

	// Neighbouring rings differ by the angular step, so cells stay square.
	const float secondRing = grid.vertices[(1 + 64) * 3 + 2];
	CHECK(secondRing / 1.0f == Approx(1.0f + grid.angularStep).epsilon(1e-4));
}

TEST_CASE("Ocean shader mask encodes distance to the shore", "[ocean]")
{
	// 16 x 16 window: land (R = 0) in column 0, open water elsewhere, one texel = 2 m.
	constexpr uint32 side = 16;
	std::vector<uint8> rg(side * side * 2, 255);
	for (uint32 z = 0; z < side; ++z)
	{
		rg[(z * side) * 2] = 0;
		rg[(z * side) * 2 + 1] = 0;
	}

	std::vector<uint8> rgba;
	ocean_shore::BuildShaderMask(rg, side, 2.0f, rgba);
	REQUIRE(rgba.size() == side * side * 4);

	const auto distanceAt = [&](const uint32 x, const uint32 z)
	{
		return rgba[(z * side + x) * 4 + 2] / 255.0f * ocean_shore::DistanceRange;
	};

	// Presence and depth pass through untouched.
	CHECK(rgba[0] == 0);
	CHECK(rgba[(5 * side + 5) * 4] == 255);
	CHECK(rgba[(5 * side + 5) * 4 + 1] == 255);

	CHECK(distanceAt(0, 8) == 0.0f);
	CHECK(distanceAt(5, 8) == Approx(10.0f).margin(0.2f));
	CHECK(distanceAt(15, 8) == Approx(30.0f).margin(0.2f));
}

TEST_CASE("Ocean shader mask without shore stays at the full range", "[ocean]")
{
	constexpr uint32 side = 8;
	const std::vector<uint8> rg(side * side * 2, 255);
	std::vector<uint8> rgba;
	ocean_shore::BuildShaderMask(rg, side, 4.0f, rgba);
	for (uint32 i = 0; i < side * side; ++i)
	{
		CHECK(rgba[i * 4 + 2] == 255);
	}
}
