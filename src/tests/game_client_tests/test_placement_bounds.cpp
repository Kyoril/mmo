// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/placement_bounds.h"

#include "math/degree.h"

using namespace mmo;

TEST_CASE("Child bounds grow the parent's bounds where the child is placed", "[placement_bounds]")
{
	const AABB root(Vector3(-10.0f, 0.0f, -10.0f), Vector3(10.0f, 5.0f, 10.0f));
	const AABB house(Vector3(-2.0f, 0.0f, -2.0f), Vector3(2.0f, 6.0f, 2.0f));

	// A house 50 m out along +X, twice as large.
	const AABB combined = CombineChildBounds(root, house, Vector3(50.0f, 0.0f, 0.0f), Quaternion::Identity, Vector3(2.0f, 2.0f, 2.0f));
	CHECK(combined.min.x == Approx(-10.0f));
	CHECK(combined.max.x == Approx(54.0f));
	CHECK(combined.max.y == Approx(12.0f));
	CHECK(combined.min.z == Approx(-10.0f));
}

TEST_CASE("A rotated child is enclosed after its rotation", "[placement_bounds]")
{
	const AABB root(Vector3(-1.0f, 0.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f));
	const AABB wall(Vector3(0.0f, 0.0f, -0.5f), Vector3(20.0f, 3.0f, 0.5f));

	// Turned 90 degrees around Y, the wall runs along Z instead of X.
	const AABB combined = CombineChildBounds(root, wall, Vector3::Zero, Quaternion(Degree(90.0f), Vector3::UnitY), Vector3(1.0f, 1.0f, 1.0f));
	CHECK(combined.max.x == Approx(1.0f).margin(0.01f));
	CHECK((combined.max.z - combined.min.z) == Approx(21.0f).margin(0.01f));
}

TEST_CASE("Null child bounds add nothing, and a null parent takes the child", "[placement_bounds]")
{
	const AABB root(Vector3(-1.0f, 0.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f));
	const AABB combined = CombineChildBounds(root, AABB(), Vector3(100.0f, 0.0f, 0.0f), Quaternion::Identity, Vector3(1.0f, 1.0f, 1.0f));
	CHECK(combined.max.x == Approx(1.0f));

	const AABB child(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
	const AABB fromNull = CombineChildBounds(AABB(), child, Vector3(5.0f, 0.0f, 0.0f), Quaternion::Identity, Vector3(1.0f, 1.0f, 1.0f));
	CHECK(fromNull.min.x == Approx(5.0f));
	CHECK(fromNull.max.x == Approx(6.0f));

	// Nested: a grandchild placed in the child's space, then the child in the root's.
	const AABB childWithGrandchild = CombineChildBounds(child, child, Vector3(10.0f, 0.0f, 0.0f), Quaternion::Identity, Vector3(1.0f, 1.0f, 1.0f));
	const AABB nested = CombineChildBounds(root, childWithGrandchild, Vector3(100.0f, 0.0f, 0.0f), Quaternion::Identity, Vector3(1.0f, 1.0f, 1.0f));
	CHECK(nested.max.x == Approx(111.0f));
}
