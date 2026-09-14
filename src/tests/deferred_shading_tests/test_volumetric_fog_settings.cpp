// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/volumetric_fog_settings.h"

#include <set>

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
	CHECK(settings.debugMode == 3u);
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
