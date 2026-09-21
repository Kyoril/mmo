// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/light.h"
#include "scene_graph/light_math.h"

using namespace mmo;

TEST_CASE("Spot cone angles default to a usable cone", "[light]")
{
	const Light light(LightType::Spot);
	CHECK(light.GetOuterConeAngle() == Approx(light_math::DefaultOuterConeAngle));
	CHECK(light.GetInnerConeAngle() == Approx(light_math::DefaultInnerConeAngle));
	CHECK(light.GetFogScattering() == Approx(1.0f));
}

TEST_CASE("Spot cone angles clamp and keep inner inside outer", "[light]")
{
	Light light(LightType::Spot);

	light.SetOuterConeAngle(500.0f);
	CHECK(light.GetOuterConeAngle() == Approx(light_math::MaxConeAngle));

	light.SetOuterConeAngle(-3.0f);
	CHECK(light.GetOuterConeAngle() == Approx(light_math::MinConeAngle));
	CHECK(light.GetInnerConeAngle() <= light.GetOuterConeAngle());

	light.SetOuterConeAngle(60.0f);
	light.SetInnerConeAngle(90.0f);
	CHECK(light.GetInnerConeAngle() == Approx(60.0f));

	light.SetInnerConeAngle(20.0f);
	light.SetOuterConeAngle(10.0f);
	CHECK(light.GetOuterConeAngle() == Approx(10.0f));
	CHECK(light.GetInnerConeAngle() == Approx(10.0f));
}

TEST_CASE("Fog scattering clamps to its range", "[light]")
{
	Light light(LightType::Point);
	light.SetFogScattering(-1.0f);
	CHECK(light.GetFogScattering() == Approx(0.0f));
	light.SetFogScattering(20.0f);
	CHECK(light.GetFogScattering() == Approx(light_math::MaxFogScattering));
	light.SetFogScattering(2.5f);
	CHECK(light.GetFogScattering() == Approx(2.5f));
}
