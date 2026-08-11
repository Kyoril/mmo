// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/math_utils.h"
#include "math/vector3.h"
#include <cmath>

using namespace mmo;

// Helper function for comparing floats with tolerance
static bool FloatApproxEqual(float a, float b, float tolerance = 1e-6f)
{
	return std::fabs(a - b) <= tolerance;
}

// ============================================================================
// EaseInOutQuad Tests
// ============================================================================

TEST_CASE("EaseInOutQuad(0) returns 0", "[easing][quad]")
{
	CHECK(EaseInOutQuad(0.0f) == 0.0f);
}

TEST_CASE("EaseInOutQuad(1) returns 1", "[easing][quad]")
{
	CHECK(EaseInOutQuad(1.0f) == 1.0f);
}

TEST_CASE("EaseInOutQuad(0.5) returns 0.5", "[easing][quad]")
{
	// At t=0.5, the function transitions from acceleration to deceleration
	CHECK(EaseInOutQuad(0.5f) == 0.5f);
}

TEST_CASE("EaseInOutQuad is monotonically increasing", "[easing][quad]")
{
	float prev = EaseInOutQuad(0.0f);
	for (int i = 1; i <= 10; ++i)
	{
		float t = static_cast<float>(i) / 10.0f;
		float current = EaseInOutQuad(t);
		CHECK(current >= prev);
		prev = current;
	}
}

TEST_CASE("EaseInOutQuad accelerates in first half", "[easing][quad]")
{
	// In the first half, second differences should be positive (convex)
	float t1 = EaseInOutQuad(0.1f);
	float t2 = EaseInOutQuad(0.2f);
	float t3 = EaseInOutQuad(0.3f);

	float d1 = t2 - t1;
	float d2 = t3 - t2;

	// Second difference should be positive (acceleration)
	CHECK(d2 > d1);
}

TEST_CASE("EaseInOutQuad decelerates in second half", "[easing][quad]")
{
	// In the second half, second differences should be negative (concave)
	float t1 = EaseInOutQuad(0.7f);
	float t2 = EaseInOutQuad(0.8f);
	float t3 = EaseInOutQuad(0.9f);

	float d1 = t2 - t1;
	float d2 = t3 - t2;

	// Second difference should be negative (deceleration)
	CHECK(d2 < d1);
}

TEST_CASE("EaseInOutQuad clamps negative values to 0", "[easing][quad]")
{
	CHECK(EaseInOutQuad(-0.5f) == 0.0f);
	CHECK(EaseInOutQuad(-10.0f) == 0.0f);
}

TEST_CASE("EaseInOutQuad clamps values > 1 to 1", "[easing][quad]")
{
	CHECK(EaseInOutQuad(1.5f) == 1.0f);
	CHECK(EaseInOutQuad(100.0f) == 1.0f);
}

TEST_CASE("EaseInOutQuad at t=0.25 matches expected value", "[easing][quad]")
{
	// At t=0.25: 2*0.25*0.25 = 0.125
	float result = EaseInOutQuad(0.25f);
	CHECK(FloatApproxEqual(result, 0.125f));
}

TEST_CASE("EaseInOutQuad at t=0.75 matches expected value", "[easing][quad]")
{
	// At t=0.75: 1 - 2*(1-0.75)^2 = 1 - 2*0.0625 = 0.875
	float result = EaseInOutQuad(0.75f);
	CHECK(FloatApproxEqual(result, 0.875f));
}

// ============================================================================
// EaseInOutCubic Tests
// ============================================================================

TEST_CASE("EaseInOutCubic(0) returns 0", "[easing][cubic]")
{
	CHECK(EaseInOutCubic(0.0f) == 0.0f);
}

TEST_CASE("EaseInOutCubic(1) returns 1", "[easing][cubic]")
{
	CHECK(EaseInOutCubic(1.0f) == 1.0f);
}

TEST_CASE("EaseInOutCubic(0.5) returns 0.5", "[easing][cubic]")
{
	// At t=0.5, the function transitions from acceleration to deceleration
	CHECK(EaseInOutCubic(0.5f) == 0.5f);
}

TEST_CASE("EaseInOutCubic is monotonically increasing", "[easing][cubic]")
{
	float prev = EaseInOutCubic(0.0f);
	for (int i = 1; i <= 10; ++i)
	{
		float t = static_cast<float>(i) / 10.0f;
		float current = EaseInOutCubic(t);
		CHECK(current >= prev);
		prev = current;
	}
}

TEST_CASE("EaseInOutCubic accelerates in first half", "[easing][cubic]")
{
	// In the first half, second differences should be positive (convex)
	float t1 = EaseInOutCubic(0.1f);
	float t2 = EaseInOutCubic(0.2f);
	float t3 = EaseInOutCubic(0.3f);

	float d1 = t2 - t1;
	float d2 = t3 - t2;

	// Second difference should be positive (acceleration)
	CHECK(d2 > d1);
}

TEST_CASE("EaseInOutCubic decelerates in second half", "[easing][cubic]")
{
	// In the second half, second differences should be negative (concave)
	float t1 = EaseInOutCubic(0.7f);
	float t2 = EaseInOutCubic(0.8f);
	float t3 = EaseInOutCubic(0.9f);

	float d1 = t2 - t1;
	float d2 = t3 - t2;

	// Second difference should be negative (deceleration)
	CHECK(d2 < d1);
}

TEST_CASE("EaseInOutCubic clamps negative values to 0", "[easing][cubic]")
{
	CHECK(EaseInOutCubic(-0.5f) == 0.0f);
	CHECK(EaseInOutCubic(-10.0f) == 0.0f);
}

TEST_CASE("EaseInOutCubic clamps values > 1 to 1", "[easing][cubic]")
{
	CHECK(EaseInOutCubic(1.5f) == 1.0f);
	CHECK(EaseInOutCubic(100.0f) == 1.0f);
}

TEST_CASE("EaseInOutCubic at t=0.25 matches expected value", "[easing][cubic]")
{
	// At t=0.25: 4*0.25^3 = 4*0.015625 = 0.0625
	float result = EaseInOutCubic(0.25f);
	CHECK(FloatApproxEqual(result, 0.0625f));
}

TEST_CASE("EaseInOutCubic at t=0.75 matches expected value", "[easing][cubic]")
{
	// At t=0.75: 1 - 4*(1-0.75)^3 = 1 - 4*0.015625 = 0.9375
	float result = EaseInOutCubic(0.75f);
	CHECK(FloatApproxEqual(result, 0.9375f));
}

TEST_CASE("EaseInOutCubic is smoother than EaseInOutQuad", "[easing][cubic]")
{
	// Cubic should have smoother transitions at extremes
	float quad_at_0_1 = EaseInOutQuad(0.1f);
	float cubic_at_0_1 = EaseInOutCubic(0.1f);

	// Cubic should progress slower at the start
	CHECK(cubic_at_0_1 < quad_at_0_1);

	float quad_at_0_9 = EaseInOutQuad(0.9f);
	float cubic_at_0_9 = EaseInOutCubic(0.9f);

	// Cubic should progress faster near the end
	CHECK(cubic_at_0_9 > quad_at_0_9);
}

// ============================================================================
// ComputeSegmentAngle Tests
// ============================================================================

TEST_CASE("ComputeSegmentAngle with straight line (0 angle)", "[segment][angle]")
{
	// Three points in a straight line should give angle close to 0 or Pi
	Vector3 prev(0.0f, 0.0f, 0.0f);
	Vector3 curr(1.0f, 0.0f, 0.0f);
	Vector3 next(2.0f, 0.0f, 0.0f);

	float angle = ComputeSegmentAngle(prev, curr, next);

	// Angle should be close to Pi (180 degrees)
	CHECK(FloatApproxEqual(angle, Pi, 1e-5f));
}

TEST_CASE("ComputeSegmentAngle with right angle", "[segment][angle]")
{
	// Three points forming a right angle
	Vector3 prev(0.0f, 1.0f, 0.0f);
	Vector3 curr(0.0f, 0.0f, 0.0f);
	Vector3 next(1.0f, 0.0f, 0.0f);

	float angle = ComputeSegmentAngle(prev, curr, next);

	// Angle should be close to Pi/2 (90 degrees)
	CHECK(FloatApproxEqual(angle, HalfPi, 1e-5f));
}

TEST_CASE("ComputeSegmentAngle with 60 degree angle", "[segment][angle]")
{
	// Three points forming a 60 degree angle
	Vector3 prev(0.0f, 1.0f, 0.0f);
	Vector3 curr(0.0f, 0.0f, 0.0f);
	Vector3 next(std::sqrt(3.0f), 1.0f, 0.0f);

	float angle = ComputeSegmentAngle(prev, curr, next);

	// Angle should be close to Pi/3 (60 degrees)
	float expected = Pi / 3.0f;
	CHECK(FloatApproxEqual(angle, expected, 1e-5f));
}

TEST_CASE("ComputeSegmentAngle is symmetric", "[segment][angle]")
{
	// Swapping prev and next should give the same angle
	Vector3 prev(1.0f, 0.0f, 0.0f);
	Vector3 curr(0.0f, 0.0f, 0.0f);
	Vector3 next(0.0f, 1.0f, 0.0f);

	float angle1 = ComputeSegmentAngle(prev, curr, next);
	float angle2 = ComputeSegmentAngle(next, curr, prev);

	CHECK(FloatApproxEqual(angle1, angle2));
}

TEST_CASE("ComputeSegmentAngle returns value in [0, Pi]", "[segment][angle]")
{
	// Test multiple random-ish configurations
	Vector3 configs[][3] = {
		{ Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(2, 0, 0) },
		{ Vector3(0, 1, 0), Vector3(0, 0, 0), Vector3(1, 0, 0) },
		{ Vector3(1, 1, 1), Vector3(0, 0, 0), Vector3(-1, -1, -1) },
		{ Vector3(1, 2, 3), Vector3(0, 0, 0), Vector3(-1, 1, -1) },
	};

	for (const auto& config : configs)
	{
		float angle = ComputeSegmentAngle(config[0], config[1], config[2]);
		CHECK(angle >= 0.0f);
		CHECK(angle <= Pi + 1e-5f);  // Allow small floating-point error
	}
}

TEST_CASE("ComputeSegmentAngle with different distances", "[segment][angle]")
{
	// The angle should be independent of segment lengths
	Vector3 center(0.0f, 0.0f, 0.0f);
	Vector3 prev1(1.0f, 0.0f, 0.0f);
	Vector3 next1(0.0f, 1.0f, 0.0f);

	Vector3 prev2(2.0f, 0.0f, 0.0f);
	Vector3 next2(0.0f, 2.0f, 0.0f);

	float angle1 = ComputeSegmentAngle(prev1, center, next1);
	float angle2 = ComputeSegmentAngle(prev2, center, next2);

	// Both should be Pi/2, independent of distance
	CHECK(FloatApproxEqual(angle1, angle2));
	CHECK(FloatApproxEqual(angle1, HalfPi, 1e-5f));
}

TEST_CASE("ComputeSegmentAngle with 3D vectors", "[segment][angle]")
{
	// Test with actual 3D positions, not just 2D
	Vector3 prev(1.0f, 0.0f, 0.0f);
	Vector3 curr(0.0f, 0.0f, 0.0f);
	Vector3 next(0.0f, 1.0f, 1.0f);

	float angle = ComputeSegmentAngle(prev, curr, next);

	// Angle should be in valid range
	CHECK(angle >= 0.0f);
	CHECK(angle <= Pi);
}

TEST_CASE("ComputeSegmentAngle acute angle", "[segment][angle]")
{
	// Three points forming an acute angle
	Vector3 prev(1.0f, 0.0f, 0.0f);
	Vector3 curr(0.0f, 0.0f, 0.0f);
	Vector3 next(0.8f, 0.6f, 0.0f);

	float angle = ComputeSegmentAngle(prev, curr, next);

	// Angle should be less than Pi/2 (acute)
	CHECK(angle < HalfPi);
	CHECK(angle > 0.0f);
}

TEST_CASE("ComputeSegmentAngle obtuse angle", "[segment][angle]")
{
	// Three points forming an obtuse angle
	Vector3 prev(1.0f, 0.0f, 0.0f);
	Vector3 curr(0.0f, 0.0f, 0.0f);
	Vector3 next(-0.8f, 0.6f, 0.0f);

	float angle = ComputeSegmentAngle(prev, curr, next);

	// Angle should be greater than Pi/2 (obtuse)
	CHECK(angle > HalfPi);
	CHECK(angle < Pi);
}
