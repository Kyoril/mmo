// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/traversal.h"

#include <cmath>
#include <limits>

using namespace mmo;

namespace
{
	VigorState Full()
	{
		VigorState state;
		state.current = 100.0f;
		state.max = 100.0f;
		state.secondsSinceDrain = 10.0f;
		return state;
	}
}

TEST_CASE("Glide drains 8 per second", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	AdvanceVigor(v, s, VigorActivity::Gliding, 0.0f, false, false, 2.0f);
	CHECK(v.current == Approx(84.0f));
}

TEST_CASE("Updraft halves glide drain", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	AdvanceVigor(v, s, VigorActivity::GlidingInUpdraft, 0.0f, false, false, 2.0f);
	CHECK(v.current == Approx(92.0f));
}

TEST_CASE("Extra drain adds", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	AdvanceVigor(v, s, VigorActivity::Draining, 12.0f, false, false, 1.0f);
	CHECK(v.current == Approx(88.0f));
}

TEST_CASE("No regen before delay", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	v.current = 50.0f;
	AdvanceVigor(v, s, VigorActivity::Draining, 10.0f, false, true, 0.1f);
	const float afterDrain = v.current;
	AdvanceVigor(v, s, VigorActivity::Idle, 0.0f, false, true, 0.5f);
	CHECK(v.current == Approx(afterDrain));
}

TEST_CASE("Regen after delay", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	v.current = 50.0f;
	v.secondsSinceDrain = 1.0f;
	AdvanceVigor(v, s, VigorActivity::Idle, 0.0f, false, true, 1.0f);
	CHECK(v.current == Approx(75.0f));
}

TEST_CASE("No regen in combat", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	v.current = 50.0f;
	AdvanceVigor(v, s, VigorActivity::Idle, 0.0f, true, true, 1.0f);
	CHECK(v.current == Approx(50.0f));
}

TEST_CASE("No regen while airborne", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	v.current = 50.0f;
	AdvanceVigor(v, s, VigorActivity::Idle, 0.0f, false, false, 1.0f);
	CHECK(v.current == Approx(50.0f));
}

TEST_CASE("Clamps at zero and max", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	v.current = 5.0f;
	AdvanceVigor(v, s, VigorActivity::Gliding, 0.0f, false, false, 5.0f);
	CHECK(v.current == Approx(0.0f));

	v.current = 90.0f;
	v.secondsSinceDrain = 5.0f;
	AdvanceVigor(v, s, VigorActivity::Idle, 0.0f, false, true, 5.0f);
	CHECK(v.current == Approx(100.0f));
}

TEST_CASE("SecondsUntilEmpty", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	v.current = 16.0f;
	CHECK(SecondsUntilEmpty(v, s, VigorActivity::Gliding, 0.0f) == Approx(2.0f));
	CHECK(std::isinf(SecondsUntilEmpty(v, s, VigorActivity::Idle, 0.0f)));
}

TEST_CASE("Updraft sphere and rotated box containment", "[traversal]")
{
	UpdraftVolume sphere;
	sphere.center = Vector3(10.0f, 0.0f, 10.0f);
	sphere.radius = 5.0f;
	sphere.strength = 8.0f;
	CHECK(GetUpdraftStrength({ sphere }, Vector3(12.0f, 1.0f, 10.0f)) == Approx(8.0f));
	CHECK(GetUpdraftStrength({ sphere }, Vector3(20.0f, 0.0f, 10.0f)) == Approx(0.0f));

	UpdraftVolume box;
	box.center = Vector3(0.0f, 0.0f, 0.0f);
	box.radius = 0.0f;
	box.halfExtents = Vector3(10.0f, 5.0f, 1.0f);
	box.strength = 6.0f;

	// Unrotated: long along X, thin along Z.
	CHECK(GetUpdraftStrength({ box }, Vector3(8.0f, 0.0f, 0.0f)) == Approx(6.0f));
	CHECK(GetUpdraftStrength({ box }, Vector3(0.0f, 0.0f, 8.0f)) == Approx(0.0f));
	CHECK(GetUpdraftStrength({ box }, Vector3(0.0f, 6.0f, 0.0f)) == Approx(0.0f));

	// Rotated a quarter turn: the long axis now lies along Z, thin along X.
	box.orientation = 1.57079632679f;
	CHECK(GetUpdraftStrength({ box }, Vector3(0.0f, 0.0f, 8.0f)) == Approx(6.0f));
	CHECK(GetUpdraftStrength({ box }, Vector3(8.0f, 0.0f, 0.0f)) == Approx(0.0f));
}

TEST_CASE("Strongest updraft wins", "[traversal]")
{
	UpdraftVolume a;
	a.center = Vector3(0.0f, 0.0f, 0.0f);
	a.radius = 5.0f;
	a.strength = 4.0f;
	UpdraftVolume b = a;
	b.strength = 9.0f;
	CHECK(GetUpdraftStrength({ a, b }, Vector3(1.0f, 0.0f, 0.0f)) == Approx(9.0f));
	CHECK(GetUpdraftStrength({}, Vector3(1.0f, 0.0f, 0.0f)) == Approx(0.0f));
}

TEST_CASE("Glide velocity converges", "[traversal]")
{
	const TraversalSettings s;
	Vector3 v(0.0f, -20.0f, 0.0f);
	for (int i = 0; i < 180; ++i)
	{
		v = StepGlideVelocity(v, Vector3(0.0f, 0.0f, 0.0f), 0.0f, s, 1.0f / 60.0f);
	}

	CHECK(v.y == Approx(-2.5f).margin(0.05f));
	CHECK(std::sqrt(v.x * v.x + v.z * v.z) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("Glide velocity rises in updraft", "[traversal]")
{
	const TraversalSettings s;
	Vector3 v(0.0f, -20.0f, 0.0f);
	for (int i = 0; i < 180; ++i)
	{
		v = StepGlideVelocity(v, Vector3(0.0f, 0.0f, 0.0f), 8.0f, s, 1.0f / 60.0f);
	}

	CHECK(v.y == Approx(8.0f).margin(0.05f));
}

TEST_CASE("Glide forward speed", "[traversal]")
{
	const TraversalSettings s;
	const Vector3 v = StepGlideVelocity(Vector3(0.0f, -5.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 0.0f, s, 0.016f);
	CHECK(std::sqrt(v.x * v.x + v.z * v.z) == Approx(10.0f));
}

TEST_CASE("Regen straddling the delay counts only the part after it", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	v.current = 50.0f;
	v.secondsSinceDrain = 0.5f;
	AdvanceVigor(v, s, VigorActivity::Idle, 0.0f, false, true, 2.0f);
	CHECK(v.current == Approx(87.5f));
}

TEST_CASE("Large idle step after drain regenerates", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	v.current = 50.0f;
	AdvanceVigor(v, s, VigorActivity::Gliding, 0.0f, false, false, 1.0f);
	CHECK(v.current == Approx(42.0f));
	AdvanceVigor(v, s, VigorActivity::Idle, 0.0f, false, true, 10.0f);
	CHECK(v.current == Approx(100.0f));
}

TEST_CASE("Non-positive step and zero max leave Vigor sane", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	AdvanceVigor(v, s, VigorActivity::Gliding, 0.0f, false, false, 0.0f);
	AdvanceVigor(v, s, VigorActivity::Gliding, 0.0f, false, false, -1.0f);
	AdvanceVigor(v, s, VigorActivity::Gliding, 0.0f, false, false, std::numeric_limits<float>::quiet_NaN());
	CHECK(v.current == Approx(100.0f));

	v.max = 0.0f;
	AdvanceVigor(v, s, VigorActivity::Idle, 0.0f, false, true, 1.0f);
	CHECK(v.current == Approx(0.0f));
}

TEST_CASE("Combined drain", "[traversal]")
{
	const TraversalSettings s;
	VigorState v = Full();
	AdvanceVigor(v, s, VigorActivity::Gliding, 12.0f, false, false, 1.0f);
	CHECK(v.current == Approx(80.0f));
}

TEST_CASE("Glide horizontal decay without input", "[traversal]")
{
	const TraversalSettings s;
	Vector3 v = StepGlideVelocity(Vector3(10.0f, -2.5f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), 0.0f, s, 1.0f);
	CHECK(std::sqrt(v.x * v.x + v.z * v.z) == Approx(0.0f).margin(1e-4f));

	v = StepGlideVelocity(Vector3(10.0f, -2.5f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), 0.0f, s, 0.5f);
	CHECK(std::sqrt(v.x * v.x + v.z * v.z) == Approx(4.0f));
}

TEST_CASE("Glide direction turns toward the input at the turn rate", "[traversal]")
{
	const TraversalSettings s;
	const Vector3 v = StepGlideVelocity(Vector3(10.0f, -2.5f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), 0.0f, s, 0.1f);

	CHECK(std::sqrt(v.x * v.x + v.z * v.z) == Approx(10.0f));
	CHECK(std::atan2(v.z, v.x) == Approx(0.3f).margin(0.001f));
}

TEST_CASE("Glide direction takes the input direction when nearly standing still", "[traversal]")
{
	const TraversalSettings s;
	const Vector3 v = StepGlideVelocity(Vector3(0.2f, -2.5f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), 0.0f, s, 0.1f);

	CHECK(v.x == Approx(0.0f).margin(1e-4f));
	CHECK(v.z == Approx(10.0f));
}

TEST_CASE("Glide direction reaches the input when the turn is smaller than one step", "[traversal]")
{
	const TraversalSettings s;
	const Vector3 v = StepGlideVelocity(Vector3(10.0f, -2.5f, 0.0f), Vector3(1.0f, 0.0f, 0.1f), 0.0f, s, 0.1f);

	const float length = std::sqrt(1.0f + 0.01f);
	CHECK(v.x == Approx(10.0f / length));
	CHECK(v.z == Approx(1.0f / length));
}

TEST_CASE("A glide reversal turns the same way however the input rounds", "[traversal]")
{
	// Owner and remote copies derive the reversed input slightly differently; both must turn alike.
	const TraversalSettings s;
	const Vector3 left = StepGlideVelocity(Vector3(10.0f, -2.5f, 0.0f), Vector3(-1.0f, 0.0f, 1e-5f), 0.0f, s, 0.1f);
	const Vector3 right = StepGlideVelocity(Vector3(10.0f, -2.5f, 0.0f), Vector3(-1.0f, 0.0f, -1e-5f), 0.0f, s, 0.1f);

	CHECK(left.z == Approx(right.z));
	CHECK(left.x == Approx(right.x));
}
