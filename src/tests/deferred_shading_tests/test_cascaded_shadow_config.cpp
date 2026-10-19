// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/cascaded_shadow_camera_setup.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Near cascades render at the full shadow map size", "[cascaded_shadows]")
{
	CascadedShadowConfig config;
	config.shadowMapSize = 2048;

	CHECK(config.GetCascadeMapSize(0) == 2048);
	CHECK(config.GetCascadeMapSize(1) == 2048);
}

TEST_CASE("Distant cascades render at half the shadow map size, never below 256", "[cascaded_shadows]")
{
	CascadedShadowConfig config;
	config.shadowMapSize = 2048;
	CHECK(config.GetCascadeMapSize(2) == 1024);
	CHECK(config.GetCascadeMapSize(3) == 1024);

	config.shadowMapSize = 512;
	CHECK(config.GetCascadeMapSize(2) == 256);
	CHECK(config.GetCascadeMapSize(3) == 256);

	config.shadowMapSize = 256;
	CHECK(config.GetCascadeMapSize(3) == 256);
}

TEST_CASE("The texel snap step matches the texel size each cascade is actually rendered at", "[cascaded_shadows]")
{
	// Snapping a half-resolution cascade on the full-resolution grid moves its window by half a texel
	// whenever the camera moves, re-rasterizing every shadow edge at a new sub-texel offset.
	CascadedShadowConfig config;
	config.shadowMapSize = 2048;
	const float window = 200.0f;

	for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
	{
		CHECK(config.GetCascadeTexelSize(window, i) == window / static_cast<float>(config.GetCascadeMapSize(i)));
	}

	CHECK(config.GetCascadeTexelSize(window, 3) == 2.0f * config.GetCascadeTexelSize(window, 0));
}

TEST_CASE("Lower shadow qualities render more cascades at half size and snap them on the half-size grid", "[cascaded_shadows]")
{
	// Low and Medium keep only the nearest cascade at full size. Cascade 1 then covers the ground right in
	// front of the player, where a snap on the wrong grid shows as shadow edges crawling while walking.
	CascadedShadowConfig config;
	config.shadowMapSize = 1024;
	config.fullResolutionCascades = 1;

	CHECK(config.GetCascadeMapSize(0) == 1024);
	CHECK(config.GetCascadeMapSize(1) == 512);
	CHECK(config.GetCascadeTexelSize(100.0f, 1) == 100.0f / 512.0f);
}

TEST_CASE("Cascades reach toward a low sun as far as a tall caster's shadow", "[cascaded_shadows]")
{
	// A tree beyond the cascade's near plane gets sliced by it, and the plane moves with the camera: long
	// low-sun shadows popped while walking. The reach covers casterHeight / tan(elevation).
	CascadedShadowConfig config;
	config.casterHeight = 50.0f;
	config.maxLightwardReach = 800.0f;

	const auto sunAt = [](const float elevationDegrees)
	{
		const float elevation = elevationDegrees * 3.14159265f / 180.0f;
		return Vector3(0.0f, -std::sin(elevation), std::cos(elevation));
	};

	CHECK(config.GetLightwardReach(sunAt(45.0f)) == Approx(50.0f).margin(0.01f));
	CHECK(config.GetLightwardReach(sunAt(10.0f)) == Approx(50.0f / std::tan(10.0f * 3.14159265f / 180.0f)).margin(0.1f));
	CHECK(config.GetLightwardReach(sunAt(1.0f)) == Approx(800.0f));
	CHECK(config.GetLightwardReach(Vector3(0.0f, 0.2f, 1.0f).NormalizedCopy()) == Approx(800.0f));
}