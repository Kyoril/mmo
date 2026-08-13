// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <limits>

using namespace mmo;

TEST_CASE("Matrix4 - IsFinite accepts ordinary matrices", "[matrix4]")
{
	REQUIRE(Matrix4::Identity.IsFinite());
	REQUIRE(Matrix4::Zero.IsFinite());

	Matrix4 transform;
	transform.MakeTransform(Vector3(1.0f, -2.0f, 3.5f), Vector3(0.25f, 0.25f, 0.25f), Quaternion(Degree(37), Vector3::UnitY));
	REQUIRE(transform.IsFinite());
}

TEST_CASE("Matrix4 - IsFinite rejects NaN and infinite components", "[matrix4]")
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();

	// One bad component anywhere is enough — check a linear entry and a translation entry.
	const Matrix4 nanLinear(
		nan, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
	REQUIRE_FALSE(nanLinear.IsFinite());

	const Matrix4 infTranslation(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, -inf,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
	REQUIRE_FALSE(infTranslation.IsFinite());
}

TEST_CASE("Matrix4 - InverseAffine of a singular transform is not finite", "[matrix4]")
{
	// InverseAffine divides by the determinant without a singularity check, so a zero
	// component in the scale yields inf/NaN entries. IsFinite is how callers detect that:
	// the result compares false against everything, including itself, so bad inverses
	// otherwise propagate silently through downstream math.
	Matrix4 flattened;
	flattened.MakeTransform(Vector3::Zero, Vector3(1.0f, 1.0f, 0.0f), Quaternion::Identity);

	REQUIRE(flattened.IsFinite());
	REQUIRE(flattened.IsAffine());
	REQUIRE_FALSE(flattened.InverseAffine().IsFinite());

	// An invertible transform with the same small magnitude still inverts cleanly.
	Matrix4 shrunk;
	shrunk.MakeTransform(Vector3::Zero, Vector3(0.001f, 0.001f, 0.001f), Quaternion::Identity);
	REQUIRE(shrunk.InverseAffine().IsFinite());
}
