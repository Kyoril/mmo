// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "catch.hpp"

#include "math/vector3.h"
#include "math/matrix4.h"
#include "math/angle.h"
#include "math/radian.h"
#include "math/degree.h"

using namespace mmo;


TEST_CASE("RadianToDegreeConversion", "[math]")
{
	const float testDegree = 180.0f;
	
	const Radian r{ Degree(testDegree) };
	CHECK(fabsf(r.GetValueDegrees() - testDegree) <= FLT_EPSILON);
}

TEST_CASE("DegreeToRadianConversion", "[math]")
{
	const float testRadian = Pi;

	const Degree d{ Radian(testRadian) };
	CHECK(fabsf(d.GetValueRadians() - testRadian) <= FLT_EPSILON);
}

TEST_CASE("AngleUnitToRadianConversion", "[math]")
{
	// Ensure angle unit is set to Degree
	Angle::SetAngleUnit(AngleUnit::Degree);

	// Initialize angle unit in degree
	const Angle a{ 180.0f };

	const Degree d = a;
	const Radian r = a;
	CHECK(fabsf(d.GetValueAngleUnits() - 180.0f) <= FLT_EPSILON);
	CHECK(fabsf(r.GetValueAngleUnits() - 180.0f) <= FLT_EPSILON);
}


TEST_CASE("Vector3Constructor", "[math]")
{
	// Create vector
	const Vector3 v{};
	REQUIRE(v.IsValid());

	CHECK(v.x == 0.0f);
	CHECK(v.y == 0.0f);
	CHECK(v.z == 0.0f);
	CHECK(v == Vector3::Zero);

	// Create second vector
	const Vector3 v2{ 1.0f, 0.0f, 0.0f };
	REQUIRE(v2.IsValid());

	CHECK(v2.x == 1.0f);
	CHECK(v2.y == 0.0f);
	CHECK(v2.z == 0.0f);
	CHECK(v2 == Vector3::UnitX);
}


TEST_CASE("Vector3Addition", "[math]")
{
	const Vector3 v = Vector3{5.0f, 0.0f, 0.0f} + Vector3{3.0f, 0.0f, 0.0f};
	REQUIRE(v.IsValid());
	CHECK(v.x == 8.0f);
	CHECK(v.y == 0.0f);
	CHECK(v.z == 0.0f);

	const Vector3 v2 = Vector3{ 5.0f, 0.0f, 0.0f } + Vector3{ -3.0f, 0.0f, 0.0f };
	REQUIRE(v2.IsValid());
	CHECK(v2.x == 2.0f);
	CHECK(v2.y == 0.0f);
	CHECK(v2.z == 0.0f);
}

TEST_CASE("Vector3Subtraction", "[math]")
{
	const Vector3 v = Vector3{ 5.0f, 0.0f, 0.0f } - Vector3{ 3.0f, 0.0f, 0.0f };
	REQUIRE(v.IsValid());
	CHECK(v.x == 2.0f);
	CHECK(v.y == 0.0f);
	CHECK(v.z == 0.0f);

	const Vector3 v2 = Vector3{ 5.0f, 0.0f, 0.0f } - Vector3{ -3.0f, 0.0f, 0.0f };
	REQUIRE(v2.IsValid());
	CHECK(v2.x == 8.0f);
	CHECK(v2.y == 0.0f);
	CHECK(v2.z == 0.0f);
}

TEST_CASE("Vector3Comparison", "[math]")
{
	const Vector3 v1{ 1.0f, 0.0f, 3.0f }; 
	REQUIRE(v1.IsValid());
	const Vector3 v2{ 1.0f, 0.0f, 3.0f };
	REQUIRE(v2.IsValid());
	const Vector3 v3{ };
	REQUIRE(v3.IsValid());
	const Vector3 v4{ 5.0f, 0.0f, 3.0f };
	REQUIRE(v4.IsValid());
	const Vector3 v5{ 1.01f, 0.002f, 3.0001f };
	REQUIRE(v5.IsValid());

	CHECK(v1 == v2);
	CHECK(v1 != v3);
	CHECK(v1 != v4);
	CHECK(v1 != v5);
}

TEST_CASE("Vector3Multiplication", "[math]")
{
	const Vector3 v1{ 1.0f, 0.0f, 3.0f };
	REQUIRE(v1.IsValid());
	const Vector3 v2{ 0.0f, 2.0f, 2.0f };
	REQUIRE(v2.IsValid());

	CHECK(v1 * 2.0f == Vector3{ 2.0f, 0.0f, 6.0f });
	CHECK(v1 * v2 == Vector3{ 0.0f, 0.0f, 6.0f });
}

TEST_CASE("Vector3Division", "[math]")
{
	const Vector3 v1{ 1.0f, 0.0f, 3.0f };
	REQUIRE(v1.IsValid());

	CHECK(v1 / 2.0f == Vector3{ 0.5f, 0.0f, 1.5f });
	
	// Force error by dividing through 0
	const Vector3 v2 = v1 / 0.0f;
	CHECK(!v2.IsValid());
}

TEST_CASE("Vector3Transform", "[math]")
{
	const Vector3 v1{ 1.0f, 0.0f, 3.0f };
	REQUIRE(v1.IsValid());

	// Identity matrix should not motify the vector
	CHECK(Matrix4::Identity * v1 == v1);
	
	// Translate the vector on the x axis
	const Matrix4 trans = Matrix4::GetTrans(3.0f, 0.0f, 0.0f);
	CHECK(trans * v1 == Vector3{ 4.0f, 0.0f, 3.0f });

	// Scale vector uniformly
	const Matrix4 scaleUniform = Matrix4::GetScale(2.0f, 2.0f, 2.0f);
	CHECK(scaleUniform * v1 == Vector3{ 2.0f, 0.0f, 6.0f });

	// Scale vector non-uniformly
	const Matrix4 scale = Matrix4::GetScale(2.0f, 1.0f, 3.0f);
	CHECK(scale * v1 == Vector3{ 2.0f, 0.0f, 9.0f });
}
