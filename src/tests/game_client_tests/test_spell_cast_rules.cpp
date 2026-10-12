// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "game_client/spell_cast_rules.h"

using namespace mmo;

TEST_CASE("Moving blocks only spells that still have a cast to interrupt", "[spell_cast_rules]")
{
	constexpr uint32 movementInterrupt = spell_interrupt_flags::Movement | spell_interrupt_flags::Interrupt;

	SECTION("A cast-time spell cannot be started while moving")
	{
		CHECK(spell_cast_rules::IsBlockedByMovement(movementInterrupt, 0, 1500, true));
	}

	SECTION("A talent that reduces the cast time to zero lets the spell be cast on the run")
	{
		// Corrupting Touch (spell 24): 1500 ms base cast, Rapid Decay (spell 84) takes 1500 ms off.
		CHECK_FALSE(spell_cast_rules::IsBlockedByMovement(movementInterrupt, 0, 0, true));
	}

	SECTION("Channels stay blocked even without a cast time")
	{
		CHECK(spell_cast_rules::IsBlockedByMovement(movementInterrupt, spell_attributes::Channeled, 0, true));
	}

	SECTION("Standing still or a spell that ignores movement is never blocked")
	{
		CHECK_FALSE(spell_cast_rules::IsBlockedByMovement(movementInterrupt, 0, 1500, false));
		CHECK_FALSE(spell_cast_rules::IsBlockedByMovement(spell_interrupt_flags::Interrupt, 0, 1500, true));
	}
}
