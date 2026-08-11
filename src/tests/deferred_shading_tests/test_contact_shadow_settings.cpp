// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/contact_shadow_settings.h"

using namespace mmo;

TEST_CASE("ContactShadowSettings defaults match the design spec", "[contact_shadows]")
{
	const ContactShadowSettings settings;

	REQUIRE(settings.enabled);
	REQUIRE_FALSE(settings.debugVisualization);
	REQUIRE(settings.rayLength == Approx(0.3f));
	REQUIRE(settings.thickness == Approx(0.2f));
	REQUIRE(settings.intensity == Approx(1.0f));
	REQUIRE(settings.normalBias == Approx(0.02f));
	REQUIRE(settings.fadeStart == Approx(21.0f));
	REQUIRE(settings.fadeEnd == Approx(35.0f));
	// Default quality is Medium.
	REQUIRE(settings.stepCount == 8);
}

TEST_CASE("ContactShadowSettings quality levels map to step counts", "[contact_shadows]")
{
	ContactShadowSettings settings;

	settings.ApplyQualityLevel(0);
	REQUIRE(settings.stepCount == 4);

	settings.ApplyQualityLevel(1);
	REQUIRE(settings.stepCount == 8);

	settings.ApplyQualityLevel(2);
	REQUIRE(settings.stepCount == 16);
}

TEST_CASE("ContactShadowSettings quality levels clamp out-of-range input", "[contact_shadows]")
{
	ContactShadowSettings settings;

	// Below range clamps to Low.
	settings.ApplyQualityLevel(-5);
	REQUIRE(settings.stepCount == 4);

	// Above range clamps to High.
	settings.ApplyQualityLevel(99);
	REQUIRE(settings.stepCount == 16);
}

TEST_CASE("ContactShadowSettings clamps ray length to a sane metric range", "[contact_shadows]")
{
	ContactShadowSettings settings;

	settings.SetRayLength(0.0f);
	REQUIRE(settings.rayLength == Approx(0.02f));

	settings.SetRayLength(-1.0f);
	REQUIRE(settings.rayLength == Approx(0.02f));

	settings.SetRayLength(1000.0f);
	REQUIRE(settings.rayLength == Approx(2.0f));

	settings.SetRayLength(0.5f);
	REQUIRE(settings.rayLength == Approx(0.5f));
}

TEST_CASE("ContactShadowSettings clamps thickness, intensity and normal bias", "[contact_shadows]")
{
	ContactShadowSettings settings;

	// A thickness of zero would stop solid geometry registering as an occluder at all.
	settings.SetThickness(0.0f);
	REQUIRE(settings.thickness == Approx(0.01f));

	settings.SetThickness(100.0f);
	REQUIRE(settings.thickness == Approx(2.0f));

	// Intensity is a plain [0, 1] multiplier: zero is legal and means "contribute nothing".
	settings.SetIntensity(-3.0f);
	REQUIRE(settings.intensity == Approx(0.0f));

	settings.SetIntensity(50.0f);
	REQUIRE(settings.intensity == Approx(1.0f));

	settings.SetIntensity(0.5f);
	REQUIRE(settings.intensity == Approx(0.5f));

	// A zero normal bias is legal - the ray also starts half a step out - but a negative one
	// would push the origin into the surface and guarantee self-shadowing.
	settings.SetNormalBias(-1.0f);
	REQUIRE(settings.normalBias == Approx(0.0f));

	settings.SetNormalBias(10.0f);
	REQUIRE(settings.normalBias == Approx(0.5f));
}

TEST_CASE("ContactShadowSettings derives fade start from fade end", "[contact_shadows]")
{
	ContactShadowSettings settings;

	settings.SetFadeDistance(50.0f);
	REQUIRE(settings.fadeEnd == Approx(50.0f));
	REQUIRE(settings.fadeStart == Approx(30.0f));

	// Clamped above: GBuffer_Normal.a is fp16, so the term is quantization noise past ~60m.
	settings.SetFadeDistance(1000.0f);
	REQUIRE(settings.fadeEnd == Approx(60.0f));
	REQUIRE(settings.fadeStart == Approx(36.0f));

	// Clamped below.
	settings.SetFadeDistance(0.0f);
	REQUIRE(settings.fadeEnd == Approx(2.0f));
	REQUIRE(settings.fadeStart == Approx(1.2f));
}

TEST_CASE("ContactShadowSettings fade start always stays below fade end", "[contact_shadows]")
{
	ContactShadowSettings settings;

	// The shader divides by (fadeEnd - fadeStart), so an inverted or degenerate pair would be a
	// division by zero or a negated fade. fadeStart is derived, never set, precisely to make that
	// unrepresentable - including at both clamp boundaries.
	const float inputs[] = { -100.0f, 0.0f, 1.0f, 2.0f, 10.0f, 35.0f, 60.0f, 61.0f, 1e9f };
	for (const float input : inputs)
	{
		settings.SetFadeDistance(input);
		REQUIRE(settings.fadeStart < settings.fadeEnd);
		REQUIRE(settings.fadeEnd - settings.fadeStart > 0.0f);
	}
}

TEST_CASE("ContactShadowSettings folds disabled into the effective step count", "[contact_shadows]")
{
	ContactShadowSettings settings;

	// This is the value the shader branches on, so "off" must be exactly zero and must be
	// unreachable while enabled - otherwise a quality preset could silently disable the effect.
	REQUIRE(settings.GetEffectiveStepCount() == 8);

	settings.enabled = false;
	REQUIRE(settings.GetEffectiveStepCount() == 0);

	// Disabled wins over every quality preset.
	settings.ApplyQualityLevel(2);
	REQUIRE(settings.GetEffectiveStepCount() == 0);

	settings.enabled = true;
	REQUIRE(settings.GetEffectiveStepCount() == 16);

	// No quality level yields zero, so zero unambiguously means "disabled".
	for (int level = -5; level <= 5; ++level)
	{
		settings.ApplyQualityLevel(level);
		REQUIRE(settings.GetEffectiveStepCount() > 0);
	}
}
