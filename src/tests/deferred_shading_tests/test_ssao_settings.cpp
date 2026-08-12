// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/ssao_settings.h"

using namespace mmo;

TEST_CASE("SsaoSettings defaults match the design spec", "[ssao]")
{
	const SsaoSettings settings;

	REQUIRE(settings.enabled);
	REQUIRE(settings.halfResolution);
	REQUIRE_FALSE(settings.debugVisualization);
	REQUIRE(settings.radius == Approx(0.75f));
	REQUIRE(settings.intensity == Approx(1.0f));
	REQUIRE(settings.thickness == Approx(0.25f));
	// Default quality is High.
	REQUIRE(settings.sliceCount == 4);
	REQUIRE(settings.stepCount == 12);
}

TEST_CASE("SsaoSettings quality levels map to slice and step counts", "[ssao]")
{
	SsaoSettings settings;

	settings.ApplyQualityLevel(0);
	REQUIRE(settings.sliceCount == 2);
	REQUIRE(settings.stepCount == 4);

	settings.ApplyQualityLevel(1);
	REQUIRE(settings.sliceCount == 3);
	REQUIRE(settings.stepCount == 8);

	settings.ApplyQualityLevel(2);
	REQUIRE(settings.sliceCount == 4);
	REQUIRE(settings.stepCount == 12);
}

TEST_CASE("SsaoSettings quality levels clamp out-of-range input", "[ssao]")
{
	SsaoSettings settings;

	// Below range clamps to Low.
	settings.ApplyQualityLevel(-5);
	REQUIRE(settings.sliceCount == 2);
	REQUIRE(settings.stepCount == 4);

	// Above range clamps to High.
	settings.ApplyQualityLevel(99);
	REQUIRE(settings.sliceCount == 4);
	REQUIRE(settings.stepCount == 12);
}

TEST_CASE("SsaoSettings clamps radius to a sane metric range", "[ssao]")
{
	SsaoSettings settings;

	settings.SetRadius(0.0f);
	REQUIRE(settings.radius == Approx(0.05f));

	settings.SetRadius(-1.0f);
	REQUIRE(settings.radius == Approx(0.05f));

	settings.SetRadius(1000.0f);
	REQUIRE(settings.radius == Approx(4.0f));

	settings.SetRadius(1.25f);
	REQUIRE(settings.radius == Approx(1.25f));
}

TEST_CASE("SsaoSettings clamps intensity and thickness", "[ssao]")
{
	SsaoSettings settings;

	settings.SetIntensity(-3.0f);
	REQUIRE(settings.intensity == Approx(0.1f));

	settings.SetIntensity(50.0f);
	REQUIRE(settings.intensity == Approx(8.0f));

	// Thickness of zero would stop solid geometry occluding and leak light through walls.
	settings.SetThickness(0.0f);
	REQUIRE(settings.thickness == Approx(0.01f));

	settings.SetThickness(100.0f);
	REQUIRE(settings.thickness == Approx(4.0f));
}
