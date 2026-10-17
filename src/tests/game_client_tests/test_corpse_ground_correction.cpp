// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/path_movement_utils.h"

using namespace mmo;

TEST_CASE("Idle ground correction runs for a standing NPC", "[corpse_ground]")
{
	REQUIRE(ShouldCorrectIdleGroundHeight(false, false));
}

TEST_CASE("Idle ground correction is left to the path update while an NPC walks a path", "[corpse_ground]")
{
	// A unit following a server-driven path has its height corrected by the path update
	// itself, so the idle correction must not duplicate that work.
	REQUIRE_FALSE(ShouldCorrectIdleGroundHeight(false, true));
}

TEST_CASE("A corpse is still put down on the ground", "[corpse_ground]")
{
	// A creature killed while running keeps the movement path it died with, but nothing
	// advances it any more: the path update no longer runs for a dead unit. Without the
	// idle correction the corpse keeps the height the server's nav mesh gave it, which can
	// sit noticeably above the client's ground geometry - the corpse hovers in the air.
	REQUIRE(ShouldCorrectIdleGroundHeight(true, true));

	// The same holds for a creature that died standing still.
	REQUIRE(ShouldCorrectIdleGroundHeight(true, false));
}
