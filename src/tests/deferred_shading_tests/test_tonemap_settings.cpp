// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/tonemap_settings.h"

using namespace mmo;

TEST_CASE("TonemapSettings defaults leave the image unchanged", "[tonemap]")
{
	const TonemapSettings settings;
	REQUIRE(settings.exposure == Approx(1.0f));
	REQUIRE(settings.ditherStrength == Approx(0.5f));
}

TEST_CASE("TonemapSettings clamps exposure", "[tonemap]")
{
	TonemapSettings settings;
	settings.SetExposure(0.0f);
	REQUIRE(settings.exposure == Approx(0.1f));
	settings.SetExposure(20.0f);
	REQUIRE(settings.exposure == Approx(8.0f));
	settings.SetExposure(1.25f);
	REQUIRE(settings.exposure == Approx(1.25f));
}
