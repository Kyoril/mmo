// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/bloom_settings.h"

using namespace mmo;

TEST_CASE("BloomSettings defaults to High quality", "[bloom]")
{
	const BloomSettings settings;
	REQUIRE(settings.qualityLevel == 2);
	REQUIRE(settings.startDivisor == 2);
	REQUIRE(settings.levelCount == 6);
	REQUIRE(settings.intensity == Approx(0.08f));
	REQUIRE(settings.threshold == Approx(0.8f));
	REQUIRE(settings.knee == Approx(0.5f));
	REQUIRE(settings.IsEnabled());
}

TEST_CASE("BloomSettings quality levels map to start resolution and level count", "[bloom]")
{
	BloomSettings settings;

	settings.ApplyQualityLevel(0);
	REQUIRE(settings.levelCount == 0);
	REQUIRE_FALSE(settings.IsEnabled());

	settings.ApplyQualityLevel(1);
	REQUIRE(settings.startDivisor == 4);
	REQUIRE(settings.levelCount == 4);

	settings.ApplyQualityLevel(2);
	REQUIRE(settings.startDivisor == 2);
	REQUIRE(settings.levelCount == 6);

	settings.ApplyQualityLevel(9);
	REQUIRE(settings.qualityLevel == 2);
	settings.ApplyQualityLevel(-9);
	REQUIRE(settings.qualityLevel == 0);
}

TEST_CASE("BloomSettings clamps intensity and threshold", "[bloom]")
{
	BloomSettings settings;
	settings.SetIntensity(-1.0f);
	REQUIRE(settings.intensity == Approx(0.0f));
	settings.SetIntensity(3.0f);
	REQUIRE(settings.intensity == Approx(1.0f));
	settings.SetThreshold(-2.0f);
	REQUIRE(settings.threshold == Approx(0.0f));
	settings.SetThreshold(100.0f);
	REQUIRE(settings.threshold == Approx(16.0f));
}
