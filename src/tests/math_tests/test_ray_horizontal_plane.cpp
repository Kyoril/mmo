// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/ray.h"

using namespace mmo;

TEST_CASE("Ray hits a horizontal plane where it crosses its height", "[ray_plane]")
{
	// From 30 m up, looking down at 45 degrees toward +x, far from the origin.
	const Vector3 direction = Vector3(1.0f, -1.0f, 0.0f).NormalizedCopy();
	const Ray ray(Vector3(3466.0f, 40.0f, 8266.0f), direction, 50000.0f);

	Vector3 hit;
	REQUIRE(IntersectHorizontalPlane(ray, 10.0f, hit));
	CHECK(hit.x == Approx(3496.0f));
	CHECK(hit.y == Approx(10.0f));
	CHECK(hit.z == Approx(8266.0f));
}

TEST_CASE("Ray misses a horizontal plane behind it or parallel to it", "[ray_plane]")
{
	Vector3 hit;

	const Ray upward(Vector3(0.0f, 40.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 1000.0f);
	CHECK_FALSE(IntersectHorizontalPlane(upward, 10.0f, hit));

	const Ray level(Vector3(0.0f, 40.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 1000.0f);
	CHECK_FALSE(IntersectHorizontalPlane(level, 10.0f, hit));
}
