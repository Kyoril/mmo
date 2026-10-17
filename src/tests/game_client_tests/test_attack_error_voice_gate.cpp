// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/attack_error_voice_gate.h"

using namespace mmo;

namespace
{
	constexpr uint32 outOfRangeVoice = 101;
	constexpr uint32 wrongFacingVoice = 102;
}

TEST_CASE("The first attack error voice line passes", "[attack_error_voice_gate]")
{
	AttackErrorVoiceGate gate;
	CHECK(gate.CanPlay(outOfRangeVoice, 1000));
}

TEST_CASE("The same attack error voice line is held back while its cooldown runs", "[attack_error_voice_gate]")
{
	// This is the reported symptom: auto attack re-reports "out of range" on every swing
	// attempt, which used to speak the same line every couple of seconds.
	AttackErrorVoiceGate gate;
	gate.NotifyPlayed(outOfRangeVoice, 1000, 8000);

	CHECK_FALSE(gate.CanPlay(outOfRangeVoice, 3000));
	CHECK_FALSE(gate.CanPlay(outOfRangeVoice, 5000));
	CHECK_FALSE(gate.CanPlay(outOfRangeVoice, 8999));
}

TEST_CASE("The same attack error voice line plays again once its cooldown passed", "[attack_error_voice_gate]")
{
	AttackErrorVoiceGate gate;
	gate.NotifyPlayed(outOfRangeVoice, 1000, 8000);

	CHECK(gate.CanPlay(outOfRangeVoice, 9000));
}

TEST_CASE("A different attack error voice line is not held back", "[attack_error_voice_gate]")
{
	// Changing from "out of range" to "I have to be facing the target" tells the player
	// something new, so it must not wait for the previous line's cooldown.
	AttackErrorVoiceGate gate;
	gate.NotifyPlayed(outOfRangeVoice, 1000, 8000);

	CHECK(gate.CanPlay(wrongFacingVoice, 1200));
}

TEST_CASE("Each attack error voice line keeps its own cooldown", "[attack_error_voice_gate]")
{
	AttackErrorVoiceGate gate;
	gate.NotifyPlayed(outOfRangeVoice, 1000, 8000);
	gate.NotifyPlayed(wrongFacingVoice, 2000, 8000);

	CHECK(gate.CanPlay(outOfRangeVoice, 9000));
	CHECK_FALSE(gate.CanPlay(wrongFacingVoice, 9000));
	CHECK(gate.CanPlay(wrongFacingVoice, 10000));
}

TEST_CASE("A held back line does not extend its own cooldown", "[attack_error_voice_gate]")
{
	AttackErrorVoiceGate gate;
	gate.NotifyPlayed(outOfRangeVoice, 1000, 8000);
	CHECK_FALSE(gate.CanPlay(outOfRangeVoice, 5000));

	CHECK(gate.CanPlay(outOfRangeVoice, 9000));
}

TEST_CASE("Clear drops all attack error voice gate state", "[attack_error_voice_gate]")
{
	AttackErrorVoiceGate gate;
	gate.NotifyPlayed(outOfRangeVoice, 1000, 8000);
	gate.Clear();

	CHECK(gate.CanPlay(outOfRangeVoice, 1100));
}

TEST_CASE("The attack error voice cooldown stays in the requested range", "[attack_error_voice_gate]")
{
	// The maintainer asked for a five to ten second gap between repeats of the same line.
	CHECK(attackErrorVoiceRepeatCooldownMs >= 5000);
	CHECK(attackErrorVoiceRepeatCooldownMs <= 10000);
}
