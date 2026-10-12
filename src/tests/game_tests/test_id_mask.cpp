// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/id_mask.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Class and race id 0 maps to bit 31", "[id_mask]")
{
	// Mage (class 0) and Human (race 0) spells are stored with mask 0x80000000.
	STATIC_REQUIRE(IdMaskBit(0) == 0x80000000u);
}

TEST_CASE("Class and race id N maps to bit N-1", "[id_mask]")
{
	STATIC_REQUIRE(IdMaskBit(1) == 0x1u);	// Warrior
	STATIC_REQUIRE(IdMaskBit(2) == 0x2u);	// Cleric
	STATIC_REQUIRE(IdMaskBit(3) == 0x4u);	// Acolyte
	STATIC_REQUIRE(IdMaskBit(4) == 0x8u);	// Scout
	STATIC_REQUIRE(IdMaskBit(31) == 0x40000000u);
}

TEST_CASE("Every id 0..31 owns a distinct bit", "[id_mask]")
{
	uint32 seen = 0;
	for (uint32 id = 0; id < 32; ++id)
	{
		const uint32 bit = IdMaskBit(id);
		CHECK((seen & bit) == 0);
		seen |= bit;
	}

	CHECK(seen == 0xFFFFFFFFu);
}
