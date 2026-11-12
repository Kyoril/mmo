// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/environment_state.h"

#include <cmath>

using namespace mmo;

TEST_CASE("The default brightness lifts the world, night more than day", "[environment]")
{
	// The world shipped too dark on an average monitor, night most of all, so the renderer gets a
	// lift on top of whatever exposure the zone's profile authored.
	CHECK(DefaultEnvironmentBrightness(0.5f) == Approx(DefaultDayBrightness));
	CHECK(DefaultEnvironmentBrightness(0.0f) == Approx(DefaultNightBrightness));
	CHECK(DefaultEnvironmentBrightness(0.95f) == Approx(DefaultNightBrightness));

	CHECK(DefaultDayBrightness > 1.0f);
	CHECK(DefaultNightBrightness > DefaultDayBrightness);
}

TEST_CASE("The default brightness crosses twilight smoothly and never leaves its two values", "[environment]")
{
	float previous = DefaultEnvironmentBrightness(0.0f);

	// Dawn: a monotone fall from the night value to the day value, no step anywhere.
	for (int i = 1; i <= 50; ++i)
	{
		const float time = static_cast<float>(i) * 0.01f;
		const float brightness = DefaultEnvironmentBrightness(time);
		CHECK(brightness <= previous + 1e-5f);
		CHECK(brightness >= DefaultDayBrightness - 1e-5f);
		CHECK(brightness <= DefaultNightBrightness + 1e-5f);
		CHECK(std::abs(brightness - previous) < 0.1f);
		previous = brightness;
	}

	// Dusk: back up to the night value, again without a step.
	for (int i = 51; i <= 100; ++i)
	{
		const float time = static_cast<float>(i) * 0.01f;
		const float brightness = DefaultEnvironmentBrightness(time);
		CHECK(brightness >= previous - 1e-5f);
		CHECK(brightness <= DefaultNightBrightness + 1e-5f);
		CHECK(std::abs(brightness - previous) < 0.1f);
		previous = brightness;
	}
}

TEST_CASE("The default brightness wraps the day, so midnight is one value", "[environment]")
{
	CHECK(DefaultEnvironmentBrightness(1.0f) == Approx(DefaultEnvironmentBrightness(0.0f)));
	CHECK(DefaultEnvironmentBrightness(-0.1f) == Approx(DefaultEnvironmentBrightness(0.9f)));
	CHECK(DefaultEnvironmentBrightness(2.5f) == Approx(DefaultEnvironmentBrightness(0.5f)));
}
