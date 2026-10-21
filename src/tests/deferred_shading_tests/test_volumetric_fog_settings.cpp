// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/volumetric_fog_settings.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

using namespace mmo;

TEST_CASE("Volumetric fog quality presets set tile size and slice count", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.qualityLevel == 3);
	CHECK(settings.tileSize == 12u);
	CHECK(settings.sliceCount == 64u);
	CHECK(settings.IsVolumeEnabled());

	settings.ApplyQualityLevel(0);
	CHECK_FALSE(settings.IsVolumeEnabled());

	settings.ApplyQualityLevel(1);
	CHECK(settings.tileSize == 24u);
	CHECK(settings.sliceCount == 32u);
	CHECK(settings.GetGridWidth(1920) == 80u);
	CHECK(settings.GetGridHeight(1080) == 45u);

	settings.ApplyQualityLevel(2);
	CHECK(settings.tileSize == 16u);
	CHECK(settings.sliceCount == 48u);
	CHECK(settings.GetGridWidth(1920) == 120u);
	CHECK(settings.GetGridHeight(1080) == 68u);

	settings.ApplyQualityLevel(3);
	CHECK(settings.GetGridWidth(1920) == 160u);
	CHECK(settings.GetGridHeight(1080) == 90u);

	settings.ApplyQualityLevel(4);
	CHECK(settings.tileSize == 8u);
	CHECK(settings.sliceCount == 96u);
	CHECK(settings.GetGridWidth(1920) == 240u);
	CHECK(settings.GetGridHeight(1080) == 135u);

	settings.ApplyQualityLevel(-5);
	CHECK(settings.qualityLevel == 0);
	settings.ApplyQualityLevel(42);
	CHECK(settings.qualityLevel == 4);
}

TEST_CASE("Volumetric fog grid never collapses to zero cells", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.GetGridWidth(1) == 1u);
	CHECK(settings.GetGridHeight(0) == 1u);
}

TEST_CASE("Volumetric fog range and debug mode clamp", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.range == Approx(200.0f));

	settings.SetRange(10.0f);
	CHECK(settings.range == Approx(VolumetricFogSettings::MinRange));
	settings.SetRange(1000.0f);
	CHECK(settings.range == Approx(VolumetricFogSettings::MaxRange));

	settings.SetDebugMode(-1);
	CHECK(settings.debugMode == 0u);
	settings.SetDebugMode(7);
	CHECK(settings.debugMode == 4u);
}

TEST_CASE("Fog light scattering strength clamps to its range", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.lightScatterStrength == Approx(1.0f));

	settings.SetLightScatterStrength(-2.0f);
	CHECK(settings.lightScatterStrength == Approx(0.0f));

	settings.SetLightScatterStrength(20.0f);
	CHECK(settings.lightScatterStrength == Approx(8.0f));

	settings.SetLightScatterStrength(2.5f);
	CHECK(settings.lightScatterStrength == Approx(2.5f));
}

TEST_CASE("Fog debug view 4 shows lights per block", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	settings.SetDebugMode(4);
	CHECK(settings.debugMode == 4u);
}

TEST_CASE("Slice and depth conversions are exact inverses and monotonic", "[volumetric_fog]")
{
	constexpr float nearDistance = VolumetricFogSettings::NearDistance;
	constexpr float farDistance = 200.0f;

	CHECK(volumetric_fog::SliceToDepth(0.0f, nearDistance, farDistance) == Approx(nearDistance));
	CHECK(volumetric_fog::SliceToDepth(1.0f, nearDistance, farDistance) == Approx(farDistance));
	CHECK(volumetric_fog::DepthToSlice(nearDistance * 0.5f, nearDistance, farDistance) == Approx(0.0f));

	float previous = 0.0f;
	for (float depth : { 0.75f, 1.0f, 5.0f, 17.0f, 64.0f, 150.0f, 199.0f })
	{
		const float slice = volumetric_fog::DepthToSlice(depth, nearDistance, farDistance);
		CHECK(slice > previous);
		CHECK(volumetric_fog::SliceToDepth(slice, nearDistance, farDistance) == Approx(depth).epsilon(1e-4));
		previous = slice;
	}

	// Beyond the range the slice coordinate keeps growing; the composite uses that to switch to the
	// closed-form tail.
	CHECK(volumetric_fog::DepthToSlice(400.0f, nearDistance, farDistance) > 1.0f);
}

TEST_CASE("Halton jitter starts at the radical inverse and stays in [0, 1)", "[volumetric_fog]")
{
	CHECK(volumetric_fog::HaltonBase2(1) == Approx(0.5f));
	CHECK(volumetric_fog::HaltonBase2(2) == Approx(0.25f));
	CHECK(volumetric_fog::HaltonBase2(3) == Approx(0.75f));

	std::set<float> values;
	for (uint64 frame = 0; frame < VolumetricFogSettings::JitterSequenceLength; ++frame)
	{
		const float jitter = volumetric_fog::JitterForFrame(frame);
		CHECK(jitter >= 0.0f);
		CHECK(jitter < 1.0f);
		values.insert(jitter);
	}

	CHECK(values.size() == VolumetricFogSettings::JitterSequenceLength);
	CHECK(volumetric_fog::JitterForFrame(VolumetricFogSettings::JitterSequenceLength) == Approx(volumetric_fog::JitterForFrame(0)));
}

TEST_CASE("Halton radical inverse in bases 3 and 5", "[volumetric_fog]")
{
	CHECK(volumetric_fog::Halton(1, 3) == Approx(1.0f / 3.0f));
	CHECK(volumetric_fog::Halton(2, 3) == Approx(2.0f / 3.0f));
	CHECK(volumetric_fog::Halton(3, 3) == Approx(1.0f / 9.0f));
	CHECK(volumetric_fog::Halton(1, 5) == Approx(0.2f));
	CHECK(volumetric_fog::Halton(6, 5) == Approx(0.2f + 1.0f / 25.0f));
	CHECK(volumetric_fog::Halton(5, 2) == Approx(volumetric_fog::HaltonBase2(5)));
}

TEST_CASE("Screen jitter covers the whole froxel and is independent of the depth jitter", "[volumetric_fog]")
{
	// A froxel sampled only at its screen centre aliases the shadow map in a pattern locked to the grid,
	// which the temporal blend drags along behind the camera. Over one sequence the screen samples must
	// spread across the cell in both axes, and differ from the depth jitter's pattern.
	float minX = 1.0f, maxX = 0.0f, minY = 1.0f, maxY = 0.0f;
	std::set<std::pair<float, float>> points;
	bool differsFromDepth = false;
	for (uint64 frame = 0; frame < VolumetricFogSettings::JitterSequenceLength; ++frame)
	{
		const auto [x, y] = volumetric_fog::ScreenJitterForFrame(frame);
		CHECK(x >= 0.0f);
		CHECK(x < 1.0f);
		CHECK(y >= 0.0f);
		CHECK(y < 1.0f);
		minX = std::min(minX, x);
		maxX = std::max(maxX, x);
		minY = std::min(minY, y);
		maxY = std::max(maxY, y);
		points.insert({ x, y });
		differsFromDepth |= x != volumetric_fog::JitterForFrame(frame);
	}

	CHECK(points.size() == VolumetricFogSettings::JitterSequenceLength);
	CHECK(maxX - minX > 0.75f);
	CHECK(maxY - minY > 0.75f);
	CHECK(differsFromDepth);
}

TEST_CASE("Integrated texel coordinate maps a slice's end to its texel centre", "[volumetric_fog]")
{
	constexpr uint32 sliceCount = 64;

	for (const uint32 z : { 0u, 1u, 10u, 32u, 63u })
	{
		const float slice01 = static_cast<float>(z + 1) / static_cast<float>(sliceCount);
		const float texelCentre = (static_cast<float>(z) + 0.5f) / static_cast<float>(sliceCount);
		CHECK(volumetric_fog::IntegratedTexelCoordinate(slice01, sliceCount) == Approx(texelCentre));
	}

	// Clamps to [0, 1] both below the near plane and past the far plane.
	CHECK(volumetric_fog::IntegratedTexelCoordinate(0.0f, sliceCount) == Approx(0.0f).margin(1e-6));
	CHECK(volumetric_fog::IntegratedTexelCoordinate(2.0f, sliceCount) == Approx(1.0f));

	// A zero slice count has no texels to address.
	CHECK(volumetric_fog::IntegratedTexelCoordinate(0.5f, 0u) == Approx(0.0f));
}

TEST_CASE("Noise density factor keeps the average density", "[volumetric_fog]")
{
	CHECK(volumetric_fog::NoiseDensityFactor(0.3f, 0.0f) == Approx(1.0f));
	CHECK(volumetric_fog::NoiseDensityFactor(0.0f, 1.0f) == Approx(0.0f));
	CHECK(volumetric_fog::NoiseDensityFactor(1.0f, 1.0f) == Approx(2.0f));

	for (float amount : { 0.0f, 0.25f, 0.5f, 1.0f })
	{
		constexpr int samples = 1000;
		double sum = 0.0;
		for (int i = 0; i < samples; ++i)
		{
			const float noise = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
			sum += volumetric_fog::NoiseDensityFactor(noise, amount);
		}

		CHECK(sum / samples == Approx(1.0).epsilon(1e-3));
	}
}

TEST_CASE("History weight defaults to the shipped blend and clamps to its range", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.historyWeight == Approx(VolumetricFogSettings::DefaultHistoryWeight));

	settings.SetHistoryWeight(0.5f);
	CHECK(settings.historyWeight == Approx(0.5f));

	settings.SetHistoryWeight(1.0f);
	CHECK(settings.historyWeight == Approx(VolumetricFogSettings::MaxHistoryWeight));

	settings.SetHistoryWeight(-1.0f);
	CHECK(settings.historyWeight == Approx(0.0f));
}

TEST_CASE("Every volumetric fog quality preset box-filters the sun over the cell with several depth taps", "[volumetric_fog]")
{
	// Two depth taps band visibly across the shafts; the cell is a needle several metres long at mid range.
	VolumetricFogSettings settings;
	for (int level = 1; level <= 4; ++level)
	{
		settings.ApplyQualityLevel(level);
		CHECK(settings.sunTapsXY >= 2u);
		CHECK(settings.sunTapsZ >= 4u);
	}
}

TEST_CASE("Volumetric fog sun taps clamp to the supported range", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	settings.SetSunTaps(0, 0);
	CHECK(settings.sunTapsXY == 1u);
	CHECK(settings.sunTapsZ == 1u);

	settings.SetSunTaps(100, 100);
	CHECK(settings.sunTapsXY == VolumetricFogSettings::MaxSunTapsXY);
	CHECK(settings.sunTapsZ == VolumetricFogSettings::MaxSunTapsZ);
}

TEST_CASE("Volumetric fog sun mode defaults to the box filter and clamps to the known modes", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.sunMode == VolumetricFogSettings::SunModeBoxFilter);
	CHECK_FALSE(settings.IsSunTemporal());

	settings.SetSunMode(1);
	CHECK(settings.sunMode == VolumetricFogSettings::SunModeTemporal);
	CHECK(settings.IsSunTemporal());

	settings.SetSunMode(9);
	CHECK(settings.sunMode == VolumetricFogSettings::SunModeTemporalCascades);

	settings.SetSunMode(-3);
	CHECK(settings.sunMode == VolumetricFogSettings::SunModeBoxFilter);
}

TEST_CASE("Golden jitter covers the unit interval evenly and never repeats a short cycle", "[volumetric_fog]")
{
	CHECK(volumetric_fog::GoldenJitterForFrame(0) == Approx(0.0f));
	CHECK(volumetric_fog::GoldenJitterForFrame(1) == Approx(0.618034f).margin(1e-5));

	// The golden ratio's three-gap property bounds the largest gap between any 16 consecutive offsets below 2/16.
	std::vector<float> offsets;
	for (uint64 frame = 1000; frame < 1016; ++frame)
	{
		const float offset = volumetric_fog::GoldenJitterForFrame(frame);
		REQUIRE(offset >= 0.0f);
		REQUIRE(offset < 1.0f);
		offsets.push_back(offset);
	}

	std::sort(offsets.begin(), offsets.end());
	float largestGap = offsets.front() + 1.0f - offsets.back();
	for (size_t i = 1; i < offsets.size(); ++i)
	{
		largestGap = std::max(largestGap, offsets[i] - offsets[i - 1]);
	}
	CHECK(largestGap < 2.0f / 16.0f);
}

TEST_CASE("Fog shadow depth taps step about two shadow texels and one noise texel along the froxel", "[volumetric_fog]")
{
	using volumetric_fog::FogShadowDepthTaps;
	constexpr float mapSize = 256.0f;
	constexpr float noNoise = 1.0e6f;	// a noise tile this large never asks for a tap

	// A near froxel 0.2 m deep barely crosses a texel: the minimum covers it.
	CHECK(FogShadowDepthTaps(0.2f, 0.01f, mapSize, 1.0f, noNoise) == VolumetricFogSettings::MinFogShadowTapsZ);

	// 20 m deep, moving 0.006 shadow NDC per metre: 20 * 0.006 * 128 = 15.36 texels = 7.68 two-texel steps -> 8 taps.
	CHECK(FogShadowDepthTaps(20.0f, 0.006f, mapSize, 1.0f, noNoise) == 8u);

	// A deeper froxel needs more taps, but never more than the supported maximum.
	CHECK(FogShadowDepthTaps(40.0f, 0.006f, mapSize, 1.0f, noNoise) == 16u);
	CHECK(FogShadowDepthTaps(500.0f, 0.01f, mapSize, 1.0f, noNoise) == VolumetricFogSettings::MaxSunTapsZ);

	// Looking straight along the light the froxel stays on one texel: only the minimum.
	CHECK(FogShadowDepthTaps(50.0f, 0.0f, mapSize, 1.0f, noNoise) == VolumetricFogSettings::MinFogShadowTapsZ);

	// The noise asks for one tap per noise texel along the ray: 2 m on a 32 m tile of 64 texels = 4 texels.
	CHECK(FogShadowDepthTaps(2.0f, 0.0f, mapSize, 1.0f, 32.0f) == 4u);
	CHECK(FogShadowDepthTaps(2.0f, 0.0f, mapSize, 1.5f, 32.0f) == 6u);

	// The count grows with the map resolution (same froxel, finer texels).
	CHECK(FogShadowDepthTaps(20.0f, 0.006f, 512.0f, 1.0f, noNoise) > FogShadowDepthTaps(20.0f, 0.006f, mapSize, 1.0f, noNoise));
}