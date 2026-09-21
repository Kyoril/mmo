// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/light_math.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Cone cosines use half of the full cone angle", "[light_math]")
{
	CHECK(light_math::ConeCosine(90.0f) == Approx(std::cos(3.14159265f / 4.0f)));
	CHECK(light_math::ConeCosine(0.0f) == Approx(1.0f));
	CHECK(light_math::ConeCosine(179.0f) > 0.0f);

	const light_math::SpotCosinePair pair = light_math::SpotCosines(30.0f, 45.0f);
	CHECK(pair.outer == Approx(light_math::ConeCosine(45.0f)));
	CHECK(pair.inner == Approx(light_math::ConeCosine(30.0f)));

	// Equal angles must not produce a zero-width smoothstep.
	const light_math::SpotCosinePair equal = light_math::SpotCosines(40.0f, 40.0f);
	CHECK(equal.inner > equal.outer);
}

TEST_CASE("Point attenuation falls to zero at the range", "[light_math]")
{
	CHECK(light_math::PointAttenuation(0.0f, 10.0f) == Approx(1.0f));
	CHECK(light_math::PointAttenuation(5.0f, 10.0f) == Approx(0.25f));
	CHECK(light_math::PointAttenuation(10.0f, 10.0f) == Approx(0.0f));
	CHECK(light_math::PointAttenuation(12.0f, 10.0f) == Approx(0.0f));
	CHECK(light_math::PointAttenuation(1.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("Spot factor blends between the outer and inner cone", "[light_math]")
{
	const light_math::SpotCosinePair pair = light_math::SpotCosines(30.0f, 60.0f);
	CHECK(light_math::SpotFactor(1.0f, pair.outer, pair.inner) == Approx(1.0f));
	CHECK(light_math::SpotFactor(pair.inner, pair.outer, pair.inner) == Approx(1.0f));
	CHECK(light_math::SpotFactor(pair.outer, pair.outer, pair.inner) == Approx(0.0f));
	CHECK(light_math::SpotFactor(0.0f, pair.outer, pair.inner) == Approx(0.0f));
	CHECK(light_math::SpotFactor((pair.outer + pair.inner) * 0.5f, pair.outer, pair.inner) == Approx(0.5f));
}

TEST_CASE("Sphere intersection includes touching spheres", "[light_math]")
{
	CHECK(light_math::SphereIntersectsSphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f, Vector3(1.5f, 0.0f, 0.0f), 1.0f));
	CHECK(light_math::SphereIntersectsSphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f, Vector3(2.0f, 0.0f, 0.0f), 1.0f));
	CHECK_FALSE(light_math::SphereIntersectsSphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f, Vector3(2.5f, 0.0f, 0.0f), 1.0f));
}

TEST_CASE("Spot cone culling rejects spheres outside the cone", "[light_math]")
{
	const Vector3 apex(0.0f, 0.0f, 0.0f);
	const Vector3 forward(0.0f, 0.0f, 1.0f);
	const float cosHalf = light_math::ConeCosine(40.0f);

	CHECK(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(0.0f, 0.0f, 10.0f), 1.0f));
	CHECK_FALSE(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(0.0f, 0.0f, -5.0f), 1.0f));
	CHECK_FALSE(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(0.0f, 0.0f, 25.0f), 1.0f));
	CHECK_FALSE(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(10.0f, 0.0f, 5.0f), 1.0f));
	// A sphere around the apex always intersects.
	CHECK(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(0.5f, 0.0f, -0.5f), 1.0f));
}

TEST_CASE("Spot cone culling never drops a sphere containing a lit point", "[light_math]")
{
	// Deterministic LCG so the test is reproducible.
	uint32 state = 12345u;
	const auto next = [&state]()
	{
		state = state * 1664525u + 1013904223u;
		return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
	};

	const Vector3 apex(3.0f, 1.0f, -2.0f);
	const Vector3 direction = Vector3(0.3f, -1.0f, 0.2f).NormalizedCopy();
	const float range = 15.0f;
	const float cosHalf = light_math::ConeCosine(70.0f);

	for (int i = 0; i < 2000; ++i)
	{
		const Vector3 point = apex + Vector3(next() * 40.0f - 20.0f, next() * 40.0f - 20.0f, next() * 40.0f - 20.0f);
		const Vector3 toPoint = point - apex;
		const float distance = toPoint.GetLength();
		if (distance <= 1e-3f || distance > range)
		{
			continue;
		}

		if (toPoint.Dot(direction) / distance < cosHalf)
		{
			continue;
		}

		// The point is lit; any sphere containing it must survive culling.
		const float radius = 0.1f + next() * 4.0f;
		const Vector3 center = point + Vector3(next() - 0.5f, next() - 0.5f, next() - 0.5f).NormalizedCopy() * (radius * next());
		CHECK(light_math::SpotConeIntersectsSphere(apex, direction, range, cosHalf, center, radius));
	}
}
