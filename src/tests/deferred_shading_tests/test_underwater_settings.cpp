// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/underwater_settings.h"

using namespace mmo;

TEST_CASE("UnderwaterState_Defaults_To_Dry", "[underwater]")
{
	const UnderwaterState state;

	CHECK_FALSE(state.active);
	CHECK(state.submersionDepth == Approx(0.0f));
	CHECK(state.transitionPhase == Approx(0.0f));
	CHECK(state.fogDensity == Approx(0.0f));
	CHECK(state.causticsStrength == Approx(0.0f));
}

TEST_CASE("UnderwaterSettings_Transition_Rises_Toward_One", "[underwater]")
{
	const UnderwaterSettings settings;

	// 0.4s default, so a 0.2s step covers half the range.
	CHECK(settings.AdvanceTransition(0.0f, true, 0.2f) == Approx(0.5f));
}

TEST_CASE("UnderwaterSettings_Transition_Clamps_At_One", "[underwater]")
{
	const UnderwaterSettings settings;

	CHECK(settings.AdvanceTransition(0.9f, true, 1.0f) == Approx(1.0f));
	CHECK(settings.AdvanceTransition(1.0f, true, 1.0f) == Approx(1.0f));
}

TEST_CASE("UnderwaterSettings_Transition_Falls_Toward_Zero", "[underwater]")
{
	const UnderwaterSettings settings;

	CHECK(settings.AdvanceTransition(1.0f, false, 0.2f) == Approx(0.5f));
	CHECK(settings.AdvanceTransition(0.1f, false, 1.0f) == Approx(0.0f));
	CHECK(settings.AdvanceTransition(0.0f, false, 1.0f) == Approx(0.0f));
}

TEST_CASE("UnderwaterSettings_Transition_Survives_Zero_Delta", "[underwater]")
{
	// A paused frame must neither advance the transition nor divide by zero.
	const UnderwaterSettings settings;

	CHECK(settings.AdvanceTransition(0.3f, true, 0.0f) == Approx(0.3f));
	CHECK(settings.AdvanceTransition(0.3f, false, 0.0f) == Approx(0.3f));
}

TEST_CASE("UnderwaterSettings_Transition_Handles_Zero_Duration", "[underwater]")
{
	// A zero transition time must snap rather than produce inf or NaN.
	UnderwaterSettings settings;
	settings.transitionSeconds = 0.0f;

	CHECK(settings.AdvanceTransition(0.0f, true, 0.016f) == Approx(1.0f));
	CHECK(settings.AdvanceTransition(1.0f, false, 0.016f) == Approx(0.0f));
}

TEST_CASE("UnderwaterSettings_Transition_Completes_Over_Its_Duration", "[underwater]")
{
	// Stepping at 60Hz for exactly transitionSeconds must reach 1, not stall just short of it.
	const UnderwaterSettings settings;

	float phase = 0.0f;
	for (int frame = 0; frame < 24; ++frame)
	{
		phase = settings.AdvanceTransition(phase, true, 1.0f / 60.0f);
	}

	CHECK(phase == Approx(1.0f));
}

TEST_CASE("UnderwaterPass_Skipped_When_Fully_Dry", "[underwater]")
{
	// The zero-cost path. Every consumer of GetFinalRenderTarget - the client world frame, the
	// world editor viewport, the world model editor, the material instance editor and the spell
	// visualization preview - depends on this being false out of water.
	const UnderwaterState state;

	CHECK_FALSE(ShouldRunUnderwaterPass(state));
}

TEST_CASE("UnderwaterPass_Runs_While_Submerged", "[underwater]")
{
	UnderwaterState state;
	state.active = true;
	state.transitionPhase = 1.0f;

	CHECK(ShouldRunUnderwaterPass(state));
}

TEST_CASE("UnderwaterPass_Runs_While_Surfacing", "[underwater]")
{
	// The camera has left the water but the crossing is still unwinding. Skipping here would
	// cut the meniscus off mid-animation.
	UnderwaterState state;
	state.active = false;
	state.transitionPhase = 0.35f;

	CHECK(ShouldRunUnderwaterPass(state));
}

TEST_CASE("UnderwaterPass_Runs_On_First_Submerged_Frame", "[underwater]")
{
	// Entering the water, the phase is still 0 on the frame active first becomes true. The pass
	// must run anyway or the first frame underwater renders dry.
	UnderwaterState state;
	state.active = true;
	state.transitionPhase = 0.0f;

	CHECK(ShouldRunUnderwaterPass(state));
}
