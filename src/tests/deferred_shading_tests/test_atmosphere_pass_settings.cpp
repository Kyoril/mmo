// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/atmosphere_pass_settings.h"

using namespace mmo;

TEST_CASE("AtmospherePassSettings defaults to High quality", "[atmosphere]")
{
	const AtmospherePassSettings settings;
	REQUIRE(settings.qualityLevel == 3);
	REQUIRE(settings.resolutionDivisor == 2);
	REQUIRE(settings.stepCount == 48);
	REQUIRE(settings.blurIterations == 2);
	REQUIRE(settings.marchDistance == Approx(200.0f));
	REQUIRE(settings.skyDistance == Approx(2000.0f));
	REQUIRE(settings.debugMode == 0);
	REQUIRE(settings.IsMarchEnabled());
}

TEST_CASE("AtmospherePassSettings quality levels map to resolution, steps and blur", "[atmosphere]")
{
	AtmospherePassSettings settings;

	settings.ApplyQualityLevel(0);
	REQUIRE(settings.qualityLevel == 0);
	REQUIRE(settings.stepCount == 0);
	REQUIRE(settings.blurIterations == 0);
	REQUIRE(settings.resolutionDivisor == 1);
	REQUIRE_FALSE(settings.IsMarchEnabled());

	settings.ApplyQualityLevel(1);
	REQUIRE(settings.resolutionDivisor == 4);
	REQUIRE(settings.stepCount == 12);
	REQUIRE(settings.blurIterations == 1);

	settings.ApplyQualityLevel(2);
	REQUIRE(settings.resolutionDivisor == 2);
	REQUIRE(settings.stepCount == 24);
	REQUIRE(settings.blurIterations == 1);

	settings.ApplyQualityLevel(3);
	REQUIRE(settings.resolutionDivisor == 2);
	REQUIRE(settings.stepCount == 48);
	REQUIRE(settings.blurIterations == 2);

	settings.ApplyQualityLevel(4);
	REQUIRE(settings.resolutionDivisor == 1);
	REQUIRE(settings.stepCount == 64);
	REQUIRE(settings.blurIterations == 2);
}

TEST_CASE("AtmospherePassSettings clamps out-of-range input", "[atmosphere]")
{
	AtmospherePassSettings settings;

	settings.ApplyQualityLevel(-3);
	REQUIRE(settings.qualityLevel == 0);
	settings.ApplyQualityLevel(42);
	REQUIRE(settings.qualityLevel == 4);

	settings.SetMarchDistance(1000.0f);
	REQUIRE(settings.marchDistance == Approx(AtmospherePassSettings::MaxMarchDistance));
	settings.SetMarchDistance(-5.0f);
	REQUIRE(settings.marchDistance == Approx(0.0f));

	settings.SetDebugMode(7);
	REQUIRE(settings.debugMode == 3);
	settings.SetDebugMode(-1);
	REQUIRE(settings.debugMode == 0);
}
