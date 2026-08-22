// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "terrain/flatten_plane.h"

using namespace mmo;
using namespace mmo::terrain;

// The Flatten brush drives terrain toward a plane rather than a single height, which is what
// turns "make a ramp" into one stroke. These pin down the plane's geometry, the two ways it is
// picked in the viewport, and the raise/lower gate that lets a stroke add ground without
// levelling the detail already standing above it.

TEST_CASE("A level plane reports its anchor height everywhere", "[flatten_plane]")
{
	const FlattenPlane plane = FlattenPlane::Level(42.0f);

	CHECK(plane.slopeDegrees == Approx(0.0f));
	CHECK(plane.HeightAt(0.0f, 0.0f) == Approx(42.0f));
	CHECK(plane.HeightAt(1000.0f, -1000.0f) == Approx(42.0f));
}

TEST_CASE("A sloped plane descends along its azimuth and stays level across it", "[flatten_plane]")
{
	// 45 degrees means one unit of drop per unit travelled, which makes the arithmetic legible.
	FlattenPlane plane;
	plane.anchor = Vector3(0.0f, 100.0f, 0.0f);
	plane.slopeDegrees = 45.0f;
	plane.azimuthDegrees = 0.0f;   // Descends toward +Z.

	CHECK(plane.HeightAt(0.0f, 0.0f) == Approx(100.0f));
	CHECK(plane.HeightAt(0.0f, 10.0f) == Approx(90.0f));
	CHECK(plane.HeightAt(0.0f, -10.0f) == Approx(110.0f));

	// Across the slope the plane must not move at all, or a ramp would come out canted.
	CHECK(plane.HeightAt(10.0f, 0.0f) == Approx(100.0f));
	CHECK(plane.HeightAt(-250.0f, 0.0f) == Approx(100.0f));
}

TEST_CASE("Azimuth 90 degrees descends toward +X", "[flatten_plane]")
{
	FlattenPlane plane;
	plane.anchor = Vector3(0.0f, 0.0f, 0.0f);
	plane.slopeDegrees = 45.0f;
	plane.azimuthDegrees = 90.0f;

	CHECK(plane.HeightAt(10.0f, 0.0f) == Approx(-10.0f));
	CHECK(plane.HeightAt(-10.0f, 0.0f) == Approx(10.0f));
	CHECK(plane.HeightAt(0.0f, 10.0f) == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Slope is clamped short of vertical", "[flatten_plane]")
{
	FlattenPlane plane;
	plane.slopeDegrees = 90.0f;
	plane.azimuthDegrees = 0.0f;

	// tan(90) is infinite; the clamp is what keeps the height finite.
	const float height = plane.HeightAt(0.0f, 1.0f);
	CHECK(std::isfinite(height));
	CHECK(height == Approx(-std::tan(MaxFlattenSlopeDegrees * Deg2Rad)));
}

TEST_CASE("FromTwoPoints builds a plane through both points", "[flatten_plane]")
{
	const Vector3 anchor(10.0f, 50.0f, 20.0f);
	const Vector3 through(30.0f, 30.0f, 20.0f);

	const FlattenPlane plane = FlattenPlane::FromTwoPoints(anchor, through);

	CHECK(plane.HeightAt(anchor.x, anchor.z) == Approx(anchor.y));
	CHECK(plane.HeightAt(through.x, through.z) == Approx(through.y));

	// 20 out and 20 down is a 45 degree ramp descending toward +X.
	CHECK(plane.slopeDegrees == Approx(45.0f));
	CHECK(plane.azimuthDegrees == Approx(90.0f));
}

TEST_CASE("FromTwoPoints handles the second point being the higher one", "[flatten_plane]")
{
	const Vector3 anchor(0.0f, 10.0f, 0.0f);
	const Vector3 through(0.0f, 30.0f, 20.0f);

	const FlattenPlane plane = FlattenPlane::FromTwoPoints(anchor, through);

	// Both points still lie on the plane; the slope stays positive and the descent direction
	// flips to point away from the higher point instead.
	CHECK(plane.HeightAt(anchor.x, anchor.z) == Approx(anchor.y));
	CHECK(plane.HeightAt(through.x, through.z) == Approx(through.y));
	CHECK(plane.slopeDegrees == Approx(45.0f));
	CHECK(plane.azimuthDegrees == Approx(180.0f));
}

TEST_CASE("FromTwoPoints degenerates to level when the points share an XZ position", "[flatten_plane]")
{
	const Vector3 anchor(5.0f, 10.0f, 5.0f);
	const FlattenPlane plane = FlattenPlane::FromTwoPoints(anchor, Vector3(5.0f, 80.0f, 5.0f));

	CHECK(plane.slopeDegrees == Approx(0.0f));
	CHECK(plane.HeightAt(100.0f, -100.0f) == Approx(10.0f));
}

TEST_CASE("FromNormal reproduces the surface it was sampled from", "[flatten_plane]")
{
	const Vector3 anchor(3.0f, 7.0f, -2.0f);

	// A 45 degree hillside falling toward +Z has this normal.
	const Vector3 normal = Vector3(0.0f, 1.0f, 1.0f).NormalizedCopy();

	const FlattenPlane plane = FlattenPlane::FromNormal(anchor, normal);

	CHECK(plane.slopeDegrees == Approx(45.0f));
	CHECK(plane.azimuthDegrees == Approx(0.0f).margin(1e-3f));
	CHECK(plane.HeightAt(anchor.x, anchor.z) == Approx(anchor.y));
	CHECK(plane.HeightAt(anchor.x, anchor.z + 10.0f) == Approx(anchor.y - 10.0f));
}

TEST_CASE("FromNormal treats a flat normal as level ground", "[flatten_plane]")
{
	const FlattenPlane plane = FlattenPlane::FromNormal(Vector3(0.0f, 5.0f, 0.0f), Vector3::UnitY);

	CHECK(plane.slopeDegrees == Approx(0.0f));
	CHECK(plane.HeightAt(50.0f, 50.0f) == Approx(5.0f));
}

TEST_CASE("GetNormal round-trips through FromNormal", "[flatten_plane]")
{
	FlattenPlane plane;
	plane.anchor = Vector3(1.0f, 2.0f, 3.0f);
	plane.slopeDegrees = 30.0f;
	plane.azimuthDegrees = 210.0f;

	const FlattenPlane rebuilt = FlattenPlane::FromNormal(plane.anchor, plane.GetNormal());

	CHECK(rebuilt.slopeDegrees == Approx(plane.slopeDegrees).margin(1e-3f));
	CHECK(rebuilt.azimuthDegrees == Approx(plane.azimuthDegrees).margin(1e-3f));
}

TEST_CASE("Raise-only leaves terrain above the plane untouched", "[flatten_plane]")
{
	CHECK(FlattenAffectsVertex(5.0f, 10.0f, flatten_mode::RaiseOnly));
	CHECK_FALSE(FlattenAffectsVertex(15.0f, 10.0f, flatten_mode::RaiseOnly));
	CHECK_FALSE(FlattenAffectsVertex(10.0f, 10.0f, flatten_mode::RaiseOnly));

	// A peak standing well above the plane keeps its exact height, which is the point: raising
	// ground to a level must not cost the detail already above that level.
	CHECK(FlattenVertexHeight(150.0f, 10.0f, 1.0f, 1.0f, flatten_mode::RaiseOnly, true) == Approx(150.0f));
	CHECK(FlattenVertexHeight(5.0f, 10.0f, 1.0f, 1.0f, flatten_mode::RaiseOnly, true) == Approx(10.0f));
}

TEST_CASE("Lower-only leaves terrain below the plane untouched", "[flatten_plane]")
{
	CHECK(FlattenAffectsVertex(15.0f, 10.0f, flatten_mode::LowerOnly));
	CHECK_FALSE(FlattenAffectsVertex(5.0f, 10.0f, flatten_mode::LowerOnly));

	CHECK(FlattenVertexHeight(-40.0f, 10.0f, 1.0f, 1.0f, flatten_mode::LowerOnly, true) == Approx(-40.0f));
	CHECK(FlattenVertexHeight(15.0f, 10.0f, 1.0f, 1.0f, flatten_mode::LowerOnly, true) == Approx(10.0f));
}

TEST_CASE("Both mode moves terrain toward the plane from either side", "[flatten_plane]")
{
	CHECK(FlattenVertexHeight(0.0f, 10.0f, 1.0f, 1.0f, flatten_mode::Both, true) == Approx(10.0f));
	CHECK(FlattenVertexHeight(20.0f, 10.0f, 1.0f, 1.0f, flatten_mode::Both, true) == Approx(10.0f));
}

TEST_CASE("Hard flattening lands on the plane in one pass at full strength", "[flatten_plane]")
{
	// Power is irrelevant at full strength: dwell time cannot make a hard result more exact,
	// and a single tick of a fast drag has to be as flat as a slow one.
	CHECK(FlattenVertexHeight(0.0f, 10.0f, 1.0f, 0.001f, flatten_mode::Both, true) == Approx(10.0f));

	// In the falloff band it blends by weight alone.
	CHECK(FlattenVertexHeight(0.0f, 10.0f, 0.25f, 999.0f, flatten_mode::Both, true) == Approx(2.5f));
}

TEST_CASE("Soft flattening eases toward the plane and never overshoots it", "[flatten_plane]")
{
	// One short application closes part of the gap.
	CHECK(FlattenVertexHeight(0.0f, 10.0f, 1.0f, 0.25f, flatten_mode::Both, false) == Approx(2.5f));

	// A big power (a long frame, or a high brush strength) settles on the plane rather than
	// shooting through it and inverting the terrain it was meant to level.
	CHECK(FlattenVertexHeight(0.0f, 10.0f, 1.0f, 5.0f, flatten_mode::Both, false) == Approx(10.0f));
	CHECK(FlattenVertexHeight(20.0f, 10.0f, 1.0f, 5.0f, flatten_mode::Both, false) == Approx(10.0f));
}

TEST_CASE("Repeated soft applications converge on the plane", "[flatten_plane]")
{
	float height = 0.0f;
	for (int i = 0; i < 64; ++i)
	{
		height = FlattenVertexHeight(height, 10.0f, 1.0f, 0.2f, flatten_mode::Both, false);
	}

	CHECK(height == Approx(10.0f).margin(0.01f));
}
