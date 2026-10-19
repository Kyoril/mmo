// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/atmosphere_math.h"
#include "scene_graph/atmosphere_settings.h"

using namespace mmo;

TEST_CASE("Distance fog ramps linearly from start to end", "[atmosphere][distance_fog]")
{
	constexpr float start = 200.0f;
	constexpr float inverseRange = 1.0f / 600.0f;

	CHECK(atmosphere::DistanceFogAmount(0.0f, 1.0f, start, inverseRange) == Approx(0.0f));
	CHECK(atmosphere::DistanceFogAmount(200.0f, 1.0f, start, inverseRange) == Approx(0.0f));
	CHECK(atmosphere::DistanceFogAmount(500.0f, 1.0f, start, inverseRange) == Approx(0.5f));
	CHECK(atmosphere::DistanceFogAmount(800.0f, 1.0f, start, inverseRange) == Approx(1.0f));
	CHECK(atmosphere::DistanceFogAmount(5000.0f, 1.0f, start, inverseRange) == Approx(1.0f));
	CHECK(atmosphere::DistanceFogAmount(5000.0f, 0.6f, start, inverseRange) == Approx(0.6f));
}

TEST_CASE("Distance fog on the sky fades out over the sky band", "[atmosphere][distance_fog]")
{
	CHECK(atmosphere::DistanceFogSkyAmount(-0.3f, 1.0f, 0.2f) == Approx(1.0f));
	CHECK(atmosphere::DistanceFogSkyAmount(0.0f, 1.0f, 0.2f) == Approx(1.0f));
	CHECK(atmosphere::DistanceFogSkyAmount(0.1f, 1.0f, 0.2f) == Approx(0.5f));
	CHECK(atmosphere::DistanceFogSkyAmount(0.2f, 1.0f, 0.2f) == Approx(0.0f));
	CHECK(atmosphere::DistanceFogSkyAmount(0.9f, 1.0f, 0.2f) == Approx(0.0f));

	// A zero band leaves the sky untouched, even below the horizon.
	CHECK(atmosphere::DistanceFogSkyAmount(-0.5f, 1.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("CombineAtmosphere leaves distance fog off by default", "[atmosphere][distance_fog]")
{
	const AtmosphereParameters parameters;
	const AtmosphereTimeOfDay timeOfDay;
	CHECK_FALSE(parameters.HasDistanceFog());

	const AtmosphereConstants constants = CombineAtmosphere(parameters, timeOfDay, true);
	CHECK(constants.distanceFogOpacity == 0.0f);
	CHECK(constants.distanceFogInverseRange == 0.0f);
}

TEST_CASE("CombineAtmosphere packs an active distance fog", "[atmosphere][distance_fog]")
{
	AtmosphereParameters parameters;
	parameters.SetDistanceFogStart(300.0f);
	parameters.SetDistanceFogEnd(800.0f);
	parameters.SetDistanceFogSkyBand(0.25f);

	AtmosphereTimeOfDay timeOfDay;
	timeOfDay.distanceFogColor[0] = 0.4f;
	timeOfDay.distanceFogColor[1] = -1.0f;
	timeOfDay.distanceFogColor[2] = 0.9f;
	timeOfDay.distanceFogOpacity = 1.5f;

	const AtmosphereConstants constants = CombineAtmosphere(parameters, timeOfDay, true);
	CHECK(constants.distanceFogOpacity == Approx(1.0f));
	CHECK(constants.distanceFogStart == Approx(300.0f));
	CHECK(constants.distanceFogInverseRange == Approx(1.0f / 500.0f));
	CHECK(constants.distanceFogSkyBand == Approx(0.25f));
	CHECK(constants.distanceFogColor[0] == Approx(0.4f));
	CHECK(constants.distanceFogColor[1] == Approx(0.0f));
	CHECK(constants.distanceFogColor[2] == Approx(0.9f));

	SECTION("disabled fog turns the distance fog off too")
	{
		CHECK(CombineAtmosphere(parameters, timeOfDay, false).distanceFogOpacity == 0.0f);
	}

	SECTION("end not beyond start turns it off")
	{
		parameters.SetDistanceFogEnd(300.0f);
		CHECK(CombineAtmosphere(parameters, timeOfDay, true).distanceFogOpacity == 0.0f);
	}
}

TEST_CASE("AtmosphereParameters clamps the distance fog values", "[atmosphere][distance_fog]")
{
	AtmosphereParameters parameters;
	parameters.SetDistanceFogStart(-5.0f);
	parameters.SetDistanceFogEnd(1.0e7f);
	parameters.SetDistanceFogSkyBand(3.0f);
	CHECK(parameters.distanceFogStart == Approx(0.0f));
	CHECK(parameters.distanceFogEnd == Approx(100000.0f));
	CHECK(parameters.distanceFogSkyBand == Approx(1.0f));
}
