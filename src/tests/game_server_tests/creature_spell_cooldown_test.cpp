// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/ai/creature_spell_cooldown.h"

using namespace mmo;

TEST_CASE("A creature spell entry's cooldown overrides the spell's own", "[creature_spell_cooldown]")
{
	// creaturespells carries the pacing the designer authored for this creature; the spell's
	// own cooldown is the fallback for creatures that do not specify one.
	const auto range = ResolveCreatureSpellCooldown(4000, 6000, 12000, false);

	REQUIRE(range.min == 4000);
	REQUIRE(range.max == 6000);
}

TEST_CASE("Without a creature cooldown the spell's own cooldown is used", "[creature_spell_cooldown]")
{
	const auto range = ResolveCreatureSpellCooldown(0, 0, 12000, false);

	REQUIRE(range.min == 12000);
	REQUIRE(range.max == 12000);
}

TEST_CASE("A maximum below the minimum collapses instead of inverting", "[creature_spell_cooldown]")
{
	// maxcooldown is optional in the data, so it is routinely 0 while mincooldown is set.
	const auto range = ResolveCreatureSpellCooldown(5000, 0, 0, false);

	REQUIRE(range.min == 5000);
	REQUIRE(range.max == 5000);
}

TEST_CASE("A failed cast always backs off even when no cooldown is authored", "[creature_spell_cooldown]")
{
	// The spin this guards against: spell 242 has cooldown 0 and no creature cooldown was ever
	// read, so a cast that fails validation was instantly available again and the AI retried it
	// as fast as the event loop allowed.
	const auto range = ResolveCreatureSpellCooldown(0, 0, 0, true);

	REQUIRE(range.min >= FailedCastRetryCooldownMs);
	REQUIRE(range.max >= FailedCastRetryCooldownMs);
}

TEST_CASE("A successful cast with no cooldown anywhere stays uncooled", "[creature_spell_cooldown]")
{
	// The floor exists to bound retries of a failing spell, not to slow down working rotations.
	const auto range = ResolveCreatureSpellCooldown(0, 0, 0, false);

	REQUIRE(range.min == 0);
	REQUIRE(range.max == 0);
}

TEST_CASE("A failed cast keeps an authored cooldown that already exceeds the floor", "[creature_spell_cooldown]")
{
	const auto range = ResolveCreatureSpellCooldown(4000, 6000, 0, true);

	REQUIRE(range.min == 4000);
	REQUIRE(range.max == 6000);
}
