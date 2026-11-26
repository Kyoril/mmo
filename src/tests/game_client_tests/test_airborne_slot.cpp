// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Which clip an airborne unit plays: jump start while ascending from a jump, the glide pose
// while the glider is open, and the falling loop otherwise.

#include "catch.hpp"

#include "game_client/animation/airborne_slot.h"

using namespace mmo;

namespace
{
	AnimationContext Airborne(const bool gliding, const float verticalVelocity)
	{
		AnimationContext ctx;
		ctx.airborne = true;
		ctx.gliding = gliding;
		ctx.verticalVelocity = verticalVelocity;
		return ctx;
	}
}

TEST_CASE("a gliding unit plays the glide pose while sinking", "[animation][glide]")
{
	CHECK(SelectAirborneSlot(Airborne(true, -2.0f), false) == airborne_slot::Glide);
}

TEST_CASE("a gliding unit lifted by an updraft keeps the glide pose over a running jump start", "[animation][glide]")
{
	CHECK(SelectAirborneSlot(Airborne(true, 3.0f), true) == airborne_slot::Glide);
}

TEST_CASE("an ascending jump plays the jump start while it runs", "[animation][glide]")
{
	CHECK(SelectAirborneSlot(Airborne(false, 3.0f), true) == airborne_slot::JumpStart);
}

TEST_CASE("an ascending unit whose jump start has ended falls", "[animation][glide]")
{
	CHECK(SelectAirborneSlot(Airborne(false, 3.0f), false) == airborne_slot::Fall);
}

TEST_CASE("a sinking unit without glider plays the falling loop", "[animation][glide]")
{
	CHECK(SelectAirborneSlot(Airborne(false, -2.0f), false) == airborne_slot::Fall);
}
