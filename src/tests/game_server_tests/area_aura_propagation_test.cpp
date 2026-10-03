// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/aura_container.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Area aura copies of a timed aura start with the caster's remaining time", "[area_aura]")
{
	// A party member who walks back into range 8 s into a 10 s buff must not get a fresh 10 s.
	const auto time = GetAreaAuraPropagationTime(10000, 2000);
	REQUIRE(time.has_value());
	CHECK(*time == 2000);
}

TEST_CASE("Area aura copies of a permanent aura stay permanent", "[area_aura]")
{
	const auto time = GetAreaAuraPropagationTime(0, 0);
	REQUIRE(time.has_value());
	CHECK(*time == 0);
}

TEST_CASE("An expiring timed area aura is not propagated", "[area_aura]")
{
	// SetInitialRemainingTime(0) falls back to the full duration, so a copy made in the last
	// tick would outlive its source by a whole duration.
	CHECK_FALSE(GetAreaAuraPropagationTime(10000, 0).has_value());
}

// Two humans A and B in one party: A casts at t=0, B casts at t=5 and so replaces A's copy on B.
// On A's next tick B no longer has an aura from A, but A's copy must not overwrite B's source aura.
TEST_CASE("Area aura propagates to a member without the aura", "[area_aura]")
{
	CHECK(ShouldPropagateAreaAura(true, false, false));
	CHECK(ShouldPropagateAreaAura(false, false, false));
}

TEST_CASE("Area aura is not propagated to a member who has it from the same caster", "[area_aura]")
{
	CHECK_FALSE(ShouldPropagateAreaAura(true, true, true));
	CHECK_FALSE(ShouldPropagateAreaAura(false, true, true));
}

TEST_CASE("Single-stack area aura does not overwrite another caster's aura", "[area_aura]")
{
	CHECK_FALSE(ShouldPropagateAreaAura(true, true, false));
}

TEST_CASE("Stackable area aura from another caster is still propagated", "[area_aura]")
{
	// Different casters stack, so the copy does not replace anything.
	CHECK(ShouldPropagateAreaAura(false, true, false));
}
