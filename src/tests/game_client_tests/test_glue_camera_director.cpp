// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/glue_camera_director.h"

using namespace mmo;

TEST_CASE("Fly-in reaches the plaza after the flight time", "[glue_camera]")
{
	GlueCameraDirector d(5.0f);
	d.FlyIn();
	d.Update(2.5f);
	CHECK(d.GetMode() == GlueCameraMode::FlyingIn);
	CHECK(d.GetPathParameter() == Approx(0.5f));
	d.Update(3.0f);
	CHECK(d.GetMode() == GlueCameraMode::Plaza);
	CHECK(d.GetPathParameter() == 1.0f);
	CHECK(d.ConsumeFlightFinished());
	CHECK_FALSE(d.ConsumeFlightFinished());
}

TEST_CASE("Flying out mid-flight reverses from the current point", "[glue_camera]")
{
	GlueCameraDirector d(4.0f);
	d.FlyIn();
	d.Update(1.0f);
	d.FlyOut();
	CHECK(d.GetPathParameter() == Approx(0.25f));
	d.Update(1.0f);
	CHECK(d.GetMode() == GlueCameraMode::Gate);
	CHECK(d.GetPathParameter() == 0.0f);
}

TEST_CASE("Skip jumps to the end of the current flight", "[glue_camera]")
{
	GlueCameraDirector d(5.0f);
	d.FlyIn();
	d.Skip();
	CHECK(d.GetMode() == GlueCameraMode::Plaza);
	CHECK(d.ConsumeFlightFinished());
}

TEST_CASE("Snapping does not report a finished flight", "[glue_camera]")
{
	GlueCameraDirector d(5.0f);
	d.SnapToPlaza();
	CHECK(d.GetMode() == GlueCameraMode::Plaza);
	CHECK(d.GetPathParameter() == 1.0f);
	CHECK_FALSE(d.ConsumeFlightFinished());
	d.SnapToGate();
	CHECK(d.GetMode() == GlueCameraMode::Gate);
	CHECK(d.GetPathParameter() == 0.0f);
}

TEST_CASE("Path evaluation hits the end keys", "[glue_camera]")
{
	const std::vector<GlueCameraKey> keys{ { { 0, 0, 0 }, { 0, 0, 1 } }, { { 10, 0, 0 }, { 10, 0, 1 } }, { { 20, 0, 0 }, { 20, 0, 1 } } };
	CHECK(EvaluateGluePath(keys, 0.0f).position.x == Approx(0.0f));
	CHECK(EvaluateGluePath(keys, 1.0f).position.x == Approx(20.0f));
	CHECK(EvaluateGluePath(keys, 0.5f).position.x == Approx(10.0f));
	CHECK(EvaluateGluePath(keys, 2.0f).position.x == Approx(20.0f));
	CHECK(EvaluateGluePath({ keys[1] }, 0.3f).position.x == Approx(10.0f));
}

TEST_CASE("A fly-out from the plaza takes the fly-out time, or the flight time without one", "[glue_camera]")
{
	GlueCameraDirector same(3.0f);
	same.SnapToPlaza();
	same.FlyOut();
	same.Update(1.5f);
	CHECK(same.GetPathParameter() == Approx(0.5f));
	same.Update(1.5f);
	CHECK(same.GetMode() == GlueCameraMode::Gate);
	CHECK(same.ConsumeFlightFinished());

	GlueCameraDirector faster(3.0f, 2.0f);
	faster.SnapToPlaza();
	faster.FlyOut();
	faster.Update(1.0f);
	CHECK(faster.GetPathParameter() == Approx(0.5f));
	faster.Update(1.0f);
	CHECK(faster.GetMode() == GlueCameraMode::Gate);

	// The fly-in keeps its own time.
	faster.FlyIn();
	faster.Update(1.5f);
	CHECK(faster.GetPathParameter() == Approx(0.5f));
}
