// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/aabb.h"

using namespace mmo;


TEST_CASE("IsNull returns true if min equals max", "[aabb]")
{
	const AABB testVolume { Vector3::Zero, Vector3::Zero };

	CHECK(testVolume.IsNull());
}

TEST_CASE("Default constructor results in null volume", "[aabb]")
{
	const AABB defaultVolume;
	CHECK(defaultVolume.IsNull());
}

TEST_CASE("IsNull returns false if min doesn't equal max", "[aabb]")
{
	const AABB testVolume { Vector3::Zero, Vector3::UnitScale };

	CHECK(!testVolume.IsNull());
}

TEST_CASE("IsNull returns true after SetNull", "[aabb]")
{
	AABB testVolume { Vector3::Zero, Vector3::UnitScale };
	REQUIRE(!testVolume.IsNull());

	testVolume.SetNull();
	CHECK(testVolume.IsNull());
}

TEST_CASE("GetVolume returns expected value", "[aabb]")
{
	const AABB testVolume { Vector3::Zero, Vector3::UnitScale };
	
	CHECK(testVolume.GetVolume() == 1.0f);
}

TEST_CASE("GetSize returns expected value", "[aabb]")
{
	const AABB testVolume { Vector3::Zero, Vector3::UnitScale };
	
	CHECK(testVolume.GetSize() == Vector3::UnitScale);
}

TEST_CASE("GetCenter returns expected value", "[aabb]")
{
	const AABB testVolume { Vector3::Zero, Vector3::UnitScale };
	
	CHECK(testVolume.GetCenter() == Vector3(0.5f, 0.5f, 0.5f));
}

TEST_CASE("GetExtents returns expected value", "[aabb]")
{
	const AABB testVolume { Vector3::Zero, Vector3::UnitScale };
	
	CHECK(testVolume.GetExtents() == Vector3(0.5f, 0.5f, 0.5f));
}

TEST_CASE("GetSurfaceArea returns expected value", "[aabb]")
{
	const AABB testVolume { Vector3::Zero, Vector3::UnitScale };
	
	CHECK(testVolume.GetSurfaceArea() == 6.0f);
}

TEST_CASE("Combine returns expected value", "[aabb]")
{
	AABB combinedVolume { Vector3::Zero, Vector3::UnitScale };
	
	const AABB secondVolume { Vector3(5.0f, 5.0f, 5.0f), Vector3(6.0f, 6.0f, 6.0f) };

	combinedVolume.Combine(secondVolume);

	CHECK(combinedVolume.min == Vector3(0.0f, 0.0f, 0.0f));
	CHECK(combinedVolume.max == secondVolume.max);
}

TEST_CASE("Combine with null volume is allowed and returns correct values", "[aabb]")
{
	AABB combinedVolume { Vector3::Zero, Vector3::UnitScale };
	
	const AABB nullVolume { Vector3(5.0f, 5.0f, 5.0f), Vector3(5.0f, 5.0f, 5.0f) };

	combinedVolume.Combine(nullVolume);

	CHECK(combinedVolume.min == Vector3(0.0f, 0.0f, 0.0f));
	CHECK(combinedVolume.max == nullVolume.max);
}

TEST_CASE("Transform applies translation", "[aabb]")
{
	AABB transformedVolume { Vector3::Zero, Vector3::UnitScale };

	Matrix4 transformation = Matrix4::Identity;
	transformation.SetTrans(Vector3::UnitX);

	transformedVolume.Transform(transformation);
	
	CHECK(transformedVolume.min == Vector3(1.0f, 0.0f, 0.0f));
	CHECK(transformedVolume.max == Vector3(2.0f, 1.0f, 1.0f));
}

TEST_CASE("Transform applies scale", "[aabb]")
{
	AABB transformedVolume { Vector3::Zero, Vector3::UnitScale };

	Matrix4 transformation = Matrix4::Identity;
	transformation.SetScale(Vector3::UnitScale * 2.0f);

	transformedVolume.Transform(transformation);
	
	CHECK(transformedVolume.min == Vector3(0.0f, 0.0f, 0.0f));
	CHECK(transformedVolume.max == Vector3(2.0f, 2.0f, 2.0f));
}

TEST_CASE("Transform applies rotation", "[aabb]")
{
	AABB transformedVolume { Vector3::Zero, Vector3::UnitScale };

	const Matrix4 transformation { Quaternion(Degree(45), Vector3::UnitY)};
	transformedVolume.Transform(transformation);
	
	CHECK(transformedVolume.min.IsNearlyEqual(Vector3(0.0f, 0.0f, -0.707107f), 0.0001f));
	CHECK(transformedVolume.max.IsNearlyEqual(Vector3(1.41421f, 1.0f, 0.707107f), 0.0001f));
}
