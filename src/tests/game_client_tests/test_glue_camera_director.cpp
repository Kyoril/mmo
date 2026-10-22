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
