// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/plaza_interaction.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Plaza pick takes the nearest pickable hit", "[plaza]")
{
	CHECK(PickNearestPlazaHit({}) == -1);
	CHECK(PickNearestPlazaHit({ { 2, 12.0f, true }, { 4, 9.5f, true }, { 1, 15.0f, true } }) == 4);
}

TEST_CASE("Plaza pick skips characters that cannot be picked", "[plaza]")
{
	// The nearest one is walking: the next one behind it is picked.
	CHECK(PickNearestPlazaHit({ { 3, 5.0f, false }, { 0, 8.0f, true } }) == 0);
	CHECK(PickNearestPlazaHit({ { 3, 5.0f, false } }) == -1);
	CHECK(PickNearestPlazaHit({ { -1, 1.0f, true }, { 2, 4.0f, true } }) == 2);
}

TEST_CASE("Hover turn faces the camera within the limit", "[plaza]")
{
	const Vector3 position(0.0f, 0.0f, 0.0f);

	// Facing 90 degrees looks down -Z: a camera straight ahead needs no turn.
	CHECK(PlazaTurnTowardsDegrees(90.0f, position, Vector3(0.0f, 2.0f, -10.0f), 40.0f) == Approx(0.0f).margin(0.001f));

	// A camera 20 degrees to the side is faced fully.
	const float rad = 70.0f * 3.14159265f / 180.0f;
	const Vector3 side(10.0f * std::cos(rad), 0.0f, -10.0f * std::sin(rad));
	CHECK(PlazaTurnTowardsDegrees(90.0f, position, side, 40.0f) == Approx(-20.0f).margin(0.01f));

	// 45 degrees away is limited to 40.
	CHECK(PlazaTurnTowardsDegrees(90.0f, position, Vector3(10.0f, 0.0f, -10.0f), 40.0f) == Approx(-40.0f));
	CHECK(PlazaTurnTowardsDegrees(90.0f, position, Vector3(-10.0f, 0.0f, -10.0f), 40.0f) == Approx(40.0f));
}

TEST_CASE("Hover turn takes the shortest way around", "[plaza]")
{
	// Yaw 350 equals -10: the camera ahead at 0 degrees (+X) is a turn of +10, not -350.
	CHECK(PlazaTurnTowardsDegrees(350.0f, Vector3::Zero, Vector3(10.0f, 0.0f, 0.0f), 40.0f) == Approx(10.0f).margin(0.01f));
	CHECK(PlazaTurnTowardsDegrees(-350.0f, Vector3::Zero, Vector3(10.0f, 0.0f, 0.0f), 40.0f) == Approx(-10.0f).margin(0.01f));

	// A camera right on top of the character needs no turn.
	CHECK(PlazaTurnTowardsDegrees(90.0f, Vector3::Zero, Vector3(0.0f, 3.0f, 0.0f), 40.0f) == 0.0f);
}

TEST_CASE("Greeting weight fades in and out", "[plaza]")
{
	CHECK(PlazaGreetingWeight(0.0f, 2.0f, 0.2f, 0.3f) == 0.0f);
	CHECK(PlazaGreetingWeight(0.1f, 2.0f, 0.2f, 0.3f) == Approx(0.5f));
	CHECK(PlazaGreetingWeight(1.0f, 2.0f, 0.2f, 0.3f) == Approx(1.0f));
	CHECK(PlazaGreetingWeight(1.85f, 2.0f, 0.2f, 0.3f) == Approx(0.5f));
	CHECK(PlazaGreetingWeight(2.0f, 2.0f, 0.2f, 0.3f) == 0.0f);
	CHECK(PlazaGreetingWeight(-1.0f, 2.0f, 0.2f, 0.3f) == 0.0f);

	// A clip shorter than both fades never reaches full weight but stays within [0, 1].
	const float shortClip = PlazaGreetingWeight(0.15f, 0.3f, 0.2f, 0.3f);
	CHECK(shortClip > 0.0f);
	CHECK(shortClip <= 1.0f);
}

TEST_CASE("Seated poses are recognised", "[plaza]")
{
	CHECK(IsPlazaSeatedPose("EmoteSitLoop01"));
	CHECK(IsPlazaSeatedPose("EmoteSleepLoop01"));
	CHECK(IsPlazaSeatedPose("KneelLoop"));
	CHECK_FALSE(IsPlazaSeatedPose("Idle"));
	CHECK_FALSE(IsPlazaSeatedPose("EmoteTalk"));
}

TEST_CASE("Hover reaction reports the enter once and eases in and out", "[plaza]")
{
	PlazaHoverReaction reaction;
	CHECK(reaction.IsIdle());

	CHECK(reaction.Update(true, 0.05f));
	CHECK_FALSE(reaction.Update(true, 0.05f));
	CHECK(reaction.GetGlow() > 0.0f);
	CHECK(reaction.GetGlow() < 1.0f);

	// The glow is fully in after its fade time, the turn takes longer.
	CHECK_FALSE(reaction.Update(true, 0.1f));
	CHECK(reaction.GetGlow() == Approx(1.0f));
	CHECK(reaction.GetTurn() < 1.0f);
	reaction.Update(true, 1.0f);
	CHECK(reaction.GetTurn() == Approx(1.0f));

	// Leaving fades both back out.
	CHECK_FALSE(reaction.Update(false, 0.075f));
	CHECK(reaction.GetGlow() == Approx(0.5f));
	reaction.Update(false, 1.0f);
	CHECK(reaction.IsIdle());

	// A new hover is a new enter.
	CHECK(reaction.Update(true, 0.01f));
}

TEST_CASE("Hover reaction reset drops everything at once", "[plaza]")
{
	PlazaHoverReaction reaction;
	reaction.Update(true, 1.0f);
	reaction.Reset();
	CHECK(reaction.IsIdle());
	CHECK(reaction.GetGlow() == 0.0f);
	CHECK(reaction.Update(true, 0.01f));
}
