// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/math_utils.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "math/vector3.h"

#include <cfloat>
#include <cmath>

using namespace mmo;

namespace
{
	constexpr float facingTolerance = 1e-5f;

	void CheckVectorNearlyEqual(const Vector3& actual, const Vector3& expected)
	{
		CHECK(std::fabs(actual.x - expected.x) <= facingTolerance);
		CHECK(std::fabs(actual.y - expected.y) <= facingTolerance);
		CHECK(std::fabs(actual.z - expected.z) <= facingTolerance);
	}
}

// This is the anchor for the whole convention: the client orients a unit by applying
// Quaternion(facing, UnitY) to its scene node, and a unit mesh points down +X once the
// importer's yaw offset has been applied. FacingToDirection must therefore agree with
// rotating +X around +Y by the facing angle, or server-side facing math and what the
// player actually sees would disagree.
TEST_CASE("FacingToDirectionMatchesYawRotationOfUnitX", "[facing]")
{
	const float facings[] = { 0.0f, 0.5f, 1.0f, Pi * 0.5f, Pi, Pi * 1.5f, 2.5f, -0.75f };

	for (const float facing : facings)
	{
		const Quaternion orientation(Radian(facing), Vector3::UnitY);
		CheckVectorNearlyEqual(FacingToDirection(Radian(facing)), orientation * Vector3::UnitX);
	}
}

TEST_CASE("FacingToDirectionUsesNegativeZForPositiveYaw", "[facing]")
{
	CheckVectorNearlyEqual(FacingToDirection(Radian(0.0f)), Vector3(1.0f, 0.0f, 0.0f));
	CheckVectorNearlyEqual(FacingToDirection(Radian(Pi * 0.5f)), Vector3(0.0f, 0.0f, -1.0f));
	CheckVectorNearlyEqual(FacingToDirection(Radian(Pi)), Vector3(-1.0f, 0.0f, 0.0f));
	CheckVectorNearlyEqual(FacingToDirection(Radian(Pi * 1.5f)), Vector3(0.0f, 0.0f, 1.0f));
}

TEST_CASE("FacingToDirectionReturnsUnitLengthOnHorizontalPlane", "[facing]")
{
	const float facings[] = { 0.0f, 0.9f, 2.2f, 4.7f, -3.3f };

	for (const float facing : facings)
	{
		const Vector3 direction = FacingToDirection(Radian(facing));
		CHECK(std::fabs(direction.GetLength() - 1.0f) <= facingTolerance);
		CHECK(std::fabs(direction.y) <= facingTolerance);
	}
}

TEST_CASE("DirectionToFacingInvertsFacingToDirection", "[facing]")
{
	// Restricted to (-Pi, Pi] because DirectionToFacing returns the atan2 principal value.
	const float facings[] = { 0.0f, 0.25f, 1.0f, Pi * 0.5f, 2.9f, -0.4f, -1.7f, -3.0f };

	for (const float facing : facings)
	{
		const Vector3 direction = FacingToDirection(Radian(facing));
		const Radian roundTripped = DirectionToFacing(direction);

		CHECK(std::fabs(roundTripped.GetValueRadians() - facing) <= facingTolerance);
	}
}

TEST_CASE("DirectionToFacingMapsCardinalDirections", "[facing]")
{
	CHECK(std::fabs(DirectionToFacing(1.0f, 0.0f).GetValueRadians() - 0.0f) <= facingTolerance);
	CHECK(std::fabs(DirectionToFacing(0.0f, -1.0f).GetValueRadians() - Pi * 0.5f) <= facingTolerance);
	CHECK(std::fabs(DirectionToFacing(0.0f, 1.0f).GetValueRadians() + Pi * 0.5f) <= facingTolerance);
}

TEST_CASE("DirectionToFacingIgnoresMagnitude", "[facing]")
{
	const Radian shortDelta = DirectionToFacing(3.0f, -4.0f);
	const Radian longDelta = DirectionToFacing(30.0f, -40.0f);

	CHECK(std::fabs(shortDelta.GetValueRadians() - longDelta.GetValueRadians()) <= facingTolerance);
}

TEST_CASE("NormalizedFacingWrapsIntoZeroToTwoPi", "[facing]")
{
	CHECK(std::fabs(NormalizeFacingPositive(Radian(0.0f)).GetValueRadians() - 0.0f) <= facingTolerance);
	CHECK(std::fabs(NormalizeFacingPositive(Radian(-Pi * 0.5f)).GetValueRadians() - Pi * 1.5f) <= facingTolerance);
	CHECK(std::fabs(NormalizeFacingPositive(Radian(Pi)).GetValueRadians() - Pi) <= facingTolerance);

	// Values well outside a single turn must still land in range.
	const Radian wrapped = NormalizeFacingPositive(Radian(Pi * 5.0f));
	CHECK(wrapped.GetValueRadians() >= 0.0f);
	CHECK(wrapped.GetValueRadians() < 2.0f * Pi);
	CHECK(std::fabs(wrapped.GetValueRadians() - Pi) <= facingTolerance);
}

// The range is half open, and a tiny negative input is the case that rounds up to exactly
// 2 * Pi in single precision. Range bounds are exact predicates, so no tolerance here.
TEST_CASE("NormalizedFacingStaysBelowTwoPiForTinyNegativeInputs", "[facing]")
{
	const float inputs[] = { -1e-8f, -1e-7f, -1e-6f, -FLT_EPSILON };

	for (const float input : inputs)
	{
		const Radian wrapped = NormalizeFacingPositive(Radian(input));
		CHECK(wrapped.GetValueRadians() >= 0.0f);
		CHECK(wrapped.GetValueRadians() < 2.0f * Pi);
	}
}

TEST_CASE("DirectionToFacingReturnsZeroForZeroLengthInput", "[facing]")
{
	CHECK(DirectionToFacing(0.0f, 0.0f).GetValueRadians() == 0.0f);
	CHECK(DirectionToFacing(Vector3::Zero).GetValueRadians() == 0.0f);

	// Negative zero must not fall through to atan2, which would return -Pi and put the
	// result 180 degrees away from the positive zero case.
	CHECK(DirectionToFacing(-0.0f, -0.0f).GetValueRadians() == 0.0f);
}
