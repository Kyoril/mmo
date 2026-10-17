// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mmo_client/client_setting_limits.h"

using namespace mmo;

// The options UI is the project's definition of the sanctioned ranges: the MaxCameraZoom
// slider runs from 2 to 15 and the NameplateDistance dropdown offers at most 60
// (data/client/Interface/GameUI/OptionsFrame.lua). Config.cfg is a plain text file the
// player can edit, so every read of these console variables has to apply those limits.
TEST_CASE("MaxCameraZoom from a hand edited config is limited to the slider range", "[client_setting_limits]")
{
	REQUIRE(ClampMaxCameraZoom(100000.0f) == Approx(MaxCameraZoomSetting));
	REQUIRE(ClampMaxCameraZoom(0.5f) == Approx(MinCameraZoomSetting));
	REQUIRE(ClampMaxCameraZoom(8.0f) == Approx(8.0f));
}

TEST_CASE("Camera zoom never exceeds the sanctioned maximum camera distance", "[client_setting_limits]")
{
	// Config.cfg: SET MaxCameraZoom "500" / SET CameraZoom "500"
	REQUIRE(ClampCameraZoom(500.0f, 500.0f) == Approx(MaxCameraZoomSetting));

	// A legitimate maximum still limits the current zoom.
	REQUIRE(ClampCameraZoom(50.0f, 8.0f) == Approx(8.0f));

	// First person and ordinary values are untouched.
	REQUIRE(ClampCameraZoom(0.0f, 8.0f) == Approx(0.0f));
	REQUIRE(ClampCameraZoom(5.0f, 8.0f) == Approx(5.0f));
	REQUIRE(ClampCameraZoom(-10.0f, 8.0f) == Approx(0.0f));
}

TEST_CASE("Nameplate distance from a hand edited config is limited to the dropdown range", "[client_setting_limits]")
{
	REQUIRE(ClampNameplateDistance(10000.0f) == Approx(MaxNameplateDistanceSetting));
	REQUIRE(ClampNameplateDistance(40.0f) == Approx(40.0f));

	// Zero keeps its meaning of "no distance filter at all" only as a lower bound, it is
	// never widened by the clamp.
	REQUIRE(ClampNameplateDistance(-5.0f) == Approx(0.0f));
}
