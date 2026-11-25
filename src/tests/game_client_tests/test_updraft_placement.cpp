// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Where an updraft's particle column stands: at the bottom of its volume, covering its
// footprint and turned like its box.

#include "catch.hpp"

#include "game_client/updraft_placement.h"

using namespace mmo;

TEST_CASE("a sphere updraft's column rises from the bottom of the sphere", "[updraft]")
{
	UpdraftVolume volume;
	volume.center = Vector3(10.0f, 20.0f, -5.0f);
	volume.radius = 4.0f;
	volume.strength = 6.0f;

	const UpdraftEmitterPlacement placement = GetUpdraftEmitterPlacement(volume);
	CHECK(placement.base.x == Approx(10.0f));
	CHECK(placement.base.y == Approx(16.0f));
	CHECK(placement.base.z == Approx(-5.0f));
	CHECK(placement.halfWidth == Approx(4.0f * UpdraftSphereFootprint));
	CHECK(placement.halfDepth == Approx(4.0f * UpdraftSphereFootprint));
	CHECK(placement.yaw == Approx(0.0f));
	CHECK(placement.height == Approx(8.0f));
}

TEST_CASE("a box updraft's column covers the box footprint and keeps its facing", "[updraft]")
{
	UpdraftVolume volume;
	volume.center = Vector3(0.0f, 5.0f, 0.0f);
	volume.halfExtents = Vector3(6.0f, 5.0f, 2.0f);
	volume.orientation = 0.5f;
	volume.strength = 6.0f;

	const UpdraftEmitterPlacement placement = GetUpdraftEmitterPlacement(volume);
	CHECK(placement.base.y == Approx(0.0f));
	CHECK(placement.halfWidth == Approx(6.0f));
	CHECK(placement.halfDepth == Approx(2.0f));
	CHECK(placement.yaw == Approx(0.5f));
	CHECK(placement.height == Approx(10.0f));
}
