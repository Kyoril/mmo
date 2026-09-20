// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/atmosphere_settings.h"

using namespace mmo;

TEST_CASE("AtmosphereParameters defaults match the design spec", "[atmosphere]")
{
	const AtmosphereParameters parameters;
	REQUIRE(parameters.density == Approx(0.0015f));
	REQUIRE(parameters.heightFalloff == Approx(0.05f));
	REQUIRE(parameters.baseHeight == Approx(0.0f));
	REQUIRE(parameters.anisotropy == Approx(0.7f));
	REQUIRE(parameters.shaftStrength == Approx(1.25f));
}

TEST_CASE("AtmosphereParameters setters clamp to sane ranges", "[atmosphere]")
{
	AtmosphereParameters parameters;

	parameters.SetDensity(-1.0f);
	REQUIRE(parameters.density == Approx(0.0f));
	parameters.SetDensity(5.0f);
	REQUIRE(parameters.density == Approx(1.0f));

	parameters.SetHeightFalloff(-0.5f);
	REQUIRE(parameters.heightFalloff == Approx(0.0f));
	parameters.SetHeightFalloff(3.0f);
	REQUIRE(parameters.heightFalloff == Approx(1.0f));

	parameters.SetBaseHeight(-50000.0f);
	REQUIRE(parameters.baseHeight == Approx(-10000.0f));

	// g >= 1 makes Henyey-Greenstein singular.
	parameters.SetAnisotropy(1.0f);
	REQUIRE(parameters.anisotropy == Approx(0.95f));
	parameters.SetAnisotropy(-0.3f);
	REQUIRE(parameters.anisotropy == Approx(0.0f));

	parameters.SetShaftStrength(100.0f);
	REQUIRE(parameters.shaftStrength == Approx(16.0f));
}

TEST_CASE("CombineAtmosphere multiplies base values by the time-of-day curve", "[atmosphere]")
{
	AtmosphereParameters parameters;
	parameters.density = 0.02f;
	parameters.shaftStrength = 2.0f;

	AtmosphereTimeOfDay timeOfDay;
	timeOfDay.densityMultiplier = 2.5f;
	timeOfDay.shaftMultiplier = 0.5f;
	timeOfDay.fogTint[0] = 0.9f;
	timeOfDay.fogTint[1] = 0.6f;
	timeOfDay.fogTint[2] = 0.4f;
	timeOfDay.sunScatterColor[0] = 1.0f;
	timeOfDay.sunScatterColor[1] = 0.7f;
	timeOfDay.sunScatterColor[2] = 0.4f;

	const AtmosphereConstants constants = CombineAtmosphere(parameters, timeOfDay, true);
	REQUIRE(constants.density == Approx(0.05f));
	REQUIRE(constants.shaftStrength == Approx(1.0f));
	REQUIRE(constants.heightFalloff == Approx(parameters.heightFalloff));
	REQUIRE(constants.baseHeight == Approx(parameters.baseHeight));
	REQUIRE(constants.anisotropy == Approx(parameters.anisotropy));
	REQUIRE(constants.fogTint[0] == Approx(0.9f));
	REQUIRE(constants.fogTint[2] == Approx(0.4f));
	REQUIRE(constants.sunScatterColor[1] == Approx(0.7f));
}

TEST_CASE("CombineAtmosphere zeroes density when fog is disabled", "[atmosphere]")
{
	const AtmosphereConstants constants = CombineAtmosphere(AtmosphereParameters(), AtmosphereTimeOfDay(), false);
	REQUIRE(constants.density == Approx(0.0f));
}

TEST_CASE("CombineAtmosphere never lets a curve overshoot make values negative", "[atmosphere]")
{
	AtmosphereTimeOfDay timeOfDay;
	timeOfDay.densityMultiplier = -0.2f;
	timeOfDay.shaftMultiplier = -1.0f;
	timeOfDay.fogTint[1] = -0.1f;
	timeOfDay.sunScatterColor[2] = -0.3f;

	const AtmosphereConstants constants = CombineAtmosphere(AtmosphereParameters(), timeOfDay, true);
	REQUIRE(constants.density == Approx(0.0f));
	REQUIRE(constants.shaftStrength == Approx(0.0f));
	REQUIRE(constants.fogTint[1] == Approx(0.0f));
	REQUIRE(constants.sunScatterColor[2] == Approx(0.0f));
}

TEST_CASE("CombineAtmosphere keeps the fog base at its authored world height", "[atmosphere]")
{
	// The base is an absolute world Y, so a fog bank stays put: flying up leaves it below you.
	AtmosphereParameters parameters;
	parameters.baseHeight = 240.0f;
	REQUIRE(CombineAtmosphere(parameters, AtmosphereTimeOfDay(), true).baseHeight == Approx(240.0f));

	parameters.baseHeight = -90.0f;
	REQUIRE(CombineAtmosphere(parameters, AtmosphereTimeOfDay(), true).baseHeight == Approx(-90.0f));
}
