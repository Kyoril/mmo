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

TEST_CASE("Idle ground correction searches up to the server height of a unit at its server position", "[corpse_ground]")
{
	// An innkeeper on a building floor at 2.45 dropped to the terrain at 1.2 because the
	// building had not streamed in yet. The server height lets the search reach the floor
	// again once the building exists.
	const std::optional<Vector3> serverPosition = Vector3(265.2f, 2.45f, 612.7f);
	const std::optional<float> reference = IdleGroundReferenceHeight(serverPosition, Vector3(265.2f, 1.2f, 612.7f));
	REQUIRE(reference.has_value());
	CHECK(*reference == 2.45f);

	// Small horizontal drift (the path arrival snap) still counts as the same spot.
	CHECK(IdleGroundReferenceHeight(serverPosition, Vector3(265.6f, 1.2f, 612.4f)).has_value());
}

TEST_CASE("Idle ground correction ignores a server height that belongs elsewhere", "[corpse_ground]")
{
	// A creature that died mid-path stands somewhere between its last server positions; the
	// height of the old spot could pull its corpse onto unrelated geometry above it.
	const std::optional<Vector3> serverPosition = Vector3(100.0f, 12.0f, 100.0f);
	CHECK_FALSE(IdleGroundReferenceHeight(serverPosition, Vector3(104.0f, 3.0f, 100.0f)).has_value());

	// No server position known yet.
	CHECK_FALSE(IdleGroundReferenceHeight(std::nullopt, Vector3(0.0f, 0.0f, 0.0f)).has_value());
}
