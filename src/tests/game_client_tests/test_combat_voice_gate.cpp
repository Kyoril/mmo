// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/combat_voice_gate.h"

using namespace mmo;

TEST_CASE("The first voice line of a unit always passes", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	CHECK(gate.TryPlay(1, 1000, 500));
}

TEST_CASE("A second line inside the block window is refused", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	CHECK_FALSE(gate.TryPlay(1, 1200, 500));
	CHECK_FALSE(gate.TryPlay(1, 1499, 500));
}

TEST_CASE("A line at or after the block window passes", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	CHECK(gate.TryPlay(1, 1500, 500));
}

TEST_CASE("The gate is per unit", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	CHECK(gate.TryPlay(2, 1000, 500));
}

TEST_CASE("A refused line does not extend the block window", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	CHECK_FALSE(gate.TryPlay(1, 1400, 500));
	CHECK(gate.TryPlay(1, 1500, 500));
}

TEST_CASE("Clear drops all gate state", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	gate.Clear();
	CHECK(gate.TryPlay(1, 1100, 500));
}
